// SPDX-License-Identifier: GPL-2.0+
/*
 * eSPI (Enhanced SPI) command
 *
 * Copyright (C) 2025, Spacemit
 */
#include <common.h>
#include <command.h>
#include <dm.h>
#include <errno.h>
#include <log.h>
#include <espi.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <string.h>
#include <linux/bitops.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include <dm/device-internal.h>
#include <dm/uclass-internal.h>
#include "../drivers/espi/espi-spacemit.h"

enum espi_cmd {
	ESPIC_INIT,
	ESPIC_PROBE,
	ESPIC_SEND_OOB,
	ESPIC_RECEIVE_OOB,
	ESPIC_VW,
	ESPIC_MEM,
	ESPIC_IO,
};
static int do_espi_init(void)
{
	struct udevice *dev;
	int ret;
	/* Get first eSPI device */
	ret = uclass_first_device(UCLASS_ESPI, &dev);
	if (ret) {
		printf("eSPI: No eSPI device found (ret=%d)\n", ret);
		return CMD_RET_FAILURE;
	}
	if (!dev) {
		printf("eSPI: No eSPI device available\n");
		return CMD_RET_FAILURE;
	}
	printf("eSPI: Found device: %s\n", dev->name);
	printf("eSPI: Device already probed and initialized\n");
	return CMD_RET_SUCCESS;
}
static int do_espi_probe(void)
{
	struct udevice *dev;
	int ret;
	printf("eSPI: Re-probing all eSPI devices...\n");
	
	/* First remove all existing eSPI devices */
	struct uclass *uc;
	uclass_id_foreach_dev(UCLASS_ESPI, dev, uc) {
		if (device_active(dev)) {
			printf("eSPI: Removing device: %s\n", dev->name);
			ret = device_remove(dev, DM_REMOVE_NORMAL);
			if (ret) {
				printf("eSPI: Failed to remove device %s (ret=%d)\n", dev->name, ret);
				/* Continue processing other devices */
			}
		}
	}
	
	/* Force probe all eSPI devices */
	ret = uclass_probe_all(UCLASS_ESPI);
	if (ret) {
		printf("eSPI: Probe failed (ret=%d)\n", ret);
		return CMD_RET_FAILURE;
	}
	printf("eSPI: All devices re-probed successfully\n");
	
	return CMD_RET_SUCCESS;
}
static int do_espi_send_oob(int argc, char *const argv[])
{
	struct udevice *dev;
	int ret;
	u8 buffer[ESPI_OOB_PAYLOAD_SIZE_MAX];
	int i, len = 0;
	/* Get first eSPI device */
	ret = uclass_first_device(UCLASS_ESPI, &dev);
	if (ret) {
		printf("eSPI: No eSPI device found (ret=%d)\n", ret);
		return CMD_RET_FAILURE;
	}
	if (!dev) {
		printf("eSPI: No eSPI device available\n");
		return CMD_RET_FAILURE;
	}
	/* Check parameters */
	if (argc < 3) {
		printf("Usage: espi send_oob <length> <byte1> [byte2] ...\n");
		printf("Example: espi send_oob 6 0x11 0x22 0x33 0x44 0x55 0x66\n");
		return CMD_RET_USAGE;
	}
	/* Parse length */
	len = (int)simple_strtoul(argv[2], NULL, 0);
	/* Check length validity */
	if (len > ESPI_OOB_PAYLOAD_SIZE_MAX) {
		printf("eSPI: Data too long (max %d bytes)\n", ESPI_OOB_PAYLOAD_SIZE_MAX);
		return CMD_RET_FAILURE;
	}
	if (len > (argc - 3)) {  // Note: checking argc-3 since argv[0]=espi, argv[1]=send_oob, argv[2]=length
		printf("eSPI: Data count mismatch\n");
		return CMD_RET_FAILURE;
	}
	/* Parse data bytes */
	printf("eSPI: Sending OOB data (%d bytes): ", len);
	for (i = 0; i < len; i++) {
		unsigned long byte_val = simple_strtoul(argv[i + 3], NULL, 0);
		if (byte_val > 0xFF) {
			printf("\neSPI: Invalid byte value '%s'\n", argv[i + 3]);
			return CMD_RET_FAILURE;
		}
		buffer[i] = (u8)byte_val;
		printf("%02x ", buffer[i]);
	}
	printf("\n");
	/* Call eSPI OOB send interface */
	ret = espi_send_oob(dev, buffer, len);
	if (ret) {
		printf("eSPI: Failed to send OOB data (ret=%d)\n", ret);
		return CMD_RET_FAILURE;
	}
	printf("eSPI: OOB data sent successfully\n");
	return CMD_RET_SUCCESS;
}
static int do_espi_receive_oob(int argc, char *const argv[])
{
	struct udevice *dev;
	int ret;
	u8 buffer[ESPI_OOB_PAYLOAD_SIZE_MAX];
	size_t len = ESPI_OOB_PAYLOAD_SIZE_MAX;
	int i;
	/* Get first eSPI device */
	ret = uclass_first_device(UCLASS_ESPI, &dev);
	if (ret) {
		printf("eSPI: No eSPI device found (ret=%d)\n", ret);
		return CMD_RET_FAILURE;
	}
	if (!dev) {
		printf("eSPI: No eSPI device available\n");
		return CMD_RET_FAILURE;
	}
	/* Check parameters */
	if (argc < 2) {
		printf("Usage: espi receive_oob [max_length]\n");
		printf("Example: espi receive_oob 64\n");
		return CMD_RET_USAGE;
	}
	/* If max length is specified */
	if (argc >= 3) {
		len = simple_strtoul(argv[2], NULL, 10);
		if (len > ESPI_OOB_PAYLOAD_SIZE_MAX) {
			printf("eSPI: Max length too large (max %d bytes)\n", ESPI_OOB_PAYLOAD_SIZE_MAX);
			return CMD_RET_FAILURE;
		}
	}
	printf("eSPI: Receiving OOB data (max %zu bytes)...\n", len);
	/* Call eSPI OOB receive interface */
	ret = espi_receive_oob(dev, buffer, len);
	if (ret < 0) {
		printf("eSPI: Failed to receive OOB data (ret=%d)\n", ret);
		return CMD_RET_FAILURE;
	}
	if (ret == 0) {
		printf("eSPI: No OOB data available\n");
		return CMD_RET_SUCCESS;
	}
	printf("eSPI: Received OOB data (%d bytes): ", ret);
	for (i = 0; i < ret; i++) {
		printf("%02x ", buffer[i]);
	}
	printf("\n");
	/* Display ASCII format (if printable) */
	printf("eSPI: ASCII: ");
	for (i = 0; i < ret; i++) {
		if (buffer[i] >= 32 && buffer[i] <= 126) {
			printf("%c", buffer[i]);
		} else {
			printf(".");
		}
	}
	printf("\n");
	return CMD_RET_SUCCESS;
}

