// SPDX-License-Identifier: GPL-2.0+ or ISC
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
 */

#include <common.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <pci.h>
#include <generic-phy.h>
#include <power-domain.h>
#include <power/regulator.h>
#include <regmap.h>
#include <reset.h>
#include <syscon.h>
#include <asm/io.h>
#include <asm-generic/gpio.h>
#include <asm/arch-rockchip/clock.h>

#include <linux/iopoll.h>

DECLARE_GLOBAL_DATA_PTR;

#define HIWORD_UPDATE(mask, val)        (((mask) << 16) | (val))
#define HIWORD_UPDATE_BIT(val)          HIWORD_UPDATE(val, val)

#define ENCODE_LANES(x)                 ((((x) >> 1) & 3) << 4)
#define PCIE_CLIENT_BASE                0x0
#define PCIE_CLIENT_CONFIG              (PCIE_CLIENT_BASE + 0x00)
#define   PCIE_CLIENT_LINK_TRAIN_ENABLE   HIWORD_UPDATE_BIT(0x0002)
#define   PCIE_CLIENT_ARI_ENABLE          HIWORD_UPDATE_BIT(0x0008)
#define   PCIE_CLIENT_CONF_LANE_NUM(x)    HIWORD_UPDATE(0x0030, ENCODE_LANES(x))
#define   PCIE_CLIENT_GEN_SEL_1           HIWORD_UPDATE(0x0080, 0)
#define PCIE_CLIENT_BASIC_STRAP_CONF	0x0000
#define  PCIE_CLIENT_PCIE_GEN_SEL_1	(((1 << 7) << 16) | (0 << 7))
#define  PCIE_CLIENT_PCIE_GEN_SEL_2	(((1 << 7) << 16) | (1 << 7))
#define  PCIE_CLIENT_MODE_SELECT_RC	(((1 << 6) << 16) | (1 << 6))
#define  PCIE_CLIENT_LINK_TRAIN_EN	(((1 << 1) << 16) | (1 << 1))
#define  PCIE_CLIENT_CONF_EN		(((1 << 0) << 16) | (1 << 0))

#define PCIE_CLIENT_BASIC_STATUS1	0x0048
#define PCIE_CLIENT_LINK_STATUS_UP	GENMASK(21, 20)
#define PCIE_CLIENT_LINK_STATUS_MASK	GENMASK(21, 20)
#define PCIE_LINK_UP(x) \
	(((x) & PCIE_CLIENT_LINK_STATUS_MASK) == PCIE_CLIENT_LINK_STATUS_UP)

#define  PCIE_CLIENT_LINK_ST		(0x3 << 20)
#define  PCIE_CLIENT_LINK_ST_UP		(0x3 << 20)
#define PCIE_CLIENT_INT_MASK		0x004c
#define  PCIE_CLIENT_INTD_MASK		(((1 << 8) << 16) | (1 << 8))
#define  PCIE_CLIENT_INTD_UNMASK	(((1 << 8) << 16) | (0 << 8))
#define  PCIE_CLIENT_INTC_MASK		(((1 << 7) << 16) | (1 << 7))
#define  PCIE_CLIENT_INTC_UNMASK	(((1 << 7) << 16) | (0 << 7))
#define  PCIE_CLIENT_INTB_MASK		(((1 << 6) << 16) | (1 << 6))
#define  PCIE_CLIENT_INTB_UNMASK	(((1 << 6) << 16) | (0 << 6))
#define  PCIE_CLIENT_INTA_MASK		(((1 << 5) << 16) | (1 << 5))
#define  PCIE_CLIENT_INTA_UNMASK	(((1 << 5) << 16) | (0 << 5))

#define PCIE_RC_NORMAL_BASE		0x800000

