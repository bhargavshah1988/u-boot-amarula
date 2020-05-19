// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Rockchip Electronics Co., Ltd
 */

#include <common.h>
#include <dm.h>
#include <asm/arch-rockchip/periph.h>
#include <power/regulator.h>
#include <spl_gpio.h>
#include <asm/io.h>
#include <asm/arch-rockchip/gpio.h>

#ifndef CONFIG_SPL_BUILD
int board_early_init_f(void)
{
	struct udevice *regulator;
	int ret;

	ret = regulator_get_by_platname("vcc5v0_host", &regulator);
	if (ret) {
		debug("%s vcc5v0_host init fail! ret %d\n", __func__, ret);
		goto out;
	}

	ret = regulator_set_enable(regulator, true);
	if (ret)
		debug("%s vcc5v0-host-en set fail! ret %d\n", __func__, ret);
out:
	return 0;
}
#endif

#if !defined(CONFIG_TPL_BUILD) && defined(CONFIG_SPL_BUILD)

#include <i2c.h>
#include <linux/compat.h>

#define BUS_NUM				2
#define ROC_RK3399_MEZZ_BAT_ADDR	0x62

int board_early_init_f(void)
{
	struct udevice *bus, *dev;
	int ret;

	ret = uclass_get_device_by_seq(UCLASS_I2C, BUS_NUM, &bus);
	if (ret) {
		dev_err(bus, "failed to get i2c bus 2\n");
		return ret;
	}

	ret = dm_i2c_probe(bus, ROC_RK3399_MEZZ_BAT_ADDR, 0, &dev);
	if (ret) {
		dev_err(dev, "failed to probe i2c2 battery controller IC\n");
		return ret;
	}

	return 0;
}

#endif /* CONFIG_SPL_BUILD */

#if defined(CONFIG_TPL_BUILD)

#define GPIO0_BASE      0xff720000

int board_early_init_f(void)
{
	struct rockchip_gpio_regs * const gpio0 = (void *)GPIO0_BASE;

	/* Turn on red LED, indicating full power mode */
	spl_gpio_output(gpio0, GPIO(BANK_B, 5), 1);

	return 0;
}
#endif