/* VW command helper functions */
static int do_espi_vw_status(struct udevice *dev)
{
	u32 vw_sts, vw_data, vw_index, vw_ctl, vw_polarity;
	phys_addr_t espi_cfg_base;

	/* Get configuration base address */
	extern phys_addr_t espi_get_cfg_base(void);
	espi_cfg_base = espi_get_cfg_base();
	if (!espi_cfg_base) {
		printf("eSPI: Unable to get configuration base address\n");
		return CMD_RET_FAILURE;
	}

	vw_sts = readl((void *)espi_cfg_base + ESPI_SLAVE0_RXVW_STS);
	vw_data = readl((void *)espi_cfg_base + ESPI_SLAVE0_RXVW_DATA);
	vw_index = readl((void *)espi_cfg_base + ESPI_SLAVE0_RXVW_INDEX);
	vw_ctl = readl((void *)espi_cfg_base + ESPI_SLAVE0_VW_CTL);
	vw_polarity = readl((void *)espi_cfg_base + ESPI_SLAVE0_VW_POLARITY);

	printf("VWire Register Status:\n");
	printf("  RXVW_STS:     0x%08x\n", vw_sts);
	printf("  RXVW_DATA:    0x%08x\n", vw_data);
	printf("  RXVW_INDEX:   0x%08x\n", vw_index);
	printf("  VW_CTL:       0x%08x\n", vw_ctl);
	printf("  VW_POLARITY:  0x%08x\n", vw_polarity);

	if (vw_sts & SLAVE0_RXVW_STS_SYS_EVT_STS) {
		printf("  System event active\n");
	}

	return CMD_RET_SUCCESS;
}

static int do_espi_vw_sys_dump(struct udevice *dev)
{
	const char* sys_events[] = {
		"SLP_S3", "SLP_S4", "SLP_S5", "RSV",
		"SUS_STAT", "PLTRST", "OOB_RST_WARN", "RSV",
		"OOB_RST_ACK", "RSV", "WAKE", "PME",
		"SLV_BOOT_LOAD_DONE", "ERROR_FATAL", "ERROR_NONFATAL", "SLV_BOOT_LOAD_STATUS",
		"SCI", "SMI", "RCIN", "HOST_RST_ACK",
		"HOST_RST_WARN", "SMIOUT", "NMIOUT", "RSV"
	};
	const u16 sys_event_ids[] = {
		ESPI_VWIRE_SYSTEM_SLP_S3, ESPI_VWIRE_SYSTEM_SLP_S4,
		ESPI_VWIRE_SYSTEM_SLP_S5, 0,
		ESPI_VWIRE_SYSTEM_SUS_STAT, ESPI_VWIRE_SYSTEM_PLTRST,
		ESPI_VWIRE_SYSTEM_OOB_RST_WARN, 0,
		ESPI_VWIRE_SYSTEM_OOB_RST_ACK, 0, ESPI_VWIRE_SYSTEM_WAKE,
		ESPI_VWIRE_SYSTEM_PME,
		ESPI_VWIRE_SYSTEM_SLV_BOOT_LOAD_DONE, ESPI_VWIRE_SYSTEM_ERROR_FATAL,
		ESPI_VWIRE_SYSTEM_ERROR_NONFATAL, ESPI_VWIRE_SYSTEM_SLV_BOOT_LOAD_STATUS,
		ESPI_VWIRE_SYSTEM_SCI, ESPI_VWIRE_SYSTEM_SMI,
		ESPI_VWIRE_SYSTEM_RCIN, ESPI_VWIRE_SYSTEM_HOST_RST_ACK,
		ESPI_VWIRE_SYSTEM_HOST_RST_WARN, ESPI_VWIRE_SYSTEM_SMIOUT,
		ESPI_VWIRE_SYSTEM_NMIOUT, 0
	};

	int grp, evt, idx = 0;
	bool state;
	int ret;

	printf("System Events Status:\n");
	printf("Group | Event | Name              | State\n");
	printf("------+-------+-------------------+--------\n");

	for (grp = ESPI_VWIRE_SYSTEM_EVENT_GROUP_MIN;
		grp <= ESPI_VWIRE_SYSTEM_EVENT_GROUP_MAX; grp++) {
		for (evt = 0; evt < 4; evt++, idx++) {
			if (sys_event_ids[idx] == 0 || strcmp(sys_events[idx], "RSV") == 0)
				continue;

			ret = espi_receive_vwire(dev, sys_event_ids[idx], &state);
			if (ret == 0) {
				printf("  %d   |   %d   | %-17s | %s\n",
					grp, evt, sys_events[idx],
					state ? "high" : "low");
			} else {
				printf("  %d   |   %d   | %-17s | error(%d)\n",
					grp, evt, sys_events[idx], ret);
			}
		}
	}

	return CMD_RET_SUCCESS;
}