#define PCIE_LM_BASE			0x900000
#define PCIE_LM_VENDOR_ID		(PCIE_LM_BASE + 0x44)
#define  PCIE_LM_VENDOR_ROCKCHIP	0x1d87
#define PCIE_LM_RCBAR			(PCIE_LM_BASE + 0x300)
#define  PCIE_LM_RCBARPIE		(1 << 19)
#define  PCIE_LM_RCBARPIS		(1 << 20)

#define PCIE_RC_BASE			0xa00000
#define PCIE_RC_CONFIG_DCR		(PCIE_RC_BASE + 0x0c4)
#define PCIE_RC_CONFIG_DCR_CSPL_SHIFT	18
#define PCIE_RC_CONFIG_DCR_CPLS_SHIFT	26
#define PCIE_RC_PCIE_LCAP		(PCIE_RC_BASE + 0x0cc)
#define  PCIE_RC_PCIE_LCAP_APMS_L0S	(1 << 10)

#define PCIE_ATR_BASE			0xc00000
#define PCIE_ATR_OB_ADDR0(i)		(PCIE_ATR_BASE + 0x000 + (i) * 0x20)
#define PCIE_ATR_OB_ADDR1(i)		(PCIE_ATR_BASE + 0x004 + (i) * 0x20)
#define PCIE_ATR_OB_DESC0(i)		(PCIE_ATR_BASE + 0x008 + (i) * 0x20)
#define PCIE_ATR_OB_DESC1(i)		(PCIE_ATR_BASE + 0x00c + (i) * 0x20)
#define PCIE_ATR_IB_ADDR0(i)		(PCIE_ATR_BASE + 0x800 + (i) * 0x8)
#define PCIE_ATR_IB_ADDR1(i)		(PCIE_ATR_BASE + 0x804 + (i) * 0x8)
#define  PCIE_ATR_HDR_MEM		0x2
#define  PCIE_ATR_HDR_IO		0x6
#define  PCIE_ATR_HDR_CFG_TYPE0		0xa
#define  PCIE_ATR_HDR_CFG_TYPE1		0xb
#define  PCIE_ATR_HDR_RID		(1 << 23)

#define PCIE_ATR_OB_REGION0_SIZE	(32 * 1024 * 1024)
#define PCIE_ATR_OB_REGION_SIZE		(1 * 1024 * 1024)

#define RK3399_GRF_SOC_CON5_PCIE	0xe214
#define  RK3399_TX_ELEC_IDLE_OFF_MASK	((1 << 3) << 16)
#define  RK3399_TX_ELEC_IDLE_OFF	(1 << 3)
#define RK3399_GRF_SOC_CON8		0xe220
#define  RK3399_PCIE_TEST_DATA_MASK	((0xf << 7) << 16)
#define  RK3399_PCIE_TEST_DATA_SHIFT	7
#define  RK3399_PCIE_TEST_ADDR_MASK	((0x3f << 1) << 16)
#define  RK3399_PCIE_TEST_ADDR_SHIFT	1
#define  RK3399_PCIE_TEST_WRITE_ENABLE	(((1 << 0) << 16) | (1 << 0))
#define  RK3399_PCIE_TEST_WRITE_DISABLE	(((1 << 0) << 16) | (0 << 0))
#define RK3399_GRF_SOC_STATUS1		0xe2a4
#define  RK3399_PCIE_PHY_PLL_LOCKED	(1 << 9)
#define  RK3399_PCIE_PHY_PLL_OUTPUT	(1 << 10)

#define RK3399_PCIE_PHY_CFG_PLL_LOCK	0x10
#define RK3399_PCIE_PHY_CFG_CLK_TEST	0x10
#define  RK3399_PCIE_PHY_CFG_SEPE_RATE	(1 << 3)
#define RK3399_PCIE_PHY_CFG_CLK_SCC	0x12
#define  RK3399_PCIE_PHY_CFG_PLL_100M	(1 << 3)

#define PCIE_PHY_COUNT			4

