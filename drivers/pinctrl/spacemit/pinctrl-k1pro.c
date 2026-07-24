// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, spacemit Corporation.
 */
#include <common.h>
#include <dm.h>
#include <dm/pinctrl.h>

#include "pinctrl-spacemit.h"

static const struct spacemit_regs k1pro_regs = {
	.cfg = 0x000,
	.reg_len = 0x80,
};

static const struct spacemit_pin_conf k1pro_pin_conf = {
	.fs_shift = 0,
	.od_shift = 4,
	.pe_shift = 8,
	.pull_shift = 9,
	.ds_shift = 12,
	.st_shift = 16,
	.rte_shift = 20,
};

static struct spacemit_pinctrl_soc_info k1pro_pinctrl_soc_info = {
	.regs = &k1pro_regs,
	.pinconf = &k1pro_pin_conf,
};

static int k1pro_pinctrl_probe(struct udevice *dev)
{
	struct spacemit_pinctrl_soc_info *info =
		(struct spacemit_pinctrl_soc_info *)dev_get_driver_data(dev);

	return spacemit_pinctrl_probe(dev, info);
}

static const struct udevice_id k1pro_pinctrl_match[] = {
	{
		.compatible = "spacemit,k1pro-pinctrl",
		.data = (ulong)&k1pro_pinctrl_soc_info
	},
	{ /* sentinel */ }
};


U_BOOT_DRIVER(k1pro_pinctrl) = {
	.name = "k1pro-pinctrl",
	.id = UCLASS_PINCTRL,
	.of_match = of_match_ptr(k1pro_pinctrl_match),
	.probe = k1pro_pinctrl_probe,
	.remove = spacemit_pinctrl_remove,
	.priv_auto	= sizeof(struct spacemit_pinctrl_priv),
	.ops = &spacemit_pinctrl_ops,
};
