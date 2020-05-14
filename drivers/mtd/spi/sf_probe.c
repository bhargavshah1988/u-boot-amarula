// SPDX-License-Identifier: GPL-2.0+
/*
 * SPI flash probing
 *
 * Copyright (C) 2008 Atmel Corporation
 * Copyright (C) 2010 Reinhard Meyer, EMK Elektronik
 * Copyright (C) 2013 Jagannadha Sutradharudu Teki, Xilinx Inc.
 */

#include <common.h>
#include <errno.h>
#include <log.h>
#include <malloc.h>
#include <spi.h>
#include <spi_flash.h>

#include "sf_internal.h"

/**
 * spi_flash_probe_slave() - Probe for a SPI flash device on a bus
 *
 * @flashp: Pointer to place to put flash info, which may be NULL if the
 * space should be allocated
 */
static int spi_flash_probe_slave(struct spi_flash *flash)
{
	struct spi_slave *spi = flash->spi;
	int ret;

	/* Setup spi_slave */
	if (!spi) {
		printf("SF: Failed to set up slave\n");
		return -ENODEV;
	}

	/* Claim spi bus */
	ret = spi_claim_bus(spi);
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
	spi_release_bus(spi);
	return ret;
}

struct spi_flash *spi_flash_probe(unsigned int busnum, unsigned int cs,
				  unsigned int max_hz, unsigned int spi_mode)
{
	struct spi_slave *bus;
	struct spi_flash *flash;

	bus = spi_setup_slave(busnum, cs, max_hz, spi_mode);
	if (!bus)
		return NULL;

	/* Allocate space if needed (not used by sf-uclass */
	flash = calloc(1, sizeof(*flash));
	if (!flash) {
		debug("SF: Failed to allocate spi_flash\n");
		return NULL;
	}

	flash->spi = bus;
	if (spi_flash_probe_slave(flash)) {
		spi_free_slave(bus);
		free(flash);
		return NULL;
	}

	return flash;
}

void spi_flash_free(struct spi_flash *flash)
{
	if (CONFIG_IS_ENABLED(SPI_FLASH_MTD))
		spi_flash_mtd_unregister();

	spi_free_slave(flash->spi);
	free(flash);
}
