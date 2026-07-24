// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Device Tree support for Spacemit SoCs
 *
 * Copyright (c) 2023 Spacemit Inc.
 */

#include <common.h>
#include <asm/io.h>
#include <malloc.h>
#include <clk-uclass.h>
#include <dm/device.h>
#include <dm/devres.h>
#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <clk.h>
#include <div64.h>
#include "ccu-k1pro.h"

#define UBOOT_DM_CLK_PLL_INT  "k1pro_clk_pll_int"
#define UBOOT_DM_CLK_PLL_FRAC "k1pro_clk_pll_frac"

#define PLL_EN_BIT              (0)
#define PLL_DSM_EN_BIT          (1)
#define PLL_BYPASS_BIT          (2)
#define PLL_FOUTPOSTDIV_EN_BIT	(3)
#define PLL_FOUT4PHASE_EN_BIT	(4)
#define PLL_LOCK_BIT            (5)

#define PLL_CONF_OFFSET         (0x4)
#define PLL_REFDIV_BIT          (0)
#define PLL_REFDIV_MASK	        (0x3F)
#define PLL_FBDIV_BIT           (8)
#define PLL_FBDIV_MASK	        (0xFFF)
#define PLL_POSTDIV1_BIT        (24)
#define PLL_POSTDIV1_MASK       (0x7)
#define PLL_POSTDIV2_BIT        (27)
#define PLL_POSTDIV2_MASK       (0x7)

#define PLL_FRAC_OFFSET         (0x8)
#define PLL_FRAC_BIT            (0)
#define PLL_FRAC_MASK	        (0xFFFFFF)

#define LOCK_TIMEOUT_US		10000

struct clk_pllxx {
	struct clk clk;
	void __iomem *base;
	void __iomem *conf;
	void __iomem *frac;
	u8 type;
};

struct clk_pllxx_rate_table {
	ulong rate;
	unsigned int refdiv;
	unsigned int fbdiv;
	unsigned int pdiv1;
	unsigned int pdiv2;
	unsigned int frac;
};

#define PLL_FRAC_RATE(_rate, _refdiv, _fbdiv, _pdiv1, _pdiv2, _frac)		\
	{						\
		.rate	=	(_rate),		\
		.refdiv	=	(_refdiv),			\
		.fbdiv	=	(_fbdiv),			\
		.pdiv1	=	(_pdiv1),			\
		.pdiv2	=	(_pdiv2),			\
		.frac	=	(_frac),			\
	}

#define PLL_INT_RATE(_rate, _refdiv, _fbdiv, _pdiv1, _pdiv2)		\
	{						\
		.rate	=	(_rate),		\
		.refdiv	=	(_refdiv),			\
		.fbdiv	=	(_fbdiv),			\
		.pdiv1	=	(_pdiv1),			\
		.pdiv2	=	(_pdiv2),			\
	}

static const struct clk_pllxx_rate_table pll_frac_tbl[] = {
	PLL_FRAC_RATE(98304000U, 1, 40, 5, 2, 16106127),
	PLL_FRAC_RATE(408000000U, 1, 34, 2, 1, 0),
	PLL_FRAC_RATE(800000000U, 3, 100, 1, 1, 0),
	PLL_FRAC_RATE(804000000U, 2, 67, 1, 1, 0),
	PLL_FRAC_RATE(816000000U, 1, 34, 1, 1, 0),
	PLL_FRAC_RATE(840000000U, 1, 35, 1, 1, 0),
	PLL_FRAC_RATE(1000000000U, 1, 100, 2, 1, 0),
	PLL_FRAC_RATE(2000000000U, 1, 100, 1, 1, 0),
	PLL_FRAC_RATE(1200000000U, 3, 250, 2, 1, 0),
	PLL_FRAC_RATE(2400000000U, 3, 250, 1, 1, 0),
	PLL_FRAC_RATE(3192000000U, 1, 133, 1, 1, 0),
};

