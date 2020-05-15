// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014 Google, Inc
 */

#include <common.h>
#include <dm.h>
#include <malloc.h>
#include <mtd.h>
#include <spi.h>
#include <spi_flash.h>
#include <dm/device-internal.h>
#include "sf_internal.h"

DECLARE_GLOBAL_DATA_PTR;

void spi_flash_free(struct spi_flash *flash)
{
	device_remove(flash->spi->dev, DM_REMOVE_NORMAL);
}

struct spi_flash *spi_flash_probe(unsigned int bus, unsigned int cs,
				  unsigned int max_hz, unsigned int spi_mode)
{
	struct spi_slave *slave;
	struct udevice *new, *bus_dev;
	char *str;
	int ret;

	/* Remove the old device, otherwise probe will just be a nop */
	ret = spi_find_bus_and_cs(bus, cs, &bus_dev, &new);
	if (!ret)
		device_remove(new, DM_REMOVE_NORMAL);

#if defined(CONFIG_SPL_BUILD) && CONFIG_IS_ENABLED(USE_TINY_PRINTF)
	str = "spi_flash";
#else
	char name[30];

	snprintf(name, sizeof(name), "spi_flash@%d:%d", bus, cs);
	str = strdup(name);
#endif
	ret = spi_get_bus_and_cs(bus, cs, max_hz, spi_mode,
				  "spi_flash_std", str, &bus_dev, &slave);
	if (ret)
		return NULL;

	return dev_get_priv(slave->dev);
}

static int spi_flash_std_read(struct udevice *dev, loff_t from, size_t len,
			      u_char *buf)
{
	struct spi_flash *flash = dev_get_priv(dev);
	struct mtd_info *mtd = &flash->mtd;
	size_t retlen;

	return log_ret(mtd->_read(mtd, from, len, &retlen, buf));
}

static int spi_flash_std_write(struct udevice *dev, loff_t to, size_t len,
			       const u_char *buf)
{
	struct spi_flash *flash = dev_get_priv(dev);
	struct mtd_info *mtd = &flash->mtd;
	size_t retlen;

	return mtd->_write(mtd, to, len, &retlen, buf);
}

static int spi_flash_std_erase(struct udevice *dev, struct erase_info *instr)
{
	struct spi_flash *flash = dev_get_priv(dev);
	struct mtd_info *mtd = &flash->mtd;

	if (instr->addr % mtd->erasesize || instr->len % mtd->erasesize) {
		dev_err(dev, "Erase offset/length not multiple of erasesize\n");
		return -EINVAL;
	}

	return mtd->_erase(mtd, instr);
}

static int spi_flash_std_probe(struct udevice *dev)
{
	struct spi_flash *flash = dev_get_priv(dev);
	struct spi_slave *slave = dev_get_parent_priv(dev);
	int ret;

	flash->dev = dev;
	flash->spi = slave;

	ret = spi_claim_bus(slave);
	if (ret) {
		dev_err(dev, "failed to claim bus (ret=%d)\n", ret);
		return ret;
	}

	ret = spi_nor_scan(flash);
	if (ret) {
		dev_err(dev, "failed to scan spinor (ret=%d)\n", ret);
		goto err_claim_bus;
	}

	if (IS_ENABLED(CONFIG_SPI_FLASH_MTD))
		ret = spi_flash_mtd_register(flash);

err_claim_bus:
	spi_release_bus(slave);
	return ret;
}

static int spi_flash_std_remove(struct udevice *dev)
{
	if (IS_ENABLED(CONFIG_SPI_FLASH_MTD))
		spi_flash_mtd_unregister();

	return 0;
}

static const struct mtd_ops spi_flash_std_ops = {
	.read = spi_flash_std_read,
	.write = spi_flash_std_write,
	.erase = spi_flash_std_erase,
};

/*
 * Manually set the parent of the SPI flash to SPI, since dtoc doesn't. We also
 * need to allocate the parent_platdata since by the time this function is
 * called device_bind() has already gone past that step.
 */
static int spi_flash_bind(struct udevice *dev)
{
	if (CONFIG_IS_ENABLED(OF_PLATDATA)) {
		struct dm_spi_slave_platdata *plat;
		struct udevice *spi;
		int ret;

		ret = uclass_first_device_err(UCLASS_SPI, &spi);
		if (ret)
			return ret;
		dev->parent = spi;

		plat = calloc(sizeof(*plat), 1);
		if (!plat)
			return -ENOMEM;
		dev->parent_platdata = plat;
	}

	return 0;
}

static const struct udevice_id spi_flash_std_ids[] = {
	{ .compatible = "jedec,spi-nor" },
	{ }
};

U_BOOT_DRIVER(spi_flash_std) = {
	.name		= "spi_flash_std",
	.id		= UCLASS_MTD,
	.of_match	= spi_flash_std_ids,
	.bind		= spi_flash_bind,
	.probe		= spi_flash_std_probe,
	.remove		= spi_flash_std_remove,
	.priv_auto_alloc_size = sizeof(struct spi_flash),
	.ops		= &spi_flash_std_ops,
};