/**
 * struct rockchip_pcie - Rockchip PCIe controller state
 *
 * @api_base: The base address of apb register space
 * @axi_base: The base address of axi register space
 * @first_busno: This driver supports multiple PCIe controllers.
 *               first_busno stores the bus number of the PCIe root-port
 *               number which may vary depending on the PCIe setup
 *               (PEX switches etc).
 */
struct rockchip_pcie {
	fdt_addr_t axi_base;
	fdt_addr_t apb_base;
	int first_busno;
	u32 lanes;
	struct udevice *dev;

	/* Resets */
	struct reset_ctl aclk_ctl;
	struct reset_ctl core_ctl;
	struct reset_ctl mgmt_ctl;
	struct reset_ctl mgmt_sticky_ctl;
	struct reset_ctl pclk_ctl;
	struct reset_ctl pipe_ctl;
	struct reset_ctl pm_ctl;
	struct reset_ctl phy_ctl;

	/* GPIO */
	struct gpio_desc ep_gpio;

	/* vpcie regulators */
	struct udevice *vpcie12v;
	struct udevice *vpcie3v3;
	struct udevice *vpcie1v8;
	struct udevice *vpcie0v9;

	/* PHY */
	struct phy phys[PCIE_PHY_COUNT];
};

/**
 * rockchip_pcie_read_config() - Read from configuration space
 *
 * @bus: Pointer to the PCI bus
 * @bdf: Identifies the PCIe device to access
 * @offset: The offset into the device's configuration space
 * @valuep: A pointer at which to store the read value
 * @size: Indicates the size of access to perform
 *
 * Read a value of size @size from offset @offset within the configuration
 * space of the device identified by the bus, device & function numbers in @bdf
 * on the PCI bus @bus.
 *
 * Return: 0 on success
 */
static int rockchip_pcie_read_config(const struct udevice *bus, pci_dev_t bdf,
				  uint offset, ulong *valuep,
				  enum pci_size_t size)
{
	struct rockchip_pcie *priv = dev_get_priv(bus);
	ulong value;
	u32 off;

	off = (PCI_BUS(bdf) << 20) | (PCI_DEV(bdf) << 15) |
	    (PCI_FUNC(bdf) << 12) | (offset & ~0x3);

	if ((PCI_BUS(bdf) == priv->first_busno) && (PCI_DEV(bdf) == 0)) {
		value = readl(priv->apb_base + PCIE_RC_NORMAL_BASE + off);
		*valuep = pci_conv_32_to_size(value, offset, size);
		return 0;
	}
	if ((PCI_BUS(bdf) == priv->first_busno + 1) && (PCI_DEV(bdf) == 0)) {
		value = readl(priv->axi_base + off);
		*valuep = pci_conv_32_to_size(value, offset, size);
		return 0;
	}

	*valuep = pci_get_ff(size);
	return 0;
}

/**
 * rockchip_pcie_write_config() - Write to configuration space
 *
 * @bus: Pointer to the PCI bus
 * @bdf: Identifies the PCIe device to access
 * @offset: The offset into the device's configuration space
 * @value: The value to write
 * @size: Indicates the size of access to perform
 *
 * Write the value @value of size @size from offset @offset within the
 * configuration space of the device identified by the bus, device & function
 * numbers in @bdf on the PCI bus @bus.
 *
 * Return: 0 on success
 */
static int rockchip_pcie_write_config(struct udevice *bus, pci_dev_t bdf,
				   uint offset, ulong value,
				   enum pci_size_t size)
{
	struct rockchip_pcie *priv = dev_get_priv(bus);
	ulong old;
	u32 off;

	off = (PCI_BUS(bdf) << 20) | (PCI_DEV(bdf) << 15) |
	    (PCI_FUNC(bdf) << 12) | (offset & ~0x3);

