/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2015 Thomas Chou <thomas@wytron.com.tw>
 */

#ifndef _MTD_H_
#define _MTD_H_

#include <linux/mtd/mtd.h>

struct mtd_ops {
	int (*erase)(struct udevice *dev, struct erase_info *instr);
	int (*read)(struct udevice *dev, loff_t from, size_t len,
		    u_char *buf);
	int (*write)(struct udevice *dev, loff_t to, size_t len,
		     const u_char *buf);
};

#define mtd_get_ops(dev) ((struct mtd_ops *)(dev)->driver->ops)

/**
 * mtd_dread() - Read data from mtd device
 *
 * @dev:	mtd udevice
 * @from:	Offset into device in bytes to read from
 * @len:	Length of bytes to read
 * @buf:	Buffer to put the data that is read
 * @return 0 if OK, -ve on error
 */
int mtd_dread(struct udevice *dev, loff_t from, size_t len, u_char *buf);

/**
 * mtd_dwrite() - Write data to mtd device
 *
 * @dev:	mtd udevice
 * @to:		Offset into device in bytes to write to
 * @len:	Length of bytes to write
 * @buf:	Buffer containing bytes to write
 * @return 0 if OK, -ve on error
 */
int mtd_dwrite(struct udevice *dev, loff_t to, size_t len, const u_char *buf);

/**
 * mtd_derase() - Erase blocks of the mtd device
 *
 * @dev:	mtd udevice
 * @instr:	Erase info details of mtd device
 * @return 0 if OK, -ve on error
 */
int mtd_derase(struct udevice *dev, loff_t off, size_t len);

int mtd_probe(struct udevice *dev);
int mtd_probe_devices(void);

void board_mtdparts_default(const char **mtdids, const char **mtdparts);

#endif	/* _MTD_H_ */
