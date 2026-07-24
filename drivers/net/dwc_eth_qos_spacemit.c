// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023 Spacemit
 */

#include <common.h>
#include <clk.h>
#include <cpu_func.h>
#include <dm.h>
#include <errno.h>
#include <eth_phy.h>
#include <log.h>
#include <malloc.h>
#include <memalign.h>
#include <miiphy.h>
#include <net.h>
#include <netdev.h>
#include <phy.h>
#include <reset.h>
#include <wait_bit.h>
#include <asm/cache.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <linux/delay.h>

#include "dwc_eth_qos.h"

#define CLK_PHASE_CNT			256
#define CLK_PHASE_REVERT		180

#define TXCLK_PHASE_DEFAULT		0
#define RXCLK_PHASE_DEFAULT		0

#define TX_PHASE			1
#define RX_PHASE			0

enum clk_tuning_way {
	/* fpga clk tuning register */
	CLK_TUNING_BY_REG,
	/* zebu/evb rgmii delayline register */
	CLK_TUNING_BY_DLINE,
	/* evb rmii only revert tx/rx clock for clk tuning */
	CLK_TUNING_BY_CLK_REVERT,
	CLK_TUNING_MAX,
};

/**
 * private plat data
 */
struct spacemit_plat_data {
	struct udevice *dev;
	void __iomem *ctrl_reg;
	void __iomem *dline_reg;
	phy_interface_t phy_iface;
	u32 phy_reset_gpio;
	u8 tx_clk_phase;
	u8 rx_clk_phase;
	u8 clk_tuning_way;
	struct clk clk_phy;
	bool clk_tuning_enable;
	bool tx_clk_from_soc;
	bool phy_clk_from_soc;
};

static inline void dev_set_plat_priv(struct udevice *dev, void *priv)
{
	struct eth_pdata *plat = dev_get_plat(dev);

	plat->priv_pdata = priv;
}

static inline void *dev_get_plat_priv(struct udevice *dev)
{
	struct eth_pdata *plat = dev_get_plat(dev);

	return plat->priv_pdata;
}

__weak u32 k1pro_get_eqos_csr_clk(void)
{
	return 50 * 1000000;
}

__weak int k1pro_eqos_txclk_set_rate(unsigned long rate)
{
	return 0;
}

static ulong k1pro_eqos_get_tick_clk_rate(struct udevice *dev)
{
	return k1pro_get_eqos_csr_clk();
}

static int k1pro_eqos_probe_resources(struct udevice *dev)
{
	struct eqos_priv *eqos = dev_get_priv(dev);
	phy_interface_t interface;

	debug("%s(dev=%p):\n", __func__, dev);

	interface = eqos->config->interface(dev);

	if (interface == PHY_INTERFACE_MODE_NA) {
		pr_err("Invalid PHY interface\n");
		return -EINVAL;
	}

	debug("%s: OK\n", __func__);
	return 0;
}

static int k1pro_eqos_stop_resets(struct udevice *dev)
{
	struct reset_ctl_bulk reset_bulk;
	int ret;

	ret = reset_get_bulk(dev, &reset_bulk);
	if (ret)
		printf("%s, Can't get reset: %d\n", __func__, ret);
	else
		reset_assert_bulk(&reset_bulk);
	return 0;
}

static int k1pro_eqos_start_resets(struct udevice *dev)
{
	struct reset_ctl_bulk reset_bulk;
	int ret;

	ret = reset_get_bulk(dev, &reset_bulk);
	if (ret)
		printf("%s, Can't get reset: %d\n", __func__, ret);
	else
		reset_deassert_bulk(&reset_bulk);

	return 0;
}

static int k1pro_eqos_set_tx_clk_speed(struct udevice *dev)
{
	struct eqos_priv *eqos = dev_get_priv(dev);
	ulong rate;
	int ret;

	debug("%s(dev=%p):\n", __func__, dev);

	switch (eqos->phy->speed) {
	case SPEED_1000:
		rate = 125 * 1000 * 1000;
		break;
	case SPEED_100:
		rate = 25 * 1000 * 1000;
		break;
	case SPEED_10:
		rate = 2.5 * 1000 * 1000;
		break;
	default:
		pr_err("invalid speed %d\n", eqos->phy->speed);
		return -EINVAL;
	}

	ret = k1pro_eqos_txclk_set_rate(rate);
	if (ret < 0) {
		pr_err("spacemit (clk_tx, %lu) failed: %d\n", rate, ret);
		return ret;
	}

	return 0;
}