static int do_espi_vw_sys_put(struct udevice *dev, int argc, char *const argv[])
{
	const char* event_names[] = {
		"SLP_S3", "SLP_S4", "SLP_S5", "SUS_STAT", "PLTRST",
		"OOB_RST_WARN", "OOB_RST_ACK", "WAKE", "PME",
		"SLV_BOOT_LOAD_DONE", "ERROR_FATAL", "ERROR_NONFATAL",
		"SLV_BOOT_LOAD_STATUS", "SCI", "SMI", "RCIN", "HOST_RST_ACK",
		"HOST_RST_WARN", "SMIOUT", "NMIOUT"
	};
	const u16 event_ids[] = {
		ESPI_VWIRE_SYSTEM_SLP_S3, ESPI_VWIRE_SYSTEM_SLP_S4,
		ESPI_VWIRE_SYSTEM_SLP_S5, ESPI_VWIRE_SYSTEM_SUS_STAT,
		ESPI_VWIRE_SYSTEM_PLTRST, ESPI_VWIRE_SYSTEM_OOB_RST_WARN,
		ESPI_VWIRE_SYSTEM_OOB_RST_ACK, ESPI_VWIRE_SYSTEM_WAKE,
		ESPI_VWIRE_SYSTEM_PME, ESPI_VWIRE_SYSTEM_SLV_BOOT_LOAD_DONE,
		ESPI_VWIRE_SYSTEM_ERROR_FATAL, ESPI_VWIRE_SYSTEM_ERROR_NONFATAL,
		ESPI_VWIRE_SYSTEM_SLV_BOOT_LOAD_STATUS, ESPI_VWIRE_SYSTEM_SCI,
		ESPI_VWIRE_SYSTEM_SMI, ESPI_VWIRE_SYSTEM_RCIN,
		ESPI_VWIRE_SYSTEM_HOST_RST_ACK, ESPI_VWIRE_SYSTEM_HOST_RST_WARN,
		ESPI_VWIRE_SYSTEM_SMIOUT, ESPI_VWIRE_SYSTEM_NMIOUT
	};

	const char *event_name;
	bool state;
	u16 event_id = 0;
	int i, ret;

	/* argc check: espi vw sys put <event> <high|low> = 6 args */
	if (argc < 6) {
		printf("Usage: espi vw sys put <event> <high|low>\n");
		printf("Available events: SLP_S3, SLP_S4, SLP_S5, PLTRST, etc.\n");
		return CMD_RET_USAGE;
	}

	event_name = argv[4];

	/* Find event ID by name */
	for (i = 0; i < ARRAY_SIZE(event_names); i++) {
		if (strcasecmp(event_name, event_names[i]) == 0) {
			event_id = event_ids[i];
			break;
		}
	}

	if (event_id == 0) {
		printf("eSPI: Invalid system event name: %s\n", event_name);
		return CMD_RET_FAILURE;
	}

	/* Parse state */
	if (strcmp(argv[5], "high") == 0 || strcmp(argv[5], "1") == 0) {
		state = true;
	} else if (strcmp(argv[5], "low") == 0 || strcmp(argv[5], "0") == 0) {
		state = false;
	} else {
		printf("eSPI: Invalid state: %s (use 'high' or 'low')\n", argv[5]);
		return CMD_RET_FAILURE;
	}

	printf("eSPI: Setting system event %s = %s\n", event_name, state ? "high" : "low");

	ret = espi_send_vwire(dev, event_id, state);
	if (ret) {
		printf("eSPI: Setting system event failed (ret=%d)\n", ret);
		return CMD_RET_FAILURE;
	}

	printf("eSPI: System event set successfully\n");
	return CMD_RET_SUCCESS;
}

static int do_espi_vw_sys(struct udevice *dev, int argc, char *const argv[])
{
	if (argc < 4) {
		printf("Usage: espi vw sys <dump|put>\n");
		return CMD_RET_USAGE;
	}

	if (strcmp(argv[3], "dump") == 0) {
		return do_espi_vw_sys_dump(dev);
	} else if (strcmp(argv[3], "put") == 0) {
		return do_espi_vw_sys_put(dev, argc, argv);
	} else {
		printf("eSPI: Unknown sys subcommand: %s\n", argv[3]);
		return CMD_RET_USAGE;
	}
}

