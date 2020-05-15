// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2020 Amarula Solutions(India)
 * Copyright (c) 2020 Jagan Teki <jagan@amarulasolutons.com>
 * Copyright (C) 2015 Thomas Chou <thomas@wytron.com.tw>
 */

#include <common.h>
#include <dm.h>
#include <dm/device-internal.h>
#include <errno.h>
#include <mtd.h>
#include <linux/log2.h>

int mtd_dread(struct udevice *dev, loff_t from, size_t len, u_char *buf)
{
	const struct mtd_ops *ops = mtd_get_ops(dev);

	if (!ops->read)
		return -EOPNOTSUPP;

	return ops->read(dev, from, len, buf);
}

int mtd_derase(struct udevice *dev, loff_t off, size_t len)
{
	const struct mtd_ops *ops = mtd_get_ops(dev);
	struct erase_info instr;

	if (!ops->erase)
		return -EOPNOTSUPP;

	memset(&instr, 0, sizeof(instr));
	instr.addr = off;
	instr.len = len;

	return ops->erase(dev, &instr);
}

int mtd_dwrite(struct udevice *dev, loff_t to, size_t len, const u_char *buf)
{
	const struct mtd_ops *ops = mtd_get_ops(dev);

	if (!ops->write)
		return -EOPNOTSUPP;

	return ops->write(dev, to, len, buf);
}

/**
 * mtd_probe - Probe the device @dev if not already done
 *
 * @dev: U-Boot device to probe
 *
 * @return 0 on success, an error otherwise.
 */
int mtd_probe(struct udevice *dev)
{
	if (device_active(dev))
		return 0;

	return device_probe(dev);
}

/*
 * Implement a MTD uclass which should include most flash drivers.
 * The uclass private is pointed to mtd_info.
 */

UCLASS_DRIVER(mtd) = {
	.id		= UCLASS_MTD,
	.name		= "mtd",
	.per_device_auto_alloc_size = sizeof(struct mtd_info),
};