static int k1pro_eqos_get_enetaddr(struct udevice *dev)
{
	return 0;
}

static struct eqos_ops eqos_spacemit_k1pro_ops = {
	.eqos_inval_desc = eqos_inval_desc_generic,
	.eqos_flush_desc = eqos_flush_desc_generic,
	.eqos_inval_buffer = eqos_inval_buffer_generic,
	.eqos_flush_buffer = eqos_flush_buffer_generic,
	.eqos_probe_resources = k1pro_eqos_probe_resources,
	.eqos_remove_resources = eqos_null_ops,
	.eqos_stop_resets = k1pro_eqos_stop_resets,
	.eqos_start_resets = k1pro_eqos_start_resets,
	.eqos_stop_clks = eqos_null_ops,
	.eqos_start_clks = eqos_null_ops,
	.eqos_calibrate_pads = eqos_null_ops,
	.eqos_disable_calibration = eqos_null_ops,
	.eqos_set_tx_clk_speed = k1pro_eqos_set_tx_clk_speed,
	.eqos_get_enetaddr = k1pro_eqos_get_enetaddr,
	.eqos_get_tick_clk_rate = k1pro_eqos_get_tick_clk_rate,
};

struct eqos_config __maybe_unused eqos_spacemit_k1pro_config = {
	.reg_access_always_ok = false,
	.mdio_wait = 10,
	.swr_wait = 50,
	.config_mac = EQOS_MAC_RXQ_CTRL0_RXQ0EN_ENABLED_DCB,
	.config_mac_mdio = EQOS_MAC_MDIO_ADDRESS_CR_250_300,
	.axi_bus_width = EQOS_AXI_WIDTH_64,
	.interface = dev_read_phy_mode,
	.ops = &eqos_spacemit_k1pro_ops
};

#define PHY_INTF_MODE_OFFSET		(3)
#define PHY_INTF_MODE_MASK		GENMASK(4, 3)

#define PHY_INTF_RMII			(0x0 << PHY_INTF_MODE_OFFSET)
#define PHY_INTF_RGMII			(0x1 << PHY_INTF_MODE_OFFSET)
#define PHY_INTF_MII			(0x3 << PHY_INTF_MODE_OFFSET)

/* only valid for rmii, invert tx clk */
#define RMII_TX_CLK_SEL			BIT(6)

/* only valid for rmii, invert rx clk */
#define RMII_RX_CLK_SEL			BIT(7)

#define PHY_IRQ_EN			BIT(12)
#define AXI_SINGLE_ID			BIT(13)

#define RMII_TX_PHASE_OFFSET		(16)
#define RMII_TX_PHASE_MASK		GENMASK(18, 16)
#define RMII_RX_PHASE_OFFSET		(20)
#define RMII_RX_PHASE_MASK		GENMASK(22, 20)

#define RGMII_TX_PHASE_OFFSET		(24)
#define RGMII_TX_PHASE_MASK		GENMASK(26, 24)
#define RGMII_RX_PHASE_OFFSET		(20)
#define RGMII_RX_PHASE_MASK		GENMASK(22, 20)

#define EMAC_RX_DLINE_EN		BIT(0)
#define EMAC_RX_DLINE_CODE_OFFSET	(8)
#define EMAC_RX_DLINE_CODE_MASK		GENMASK(15, 8)

#define EMAC_TX_DLINE_EN		BIT(16)
#define EMAC_TX_DLINE_CODE_OFFSET	(24)
#define EMAC_TX_DLINE_CODE_MASK		GENMASK(31, 24)

static bool phy_iface_is_rmii(struct spacemit_plat_data *pdata)
{
	return pdata->phy_iface == PHY_INTERFACE_MODE_RMII;
}