static int do_espi_vw_gpio_dump(struct udevice *dev)
{
	u8 group, vwire;
	u16 gpio;
	bool state;
	int ret;

	printf("GPIO Expander Status:\n");
	printf("Group | Wire | State\n");
	printf("------+------+--------\n");

	for (group = ESPI_VWIRE_GPIO_EXPANDER_GROUP_MIN;
		group <= ESPI_VWIRE_GPIO_EXPANDER_GROUP_MAX; group++) {
		for (vwire = 0; vwire < 4; vwire++) {
			gpio = ESPI_VWIRE_GPIO_EXPANDER(group, vwire);

			/* Use espi_rx_vwire to get current state */
			ret = espi_rx_vwire(gpio, &state);
			if (ret) {
				printf(" %3d  |  %d   | error(ret=%d)\n", group, vwire, ret);
			} else {
				printf(" %3d  |  %d   | %s\n", group, vwire,
					state ? "high" : "low");
			}
		}
	}

	return CMD_RET_SUCCESS;
}

static int do_espi_vw_gpio_put(struct udevice *dev, int argc, char *const argv[])
{
	u16 gpio;
	u8 group, vwire;
	bool state;
	int ret;

	/* argc check: espi vw gpio put <group> <wire> <high|low> = 7 args */
	if (argc < 7) {
		printf("Usage: espi vw gpio put <group> <wire> <high|low>\n");
		return CMD_RET_USAGE;
	}

	group = simple_strtoul(argv[4], NULL, 0);
	vwire = simple_strtoul(argv[5], NULL, 0);

	if (group < ESPI_VWIRE_GPIO_EXPANDER_GROUP_MIN || 
		group > ESPI_VWIRE_GPIO_EXPANDER_GROUP_MAX) {
		printf("eSPI: Invalid group: %d (range: %d-%d)\n",
			group, ESPI_VWIRE_GPIO_EXPANDER_GROUP_MIN, 
			ESPI_VWIRE_GPIO_EXPANDER_GROUP_MAX);
		return CMD_RET_FAILURE;
	}

	if (vwire >= 4) {
		printf("eSPI: Invalid vwire: %d (range: 0-3)\n", vwire);
		return CMD_RET_FAILURE;
	}

	if (strcmp(argv[6], "high") == 0 || strcmp(argv[6], "1") == 0) {
		state = true;
	} else if (strcmp(argv[6], "low") == 0 || strcmp(argv[6], "0") == 0) {
		state = false;
	} else {
		printf("eSPI: Invalid state: %s (use 'high' or 'low')\n", argv[6]);
		return CMD_RET_FAILURE;
	}

	gpio = ESPI_VWIRE_GPIO_EXPANDER(group, vwire);
	printf("eSPI: Setting GPIO expander (%d:%d) = %s\n", group, vwire, 
		state ? "high" : "low");

	ret = espi_tx_vwire(gpio, state);
	if (ret) {
		printf("eSPI: Failed to set GPIO expander state (ret=%d)\n", ret);
		return CMD_RET_FAILURE;
	}

	printf("eSPI: GPIO expander set successfully\n");
	return CMD_RET_SUCCESS;
}

static int do_espi_vw_gpio(struct udevice *dev, int argc, char *const argv[])
{
	if (argc < 4) {
		printf("Usage: espi vw gpio <dump|put>\n");
		return CMD_RET_USAGE;
	}

	if (strcmp(argv[3], "dump") == 0) {
		return do_espi_vw_gpio_dump(dev);
	} else if (strcmp(argv[3], "put") == 0) {
		return do_espi_vw_gpio_put(dev, argc, argv);
	} else {
		printf("eSPI: Unknown gpio subcommand: %s\n", argv[3]);
		return CMD_RET_USAGE;
	}
}

static int do_espi_vw_reset(struct udevice *dev)
{
	phys_addr_t espi_cfg_base;
	u32 vw_ctl;

	/* Get configuration base address */
	extern phys_addr_t espi_get_cfg_base(void);
	espi_cfg_base = espi_get_cfg_base();
	if (!espi_cfg_base) {
		printf("eSPI: Unable to get configuration base address\n");
		return CMD_RET_FAILURE;
	}

	/* Clear all IRQ status */
	writel(0xFFFFFFFF, (void *)espi_cfg_base + ESPI_SLAVE0_RXVW_STS);

	/* Reset VWire control register */
	vw_ctl = readl((void *)espi_cfg_base + ESPI_SLAVE0_VW_CTL);
	writel(0, (void *)espi_cfg_base + ESPI_SLAVE0_VW_CTL);
	mdelay(1);
	writel(vw_ctl, (void *)espi_cfg_base + ESPI_SLAVE0_VW_CTL);

	printf("eSPI: All VWires reset\n");
	return CMD_RET_SUCCESS;
}

