// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014 Google, Inc
 */

#include <common.h>
#include <dm.h>
#include <malloc.h>
#include <spi.h>
#include <spi_flash.h>
#include <dm/device-internal.h>
#include "sf_internal.h"

DECLARE_GLOBAL_DATA_PTR;

int spi_flash_read_dm(struct udevice *dev, u32 offset, size_t len, void *buf)
{
	return log_ret(sf_get_ops(dev)->read(dev, offset, len, buf));
}

int spi_flash_write_dm(struct udevice *dev, u32 offset, size_t len,
		       const void *buf)
{
	return log_ret(sf_get_ops(dev)->write(dev, offset, len, buf));
}

int spi_flash_erase_dm(struct udevice *dev, u32 offset, size_t len)
{
	return log_ret(sf_get_ops(dev)->erase(dev, offset, len));
}

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

	return dev_get_uclass_priv(slave->dev);
}

static int spi_flash_post_bind(struct udevice *dev)
{
#if defined(CONFIG_NEEDS_MANUAL_RELOC)
	struct dm_spi_flash_ops *ops = sf_get_ops(dev);
	static int reloc_done;

	if (!reloc_done) {
		if (ops->read)
			ops->read += gd->reloc_off;
		if (ops->write)
			ops->write += gd->reloc_off;
		if (ops->erase)
			ops->erase += gd->reloc_off;

		reloc_done++;
	}
#endif
	return 0;
}

static int spi_flash_std_read(struct udevice *dev, u32 offset, size_t len,
			      void *buf)
{
	struct spi_flash *flash = dev_get_uclass_priv(dev);
	struct mtd_info *mtd = &flash->mtd;
	size_t retlen;

	return log_ret(mtd->_read(mtd, offset, len, &retlen, buf));
}

static int spi_flash_std_write(struct udevice *dev, u32 offset, size_t len,
			       const void *buf)
{
	struct spi_flash *flash = dev_get_uclass_priv(dev);
	struct mtd_info *mtd = &flash->mtd;
	size_t retlen;

	return mtd->_write(mtd, offset, len, &retlen, buf);
}

static int spi_flash_std_erase(struct udevice *dev, u32 offset, size_t len)
{
	struct spi_flash *flash = dev_get_uclass_priv(dev);
	struct mtd_info *mtd = &flash->mtd;
	struct erase_info instr;

	if (offset % mtd->erasesize || len % mtd->erasesize) {
		printf("SF: Erase offset/length not multiple of erase size\n");
		return -EINVAL;
	}

	memset(&instr, 0, sizeof(instr));
	instr.addr = offset;
	instr.len = len;

	return mtd->_erase(mtd, &instr);
}

int spi_flash_std_probe(struct udevice *dev)
{
	struct spi_slave *slave = dev_get_parent_priv(dev);
	struct spi_flash *flash;
	int ret;

	if (!slave) {
		printf("SF: Failed to set up slave\n");
		return -ENODEV;
	}

	flash = dev_get_uclass_priv(dev);
	flash->dev = dev;
	flash->spi = slave;

	/* Claim spi bus */
	ret = spi_claim_bus(slave);
	if (ret) {
		debug("SF: Failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	ret = spi_nor_scan(flash);
	if (ret)
		goto err_read_id;

	if (CONFIG_IS_ENABLED(SPI_FLASH_MTD))
		ret = spi_flash_mtd_register(flash);

err_read_id:
	spi_release_bus(slave);
	return ret;
}

static int spi_flash_std_remove(struct udevice *dev)
{
	if (CONFIG_IS_ENABLED(SPI_FLASH_MTD))
		spi_flash_mtd_unregister();

	return 0;
}

static const struct dm_spi_flash_ops spi_flash_std_ops = {
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
	.id		= UCLASS_SPI_FLASH,
	.of_match	= spi_flash_std_ids,
	.bind		= spi_flash_bind,
	.probe		= spi_flash_std_probe,
	.remove		= spi_flash_std_remove,
	.priv_auto_alloc_size = sizeof(struct spi_flash),
	.ops		= &spi_flash_std_ops,
};

UCLASS_DRIVER(spi_flash) = {
	.id		= UCLASS_SPI_FLASH,
	.name		= "spi_flash",
	.post_bind	= spi_flash_post_bind,
	.per_device_auto_alloc_size = sizeof(struct spi_flash),
};