static int clk_phase_rmii_set(struct spacemit_plat_data *pdata, bool is_tx)
{
	u32 val;

	switch (pdata->clk_tuning_way) {
	case CLK_TUNING_BY_REG:
		val = readl(pdata->ctrl_reg);
		if (is_tx) {
			val &= ~RMII_TX_PHASE_MASK;
			val |= (pdata->tx_clk_phase & 0x7) << RMII_TX_PHASE_OFFSET;
		} else {
			val &= ~RMII_RX_PHASE_MASK;
			val |= (pdata->rx_clk_phase & 0x7) << RMII_RX_PHASE_OFFSET;
		}
		writel(val, pdata->ctrl_reg);
		break;
	case CLK_TUNING_BY_CLK_REVERT:
		val = readl(pdata->ctrl_reg);
		if (is_tx) {
			if (pdata->tx_clk_phase == CLK_PHASE_REVERT)
				val |= RMII_TX_CLK_SEL;
			else
				val &= ~RMII_TX_CLK_SEL;
		} else {
			if (pdata->rx_clk_phase == CLK_PHASE_REVERT)
				val |= RMII_RX_CLK_SEL;
			else
				val &= ~RMII_RX_CLK_SEL;
		}
		writel(val, pdata->ctrl_reg);
		break;
	default:
		pr_err("wrong clk tuning way (%d)\n", pdata->clk_tuning_way);
		return -1;
	}
	pr_debug("%s phase=%d\n", is_tx ? "tx" : "rx",
		 is_tx ? pdata->tx_clk_phase : pdata->rx_clk_phase);
	return 0;
}

static int clk_phase_rgmii_set(struct spacemit_plat_data *pdata, bool is_tx)
{
	u32 val;

	switch (pdata->clk_tuning_way) {
	case CLK_TUNING_BY_REG:
		val = readl(pdata->ctrl_reg);
		if (is_tx) {
			val &= ~RGMII_TX_PHASE_MASK;
			val |= (pdata->tx_clk_phase & 0x7) << RGMII_TX_PHASE_OFFSET;
		} else {
			val &= ~RGMII_RX_PHASE_MASK;
			val |= (pdata->rx_clk_phase & 0x7) << RGMII_RX_PHASE_OFFSET;
		}
		writel(val, pdata->ctrl_reg);
		break;
	case CLK_TUNING_BY_DLINE:
		val = readl(pdata->dline_reg);
		if (is_tx) {
			val &= ~EMAC_TX_DLINE_CODE_MASK;
			val |= (pdata->tx_clk_phase & 0xff) << EMAC_TX_DLINE_CODE_OFFSET;
		} else {
			val &= ~EMAC_RX_DLINE_CODE_MASK;
			val |= (pdata->rx_clk_phase & 0xff) << EMAC_RX_DLINE_CODE_OFFSET;
		}
		writel(val, pdata->dline_reg);
		break;
	default:
		pr_err("wrong clk tuning way (%d)\n", pdata->clk_tuning_way);
		return -1;
	}
	pr_debug("%s phase=%d\n", is_tx ? "tx" : "rx",
		 is_tx ? pdata->tx_clk_phase : pdata->rx_clk_phase);
	return 0;
}

static int clk_phase_set(struct spacemit_plat_data *pdata, bool is_tx)
{
	if (!pdata->clk_tuning_enable)
		return 0;

	if (pdata->phy_iface == PHY_INTERFACE_MODE_MII)
		return 0;

	if (phy_iface_is_rmii(pdata))
		return clk_phase_rmii_set(pdata, is_tx);
	else
		return clk_phase_rgmii_set(pdata, is_tx);
}

static void k3_delayline_init(struct spacemit_plat_data *pdata)
{
	u32 val;

	/*
	 * On K3, TX/RX delayline must be enabled for reliable DMA init.
	 * This is required for all phy-modes (rgmii/rmii/mii).
	 */
	val = readl(pdata->dline_reg);
	val |= (EMAC_TX_DLINE_EN | EMAC_RX_DLINE_EN);
	writel(val, pdata->dline_reg);
}

static void k3_eqos_iface_config(struct spacemit_plat_data *pdata)
{
	phy_interface_t iface = pdata->phy_iface;
	u32 val;

	val = readl(pdata->ctrl_reg);
	val &= ~PHY_INTF_MODE_MASK;

	switch (iface) {
	case PHY_INTERFACE_MODE_MII:
		val |= PHY_INTF_MII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		val |= PHY_INTF_RMII;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val |= PHY_INTF_RGMII;
		break;

	default:
		pr_warn("unsupported phy-mode=%s\n",
			phy_string_for_interface(iface));
		return;
	}
	writel(val, pdata->ctrl_reg);
}