static int do_espi_vw_send(struct udevice *dev, int argc, char *const argv[])
{
	u16 vwire;
	bool state;
	int ret;

	/* argc check: espi vw send <vwire_id> <state> = 5 args */
	if (argc < 5) {
		printf("Usage: espi vw send <id> <0|1>\n");
		printf("Example: espi vw send 0x41 1\n");
		return CMD_RET_USAGE;
	}

	/* Parse VWire ID */
	if (strncmp(argv[3], "0x", 2) == 0) {
		vwire = simple_strtoul(argv[3], NULL, 16);
	} else {
		vwire = simple_strtoul(argv[3], NULL, 10);
	}

	/* Parse state */
	if (strcmp(argv[4], "1") == 0 || strcmp(argv[4], "high") == 0) {
		state = true;
	} else if (strcmp(argv[4], "0") == 0 || strcmp(argv[4], "low") == 0) {
		state = false;
	} else {
		printf("eSPI: Invalid state: %s (use 0 or 1)\n", argv[4]);
		return CMD_RET_FAILURE;
	}

	printf("eSPI: Sending VWire 0x%04x = %s\n", vwire, state ? "high" : "low");

	ret = espi_send_vwire(dev, vwire, state);
	if (ret) {
		printf("eSPI: Sending VWire failed (ret=%d)\n", ret);
		return CMD_RET_FAILURE;
	}

	printf("eSPI: VWire sent successfully\n");
	return CMD_RET_SUCCESS;
}

static int do_espi_vw_receive(struct udevice *dev, int argc, char *const argv[])
{
	u16 vwire;
	bool state;
	int ret;

	/* argc check: espi vw receive <vwire_id> = 4 args */
	if (argc < 4) {
		printf("Usage: espi vw receive <id>\n");
		printf("Example: espi vw receive 0x41\n");
		return CMD_RET_USAGE;
	}

	/* Parse VWire ID */
	if (strncmp(argv[3], "0x", 2) == 0) {
		vwire = simple_strtoul(argv[3], NULL, 16);
	} else {
		vwire = simple_strtoul(argv[3], NULL, 10);
	}

	printf("eSPI: Receiving VWire 0x%04x...\n", vwire);

	ret = espi_receive_vwire(dev, vwire, &state);
	if (ret) {
		printf("eSPI: Receiving VWire failed (ret=%d)\n", ret);
		return CMD_RET_FAILURE;
	}

	printf("eSPI: VWire 0x%04x = %s\n", vwire, state ? "high" : "low");
	return CMD_RET_SUCCESS;
}

static int do_espi_vw(int argc, char *const argv[])
{
	struct udevice *dev;
	int ret;

	/* Get first eSPI device */
	ret = uclass_first_device(UCLASS_ESPI, &dev);
	if (ret) {
		printf("eSPI: No eSPI device found (ret=%d)\n", ret);
		return CMD_RET_FAILURE;
	}
	if (!dev) {
		printf("eSPI: No eSPI device available\n");
		return CMD_RET_FAILURE;
	}

	if (argc < 3) {
		printf("Usage: espi vw <subcommand> [params...]\n");
		printf("Subcommands:\n");
		printf("  status               - show VW register status\n");
		printf("  sys dump             - dump all system VW states\n");
		printf("  sys put <event> <high|low> - set system VW state\n");
		printf("  gpio dump            - dump all GPIO VW states\n");
		printf("  gpio put <group> <wire> <high|low> - set GPIO VW state\n");
		printf("  send <id> <state>    - send VW message\n");
		printf("  receive <id>         - receive VW state\n");
		printf("  reset                - reset all VWires\n");
		return CMD_RET_USAGE;
	}

	if (strcmp(argv[2], "status") == 0) {
		return do_espi_vw_status(dev);
	} else if (strcmp(argv[2], "sys") == 0) {
		return do_espi_vw_sys(dev, argc, argv);
	} else if (strcmp(argv[2], "gpio") == 0) {
		return do_espi_vw_gpio(dev, argc, argv);
	} else if (strcmp(argv[2], "send") == 0) {
		return do_espi_vw_send(dev, argc, argv);
	} else if (strcmp(argv[2], "receive") == 0) {
		return do_espi_vw_receive(dev, argc, argv);
	} else if (strcmp(argv[2], "reset") == 0) {
		return do_espi_vw_reset(dev);
	} else {
		printf("eSPI: Unknown VW subcommand: %s\n", argv[2]);
		return CMD_RET_USAGE;
	}
}

static u32 espi_get_pr_mem_size(void)
{
	u32 mem_size = ESPI_MEM_SIZE;

	if (g_espi_priv && g_espi_priv->pr_mem_base1 > g_espi_priv->pr_mem_base0)
		mem_size = g_espi_priv->pr_mem_base1 - g_espi_priv->pr_mem_base0;

	return mem_size;
}