	if ((PCI_BUS(bdf) == priv->first_busno) && (PCI_DEV(bdf) == 0)) {
		old = readl(priv->apb_base + PCIE_RC_NORMAL_BASE + off);
		value = pci_conv_size_to_32(old, value, offset, size);
		writel(value, priv->apb_base + PCIE_RC_NORMAL_BASE + off);
		return 0;
	}
	if ((PCI_BUS(bdf) == priv->first_busno + 1) && (PCI_DEV(bdf) == 0)) {
		old = readl(priv->axi_base + off);
		value = pci_conv_size_to_32(old, value, offset, size);
		writel(value, priv->axi_base + off);
		return 0;
	}

	return 0;
}

#if 0
static void rockchip_pcie_phy_write_conf(struct rockchip_pcie *priv,
					 uint8_t addr, uint8_t data)
{
	writel(RK3399_PCIE_TEST_ADDR_MASK |
	    (addr << RK3399_PCIE_TEST_ADDR_SHIFT) |
	    RK3399_PCIE_TEST_DATA_MASK |
	    (data << RK3399_PCIE_TEST_DATA_SHIFT) |
	    RK3399_PCIE_TEST_WRITE_DISABLE,
	    priv->phy_regs + RK3399_GRF_SOC_CON8);
	udelay(1);
	writel(RK3399_PCIE_TEST_WRITE_ENABLE,
	    priv->phy_regs + RK3399_GRF_SOC_CON8);
	udelay(1);
	writel(RK3399_PCIE_TEST_WRITE_DISABLE,
	    priv->phy_regs + RK3399_GRF_SOC_CON8);
}

static int rockchip_pcie_phy_init(struct rockchip_pcie *priv)
{
	int ret;

	ret = reset_get_by_index_nodev(priv->phy_node, 0, &priv->phy_ctl);
	if (ret) {
		printf("%s: phy_ctl not found\n", __func__);
		return -EINVAL;
	}

	/* XXX clock enable refclk */
	reset_assert(&priv->phy_ctl);
	return 0;
}


static int rockchip_pcie_phy_poweron(struct rockchip_pcie *priv)
{
	int timo, lane = 0;
	u32 status;

	reset_deassert(&priv->phy_ctl);

	priv->phy_regs = (u64)syscon_get_first_range(ROCKCHIP_SYSCON_GRF);

	writel(RK3399_PCIE_TEST_ADDR_MASK |
	    RK3399_PCIE_PHY_CFG_PLL_LOCK << RK3399_PCIE_TEST_ADDR_SHIFT,
	    priv->phy_regs + RK3399_GRF_SOC_CON8);
	writel(RK3399_TX_ELEC_IDLE_OFF_MASK << lane | 0,
	    priv->phy_regs + RK3399_GRF_SOC_CON5_PCIE);

	for (timo = 50; timo > 0; timo--) {
		status = readl(priv->phy_regs + RK3399_GRF_SOC_STATUS1);
		if (status & RK3399_PCIE_PHY_PLL_LOCKED)
			break;
		udelay(20000);
	}
	if (timo == 0)
		return -ENXIO;

	rockchip_pcie_phy_write_conf(priv, RK3399_PCIE_PHY_CFG_CLK_TEST,
	    RK3399_PCIE_PHY_CFG_SEPE_RATE);
	rockchip_pcie_phy_write_conf(priv, RK3399_PCIE_PHY_CFG_CLK_SCC,
	    RK3399_PCIE_PHY_CFG_PLL_100M);

	for (timo = 50; timo > 0; timo--) {
		status = readl(priv->phy_regs + RK3399_GRF_SOC_STATUS1);
		if ((status & RK3399_PCIE_PHY_PLL_OUTPUT) == 0)
			break;
		udelay(20000);
	}
	if (timo == 0)
		return -ENXIO;

	writel(RK3399_PCIE_TEST_ADDR_MASK |
	    RK3399_PCIE_PHY_CFG_PLL_LOCK << RK3399_PCIE_TEST_ADDR_SHIFT,
	    priv->phy_regs + RK3399_GRF_SOC_CON8);

	for (timo = 50; timo > 0; timo--) {
		status = readl(priv->phy_regs + RK3399_GRF_SOC_STATUS1);
		if (status & RK3399_PCIE_PHY_PLL_LOCKED)
			break;
		udelay(20000);
	}
	if (timo == 0)
		return -ENXIO;
	return 0;
}
#endif

