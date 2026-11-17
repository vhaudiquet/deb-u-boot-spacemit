// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Spacemit Co., Ltd.
 *
 */

#include <asm/gpio.h>
#include <asm/io.h>
#include <common.h>
#include <div64.h>
#include <clk.h>
#include <display.h>
#include <dm.h>
#include <regmap.h>
#include <panel.h>
#include <regmap.h>
#include <syscon.h>
#include <power-domain-uclass.h>
#include <power-domain.h>
#include <power/regulator.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <mipi_dsi.h>
#include <reset.h>

#include "spacemit_mipi.h"
#include "./dsi/include/spacemit_dsi_common.h"
#include "./dsi/drv/spacemit_dsi_hw.h"

/* DSI bitclk pll control regs */
#define PLL_CTRL_REG0   0xc0
#define PLL_CTRL_REG1   0xc4
#define PLL_CTRL_REG2   0xc8
#define PLL_CTRL_REG3   0xcc
#define PLL_CTRL_STATUS 0x230

#define PLL_LK          BIT(29)
#define PLL_UP          BIT(31)
#define PLL_DIV_EN      (0xf << 4)

static const struct pll_freq_range_t pll_freq_table[32] = {
	{780,  1010, 1090},
	{1090, 1170, 1250},
	{1250, 1330, 1410},
	{1410, 1490, 1570},
	{1570, 1650, 1730},
	{1730, 1810, 1890},
	{1890, 1970, 2050},
	{2050, 2130, 2210},
	{2210, 2290, 2370},
	{2370, 2450, 2530},
	{2530, 2610, 2695},
	{2695, 2780, 2860},
	{2860, 2940, 3020},
	{3020, 3100, 3185},
	{3185, 3270, 3350},
	{3350, 3430, 3510},

	{3510, 3590, 3675},
	{3675, 3760, 3840},
	{3840, 3920, 4000},
	{4000, 4080, 4165},
	{4165, 4250, 4330},
	{4330, 4410, 4485},
	{4485, 4560, 4650},
	{4650, 4740, 4825},
	{4825, 4910, 4990},
	{4990, 5070, 5150},
	{5150, 5230, 5315},
	{5315, 5400, 5480},
	{5480, 5560, 5640},
	{5640, 5720, 5805},
	{5805, 5890, 5970},
	{5970, 6050, 6500}
};

static int pll_get_rate_sel(uint32_t vco_freq)
{
	if (vco_freq < 2000)
		return 0;
	else if (vco_freq < 4000)
		return 1;
	else if (vco_freq <= 6500)
		return 2;
	else
		return -1;
}

static int pll_get_range_index(uint32_t bitclock)
{
	uint32_t vco_freq = bitclock * 2;

	for (int i = 0; i < 32; i++) {
		if (vco_freq > pll_freq_table[i].low &&
		vco_freq <= pll_freq_table[i].high) {
		return i;
		}
	}
	return -1;
}

/* pll_reg5 / pll_reg6 */
static uint8_t pll_make_range_reg(int range_idx)
{
	return (uint8_t)((0b100 << 5) | (range_idx & 0x1F));
}

/* pll_reg7 */
static uint8_t pll_make_rate_reg(uint32_t vco_freq)
{
	int rate_sel = pll_get_rate_sel(vco_freq);
	if (rate_sel < 0)
		return 0xFF;

	/* 01 | rate_sel | 0101 */
	return (uint8_t)((0x01 << 6) |
			(rate_sel << 4) |
			0x05);
}

/* ===================== ctrl_reg0 ===================== */
static u32 pll_make_ctrl_reg0(u32 vco_freq)
{
	u32 denom = 24;
	u32 div_int;
	u64 frac22;

	/* integer part */
	div_int = vco_freq / denom;

	/* fractional part: round((vco % denom) / denom * 2^22) */
	frac22 = (u64)(vco_freq % denom) << 22;
	frac22 += denom / 2;
	do_div(frac22, denom);   /* frac22 is 22-bit */

	return ((div_int & 0xff) << 24) | ((u32)frac22 & 0x00ffffff);
}

