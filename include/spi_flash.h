/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common SPI flash Interface
 *
 * Copyright (C) 2008 Atmel Corporation
 * Copyright (C) 2013 Jagannadha Sutradharudu Teki, Xilinx Inc.
 */

#ifndef _SPI_FLASH_H_
#define _SPI_FLASH_H_

#include <dm.h>	/* Because we dereference struct udevice here */
#include <mtd.h>
#include <linux/types.h>
#include <linux/mtd/spi-nor.h>

/* by default ENV use the same parameters than SF command */
#ifndef CONFIG_ENV_SPI_BUS
# define CONFIG_ENV_SPI_BUS	CONFIG_SF_DEFAULT_BUS
#endif
#ifndef CONFIG_ENV_SPI_CS
# define CONFIG_ENV_SPI_CS	CONFIG_SF_DEFAULT_CS
#endif
#ifndef CONFIG_ENV_SPI_MAX_HZ
# define CONFIG_ENV_SPI_MAX_HZ	CONFIG_SF_DEFAULT_SPEED
#endif
#ifndef CONFIG_ENV_SPI_MODE
# define CONFIG_ENV_SPI_MODE	CONFIG_SF_DEFAULT_MODE
#endif

#ifdef CONFIG_DM_SPI_FLASH

static inline int spi_flash_read(struct spi_flash *flash, u32 offset,
				 size_t len, void *buf)
{
	return mtd_dread(flash->dev, offset, len, buf);
}

static inline int spi_flash_write(struct spi_flash *flash, u32 offset,
				  size_t len, const void *buf)
{
	return mtd_dwrite(flash->dev, offset, len, buf);
}

static inline int spi_flash_erase(struct spi_flash *flash, u32 offset,
				  size_t len)
{
	return mtd_derase(flash->dev, offset, len);
}

struct sandbox_state;

int sandbox_sf_bind_emul(struct sandbox_state *state, int busnum, int cs,
			 struct udevice *bus, ofnode node, const char *spec);

void sandbox_sf_unbind_emul(struct sandbox_state *state, int busnum, int cs);

#else

static inline int spi_flash_read(struct spi_flash *flash, u32 offset,
		size_t len, void *buf)
{
	struct mtd_info *mtd = &flash->mtd;
	size_t retlen;

	return mtd->_read(mtd, offset, len, &retlen, buf);
}

static inline int spi_flash_write(struct spi_flash *flash, u32 offset,
		size_t len, const void *buf)
{
	struct mtd_info *mtd = &flash->mtd;
	size_t retlen;

	return mtd->_write(mtd, offset, len, &retlen, buf);
}

static inline int spi_flash_erase(struct spi_flash *flash, u32 offset,
		size_t len)
{
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
#endif

struct spi_flash *spi_flash_probe(unsigned int bus, unsigned int cs,
				  unsigned int max_hz, unsigned int spi_mode);

void spi_flash_free(struct spi_flash *flash);

static inline int spi_flash_protect(struct spi_flash *flash, u32 ofs, u32 len,
					bool prot)
{
	if (!flash->flash_lock || !flash->flash_unlock)
		return -EOPNOTSUPP;

	if (prot)
		return flash->flash_lock(flash, ofs, len);
	else
		return flash->flash_unlock(flash, ofs, len);
}

#endif /* _SPI_FLASH_H_ */
