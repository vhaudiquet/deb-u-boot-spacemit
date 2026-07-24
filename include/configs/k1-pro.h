/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2023 Spacemit, Inc
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include <linux/sizes.h>


#ifdef CONFIG_K1_PRO_BOARD_QEMU
    #define RISCV_MMODE_TIMERBASE		0x2000000
    #define RISCV_MMODE_TIMER_FREQ		1000000
    #define RISCV_SMODE_TIMER_FREQ		1000000
#elif defined(CONFIG_K1_PRO_BOARD_FPGA) || defined(CONFIG_K1_PRO_BOARD_SIMULATION)
    #define RISCV_MMODE_TIMERBASE		0x2000000
    #define RISCV_MMODE_TIMER_FREQ		1000000
    #define RISCV_SMODE_TIMER_FREQ		1000000
#else
    #error "unknown k1-pro board defined"
#endif

#define CONFIG_IPADDR    10.0.92.253
#define CONFIG_SERVERIP  10.0.92.134
#define CONFIG_GATEWAYIP 10.0.92.1
#define CONFIG_NETMASK   255.255.255.0

/*
 use (ram_base+4MB offset) as the address to loading image.
 use ram_size-32MB as the max size to loading image, if
 (ram_size-32MB) more than 500MB, set load image size as
 500MB.
*/
#define RECOVERY_RAM_SIZE (gd->ram_size - 0x2000000)
#define RECOVERY_LOAD_IMG_SIZE_MAX (RECOVERY_RAM_SIZE > 0x1f400000 ? 0x1f400000 : RECOVERY_RAM_SIZE)
#define RECOVERY_LOAD_IMG_ADDR (gd->ram_base + 0x400000)
#define RECOVERY_LOAD_IMG_SIZE (RECOVERY_LOAD_IMG_SIZE_MAX)

#ifndef __ASSEMBLY__
enum board_boot_mode {
	BOOT_MODE_NONE = 0,
	BOOT_MODE_USB = 0x55a,
	BOOT_MODE_EMMC,
	BOOT_MODE_NAND,
	BOOT_MODE_NOR,
	BOOT_MODE_SD,
	BOOT_MODE_SHELL = 0x55f,
};
#endif

/* Environment options */

#define BOOT_TARGET_DEVICES(func) \
	func(QEMU, qemu, na)

#include <config_distro_bootcmd.h>

#define BOOTENV_DEV_QEMU(devtypeu, devtypel, instance) \
	"bootcmd_qemu=" \
		"if env exists kernel_start; then " \
			"bootm ${kernel_start} - ${fdtcontroladdr};" \
		"fi;\0"

#define BOOTENV_DEV_NAME_QEMU(devtypeu, devtypel, instance) \
	"qemu "

#define CONFIG_EXTRA_ENV_SETTINGS \
	"fdt_high=0xffffffffffffffff\0" \
	"initrd_high=0xffffffffffffffff\0" \
	"kernel_addr_r=0x84000000\0" \
	"kernel_comp_addr_r=0x88000000\0" \
	"kernel_comp_size=0x4000000\0" \
	"fdt_addr_r=0x8c000000\0" \
	"scriptaddr=0x8c100000\0" \
	"pxefile_addr_r=0x8c200000\0" \
	"ramdisk_addr_r=0x8c300000\0" \
	"ethaddr=02:f6:c3:67:27:55\0" \
	BOOTENV

#define SPL_SPI_BOOT_MTD_NAME "uboot"

#endif /* __CONFIG_H */
