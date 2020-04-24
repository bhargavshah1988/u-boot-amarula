/*
 * Rockchip PCIe PHY driver
 *
 * Copyright (C) 2020 Amarula Solutions(India)
 *
 * SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
 */

#include <common.h>
#include <clk.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <dm/device.h>
#include <generic-phy.h>
#include <phy-sun4i-usb.h>
#include <regmap.h>
#include <reset.h>
#include <syscon.h>
#include <asm/gpio.h>
#include <asm/io.h>

/*
 * The higher 16-bit of this register is used for write protection
 * only if BIT(x + 16) set to 1 the BIT(x) can be written.
 */
#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

#define PHY_MAX_LANE_NUM      4
#define PHY_CFG_DATA_SHIFT    7
#define PHY_CFG_ADDR_SHIFT    1
#define PHY_CFG_DATA_MASK     0xf
#define PHY_CFG_ADDR_MASK     0x3f
#define PHY_CFG_RD_MASK       0x3ff
#define PHY_CFG_WR_ENABLE     1
#define PHY_CFG_WR_DISABLE    1
#define PHY_CFG_WR_SHIFT      0
#define PHY_CFG_WR_MASK       1
#define PHY_CFG_PLL_LOCK      0x10
#define PHY_CFG_CLK_TEST      0x10
#define PHY_CFG_CLK_SCC       0x12
#define PHY_CFG_SEPE_RATE     BIT(3)
#define PHY_CFG_PLL_100M      BIT(3)
#define PHY_PLL_LOCKED        BIT(9)
#define PHY_PLL_OUTPUT        BIT(10)
#define PHY_LANE_A_STATUS     0x30
#define PHY_LANE_B_STATUS     0x31
#define PHY_LANE_C_STATUS     0x32
#define PHY_LANE_D_STATUS     0x33
#define PHY_LANE_RX_DET_SHIFT 11
#define PHY_LANE_RX_DET_TH    0x1
#define PHY_LANE_IDLE_OFF     0x1
#define PHY_LANE_IDLE_MASK    0x1
#define PHY_LANE_IDLE_A_SHIFT 3
#define PHY_LANE_IDLE_B_SHIFT 4
#define PHY_LANE_IDLE_C_SHIFT 5
#define PHY_LANE_IDLE_D_SHIFT 6

struct rockchip_pcie_phy_data {
	unsigned int pcie_conf;
	unsigned int pcie_status;
	unsigned int pcie_laneoff;
};

struct rockchip_pcie_phy_plat {
	struct clk refclk;
	struct reset_ctl phy_rst;
	u32 index;
};

struct rockchip_pcie_phy {
	struct regmap *reg_base;
	struct rockchip_pcie_phy_plat *phy_plat;
	const struct rockchip_pcie_phy_data *phy_data;
};

static inline void phy_wr_cfg(struct rockchip_pcie_phy *priv,
			      u32 addr, u32 data)
{
	regmap_write(priv->reg_base, priv->phy_data->pcie_conf,
		     HIWORD_UPDATE(data,
				   PHY_CFG_DATA_MASK,
				   PHY_CFG_DATA_SHIFT) |
		     HIWORD_UPDATE(addr,
				   PHY_CFG_ADDR_MASK,
				   PHY_CFG_ADDR_SHIFT));
	udelay(1);
	regmap_write(priv->reg_base, priv->phy_data->pcie_conf,
		     HIWORD_UPDATE(PHY_CFG_WR_ENABLE,
				   PHY_CFG_WR_MASK,
				   PHY_CFG_WR_SHIFT));
	udelay(1);
	regmap_write(priv->reg_base, priv->phy_data->pcie_conf,
		     HIWORD_UPDATE(PHY_CFG_WR_DISABLE,
				   PHY_CFG_WR_MASK,
				   PHY_CFG_WR_SHIFT));
}