static int rockchip_pcie_atr_init(struct rockchip_pcie *priv)
{
	struct udevice *ctlr = pci_get_controller(priv->dev);
	struct pci_controller *hose = dev_get_uclass_priv(ctlr);
	u64 addr, size, offset;
	u32 type;
	int i, region;

	/* Use region 0 to map PCI configuration space. */
	writel(25 - 1, priv->apb_base + PCIE_ATR_OB_ADDR0(0));
	writel(0, priv->apb_base + PCIE_ATR_OB_ADDR1(0));
	writel(PCIE_ATR_HDR_CFG_TYPE0 | PCIE_ATR_HDR_RID,
	    priv->apb_base + PCIE_ATR_OB_DESC0(0));
	writel(0, priv->apb_base + PCIE_ATR_OB_DESC1(0));

	for (i = 0; i < hose->region_count; i++) {
		if (hose->regions[i].flags == PCI_REGION_SYS_MEMORY)
			continue;

		if (hose->regions[i].flags == PCI_REGION_IO)
			type = PCIE_ATR_HDR_IO;
		else
			type = PCIE_ATR_HDR_MEM;

		/* Only support identity mappings. */
		if (hose->regions[i].bus_start !=
		    hose->regions[i].phys_start)
			return -EINVAL;

		/* Only support mappings aligned on a region boundary. */
		addr = hose->regions[i].bus_start;
		if (addr & (PCIE_ATR_OB_REGION_SIZE - 1))
			return -EINVAL;

		/* Mappings should lie between AXI and APB regions. */
		size = hose->regions[i].size;
		if (addr < (u64)priv->axi_base + PCIE_ATR_OB_REGION0_SIZE)
			return -EINVAL;
		if (addr + size > (u64)priv->apb_base)
			return -EINVAL;

		offset = addr - (u64)priv->axi_base - PCIE_ATR_OB_REGION0_SIZE;
		region = 1 + (offset / PCIE_ATR_OB_REGION_SIZE);
		while (size > 0) {
			writel(32 - 1, priv->apb_base + PCIE_ATR_OB_ADDR0(region));
			writel(0, priv->apb_base + PCIE_ATR_OB_ADDR1(region));
			writel(type | PCIE_ATR_HDR_RID,
			    priv->apb_base + PCIE_ATR_OB_DESC0(region));
			writel(0, priv->apb_base + PCIE_ATR_OB_DESC1(region));

			addr += PCIE_ATR_OB_REGION_SIZE;
			size -= PCIE_ATR_OB_REGION_SIZE;
			region++;
		}
	}

	/* Passthrough inbound translations unmodified. */
	writel(32 - 1, priv->apb_base + PCIE_ATR_IB_ADDR0(2));
	writel(0, priv->apb_base + PCIE_ATR_IB_ADDR1(2));

	return 0;
}