/* ===================== ctrl_reg1 ===================== */
static uint32_t pll_make_ctrl_reg1(uint32_t bitclock, int pllmode)
{
	uint32_t vco_freq = bitclock * 2;

	int range_idx = pll_get_range_index(bitclock);
	if (range_idx < 0)
		return 0xFFFFFFFF;

	uint8_t pll_reg4 = (uint8_t)(3 + pllmode * 8);
	uint8_t pll_reg5 = pll_make_range_reg(range_idx);
	uint8_t pll_reg6 = pll_reg5;
	uint8_t pll_reg7 = pll_make_rate_reg(vco_freq);

	return (pll_reg7 << 24) | (pll_reg6 << 16) |
		(pll_reg5 << 8) | pll_reg4;
}

static int spacemit_calc_pll_regs(uint32_t bitclock,
			   uint32_t *pll_ctrl_reg0,
			   uint32_t *pll_ctrl_reg1)
{
	const int pllmode = PLL_MODE_5G_PLL;

	uint32_t vco_freq = bitclock * 2;

	int rate_sel = pll_get_rate_sel(vco_freq);
	if (rate_sel < 0)
		return -EINVAL;

	*pll_ctrl_reg0 = pll_make_ctrl_reg0(vco_freq);
	*pll_ctrl_reg1 = pll_make_ctrl_reg1(bitclock, pllmode);

	pr_info("pll_ctrl_reg0 = 0x%08X\n", *pll_ctrl_reg0);
	pr_info("pll_ctrl_reg1 = 0x%08X\n", *pll_ctrl_reg1);

	return 0;
}

static void dpu_enable_dsipll(u32 bitclock)
{
	u32 pll_ctrl_reg0;
	u32 pll_ctrl_reg1;
	u32 value;
	unsigned int timeout = 100;
	int ret;
	bitclock /= 1000000; /* MHz */
	/* not touch reg if running */
	if (dsi_read(PLL_CTRL_REG2) & PLL_UP)
		return;

	ret = spacemit_calc_pll_regs(bitclock,
				     &pll_ctrl_reg0,
				     &pll_ctrl_reg1);
	if (ret) {
		pr_err("DSI PLL calc failed, bitclock=%u\n", bitclock);
		return;
	}

	/* cfg reg5 - 8 */
	// writel(a_crtc->dsipll_reg0, a_crtc->dsipll_base + PLL_CTRL_REG0);
	dsi_write(PLL_CTRL_REG0, pll_ctrl_reg0);
	dsi_write(PLL_CTRL_REG1, pll_ctrl_reg1);
	value = 0xa00010a0;
	dsi_write(PLL_CTRL_REG2, value);

	if ((pll_ctrl_reg1 & (BIT(29) | BIT(30))) != 0)
		dsi_set_bits(DSI_PHY_ANA_CTRL1, BIT(28));
	else
		dsi_clear_bits(DSI_PHY_ANA_CTRL1, BIT(28));


	/* pu */
	value |= PLL_UP;
	dsi_write(PLL_CTRL_REG2, value);

	/* wait pll lock */
	while (true) {
		value = dsi_read(PLL_CTRL_REG3);

		if (value & PLL_LK) {
			pr_debug("bitclk pll locked\n");
			break;
		}

		if (timeout == 0) {
			pr_err("failed to got bitclk pll lock 0x%x\n", value);
			break;
		}

		timeout--;

		udelay(10);
	}
}

static int spacemit_mipi_dsi_enable(struct udevice *dev,
		       const struct display_timing *timing)
{
	ofnode node, timing_node;
	int val;
	// struct spacemit_mipi_priv *priv = dev_get_priv(dev);

	/* Set dpi color coding depth 24 bit */
	timing_node = ofnode_find_subnode(dev_ofnode(dev), "display-timings");
	node = ofnode_first_subnode(timing_node);

	val = ofnode_read_u32_default(node, "bits-per-pixel", -1);

	return 0;
}

static int spacemit_mipi_phy_enable(struct udevice *dev)
{
	// struct spacemit_mipi_priv *priv = dev_get_priv(dev);
	return 0;
}


static int mipi_dsi_read_timing(struct udevice *dev,
			struct display_timing *timing)
{
	// int ret;

	// ret = ofnode_decode_display_timing(dev_ofnode(dev), 0, timing);
	// if (ret) {
	// 	pr_debug("%s: Failed to decode display timing (ret=%d)\n",
	// 	      __func__, ret);
	// 	return -EINVAL;
	// }

	return 0;
}