static int rockchip_pcie_phy_power_on(struct phy *phy)
{
	struct rockchip_pcie_phy *priv = dev_get_priv(phy->dev);
	struct rockchip_pcie_phy_plat *plat_phy = &priv->phy_plat[phy->id];
	int ret = 0;
	u32 status;

	ret = reset_assert(&plat_phy->phy_rst);
	if (ret) {
		dev_err(dev, "failed to assert phy reset\n");
		return ret;
	}

	regmap_write(priv->reg_base, priv->phy_data->pcie_conf,
		     HIWORD_UPDATE(PHY_CFG_PLL_LOCK,
				   PHY_CFG_ADDR_MASK,
				   PHY_CFG_ADDR_SHIFT));

	regmap_write(priv->reg_base,
		     priv->phy_data->pcie_laneoff,
		     HIWORD_UPDATE(!PHY_LANE_IDLE_OFF,
				   PHY_LANE_IDLE_MASK,
				   PHY_LANE_IDLE_A_SHIFT + plat_phy->index));

	ret = -EINVAL;
	ret = regmap_read_poll_timeout(priv->reg_base,
				       priv->phy_data->pcie_status,
				       status, !(status & PHY_PLL_LOCKED),
				       20 * 1000, 50);
	if (ret) {
		dev_err(&phy->dev, "pll lock timeout!\n");
		goto err_pll_lock;
	}
#if 0
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	/* timeout */
	ret = -EINVAL;
	while (time_before(jiffies, timeout)) {
		regmap_read(priv->reg_base,
			    priv->phy_data->pcie_status,
			    &status);
		if (status & PHY_PLL_LOCKED) {
			dev_dbg(&phy->dev, "pll locked!\n");
			ret = 0;
			break;
		}
		msleep(20);
	}

	if (ret) {
		dev_err(&phy->dev, "pll lock timeout!\n");
		goto err_pll_lock;
	}
#endif

	phy_wr_cfg(priv, PHY_CFG_CLK_TEST, PHY_CFG_SEPE_RATE);
	phy_wr_cfg(priv, PHY_CFG_CLK_SCC, PHY_CFG_PLL_100M);

	ret = -ETIMEDOUT;
	ret = regmap_read_poll_timeout(priv->reg_base,
				       priv->phy_data->pcie_status,
				       status, (status & PHY_PLL_OUTPUT),
				       20 * 1000, 50);
	if (ret) {
		dev_err(&phy->dev, "pll output enable timeout!\n");
		goto err_pll_lock;
	}
#if 0	
	/* timeout */
	ret = -ETIMEDOUT;
	while (time_before(jiffies, timeout)) {
		regmap_read(priv->reg_base,
			    priv->phy_data->pcie_status,
			    &status);
		if (!(status & PHY_PLL_OUTPUT)) {
			dev_dbg(&phy->dev, "pll output enable done!\n");
			ret = 0;
			break;
		}
		msleep(20);
	}

	if (ret) {
		dev_err(&phy->dev, "pll output enable timeout!\n");
		goto err_pll_lock;
	}
#endif

	regmap_write(priv->reg_base, priv->phy_data->pcie_conf,
		     HIWORD_UPDATE(PHY_CFG_PLL_LOCK,
				   PHY_CFG_ADDR_MASK,
				   PHY_CFG_ADDR_SHIFT));

	ret = -EINVAL;
	ret = regmap_read_poll_timeout(priv->reg_base,
				       priv->phy_data->pcie_status,
				       status, !(status & PHY_PLL_LOCKED),
				       20 * 1000, 50);
	if (ret) {
		dev_err(&phy->dev, "pll relock timeout!\n");
		goto err_pll_lock;
	}
#if 0	
	/* timeout */
	ret = -EINVAL;
	while (time_before(jiffies, timeout)) {
		regmap_read(priv->reg_base,
			    priv->phy_data->pcie_status,
			    &status);
		if (status & PHY_PLL_LOCKED) {
			dev_dbg(&phy->dev, "pll relocked!\n");
			ret = 0;
			break;
		}
		msleep(20);
	}

	if (ret) {
		dev_err(&phy->dev, "pll relock timeout!\n");
		goto err_pll_lock;
	}
#endif

	return 0;

err_pll_lock:
	reset_assert(&plat_phy->phy_rst);

	return ret;
}