static int k3_eqos_phy_reset(struct spacemit_plat_data *pdata)
{
	int ret;

	ret = gpio_direction_output(pdata->phy_reset_gpio, 1);
	if (ret < 0) {
		pr_err("gpio_direction_output() failed(level=HIGH)\n");
		return ret;
	}

	udelay(2);

	ret = gpio_direction_output(pdata->phy_reset_gpio, 0);
	if (ret < 0) {
		pr_err("gpio_direction_output() failed(level=LOW)\n");
		return ret;
	}

	mdelay(20);

	ret = gpio_direction_output(pdata->phy_reset_gpio, 1);
	if (ret < 0) {
		pr_err("gpio_direction_output() failed(level=HIGH)\n");
		return ret;
	}

	mdelay(100);

	return 0;
}

__weak u32 k3_eqos_get_csr_clk(void)
{
	return 100 * 1000000;
}

static ulong k3_eqos_get_tick_clk_rate(struct udevice *dev)
{
	return k3_eqos_get_csr_clk();
}

static int k3_validate_iface_and_clk(struct spacemit_plat_data *pdata)
{
	switch (pdata->phy_iface) {
	case PHY_INTERFACE_MODE_MII:
		/* MII: tx_clk cannot be configured */
		return pdata->tx_clk_from_soc ? -EOPNOTSUPP : 0;

	case PHY_INTERFACE_MODE_RMII:
		/* RMII: TXC pin is unused */
		return pdata->tx_clk_from_soc ? -EOPNOTSUPP : 0;

	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		return 0;

	default:
		/* Only support MII / RMII / RGMII */
		return -EOPNOTSUPP;
	}
}

static void k3_eqos_set_clk_phase(struct spacemit_plat_data *pdata)
{
	phy_interface_t iface = pdata->phy_iface;

	if (!pdata->clk_tuning_enable)
		return;

	switch (iface) {
	case PHY_INTERFACE_MODE_RGMII_ID:
		/* PHY already provides TX+RX delay */
		return;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		/* PHY provides TX delay; only adjust RX */
		clk_phase_set(pdata, RX_PHASE);
		return;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		/* PHY provides RX delay; only adjust TX */
		clk_phase_set(pdata, TX_PHASE);
		return;
	case PHY_INTERFACE_MODE_RMII:
	case PHY_INTERFACE_MODE_RGMII:
		/* rgmii/rmii: adjust both TX and RX phases */
		clk_phase_set(pdata, TX_PHASE);
		clk_phase_set(pdata, RX_PHASE);
		return;
	default:
		pr_warn("clk tuning skipped for phy-mode=%s\n",
			phy_string_for_interface(iface));
		return;
	}
}