static int mipi_dsi_display_enable(struct udevice *dev, int panel_bpp,
			  const struct display_timing *timing)
{
	int ret;
	// struct spacemit_mipi_priv *priv = dev_get_priv(dev);

	/* Config  and enable mipi dsi according to timing */
	ret = spacemit_mipi_dsi_enable(dev, timing);
	if (ret) {
		pr_debug("%s: spacemit_mipi_dsi_enable() failed (err=%d)\n",
		      __func__, ret);
		return ret;
	}

	/* Config and enable mipi phy */
	ret = spacemit_mipi_phy_enable(dev);
	if (ret) {
		pr_debug("%s: spacemit_mipi_phy_enable() failed (err=%d)\n",
		      __func__, ret);
		return ret;
	}

	/* Enable backlight */
	// ret = panel_enable_backlight(priv->panel);
	// if (ret) {
	// 	pr_debug("%s: panel_enable_backlight() failed (err=%d)\n",
	// 	      __func__, ret);
	// 	return ret;
	// }

	return 0;
}

static int spacemit_mipi_dsi_of_to_plat(struct udevice *dev)
{
	lcd_mipi_probe();
	return 0;
}

static int spacemit_mipi_dsi_probe(struct udevice *dev)
{
	struct spacemit_mipi_priv *priv = dev_get_priv(dev);
	struct power_domain pm_domain;
	unsigned long rate;
	int ret;
	u32 bit_clk, pix_clk;
	void __iomem *ciu_addr;
	u32 value;

	pr_debug("%s: device %s \n", __func__, dev->name);

	ret = power_domain_get(dev, &pm_domain);
	if (ret) {
		pr_err("power_domain_get mipi dsi failed: %d", ret);
		return ret;
	}

	ret = clk_get_by_name(dev, "pxclk", &priv->pxclk);
	if (ret) {
		pr_err("clk_get_by_name mipi dsi pxclk failed: %d", ret);
		return ret;
	}

	ret = clk_get_by_name(dev, "mclk", &priv->mclk);
	if (ret) {
		pr_err("clk_get_by_name mipi dsi mclk failed: %d", ret);
		return ret;
	}

	ret = clk_get_by_name(dev, "hclk", &priv->hclk);
	if (ret) {
		pr_err("clk_get_by_name mipi dsi hclk failed: %d", ret);
		return ret;
	}

	ret = clk_get_by_name(dev, "escclk", &priv->escclk);
	if (ret) {
		pr_err("clk_get_by_name mipi dsi escclk failed: %d", ret);
		return ret;
	}

	ret = clk_get_by_name(dev, "dscclk", &priv->dscclk);
	if (ret) {
		pr_err("clk_get_by_name mipi dsi dscclk failed: %d", ret);
		return ret;
	}

	ret = clk_get_by_name(dev, "aclk", &priv->aclk);
	if (ret) {
		pr_err("clk_get_by_name mipi dsi aclk failed: %d", ret);
		return ret;
	}

	ret = reset_get_by_name(dev, "aclk_reset", &priv->aclk_reset);
	if (ret) {
		pr_err("reset_get_by_name dsi_reset failed: %d\n", ret);
		return ret;
	}

	ret = reset_get_by_name(dev, "mclk_reset", &priv->mclk_reset);
	if (ret) {
		pr_err("reset_get_by_name mclk_reset failed: %d\n", ret);
		return ret;
	}

	ret = reset_get_by_name(dev, "esc_reset", &priv->esc_reset);
	if (ret) {
		pr_err("reset_get_by_name esc_reset failed: %d\n", ret);
		return ret;
	}

	ret = reset_get_by_name(dev, "lcd_reset", &priv->lcd_reset);
	if (ret) {
		pr_err("reset_get_by_name lcd_reset failed: %d\n", ret);
		return ret;
	}

	ret = reset_get_by_name(dev, "dsc_reset", &priv->dsc_reset);
	if (ret) {
		pr_err("reset_get_by_name dsc_reset failed: %d\n", ret);
		return ret;
	}

	ret = reset_deassert(&priv->aclk_reset);
	if (ret) {
		pr_err("reset_assert aclk_reset failed: %d\n", ret);
		return ret;
	}

	ret = reset_deassert(&priv->mclk_reset);
	if (ret) {
		pr_err("reset_assert mclk_reset failed: %d\n", ret);
		return ret;
	}

	ret = reset_deassert(&priv->esc_reset);
	if (ret) {
		pr_err("reset_assert esc_reset failed: %d\n", ret);
		return ret;
	}

	ret = reset_deassert(&priv->lcd_reset);
	if (ret) {
		pr_err("reset_assert lcd_reset failed: %d\n", ret);
		return ret;
	}

	ret = reset_deassert(&priv->dsc_reset);
	if (ret) {
		pr_err("reset_assert dsc_reset failed: %d\n", ret);
		return ret;
	}

	/* dpu0 mux mipi dsi */
	ciu_addr = (void __iomem *)0xd4282c00;
	value = readl(ciu_addr + 0x12c);
	value &= ~BIT(8);
	writel(value, ciu_addr + 0x12c);

	ret = clk_enable(&priv->pxclk);
	if (ret < 0) {
		pr_err("clk_enable mipi dsi pxclk failed: %d\n", ret);
		return ret;
	}

	ret = clk_enable(&priv->mclk);
	if (ret < 0) {
		pr_err("clk_enable mipi dsi mclk failed: %d\n", ret);
		return ret;
	}

	ret = clk_enable(&priv->hclk);
	if (ret < 0) {
		pr_err("clk_enable mipi dsi hclk failed: %d\n", ret);
		return ret;
	}

	ret = clk_enable(&priv->escclk);
	if (ret < 0) {
		pr_err("clk_enable mipi dsi escclk failed: %d\n", ret);
		return ret;
	}

	ret = clk_enable(&priv->aclk);
	if (ret < 0) {
		pr_err("clk_enable mipi dsi aclk failed: %d\n", ret);
		return ret;
	}

	bit_clk = dev_read_u32_default(dev, "bit-clk", 614400000);
	dpu_enable_dsipll(bit_clk);

	ret = clk_enable(&priv->dscclk);
	if (ret < 0) {
		pr_err("clk_enable mipi dsi dscclk failed: %d\n", ret);
		return ret;
	}

	pix_clk = dev_read_u32_default(dev, "pix-clk", 88000000);
	ret = clk_set_rate(&priv->pxclk, pix_clk);

	if (ret < 0) {
		pr_err("clk_set_rate mipi dsi pxclk failed: %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(&priv->mclk, 307200000);
	if (ret < 0) {
		pr_err("clk_set_rate mipi dsi mclk failed: %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(&priv->escclk, 51200000);
	if (ret < 0) {
		pr_err("clk_set_rate mipi dsi escclk failed: %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(&priv->dscclk, 307200000);
	if (ret < 0) {
		pr_err("clk_set_rate mipi dsi dscclk failed: %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(&priv->aclk, 409000000);
	if (ret < 0) {
		pr_err("clk_set_rate mipi dsi aclk failed: %d\n", ret);
		return ret;
	}

	rate = clk_get_rate(&priv->pxclk);
	pr_debug("%s clk_get_rate pxclk rate = %ld\n", __func__, rate);

	rate = clk_get_rate(&priv->mclk);
	pr_debug("%s clk_get_rate mclk rate = %ld\n", __func__, rate);

	rate = clk_get_rate(&priv->hclk);
	pr_debug("%s clk_get_rate hclk rate = %ld\n", __func__, rate);

	rate = clk_get_rate(&priv->escclk);
	pr_debug("%s clk_get_rate escclk rate = %ld\n", __func__, rate);

	rate = clk_get_rate(&priv->dscclk);
	pr_debug("%s clk_get_rate dscclk rate = %ld\n", __func__, rate);

	rate = clk_get_rate(&priv->aclk);
	pr_debug("%s clk_get_rate aclk rate = %ld\n", __func__, rate);

	return 0;
}

static const struct dm_display_ops spacemit_mipi_dsi_ops = {
	.read_timing = mipi_dsi_read_timing,
	.enable = mipi_dsi_display_enable,
};

static const struct udevice_id spacemit_mipi_dsi_ids[] = {
	{ .compatible = "spacemit,mipi-dsi" },
	{ }
};

U_BOOT_DRIVER(spacemit_mipi_dsi) = {
	.name = "spacemit_mipi_dsi",
	.id = UCLASS_DISPLAY,
	.of_match = spacemit_mipi_dsi_ids,
	.ops = &spacemit_mipi_dsi_ops,
	.of_to_plat = spacemit_mipi_dsi_of_to_plat,
	.probe = spacemit_mipi_dsi_probe,
	.priv_auto	= sizeof(struct spacemit_mipi_priv),
};
