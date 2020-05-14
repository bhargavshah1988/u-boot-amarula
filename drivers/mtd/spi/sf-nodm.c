// SPDX-License-Identifier: GPL-2.0+
/*
 * SPI flash probing
 *
 * Copyright (C) 2008 Atmel Corporation
 * Copyright (C) 2010 Reinhard Meyer, EMK Elektronik
 * Copyright (C) 2013 Jagannadha Sutradharudu Teki, Xilinx Inc.
 */

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <malloc.h>
#include <spi.h>
#include <spi_flash.h>

#include "sf_internal.h"

struct spi_flash *spi_flash_probe(unsigned int busnum, unsigned int cs,
				  unsigned int max_hz, unsigned int spi_mode)
{
	struct spi_slave *bus;
	struct spi_flash *flash;
	int ret;

	/* Allocate space if needed (not used by sf-uclass) */
	flash = calloc(1, sizeof(*flash));
	if (!flash) {
		debug("SF: Failed to allocate spi_flash\n");
		return NULL;
	}

	bus = spi_setup_slave(busnum, cs, max_hz, spi_mode);
	if (!bus) {
		printf("SF: Failed to set up slave\n");
		goto err_free;
	}

	flash->spi = bus;

	/* Claim spi bus */
	ret = spi_claim_bus(bus);
	if (ret) {
		debug("SF: Failed to claim SPI bus: %d\n", ret);
		goto err_free_slave;
	}

	ret = spi_nor_scan(flash);
	if (ret)
		goto err_read_id;

	if (CONFIG_IS_ENABLED(SPI_FLASH_MTD)) {
		ret = spi_flash_mtd_register(flash);
		if (ret)
			goto err_read_id;
	}

	return flash;

err_read_id:
	spi_release_bus(bus);
err_free_slave:
	spi_free_slave(bus);
err_free:
	free(flash);
	return NULL;
}

void spi_flash_free(struct spi_flash *flash)
{
	if (CONFIG_IS_ENABLED(SPI_FLASH_MTD))
		spi_flash_mtd_unregister();

	spi_free_slave(flash->spi);
	free(flash);
}
