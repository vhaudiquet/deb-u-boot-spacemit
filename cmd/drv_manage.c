// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025 Spacemit, Inc
 */

#include <common.h>
#include <command.h>
#include <dm.h>
#include <dm/device-internal.h>
#include <dm/uclass-internal.h>
#include <dm/lists.h>
#include <dm/root.h>

/**
 * do_drv_unbind() - Unbind devices from a driver
 *
 * Usage: drv unbind <uclass> [driver_name]
 */
static int do_drv_unbind(struct cmd_tbl *cmdtp, int flag, int argc,
			 char *const argv[])
{
	struct uclass *uc;
	struct udevice *dev, *next;
	enum uclass_id id;
	const char *uclass_name;
	const char *drv_name = NULL;
	int ret;
	int count = 0;

	if (argc < 2)
		return CMD_RET_USAGE;

	uclass_name = argv[1];
	if (argc > 2)
		drv_name = argv[2];

	id = uclass_get_by_name(uclass_name);
	if (id == UCLASS_INVALID) {
		printf("Invalid uclass: %s\n", uclass_name);
		return CMD_RET_FAILURE;
	}

	ret = uclass_get(id, &uc);
	if (ret) {
		printf("Cannot find uclass %s\n", uclass_name);
		return CMD_RET_FAILURE;
	}

	uclass_foreach_dev_safe (dev, next, uc) {
		if (drv_name && strcmp(dev->driver->name, drv_name))
			continue;

		printf("Unbinding device: %s (driver: %s)\n", dev->name,
		       dev->driver->name);

		ret = device_remove(dev, DM_REMOVE_NORMAL);
		if (ret)
			printf("  Warning: Failed to remove device '%s': %d\n",
			       dev->name, ret);

		ret = device_unbind(dev);
		if (ret) {
			printf("  Error: Failed to unbind device '%s': %d\n",
			       dev->name, ret);
		} else {
			count++;
		}
	}

	printf("Unbound %d devices.\n", count);
	return CMD_RET_SUCCESS;
}

/**
 * do_drv_remove() - Stop (remove) devices but do not unbind
 *
 * Usage: drv remove <uclass> [driver_name]
 */
static int do_drv_remove(struct cmd_tbl *cmdtp, int flag, int argc,
			 char *const argv[])
{
	struct uclass *uc;
	struct udevice *dev, *next;
	enum uclass_id id;
	const char *uclass_name;
	const char *drv_name = NULL;
	int ret;
	int count = 0;

	if (argc < 2)
		return CMD_RET_USAGE;

	uclass_name = argv[1];
	if (argc > 2)
		drv_name = argv[2];

	id = uclass_get_by_name(uclass_name);
	if (id == UCLASS_INVALID) {
		printf("Invalid uclass: %s\n", uclass_name);
		return CMD_RET_FAILURE;
	}

	ret = uclass_get(id, &uc);
	if (ret) {
		printf("Cannot find uclass %s\n", uclass_name);
		return CMD_RET_FAILURE;
	}

	uclass_foreach_dev_safe (dev, next, uc) {
		if (drv_name && strcmp(dev->driver->name, drv_name))
			continue;

		if (!device_active(dev))
			continue;

		printf("Removing (stopping) device: %s\n", dev->name);

		ret = device_remove(dev, DM_REMOVE_NORMAL);
		if (ret) {
			printf("  Error: Failed to remove device '%s': %d\n",
			       dev->name, ret);
		} else {
			count++;
		}
	}

	printf("Removed %d devices.\n", count);
	return CMD_RET_SUCCESS;
}

/**
 * do_drv_probe() - Probe a device by name
 *
 * Usage: drv probe <uclass> [device_name]
 */
static int do_drv_probe(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	struct udevice *dev;
	enum uclass_id id;
	const char *uclass_name;
	const char *dev_name = NULL;
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;

	uclass_name = argv[1];
	if (argc > 2)
		dev_name = argv[2];

	id = uclass_get_by_name(uclass_name);
	if (id == UCLASS_INVALID) {
		printf("Invalid uclass: %s\n", uclass_name);
		return CMD_RET_FAILURE;
	}

	if (dev_name) {
		printf("Searching for device '%s' in uclass '%s'...\n",
		       dev_name, uclass_name);
		ret = uclass_get_device_by_name(id, dev_name, &dev);
		if (ret) {
			printf("Failed to find/probe device '%s': %d\n",
			       dev_name, ret);
			return CMD_RET_FAILURE;
		}
		printf("Device '%s' probed successfully.\n", dev->name);
	} else {
		struct udevice *dev;
		int count = 0;

		printf("Probing all devices in uclass '%s'...\n", uclass_name);
		/* Iterate and probe manually to count them */
		uclass_foreach_dev_probe(id, dev)
		{
			count++;
		}
		printf("Probed %d devices in uclass '%s'.\n", count,
		       uclass_name);
	}

	return CMD_RET_SUCCESS;
}

static struct cmd_tbl drv_cmds[] = {
	U_BOOT_CMD_MKENT(unbind, 3, 1, do_drv_unbind, "", ""),
	U_BOOT_CMD_MKENT(remove, 3, 1, do_drv_remove, "", ""),
	U_BOOT_CMD_MKENT(probe, 3, 1, do_drv_probe, "", ""),
};

static int do_drv(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct cmd_tbl *cp;

	if (argc < 2)
		return CMD_RET_USAGE;

	cp = find_cmd_tbl(argv[1], drv_cmds, ARRAY_SIZE(drv_cmds));

	/* Drop the drv command */
	argc--;
	argv++;

	if (cp == NULL || argc > cp->maxargs)
		return CMD_RET_USAGE;

	if (flag == CMD_FLAG_REPEAT && !cmd_is_repeatable(cp))
		return CMD_RET_SUCCESS;

	return cp->cmd(cmdtp, flag, argc, argv);
}

U_BOOT_CMD(
	drv, 4, 1, do_drv, "Driver management tools",
	"drv unbind <uclass> [driver_name]  - Unbind (delete) devices\n"
	"drv remove <uclass> [driver_name]  - Remove (stop) devices\n"
	"drv probe <uclass> [device_name]   - Probe (start) a device (or all if no name)");
