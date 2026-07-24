// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023 Spacemit, Inc
 */

#include <common.h>
#include <dm.h>
#include <dm/ofnode.h>
#include <env.h>
#include <fdtdec.h>
#include <image.h>
#include <log.h>
#include <mapmem.h>
#include <spl.h>
#include <init.h>
#include <virtio_types.h>
#include <virtio.h>
#include <asm/io.h>
#include <asm/sections.h>

#define SYS_GMAC_CFG	(0x2f028004)

DECLARE_GLOBAL_DATA_PTR;
void k1pro_gmac_init(void);

int board_init(void)
{
	return 0;
}

int board_late_init(void)
{
	ulong kernel_start;
	ofnode chosen_node;
	int ret;

#ifndef CONFIG_K1_PRO_BOARD_QEMU
	k1pro_gmac_init();
#endif
	chosen_node = ofnode_path("/chosen");
	if (!ofnode_valid(chosen_node)) {
		debug("No chosen node found, can't get kernel start address\n");
		return 0;
	}

	ret = ofnode_read_u64(chosen_node, "riscv,kernel-start",
			      (u64 *)&kernel_start);
	if (ret) {
		debug("Can't find kernel start address in device tree\n");
		return 0;
	}

	env_set_hex("kernel_start", kernel_start);

	return 0;
}

#ifdef CONFIG_SPL
u32 spl_boot_device(void)
{
#ifdef CONFIG_K1_PRO_BOARD_QEMU
	/* RISC-V QEMU only supports RAM as SPL boot device */
	return BOOT_DEVICE_RAM;
#endif

	int boot_mode = 0;

	/*select spl boot device*/

	/*select sd as default. later it can select diff boot type by
	 *obtaining pin info.
	 * */
	boot_mode = 0x1;

	switch (boot_mode) {
	case 0:
		return BOOT_DEVICE_SPI;
	case 1:
		return BOOT_DEVICE_MMC2;//emmc
	case 2:
		return BOOT_DEVICE_MMC1;//sd
	case 3:
		return BOOT_DEVICE_UART;
	case 4:
		return BOOT_DEVICE_NAND;
	default:
		debug("Unsupported boot device 0x%x.\n",
			  boot_mode);
		return BOOT_DEVICE_NONE;
	}
}

void board_boot_order(u32 *spl_boot_list)
{
	/*select one boot device*/
	spl_boot_list[0] = BOOT_DEVICE_MMC1;
	spl_boot_list[1] = spl_boot_device();
	spl_boot_list[2] = BOOT_DEVICE_RAM;
}

#endif

void *board_fdt_blob_setup(int *err)
{
	*err = 0;

	/* Stored the DTB address there during our init */
	if (IS_ENABLED(CONFIG_OF_SEPARATE) || IS_ENABLED(CONFIG_OF_BOARD)) {
		if (gd->arch.firmware_fdt_addr){
			if (!fdt_check_header((void *)(ulong)gd->arch.firmware_fdt_addr)){
				return (void *)(ulong)gd->arch.firmware_fdt_addr;
			}
		}
	}
	return (ulong *)&_end;
}

void k1pro_gmac_init(void)
{
	volatile unsigned int val;

	//enable rmii
	val = readl((const volatile void *)SYS_GMAC_CFG);
	val |= BIT(0);
	writel(val, (volatile void __iomem *)SYS_GMAC_CFG);
}

enum board_boot_mode get_boot_pin_select(void)
{
	return BOOT_MODE_SD;
}