static int k3_eqos_probe_resources(struct udevice *dev)
{
	struct eqos_priv *eqos = dev_get_priv(dev);
	struct spacemit_plat_data *pdata;
	u32 apmu_base_reg;
	u32 ctrl_reg, dline_reg;
	int ret = -EINVAL;

	debug("%s(dev=%p):\n", __func__, dev);

	pdata = malloc(sizeof(*pdata));
	if (!pdata)
		return -ENOMEM;
	memset(pdata, 0, sizeof(*pdata));

	pdata->dev = dev;
	dev_set_plat_priv(dev, pdata);

	/* Get PHY interface */
	pdata->phy_iface = eqos->config->interface(dev);
	if (pdata->phy_iface == PHY_INTERFACE_MODE_NA) {
		pr_err("invalid PHY interface\n");
		goto err_free_pdata;
	}

	/* tx clock source select */
	pdata->tx_clk_from_soc = dev_read_bool(dev, "tx-clock-from-soc");
	/* phy clock source select */
	pdata->phy_clk_from_soc = dev_read_bool(dev, "phy-clock-from-soc");

	ret = k3_validate_iface_and_clk(pdata);
	if (ret) {
		pr_err("unsupported phy-mode=%s with tx clk from %s\n",
		       phy_string_for_interface(pdata->phy_iface),
		       pdata->tx_clk_from_soc ? "soc" : "phy");
		goto err_free_pdata;
	}

	ret = dev_read_u32(dev, "phy-reset-pin", &pdata->phy_reset_gpio);
	if (ret) {
		pr_err("'phy-reset-pin' not configured in device tree!\n");
		goto err_free_pdata;
	}

	ret = gpio_request(pdata->phy_reset_gpio, "phy-reset");
	if (ret) {
		pr_err("failed to request GPIO %u\n", pdata->phy_reset_gpio);
		goto err_free_pdata;
	}

	ret = dev_read_u32(dev, "apmu-base-reg", &apmu_base_reg);
	if (ret) {
		pr_err("'apmu-base-reg' not configured in device tree!\n");
		goto err_free_gpio;
	}

	ret = dev_read_u32(dev, "ctrl-reg", &ctrl_reg);
	if (ret) {
		pr_err("'ctrl-reg' not configured in device tree!\n");
		goto err_free_gpio;
	}
	pdata->ctrl_reg = (void *)((ulong)(apmu_base_reg + ctrl_reg));

	ret = dev_read_u32(dev, "dline-reg", &dline_reg);
	if (ret) {
		pr_err("'dline-reg' not configured in device tree!\n");
		goto err_free_gpio;
	}
	pdata->dline_reg = (void *)((ulong)(apmu_base_reg + dline_reg));

	pdata->clk_tuning_enable = dev_read_bool(dev, "clk_tuning_enable");
	if (pdata->clk_tuning_enable) {
		if (dev_read_bool(dev, "clk-tuning-by-reg")) {
			pdata->clk_tuning_way = CLK_TUNING_BY_REG;
		} else if (dev_read_bool(dev, "clk-tuning-by-clk-revert")) {
			pdata->clk_tuning_way = CLK_TUNING_BY_CLK_REVERT;
		} else if (dev_read_bool(dev, "clk-tuning-by-delayline")) {
			pdata->clk_tuning_way = CLK_TUNING_BY_DLINE;
		} else {
			pdata->clk_tuning_way = CLK_TUNING_BY_REG;
		}

		pdata->tx_clk_phase = dev_read_u32_default(dev, "tx-phase", TXCLK_PHASE_DEFAULT);
		pdata->rx_clk_phase = dev_read_u32_default(dev, "rx-phase", RXCLK_PHASE_DEFAULT);

		debug("tx_phase:%d  rx_phase:%d clk_tuning:%d\n",
		      pdata->tx_clk_phase, pdata->rx_clk_phase, pdata->clk_tuning_enable);
	}

	ret = reset_get_by_name(dev, "mac_reset", &eqos->reset_ctl);
	if (ret) {
		pr_err("reset_get_by_name(mac_reset) failed\n");
		goto err_free_gpio;
	}

	ret = clk_get_by_name(dev, "master_bus_clk", &eqos->clk_master_bus);
	if (ret) {
		pr_err("clk_get_by_name(master_bus_clk) failed\n");
		goto err_free_reset;
	}

	if (pdata->tx_clk_from_soc) {
		ret = clk_get_by_name(dev, "tx_clk", &eqos->clk_tx);
		if (ret) {
			pr_err("clk_get_by_name(tx_clk) failed\n");
			goto err_free_clk_master_bus;
		}
	}

	if (pdata->phy_clk_from_soc) {
		ret = clk_get_by_name(dev, "phy_clk", &pdata->clk_phy);
		if (ret) {
			pr_err("clk_get_by_name(phy_clk) failed\n");
			goto err_free_tx_clk;
		}
		/* Provide the clock before resetting the PHY */
		ret = clk_enable(&pdata->clk_phy);
		if (ret) {
			pr_err("clk_enable(clk_phy) failed\n");
			goto err_free_phy_clk;
		}
	}

	ret = k3_eqos_phy_reset(pdata);
	if (ret) {
		pr_err("k3_eqos_phy_reset() failed\n");
		goto err_disable_phy_clk;
	}
	/* After k3_validate_iface_and_clk() passes, these won't fail */
	k3_eqos_iface_config(pdata);
	k3_delayline_init(pdata);
	k3_eqos_set_clk_phase(pdata);

	debug("%s: OK\n", __func__);

	return 0;

err_disable_phy_clk:
	if (pdata->phy_clk_from_soc)
		clk_disable(&pdata->clk_phy);
err_free_phy_clk:
	if (pdata->phy_clk_from_soc)
		clk_free(&pdata->clk_phy);
err_free_tx_clk:
	if (pdata->tx_clk_from_soc)
		clk_free(&eqos->clk_tx);
err_free_clk_master_bus:
	clk_free(&eqos->clk_master_bus);
err_free_reset:
	reset_free(&eqos->reset_ctl);
err_free_gpio:
	gpio_free(pdata->phy_reset_gpio);
err_free_pdata:
	free(pdata);
	dev_set_plat_priv(dev, NULL);
	return ret;
}