static int do_espi_mem_read(int argc, char *const argv[])
{
	u32 addr;
	u32 data32;
	u16 data16;
	u8 data8;
	int width;
	void *mapped_addr;
	u32 mem_size;
	/* Check parameters */
	if (argc < 5) {
		mem_size = espi_get_pr_mem_size();
		printf("Usage: espi mem read <addr> <width>\n");
		printf("  addr: Memory address (0x0 - 0x%x, size %u bytes)\n",
		       mem_size - 1, mem_size);
		printf("  width: Access width (1, 2, or 4 bytes)\n");
		printf("Example: espi mem read 0x100 4\n");
		return CMD_RET_USAGE;
	}
	mem_size = espi_get_pr_mem_size();
	/* Parse address */
	if (strncmp(argv[3], "0x", 2) == 0) {
		addr = simple_strtoul(argv[3], NULL, 16);
	} else {
		addr = simple_strtoul(argv[3], NULL, 10);
	}
	/* Parse access width */
	width = simple_strtoul(argv[4], NULL, 10);
	/* Check address boundary */
	if (addr >= mem_size) {
		printf("eSPI: Address 0x%x exceeds %u-byte boundary\n", addr, mem_size);
		return CMD_RET_FAILURE;
	}
	/* Check access width */
	if (width != 1 && width != 2 && width != 4) {
		printf("eSPI: Invalid width %d. Must be 1, 2, or 4 bytes\n", width);
		return CMD_RET_FAILURE;
	}
	/* Check address alignment */
	if (addr % width != 0) {
		printf("eSPI: Address 0x%x not aligned to %d-byte boundary\n", addr, width);
		return CMD_RET_FAILURE;
	}
	/* Check if exceeds boundary */
	if (addr + width > mem_size) {
		printf("eSPI: Access would exceed %u-byte boundary\n", mem_size);
		return CMD_RET_FAILURE;
	}
	printf("eSPI: Reading %d byte(s) from Shared Memory address 0x%03x...\n", width, addr);
	/* Use address from global structure */
	if (!g_espi_priv || !g_espi_priv->pr_mem_base0) {
		printf("eSPI: Shared Memory not configured or device not initialized\n");
		return CMD_RET_FAILURE;
	}
	mapped_addr = (void *)(uintptr_t)(g_espi_priv->pr_mem_base0 + addr);
	/* Perform read operation based on width */
	switch (width) {
	case 1:
		data8 = readb(mapped_addr);
		printf("eSPI: Shared Memory[0x%03x] = 0x%02x\n", addr, data8);
		break;
	case 2:
		data16 = readw(mapped_addr);
		printf("eSPI: Shared Memory[0x%03x] = 0x%04x\n", addr, data16);
		break;
	case 4:
		data32 = readl(mapped_addr);
		printf("eSPI: Shared Memory[0x%03x] = 0x%08x\n", addr, data32);
		break;
	}
	return CMD_RET_SUCCESS;
}
static int do_espi_mem_write(int argc, char *const argv[])
{
	u32 addr;
	u32 data32;
	u16 data16;
	u8 data8;
	int width;
	unsigned long value;
	void *mapped_addr;
	u32 mem_size;
	/* Check parameters */
	if (argc < 6) {
		mem_size = espi_get_pr_mem_size();
		printf("Usage: espi mem write <addr> <width> <value>\n");
		printf("  addr: Memory address (0x0 - 0x%x, size %u bytes)\n",
		       mem_size - 1, mem_size);
		printf("  width: Access width (1, 2, or 4 bytes)\n");
		printf("  value: Data value to write\n");
		printf("Example: espi mem write 0x100 4 0x12345678\n");
		return CMD_RET_USAGE;
	}
	mem_size = espi_get_pr_mem_size();
	/* Parse address */
	if (strncmp(argv[3], "0x", 2) == 0) {
		addr = simple_strtoul(argv[3], NULL, 16);
	} else {
		addr = simple_strtoul(argv[3], NULL, 10);
	}
	/* Parse access width */
	width = simple_strtoul(argv[4], NULL, 10);
	/* Parse data value */
	if (strncmp(argv[5], "0x", 2) == 0) {
		value = simple_strtoul(argv[5], NULL, 16);
	} else {
		value = simple_strtoul(argv[5], NULL, 10);
	}
	/* Check address boundary */
	if (addr >= mem_size) {
		printf("eSPI: Address 0x%x exceeds %u-byte boundary\n", addr, mem_size);
		return CMD_RET_FAILURE;
	}
	/* Check access width */
	if (width != 1 && width != 2 && width != 4) {
		printf("eSPI: Invalid width %d. Must be 1, 2, or 4 bytes\n", width);
		return CMD_RET_FAILURE;
	}
	/* Check address alignment */
	if (addr % width != 0) {
		printf("eSPI: Address 0x%x not aligned to %d-byte boundary\n", addr, width);
		return CMD_RET_FAILURE;
	}
	/* Check if exceeds boundary */
	if (addr + width > mem_size) {
		printf("eSPI: Access would exceed %u-byte boundary\n", mem_size);
		return CMD_RET_FAILURE;
	}
	/* Check data value range */
	switch (width) {
	case 1:
		if (value > 0xFF) {
			printf("eSPI: Value 0x%lx too large for 1-byte access\n", value);
			return CMD_RET_FAILURE;
		}
		data8 = (u8)value;
		break;
	case 2:
		if (value > 0xFFFF) {
			printf("eSPI: Value 0x%lx too large for 2-byte access\n", value);
			return CMD_RET_FAILURE;
		}
		data16 = (u16)value;
		break;
	case 4:
		data32 = (u32)value;
		break;
	}
	printf("eSPI: Writing %d byte(s) to Shared Memory address 0x%03x, value = 0x%0*lx\n", 
		width, addr, width * 2, value);
	/* Use address from global structure */
	if (!g_espi_priv || !g_espi_priv->pr_mem_base0) {
		printf("eSPI: Shared Memory not configured or device not initialized\n");
		return CMD_RET_FAILURE;
	}
	mapped_addr = (void *)(uintptr_t)(g_espi_priv->pr_mem_base0 + addr);
	/* Perform write operation based on width */
	switch (width) {
	case 1:
		writeb(data8, mapped_addr);
		break;
	case 2:
		writew(data16, mapped_addr);
		break;
	case 4:
		writel(data32, mapped_addr);
		break;
	}
	printf("eSPI: Shared Memory write completed successfully\n");
	return CMD_RET_SUCCESS;
}
static int do_espi_io_read(int argc, char *const argv[])
{
	u32 addr;
	u32 data32;
	u16 data16;
	u8 data8;
	int width;
	void *mapped_addr;
	/* Check parameters */
	if (argc < 5) {
		printf("Usage: espi io read <addr> <width>\n");
		printf("  addr: IO port address\n");
		printf("  width: Access width (1, 2, or 4 bytes)\n");
		printf("Example: espi io read 0x80 1\n");
		return CMD_RET_USAGE;
	}
	/* Parse address */
	if (strncmp(argv[3], "0x", 2) == 0) {
		addr = simple_strtoul(argv[3], NULL, 16);
	} else {
		addr = simple_strtoul(argv[3], NULL, 10);
	}
	/* Parse access width */
	width = simple_strtoul(argv[4], NULL, 10);
	/* Check access width */
	if (width != 1 && width != 2 && width != 4) {
		printf("eSPI: Invalid width %d. Must be 1, 2, or 4 bytes\n", width);
		return CMD_RET_FAILURE;
	}
	/* Check address alignment */
	if (addr % width != 0) {
		printf("eSPI: Address 0x%x not aligned to %d-byte boundary\n", addr, width);
		return CMD_RET_FAILURE;
	}
	printf("eSPI: Reading %d byte(s) from Shared IO address 0x%x...\n", width, addr);
	/* Use address from global structure */
	if (!g_espi_priv || !g_espi_priv->pr_io_base) {
		printf("eSPI: Shared IO not configured or device not initialized\n");
		return CMD_RET_FAILURE;
	}
	mapped_addr = (void *)(uintptr_t)(g_espi_priv->pr_io_base + addr);
	/* Perform read operation based on width */
	switch (width) {
	case 1:
		data8 = readb(mapped_addr);
		printf("eSPI: Shared IO[0x%x] = 0x%02x\n", addr, data8);
		break;
	case 2:
		data16 = readw(mapped_addr);
		printf("eSPI: Shared IO[0x%x] = 0x%04x\n", addr, data16);
		break;
	case 4:
		data32 = readl(mapped_addr);
		printf("eSPI: Shared IO[0x%x] = 0x%08x\n", addr, data32);
		break;
	}
	return CMD_RET_SUCCESS;
}
static int do_espi_io_write(int argc, char *const argv[])
{
	u32 addr;
	u32 data32;
	u16 data16;
	u8 data8;
	int width;
	unsigned long value;
	void *mapped_addr;
	/* Check parameters */
	if (argc < 6) {
		printf("Usage: espi io write <addr> <width> <value>\n");
		printf("  addr: IO port address\n");
		printf("  width: Access width (1, 2, or 4 bytes)\n");
		printf("  value: Data value to write\n");
		printf("Example: espi io write 0x80 1 0x42\n");
		return CMD_RET_USAGE;
	}
	/* Parse address */
	if (strncmp(argv[3], "0x", 2) == 0) {
		addr = simple_strtoul(argv[3], NULL, 16);
	} else {
		addr = simple_strtoul(argv[3], NULL, 10);
	}
	/* Parse access width */
	width = simple_strtoul(argv[4], NULL, 10);
	/* Parse data value */
	if (strncmp(argv[5], "0x", 2) == 0) {
		value = simple_strtoul(argv[5], NULL, 16);
	} else {
		value = simple_strtoul(argv[5], NULL, 10);
	}
	/* Check access width */
	if (width != 1 && width != 2 && width != 4) {
		printf("eSPI: Invalid width %d. Must be 1, 2, or 4 bytes\n", width);
		return CMD_RET_FAILURE;
	}
	/* Check address alignment */
	if (addr % width != 0) {
		printf("eSPI: Address 0x%x not aligned to %d-byte boundary\n", addr, width);
		return CMD_RET_FAILURE;
	}
	/* Check data value range */
	switch (width) {
	case 1:
		if (value > 0xFF) {
			printf("eSPI: Value 0x%lx too large for 1-byte access\n", value);
			return CMD_RET_FAILURE;
		}
		data8 = (u8)value;
		break;
	case 2:
		if (value > 0xFFFF) {
			printf("eSPI: Value 0x%lx too large for 2-byte access\n", value);
			return CMD_RET_FAILURE;
		}
		data16 = (u16)value;
		break;
	case 4:
		data32 = (u32)value;
		break;
	}
	printf("eSPI: Writing %d byte(s) to Shared IO address 0x%x, value = 0x%0*lx\n", 
		width, addr, width * 2, value);
	/* Use address from global structure */
	if (!g_espi_priv || !g_espi_priv->pr_io_base) {
		printf("eSPI: Shared IO not configured or device not initialized\n");
		return CMD_RET_FAILURE;
	}
	mapped_addr = (void *)(uintptr_t)(g_espi_priv->pr_io_base + addr);
	/* Perform write operation based on width */
	switch (width) {
	case 1:
		writeb(data8, mapped_addr);
		break;
	case 2:
		writew(data16, mapped_addr);
		break;
	case 4:
		writel(data32, mapped_addr);
		break;
	}
	printf("eSPI: Shared IO write completed successfully\n");
	return CMD_RET_SUCCESS;
}
static int do_espi_mem(int argc, char *const argv[])
{
	if (argc < 3) {
		u32 mem_size = espi_get_pr_mem_size();
		printf("Usage: espi mem <read|write> [args...]\n");
		printf("  espi mem read <addr> <width> - read from shared memory\n");
		printf("  espi mem write <addr> <width> <value> - write to shared memory\n");
		printf("  addr: Memory address (0x0 - 0x%x, size %u bytes)\n",
		       mem_size - 1, mem_size);
		printf("  width: Access width (1, 2, or 4 bytes)\n");
		return CMD_RET_USAGE;
	}
	if (!strncmp(argv[2], "read", 4)) {
		return do_espi_mem_read(argc, argv);
	} else if (!strncmp(argv[2], "write", 5)) {
		return do_espi_mem_write(argc, argv);
	} else {
		printf("eSPI: Invalid mem operation '%s'. Use 'read' or 'write'\n", argv[2]);
		return CMD_RET_USAGE;
	}
}
static int do_espi_io(int argc, char *const argv[])
{
	if (argc < 3) {
		printf("Usage: espi io <read|write> [args...]\n");
		printf("  espi io read <addr> <width> - read from shared IO\n");
		printf("  espi io write <addr> <width> <value> - write to shared IO\n");
		printf("  addr: IO port address\n");
		printf("  width: Access width (1, 2, or 4 bytes)\n");
		return CMD_RET_USAGE;
	}
	if (!strncmp(argv[2], "read", 4)) {
		return do_espi_io_read(argc, argv);
	} else if (!strncmp(argv[2], "write", 5)) {
		return do_espi_io_write(argc, argv);
	} else {
		printf("eSPI: Invalid io operation '%s'. Use 'read' or 'write'\n", argv[2]);
		return CMD_RET_USAGE;
	}
}
static int do_espi(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	enum espi_cmd sub_cmd;
	const char *str_cmd;
	if (argc < 2)
		return CMD_RET_USAGE;
	str_cmd = argv[1];
	/* Parse subcommand */
	if (!strncmp(str_cmd, "init", 4)) {
		sub_cmd = ESPIC_INIT;
	} else if (!strncmp(str_cmd, "probe", 5)) {
		sub_cmd = ESPIC_PROBE;
	} else if (!strncmp(str_cmd, "send_oob", 8)) {
		sub_cmd = ESPIC_SEND_OOB;
	} else if (!strncmp(str_cmd, "receive_oob", 11)) {
		sub_cmd = ESPIC_RECEIVE_OOB;
	} else if (!strncmp(str_cmd, "vw", 2)) {
		sub_cmd = ESPIC_VW;
	} else if (!strncmp(str_cmd, "mem", 3)) {
		sub_cmd = ESPIC_MEM;
	} else if (!strncmp(str_cmd, "io", 2)) {
		sub_cmd = ESPIC_IO;
	} else {
		return CMD_RET_USAGE;
	}
	/* Execute corresponding subcommand */
	switch (sub_cmd) {
	case ESPIC_INIT:
		return do_espi_init();
	case ESPIC_PROBE:
		return do_espi_probe();
	case ESPIC_SEND_OOB:
		return do_espi_send_oob(argc, argv);
	case ESPIC_RECEIVE_OOB:
		return do_espi_receive_oob(argc, argv);
	case ESPIC_VW:
		return do_espi_vw(argc, argv);
	case ESPIC_MEM:
		return do_espi_mem(argc, argv);
	case ESPIC_IO:
		return do_espi_io(argc, argv);
	default:
		return CMD_RET_USAGE;
	}
}
U_BOOT_CMD(espi, CONFIG_SYS_MAXARGS, 0, do_espi,
	   "eSPI (Enhanced SPI) operations",
	   "init   - initialize eSPI devices\n"
	   "espi probe  - probe all eSPI devices\n"
	   "espi send_oob <length> <byte1> [byte2] ... - send OOB data\n"
	   "espi receive_oob [max_len] - receive OOB data\n"
	   "espi vw <subcommand> [params...] - virtual wire operations\n"
	   "  status               - show VW register status\n"
	   "  sys dump             - dump all system VW states\n"
	   "  sys put <event> <high|low> - set system VW state\n"
	   "  gpio dump            - dump all GPIO VW states\n"
	   "  gpio put <group> <wire> <high|low> - set GPIO VW state\n"
	   "  send <id> <state>    - send VW message\n"
	   "  receive <id>         - receive VW state\n"
	   "  reset                - reset all VWires\n"
	   "espi mem read <addr> <width> - read from shared memory\n"
	   "espi mem write <addr> <width> <value> - write to shared memory\n"
	   "espi io read <addr> <width> - read from shared IO\n"
	   "espi io write <addr> <width> <value> - write to shared IO"
);
