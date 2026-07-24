// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 Spacemit, Inc
 */

#include <dm/device.h>
#include <dm/device_compat.h>
#include <dm/uclass.h>
#include <dm/read.h>
#include <usb/tcpm.h>
#include <usb/typec_switch.h>

/**
 * typec_switch_get - Find USB Type-C orientation switch
 * @connector:	connector node
 */
int ofnode_typec_switch_get(ofnode connector, struct udevice **devp)
{
	ofnode port, node, pnode;
	struct ofnode_phandle_args phandle;
	int ret;

	port = ofnode_find_subnode(connector, "port");
	if (!ofnode_valid(port)) {
		pr_err("'port' subnode not found\n");
		return -ENODEV;
	}

	for (node = ofnode_first_subnode(port);
		ofnode_valid(node);
		node = ofnode_next_subnode(node)) {
		ret = ofnode_parse_phandle_with_args(node, "remote-endpoint",
						     NULL, 0, 0, &phandle);
		if (ret)
			continue;

		pnode = ofnode_get_parent(phandle.node);
		if (!ofnode_read_bool(pnode, "orientation-switch")) {
			pnode = ofnode_get_parent(pnode);
			if (!ofnode_read_bool(pnode, "orientation-switch"))
				continue;
		}

		pr_info("find switch: %s\n", ofnode_get_name(pnode));
		ret = uclass_get_device_by_ofnode(UCLASS_TYPEC_SWITCH,
						  pnode,
						  devp);
		if (ret)
			continue;

		return 0;
	}

	return -ENODEV;
}

/**
 * typec_switch_get - Find USB Type-C orientation switch
 * @dev:	device node
 */
int typec_switch_get(struct udevice *dev, struct udevice **devp)
{
	ofnode connector_node = dev_read_subnode(dev, "connector");
	if (!ofnode_valid(connector_node)) {
		dev_err(dev, "'connector' node is not found\n");
		return -ENODEV;
	}

	return ofnode_typec_switch_get(connector_node, devp);
}

int typec_switch_set(struct udevice *dev,
		     enum typec_orientation orientation)
{
	const struct dm_typec_switch_ops *drvops = dev_get_driver_ops(dev);

	if (!drvops->set)
		return -ENOSYS;

	return drvops->set(dev, orientation);
}

static int typec_switch_post_probe(struct udevice *dev)
{
	const struct dm_typec_switch_ops *drvops = dev_get_driver_ops(dev);

	if (!drvops->set)
		return -ENOSYS;

	return drvops->set(dev, TYPEC_ORIENTATION_NONE);
}

UCLASS_DRIVER(typec_switch) = {
	.id		= UCLASS_TYPEC_SWITCH,
	.name		= "typec_switch",
	.post_probe	= typec_switch_post_probe,
};