const struct clk_pllxx_rate_table pll_int_tbl[] = {
	PLL_INT_RATE(16650000U, 1, 34, 7, 7),
	PLL_INT_RATE(16410000U, 2, 67, 7, 7),
	PLL_INT_RATE(16330000U, 3, 100, 7, 7),
	PLL_INT_RATE(65140000U, 1, 133, 7, 7),
	PLL_INT_RATE(408000000U, 1, 34, 2, 1),
	PLL_INT_RATE(800000000U, 3, 100, 1, 1),
	PLL_INT_RATE(804000000U, 2, 67, 1, 1),
	PLL_INT_RATE(816000000U, 1, 34, 1, 1),
	PLL_INT_RATE(840000000U, 1, 35, 1, 1),
	PLL_INT_RATE(1000000000U, 3, 250, 2, 1),
	PLL_INT_RATE(2000000000U, 3, 250, 1, 1),
	PLL_INT_RATE(1200000000U, 1, 100, 2, 1),
	PLL_INT_RATE(2400000000U, 1, 100, 1, 1),
	PLL_INT_RATE(3192000000U, 1, 133, 1, 1),
};

#define to_clk_pllxx(_clk) container_of(_clk, struct clk_pllxx, clk)


static int clk_pllxx_wait_lock(struct clk_pllxx *pll)
{
	u32 val;

	return readl_poll_timeout(pll->base, val, val & (1<<PLL_LOCK_BIT),
			LOCK_TIMEOUT_US);
	return 0;
}

static ulong clk_pllxx_get_pll_rate(u32 refdiv, u32 fbdiv, u32 pdiv1, u32 pdiv2, u32 frac, u8 pll_type)
{
	u32 i, num;
	const struct clk_pllxx_rate_table *table;

	if(pll_type == CLK_TYPE_PLL_FRAC){
		table = &pll_frac_tbl[0];
		num = ARRAY_SIZE(pll_frac_tbl);
	}
	else{
		table = &pll_int_tbl[0];
		num = ARRAY_SIZE(pll_int_tbl);
	}

	for(i = 0; i < num; i++){
		if(refdiv == table[i].refdiv && fbdiv ==  table[i].fbdiv
			&& pdiv1 ==  table[i].pdiv1 && pdiv2 ==  table[i].pdiv2){
			if(pll_type == CLK_TYPE_PLL_FRAC){
				if(frac == table[i].frac){
					return table[i].rate;
				}
			}
			else{
				return table[i].rate;
			}
		}
	}
	return 0;
}

static ulong clk_pllxx_get_rate(struct clk *clk)
{
	u32 val, refdiv, fbdiv, pdiv1, pdiv2, frac;
	struct clk_pllxx *pll = to_clk_pllxx(dev_get_clk_ptr(clk->dev));

	val = readl(pll->conf);
	refdiv = (val & PLL_REFDIV_MASK << PLL_REFDIV_BIT) >> PLL_REFDIV_BIT;
	fbdiv = (val & PLL_FBDIV_MASK << PLL_FBDIV_BIT) >> PLL_FBDIV_BIT;
	pdiv1 = (val & PLL_POSTDIV1_MASK << PLL_POSTDIV1_BIT) >> PLL_POSTDIV1_BIT;
	pdiv2 = (val & PLL_POSTDIV2_MASK << PLL_POSTDIV2_BIT) >> PLL_POSTDIV2_BIT;

	if(pll->type == CLK_TYPE_PLL_FRAC){
		val = readl(pll->frac);
		frac = (val & PLL_FRAC_MASK) >> PLL_FRAC_BIT;
	}
	return clk_pllxx_get_pll_rate(refdiv, fbdiv, pdiv1, pdiv2, frac, pll->type);
}

static int clk_pllxx_find_params(ulong rate, const struct clk_pllxx_rate_table *table, u32 num)
{
	int i;
	for(i = 0; i < num; i++){
		if(rate == table[i].rate){
			return i;
		}
	}
	return -1;
}

static int clk_pllxx_write(int index, const struct clk_pllxx_rate_table *table, struct clk_pllxx *pll)
{
	u32	val;

	val = readl(pll->base);
	if(pll->type == CLK_TYPE_PLL_FRAC)
		val |= (1<<PLL_DSM_EN_BIT);
	else
		val &= ~(1<<PLL_DSM_EN_BIT);
	writel(val, pll->base);

	val = readl(pll->conf);
	val &= ~((PLL_REFDIV_MASK << PLL_REFDIV_BIT));
	val |= ((table[index].refdiv) << PLL_REFDIV_BIT);
	writel(val, pll->conf);

	val = readl(pll->conf);
	val &= ~((PLL_FBDIV_MASK << PLL_FBDIV_BIT));
	val |= ((table[index].fbdiv) << PLL_FBDIV_BIT);
	writel(val, pll->conf);

	if(pll->type == CLK_TYPE_PLL_FRAC){
		val = readl(pll->frac);
		val &= ~(PLL_FRAC_MASK << PLL_FRAC_BIT);
		val |= ((table[index].frac) << PLL_FRAC_BIT);
		writel(val, pll->frac);
	}

	val = readl(pll->base);
	if(pll->type == CLK_TYPE_PLL_FRAC)
		val |= (1<<PLL_FOUTPOSTDIV_EN_BIT);
	else
		val &= ~(1<<PLL_FOUTPOSTDIV_EN_BIT);
	writel(val, pll->base);

	val = readl(pll->conf);
	val &= ~((PLL_POSTDIV1_MASK << PLL_POSTDIV1_BIT) | (PLL_POSTDIV2_MASK << PLL_POSTDIV2_BIT));
	val |= ((table[index].pdiv1) << PLL_POSTDIV1_BIT) | ((table[index].pdiv2) << PLL_POSTDIV2_BIT);
	writel(val, pll->conf);

	return 0;
}