static int k3_eqos_remove_resources(struct udevice *dev)
{
	struct spacemit_plat_data *pdata = dev_get_plat_priv(dev);
	struct eqos_priv *eqos = dev_get_priv(dev);

	if (pdata->phy_clk_from_soc) {
		clk_disable(&pdata->clk_phy);
		clk_free(&pdata->clk_phy);
	}

	if (pdata->tx_clk_from_soc)
		clk_free(&eqos->clk_tx);

	clk_free(&eqos->clk_master_bus);

	reset_free(&eqos->reset_ctl);
	gpio_free(pdata->phy_reset_gpio);
	free(pdata);
	dev_set_plat_priv(dev, NULL);

	return 0;
}

static int k3_eqos_stop_resets(struct udevice *dev)
{
	struct eqos_priv *eqos = dev_get_priv(dev);
	int ret;

	debug("%s(dev=%p):\n", __func__, dev);

	ret = reset_assert(&eqos->reset_ctl);
	if (ret < 0) {
		pr_err("reset_assert() failed\n");
		return ret;
	}

	debug("%s: OK\n", __func__);
	return 0;
}

static int k3_eqos_start_resets(struct udevice *dev)
{
	struct eqos_priv *eqos = dev_get_priv(dev);
	int ret;

	debug("%s(dev=%p):\n", __func__, dev);

	ret = reset_deassert(&eqos->reset_ctl);
	if (ret < 0) {
		pr_err("reset_deassert() failed\n");
		return ret;
	}

	debug("%s: OK\n", __func__);
	return 0;
}

static int k3_eqos_stop_clks(struct udevice *dev)
{
	struct spacemit_plat_data *pdata = dev_get_plat_priv(dev);
	struct eqos_priv *eqos = dev_get_priv(dev);

	debug("%s(dev=%p):\n", __func__, dev);

	if (pdata->tx_clk_from_soc)
		clk_disable(&eqos->clk_tx);

	clk_disable(&eqos->clk_master_bus);

	debug("%s: OK\n", __func__);
	return 0;
}

static int k3_eqos_start_clks(struct udevice *dev)
{
	struct spacemit_plat_data *pdata = dev_get_plat_priv(dev);
	struct eqos_priv *eqos = dev_get_priv(dev);
	int ret;

	debug("%s(dev=%p):\n", __func__, dev);

	ret = clk_enable(&eqos->clk_master_bus);
	if (ret) {
		pr_err("clk_enable(clk_master_bus) failed\n");
		goto err;
	}

	if (pdata->tx_clk_from_soc) {
		ret = clk_enable(&eqos->clk_tx);
		if (ret) {
			pr_err("clk_enable(clk_tx) failed\n");
			goto err_disable_clk_master_bus;
		}
	}

	debug("%s: OK\n", __func__);
	return 0;

err_disable_clk_master_bus:
	clk_disable(&eqos->clk_master_bus);
err:
	return ret;
}

static struct eqos_ops eqos_spacemit_k3_ops = {
	.eqos_inval_desc = eqos_inval_desc_generic,
	.eqos_flush_desc = eqos_flush_desc_generic,
	.eqos_inval_buffer = eqos_inval_buffer_generic,
	.eqos_flush_buffer = eqos_flush_buffer_generic,
	.eqos_probe_resources = k3_eqos_probe_resources,
	.eqos_remove_resources = k3_eqos_remove_resources,
	.eqos_stop_resets = k3_eqos_stop_resets,
	.eqos_start_resets = k3_eqos_start_resets,
	.eqos_stop_clks = k3_eqos_stop_clks,
	.eqos_start_clks = k3_eqos_start_clks,
	.eqos_calibrate_pads = eqos_null_ops,
	.eqos_disable_calibration = eqos_null_ops,
	.eqos_set_tx_clk_speed = eqos_null_ops,
	.eqos_get_enetaddr = eqos_null_ops,
	.eqos_get_tick_clk_rate = k3_eqos_get_tick_clk_rate,
};

struct eqos_config __maybe_unused eqos_spacemit_k3_config = {
	.reg_access_always_ok = false,
	.mdio_wait = 10,
	.swr_wait = 500,
	.config_mac = EQOS_MAC_RXQ_CTRL0_RXQ0EN_ENABLED_DCB,
	.config_mac_mdio = EQOS_MAC_MDIO_ADDRESS_CR_250_300,
	.axi_bus_width = EQOS_AXI_WIDTH_64,
	.interface = dev_read_phy_mode,
	.ops = &eqos_spacemit_k3_ops
};