static int rockchip_pcie_init_port(struct udevice *dev)
{
	struct rockchip_pcie *priv = dev_get_priv(dev);
	u32 cr, val, status;
	int i, ret;

	dm_gpio_set_value(&priv->ep_gpio, 0);

	reset_assert(&priv->aclk_ctl);
	reset_assert(&priv->pclk_ctl);
	reset_assert(&priv->pm_ctl);

	/* phy init */
	for (i = 0; i < PCIE_PHY_COUNT; i++) {
		if (!generic_phy_valid(&priv->phys[i]))
			continue;

		ret = generic_phy_init(&priv->phys[i]);
		if (ret) {
			printf("Error PHY%d init\n", i);
			return ret;
		}
	}

	reset_assert(&priv->core_ctl);
	reset_assert(&priv->mgmt_ctl);
	reset_assert(&priv->mgmt_sticky_ctl);
	reset_assert(&priv->pipe_ctl);

	udelay(10);

	reset_deassert(&priv->aclk_ctl);
	reset_deassert(&priv->pclk_ctl);
	reset_deassert(&priv->pm_ctl);

	cr = PCIE_CLIENT_GEN_SEL_1;
	cr = PCIE_CLIENT_LINK_TRAIN_ENABLE | PCIE_CLIENT_ARI_ENABLE |
	     PCIE_CLIENT_CONF_LANE_NUM(priv->lanes);

	writel(cr, priv->apb_base + PCIE_CLIENT_CONFIG);

	/* phy power on */
	for (i = 0; i < PCIE_PHY_COUNT; i++) {
		if (!generic_phy_valid(&priv->phys[i]))
			continue;

		ret = generic_phy_power_on(&priv->phys[i]);
		if (ret) {
			printf("Error PHY%d power on\n", i);
			return ret;
		}
	}

	reset_deassert(&priv->core_ctl);
	reset_deassert(&priv->mgmt_ctl);
	reset_deassert(&priv->mgmt_sticky_ctl);
	reset_deassert(&priv->pipe_ctl);

	/* Enable Gen1 training */
	writel(PCIE_CLIENT_LINK_TRAIN_ENABLE,
	       priv->apb_base + PCIE_CLIENT_CONFIG);

	dm_gpio_set_value(&priv->ep_gpio, 1);

	ret = readl_poll_sleep_timeout(
			priv->apb_base + PCIE_CLIENT_BASIC_STATUS1,
			status, PCIE_LINK_UP(status), 20, 500 * 1000);
	if (ret) {
		dev_err(dev, "PCIe link training gen1 timeout!\n");
		return ret;
	}

	/* Initialize Root Complex registers. */
	writel(PCIE_LM_VENDOR_ROCKCHIP, priv->apb_base + PCIE_LM_VENDOR_ID);
	writel(PCI_CLASS_BRIDGE_PCI << 16, priv->apb_base +
	    PCIE_RC_BASE + PCI_CLASS_REVISION);
	writel(PCIE_LM_RCBARPIE | PCIE_LM_RCBARPIS,
	    priv->apb_base + PCIE_LM_RCBAR);

	if (dev_read_bool(dev, "aspm-no-l0s")) {
		val = readl(priv->apb_base + PCIE_RC_PCIE_LCAP);
		val &= ~PCIE_RC_PCIE_LCAP_APMS_L0S;
		writel(val, priv->apb_base + PCIE_RC_PCIE_LCAP);
	}

	/* Configure Address Translation. */
	if (rockchip_pcie_atr_init(priv)) {
		printf("PCIE-%d: ATR init failed\n", dev->seq);
		return -ENODEV;
	}

	return 0;
}

