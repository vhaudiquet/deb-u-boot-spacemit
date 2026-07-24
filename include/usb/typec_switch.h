/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2025 Spacemit, Inc
 */

#ifndef __LINUX_TYPEC_SWITCH_H
#define __LINUX_TYPEC_SWITCH_H

struct dm_typec_switch_ops {
	int (*set)(struct udevice *dev,
		   enum typec_orientation orientation);
};

int typec_switch_get(struct udevice *dev, struct udevice **devp);
int typec_switch_set(struct udevice *dev,
		     enum typec_orientation orientation);
#endif
