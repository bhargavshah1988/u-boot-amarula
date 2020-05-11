// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2020 Amarula Solutions(India)
 */

#include <common.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <board.h>
#include <spl.h>
#include <asm/io.h>

#define MODE_SELECT_QSPI	0x6
#define MODE_SELECT_SD		0xb
#define MODE_SELECT_MASK	GENMASK(3, 0)

struct fu540_board {
	void __iomem *regs;
};

static int fu540_get_boot_device(struct udevice *dev)
{
	struct fu540_board *priv = dev_get_priv(dev);
	u8 boot_device = BOOT_DEVICE_MMC1;
	u32 reg;

	reg = readl(priv->regs);
	switch (reg & MODE_SELECT_MASK) {
	case MODE_SELECT_QSPI:
		boot_device = BOOT_DEVICE_SPI;
		break;
	case MODE_SELECT_SD:
		boot_device = BOOT_DEVICE_MMC1;
		break;
	default:
		dev_err(dev,
			"Unsupported boot device 0x%x but trying MMC1\n",
			boot_device);
		break;
	}

	return boot_device;
}

static int fu540_board_get_int(struct udevice *dev, int id, int *val)
{
	switch (id) {
	case BOARD_SPL_BOOT_DEVICE:
		*val = fu540_get_boot_device(dev);
		break;
	default:
		dev_err(dev, "%s: Integer value %d unknown\n", dev->name, id);
		return -EINVAL;
	}

	return 0;
}

static const struct board_ops fu540_board_ops = {
	.get_int = fu540_board_get_int,
};

static int fu540_board_probe(struct udevice *dev)
{
	struct fu540_board *priv = dev_get_priv(dev);

	priv->regs = (void __iomem *)dev_read_addr(dev);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	return 0;
}

static const struct udevice_id fu540_board_ids[] = {
	{ .compatible = "sifive,fu540-modeselect", },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(fu540_board) = {
	.id		= UCLASS_BOARD,
	.name		= "fu540_board",
	.of_match	= fu540_board_ids,
	.ops		= &fu540_board_ops,
	.priv_auto_alloc_size = sizeof(struct fu540_board),
	.probe		= fu540_board_probe,
};
