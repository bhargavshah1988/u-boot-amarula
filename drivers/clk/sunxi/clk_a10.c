// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (C) 2018 Amarula Solutions.
 * Author: Jagan Teki <jagan@amarulasolutions.com>
 */

#include <common.h>
#include <clk-uclass.h>
#include <dm.h>
#include <errno.h>
#include <asm/arch/ccu.h>
#include <dt-bindings/clock/sun4i-a10-ccu.h>
#include <dt-bindings/reset/sun4i-a10-ccu.h>

static struct ccu_clk_gate a10_gates[] = {
	[CLK_AHB_OTG]		= GATE(0x060, BIT(0)),
	[CLK_AHB_EHCI0]		= GATE(0x060, BIT(1)),
	[CLK_AHB_OHCI0]		= GATE(0x060, BIT(2)),
	[CLK_AHB_EHCI1]		= GATE(0x060, BIT(3)),
	[CLK_AHB_OHCI1]		= GATE(0x060, BIT(4)),

	[CLK_USB_OHCI0]		= GATE(0x0cc, BIT(6)),
	[CLK_USB_OHCI1]		= GATE(0x0cc, BIT(7)),
	[CLK_USB_PHY]		= GATE(0x0cc, BIT(8)),
};

static struct ccu_reset a10_resets[] = {
	[RST_USB_PHY0]		= RESET(0x0cc, BIT(0)),
	[RST_USB_PHY1]		= RESET(0x0cc, BIT(1)),
	[RST_USB_PHY2]		= RESET(0x0cc, BIT(2)),
};

static const struct ccu_desc a10_ccu_desc = {
	.gates = a10_gates,
	.resets = a10_resets,
};

static int a10_clk_bind(struct udevice *dev)
{
	return sunxi_reset_bind(dev, ARRAY_SIZE(a10_resets));
}

static const struct udevice_id a10_ccu_ids[] = {
	{ .compatible = "allwinner,sun4i-a10-ccu",
	  .data = (ulong)&a10_ccu_desc },
	{ .compatible = "allwinner,sun7i-a20-ccu",
	  .data = (ulong)&a10_ccu_desc },
	{ }
};

U_BOOT_DRIVER(clk_sun4i_a10) = {
	.name		= "sun4i_a10_ccu",
	.id		= UCLASS_CLK,
	.of_match	= a10_ccu_ids,
	.priv_auto_alloc_size	= sizeof(struct ccu_priv),
	.ops		= &sunxi_clk_ops,
	.probe		= sunxi_clk_probe,
	.bind		= a10_clk_bind,
};