static int rockchip_pcie_phy_power_off(struct phy *phy)
{
	struct rockchip_pcie_phy *priv = dev_get_priv(phy->dev);
	struct rockchip_pcie_phy_plat *plat_phy = &priv->phy_plat[phy->id];
	int ret;

	regmap_write(priv->reg_base,
		     priv->phy_data->pcie_laneoff,
		     HIWORD_UPDATE(PHY_LANE_IDLE_OFF,
				   PHY_LANE_IDLE_MASK,
				   PHY_LANE_IDLE_A_SHIFT + plat_phy->index));

	ret = reset_assert(&plat_phy->phy_rst);
	if (ret) {
		dev_err(dev, "failed to assert phy reset\n");
		return ret;
	}

	return 0;	
}

static int rockchip_pcie_phy_init(struct phy *phy)
{
	struct rockchip_pcie_phy *priv = dev_get_priv(phy->dev);
	struct rockchip_pcie_phy_plat *plat_phy = &priv->phy_plat[phy->id];
	int ret;

	ret = clk_enable(&plat_phy->refclk);
	if (ret) {
		dev_err(dev, "failed to enable refclk clock\n");
		return ret;
	}

	ret = reset_assert(&plat_phy->phy_rst);
	if (ret) {
		dev_err(dev, "failed to assert phy reset\n");
		goto err_reset;
	}

	return 0;

err_reset:
	clk_disable(&plat_phy->refclk);
	return ret;
}

static int rockchip_pcie_phy_exit(struct phy *phy)
{
	struct rockchip_pcie_phy *priv = dev_get_priv(phy->dev);
	struct rockchip_pcie_phy_plat *plat_phy = &priv->phy_plat[phy->id];

	clk_disable(&plat_phy->refclk);

	return 0;
}

static struct phy_ops rockchip_pcie_phy_ops = {
	.init = rockchip_pcie_phy_init,
	.power_on = rockchip_pcie_phy_power_on,
	.power_off = rockchip_pcie_phy_power_off,
	.exit = rockchip_pcie_phy_exit,
};

static int rockchip_pcie_phy_probe(struct udevice *dev)
{
	struct rockchip_pcie_phy_plat *plat = dev_get_platdata(dev);
	struct rockchip_pcie_phy *priv = dev_get_priv(dev);
	u32 phy_num;
	int i, ret;

	priv->phy_data = (const struct rockchip_pcie_phy_data *)dev_get_driver_data(dev);
	if (!priv->phy_data)
		return -EINVAL;

	priv->reg_base = syscon_node_to_regmap(dev_get_parent(dev)->node);
	if (IS_ERR(priv->reg_base))
		return PTR_ERR(priv->reg_base);

	ret = ofnode_read_u32(dev_ofnode(dev), "#phy-cells", &phy_num);
	if (ret)
		return ret;

	phy_num = (phy_num == 0) ? 1 : PHY_MAX_LANE_NUM;

	dev_info(dev, "PHY NUMBER is %d\n", phy_num);
	for (i = 0; i < phy_num; i++) {
		struct rockchip_pcie_phy_plat *phy_plat = &plat[i];

		ret = clk_get_by_name(dev, "refclk", &phy_plat->refclk);
		if (ret) {
			dev_err(dev, "failed to get refclk clock phandle\n");
			return ret;
		}

		ret = reset_get_by_name(dev, "phy", &phy_plat->phy_rst);
		if (ret) {
			dev_err(dev, "failed to get phy reset phandle\n");
			return ret;
		}

		phy_plat->index = i;
	}

	dev_info(dev, "Rockchip PCIe PHY driver loaded\n");

	return 0;
}

static const struct rockchip_pcie_phy_data rk3399_pcie_data = {
	.pcie_conf = 0xe220,
	.pcie_status = 0xe2a4,
	.pcie_laneoff = 0xe214,
};

static const struct udevice_id rockchip_pcie_phy_ids[] = {
	{ 
		.compatible = "rockchip,rk3399-pcie",
		.data = (ulong)&rk3399_pcie_data,
	},
	{ /* sentile */ }
};

U_BOOT_DRIVER(rockchip_pcie_phy) = {
	.name	= "rockchip_pcie_phy",
	.id	= UCLASS_PHY,
	.of_match = rockchip_pcie_phy_ids,
	.ops = &rockchip_pcie_phy_ops,
	.probe = rockchip_pcie_phy_probe,
	.platdata_auto_alloc_size = sizeof(struct rockchip_pcie_phy_plat[PHY_MAX_LANE_NUM]),
	.priv_auto_alloc_size = sizeof(struct rockchip_pcie_phy_data),
};