static ulong clk_pllxx_set_rate(struct clk *clk, ulong rate)
{
	u32 num;
	int index;
	struct clk_pllxx *pll = to_clk_pllxx(dev_get_clk_ptr(clk->dev));
	const struct clk_pllxx_rate_table *table;

	if(pll->type == CLK_TYPE_PLL_FRAC){
		table = &pll_frac_tbl[0];
		num = ARRAY_SIZE(pll_frac_tbl);
	}
	else{
		table = &pll_int_tbl[0];
		num = ARRAY_SIZE(pll_int_tbl);
	}
	index = clk_pllxx_find_params(rate, table, num);
	if(index < 0)
		return 0;

	clk_pllxx_write(index, table, pll);

	return clk_pllxx_wait_lock(pll);

}

static int clk_pllxx_prepare(struct clk *clk)
{
	struct clk_pllxx *pll = to_clk_pllxx(dev_get_clk_ptr(clk->dev));
	u32 val;

	val = readl(pll->base);
	val |= (1<<PLL_EN_BIT);
	writel(val, pll->base);

	return clk_pllxx_wait_lock(pll);
}

static int clk_pllxx_unprepare(struct clk *clk)
{
	struct clk_pllxx *pll = to_clk_pllxx(dev_get_clk_ptr(clk->dev));
	u32 val;

	val = readl(pll->base);
	val &= ~(1<<PLL_EN_BIT);
	writel(val, pll->base);

	return 0;
}

static const struct clk_ops clk_pllxx_ops = {
	.enable		= clk_pllxx_prepare,
	.disable	= clk_pllxx_unprepare,
	.set_rate	= clk_pllxx_set_rate,
	.get_rate	= clk_pllxx_get_rate,
};

struct clk *k1pro_clk_register_pllxx(const char *name, const char *parent_name,
	void __iomem *base, int pll_type)
{
	struct clk_pllxx *pll;
	struct clk *clk;
	char *type_name;
	int ret;
	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	switch (pll_type) {
	case CLK_TYPE_PLL_FRAC:
		type_name = UBOOT_DM_CLK_PLL_FRAC;
		break;
	case CLK_TYPE_PLL_INT:
		type_name = UBOOT_DM_CLK_PLL_INT;
		break;
	default:
		pr_err("%s: Unknown pll type for pll clk %s\n",
		       __func__, name);
		return ERR_PTR(-EINVAL);
	};

	pll->base = base;
	pll->conf = base + PLL_CONF_OFFSET;
	if(pll_type == CLK_TYPE_PLL_FRAC)
		pll->frac = base + PLL_FRAC_OFFSET;
	pll->type = pll_type;

	clk = &pll->clk;
	ret = clk_register(clk, type_name, name, parent_name);
	if (ret) {
		pr_err("%s: failed to register pll %s %d\n",
		       __func__, name, ret);
		kfree(pll);
		return ERR_PTR(ret);
	}
	return clk;
}

U_BOOT_DRIVER(clk_pll_int) = {
	.name	= UBOOT_DM_CLK_PLL_INT,
	.id	= UCLASS_CLK,
	.ops	= &clk_pllxx_ops,
	.flags = DM_FLAG_PRE_RELOC,
};

U_BOOT_DRIVER(clk_pll_frac) = {
	.name	= UBOOT_DM_CLK_PLL_FRAC,
	.id	= UCLASS_CLK,
	.ops	= &clk_pllxx_ops,
	.flags = DM_FLAG_PRE_RELOC,
};