static int rockchip_pcie_parse_dt(struct udevice *dev)
{
	struct rockchip_pcie *priv = dev_get_priv(dev);
	int i, ret;

	priv->axi_base = dev_read_addr_name(dev, "axi-base");
	if (!priv->axi_base)
		return -ENODEV;

	priv->apb_base = dev_read_addr_name(dev, "apb-base");
	if (!priv->axi_base)
		return -ENODEV;

	ret = gpio_request_by_name(dev, "ep-gpios", 0, &priv->ep_gpio, GPIOD_IS_OUT);	
	if (ret) {
		dev_err(dev, "failed to find ep-gpios property\n");
		return ret;
	}

	ret = dev_read_u32(dev, "num-lanes", &priv->lanes);
	if (!ret && (priv->lanes == 0 ||
		     priv->lanes == 3 ||
		     priv->lanes > 4)) {
		printf("%d is invalid num-lanes, default to use 1 lane\n",
		       priv->lanes);
		priv->lanes = 1;
	}	

	if (reset_get_by_name(dev, "aclk", &priv->aclk_ctl) ||
	    reset_get_by_name(dev, "core", &priv->core_ctl) ||
	    reset_get_by_name(dev, "mgmt", &priv->mgmt_ctl) ||
	    reset_get_by_name(dev, "mgmt-sticky", &priv->mgmt_sticky_ctl) ||
	    reset_get_by_name(dev, "pclk", &priv->pclk_ctl) ||
	    reset_get_by_name(dev, "pipe", &priv->pipe_ctl) ||
	    reset_get_by_name(dev, "pm", &priv->pm_ctl)) {
		printf("failed to get resets\n");
		return -ENODEV;
	}

	ret = device_get_supply_regulator(dev, "vpcie3v3-supply", &priv->vpcie3v3);
	if (ret) {
		printf("No vpcie3v3 regulator(ret=%d)\n", ret);
	} else {
		ret = regulator_set_enable(priv->vpcie3v3, true);
		if (ret)
			printf("failed to enable vpcie3v3\n");
	}

	ret = device_get_supply_regulator(dev, "vpcie1v8-supply", &priv->vpcie1v8);
	if (ret) {
		printf("no vpcie1v8 regulator(ret=%d)\n", ret);
	} else {
		ret = regulator_set_enable(priv->vpcie1v8, true);
		if (ret)
			printf("failed to enable vpcie1v8\n");
	}

	ret = device_get_supply_regulator(dev, "vpcie0v9-supply", &priv->vpcie0v9);
	if (ret) {
		printf("no vpcie0v9 regulator(ret=%d)\n", ret);
	} else {
		ret = regulator_set_enable(priv->vpcie0v9, true);
		if (ret)
			printf("failed to enable vpcie0v9\n");
	}

#if 0
	for (i = 0; i < PCIE_PHY_COUNT; i++) {
		int index;
		char phy_name[16];

		snprintf(phy_name, sizeof(phy_name), "pcie-phy-%d", i);

		index = dev_read_stringlist_search(dev, "phy-names", phy_name);
		printf("name %s, index %d\n", phy_name, index);

//		ret = generic_phy_get_by_name(dev, name, &priv->phys[i]);
		ret = generic_phy_get_by_index(dev, index, &priv->phys[i]);
		if (ret == -ENOENT) {
			printf("-ENOENT phy %d\n", i);
                        continue;
		}

		if (ret) {
			printf("failed get phy %d\n", i);
			return ret;
		}
	}
#endif

	ret = generic_phy_get_by_name(dev, "pciephy", &priv->phys[0]);
	if (ret) {
		printf("failed get phy\n");
		return ret;
	}

	return 0;
}

/**
 * rockchip_pcie_probe() - Probe the PCIe bus for active link
 *
 * @dev: A pointer to the device being operated on
 *
 * Probe for an active link on the PCIe bus and configure the controller
 * to enable this port.
 *
 * Return: 0 on success, else -ENODEV
 */
static int rockchip_pcie_probe(struct udevice *dev)
{
	struct rockchip_pcie *priv = dev_get_priv(dev);
	struct udevice *ctlr = pci_get_controller(dev);
	struct pci_controller *hose = dev_get_uclass_priv(ctlr);
	int ret;

	priv->first_busno = dev->seq;
	priv->dev = dev;

	ret = rockchip_pcie_parse_dt(dev);
	if (ret) {
		printf("parse\n");
		return ret;
	}

#if 0
	ret = rockchip_pcie_enable_clocks(priv);
	if (ret)
		return ret;

	ret = rockchip_pcie_set_vpcie(priv);
	if (ret)
		return ret;
#endif

	ret = rockchip_pcie_init_port(dev);
	if (ret) {
		printf("init\n");
		return ret;
	}
#if 0
	dm_gpio_set_value(&priv->ep_gpio, 0);

	reset_assert(&priv->aclk_ctl);
	reset_assert(&priv->pclk_ctl);
	reset_assert(&priv->pm_ctl);

	if (rockchip_pcie_phy_init(priv)) {
		printf("PCIE-%d: Link down\n", dev->seq);
		return -ENODEV;
	}

	reset_assert(&priv->core_ctl);
	reset_assert(&priv->mgmt_ctl);
	reset_assert(&priv->mgmt_sticky_ctl);
	reset_assert(&priv->pipe_ctl);

	udelay(10);

	reset_deassert(&priv->aclk_ctl);
	reset_deassert(&priv->pclk_ctl);
	reset_deassert(&priv->pm_ctl);

	/* Only advertise Gen 1 support for now. */
	writel(PCIE_CLIENT_PCIE_GEN_SEL_1,
	    priv->apb_base + PCIE_CLIENT_BASIC_STRAP_CONF);

	/* Switch into Root Complex mode. */
	writel(PCIE_CLIENT_MODE_SELECT_RC | PCIE_CLIENT_CONF_EN,
	    priv->apb_base + PCIE_CLIENT_BASIC_STRAP_CONF);

	if (rockchip_pcie_phy_poweron(priv)) {
		printf("PCIE-%d: Power down\n", dev->seq);
		return -ENODEV;
	}

	reset_deassert(&priv->core_ctl);
	reset_deassert(&priv->mgmt_ctl);
	reset_deassert(&priv->mgmt_sticky_ctl);
	reset_deassert(&priv->pipe_ctl);

	/* Start link training. */
	writel(PCIE_CLIENT_LINK_TRAIN_EN,
	    priv->apb_base + PCIE_CLIENT_BASIC_STRAP_CONF);

	/* XXX Advertise power limits? */

	dm_gpio_set_value(&priv->ep_gpio, 1);

	ret = readl_poll_sleep_timeout(
			priv->apb_base + PCIE_CLIENT_BASIC_STATUS1,
			status, PCIE_LINK_UP(status), 20, 500 * 1000);
	if (ret) {
		dev_err(dev, "PCIe link training gen1 timeout!\n");
		return ret;
	}

	/* Initialize Root Complex registers. */
	writel(PCIE_LM_VENDOR_ROCKCHIP, priv->apb_base + PCIE_LM_VENDOR_ID);
	writel(PCI_CLASS_BRIDGE_PCI << 16, priv->apb_base +
	    PCIE_RC_BASE + PCI_CLASS_REVISION);
	writel(PCIE_LM_RCBARPIE | PCIE_LM_RCBARPIS,
	    priv->apb_base + PCIE_LM_RCBAR);

	if (dev_read_bool(dev, "aspm-no-l0s")) {
		val = readl(priv->apb_base + PCIE_RC_PCIE_LCAP);
		val &= ~PCIE_RC_PCIE_LCAP_APMS_L0S;
		writel(val, priv->apb_base + PCIE_RC_PCIE_LCAP);
	}

	/* Configure Address Translation. */
	if (rockchip_pcie_atr_init(priv)) {
		printf("PCIE-%d: ATR init failed\n", dev->seq);
		return -ENODEV;
	}
#endif

	dev_info(dev, "PCIE-%d: Link up (Bus%d)\n", dev->seq, hose->first_busno);

	return 0;
}

static const struct dm_pci_ops rockchip_pcie_ops = {
	.read_config	= rockchip_pcie_read_config,
	.write_config	= rockchip_pcie_write_config,
};

static const struct udevice_id rockchip_pcie_ids[] = {
	{ .compatible = "rockchip,rk3399-pcie" },
	{ }
};

U_BOOT_DRIVER(rockchip_pcie) = {
	.name			= "rockchip_pcie",
	.id			= UCLASS_PCI,
	.of_match		= rockchip_pcie_ids,
	.ops			= &rockchip_pcie_ops,
	.probe			= rockchip_pcie_probe,
	.priv_auto_alloc_size	= sizeof(struct rockchip_pcie),
};
