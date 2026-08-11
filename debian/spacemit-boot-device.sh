# Shared boot-device detection for the SpacemiT K3 u-boot packages.
#
# This file is intended to be sourced (".") by a package postinst script,
# not executed directly.
#
# When sourced it determines the target boot medium and the write offsets
# for every firmware component (bootinfo, FSBL, environment, u-boot.itb)
# and leaves the following variables set in the calling shell:
#
#   target          package sub-directory under /usr/lib/u-boot
#   BIN_DIR         directory holding the firmware binaries
#   IS_UEFI         1 when the system was booted via UEFI, 0 otherwise
#   BOOTINFO_FILE   bootinfo image name to flash
#   BOOTINFO        destination device for bootinfo
#   BOOTINFO_SEEK   byte seek offset for bootinfo
#   FSBL            destination device for the FSBL (u-boot SPL)
#   FSBL_SEEK       byte seek offset for the FSBL
#   ENV             destination device for the U-Boot environment
#   ENV_SEEK        byte seek offset for the environment
#   UBOOT           destination device for u-boot proper (u-boot.itb)
#   UBOOT_SEEK      kilobyte seek offset for u-boot proper
#
# The script exits the caller with status 0 when the installation has to
# be skipped (non-SpacemiT host, chroot, ambiguous root device) and with a
# non-zero status when the boot medium cannot be determined.

# Return 0 when running inside a chroot, 1 otherwise.
running_in_chroot() {
    if [ "${SYSTEMD_IGNORE_CHROOT:-0}" = "1" ]; then
        return 1
    fi

    if [ -e "/proc/1/root" ]; then
        root_dev_ino=$(stat -c '%d:%i' / 2>/dev/null) || return 0
        proc1_root_dev_ino=$(stat -L -c '%d:%i' /proc/1/root 2>/dev/null) || return 0

        [ "$root_dev_ino" = "$proc1_root_dev_ino" ] && return 1 || return 0
    fi

    if [ ! -d "/proc" ] || [ ! -r "/proc/version" ]; then
        [ "$$" = "1" ] && return 1 || return 0
    fi

    return 0
}

# Detect a Spacemit K3 / X100 platform.  Different kernels expose the SoC
# identity differently, so accept any of:
#   - "Spacemit(R) X100" model string in /proc/cpuinfo (older kernels)
#   - "spacemit,x100" uarch line in /proc/cpuinfo (newer kernels)
#   - "spacemit,k3" in /proc/device-tree/compatible (device tree)
target=""
if grep -q 'Spacemit(R) X100' /proc/cpuinfo \
    || grep -q 'spacemit,x100' /proc/cpuinfo \
    || { [ -r /proc/device-tree/compatible ] &&
         grep -qa 'spacemit,k3' /proc/device-tree/compatible; }; then
    target="spacemit"
else
    echo "Running not in Spacemit K3/X100, skipping install U-Boot to bootloader partition."
    exit 0
fi

# Never touch the boot media from inside a chroot.
if running_in_chroot; then
    echo "Running inside a chroot, skipping U-Boot installation to the bootloader partition."
    exit 0
fi

BOOT_MODE=""
for x in $(cat /proc/cmdline); do
    case $x in
    root=UUID=*)
        ROOT=${x#root=UUID=}
        DEVICES=$(blkid -s UUID | grep "$ROOT" | awk '{print $1}')
        DEVICE_COUNT=$(echo "$DEVICES" | wc -l)
        if [ "$DEVICE_COUNT" -gt 1 ]; then
            echo "Warning: multiple devices found with the same UUID $ROOT:"
            echo "$DEVICES"
            echo "This may cause installation issues, please ensure unique UUIDs."
            exit 0
        fi
        ROOT=$(echo "$DEVICES" | head -n1)
        ;;
    root=*)
        ROOT=${x#root=}
        ;;
    boot_mode=*)
        BOOT_MODE=${x#boot_mode=}
        ;;
    esac
done

# Map a boot_mode keyword to the matching boot device node.
get_boot_device_from_mode() {
    local mode=$1
    case $mode in
    emmc)
        echo "/dev/mmcblk2"
        ;;
    sdcard)
        echo "/dev/mmcblk0"
        ;;
    nor)
        if [ -e "/dev/mtdblock0" ]; then
            echo "/dev/mtdblock0"
        else
            echo ""
        fi
        ;;
    nand)
        # NAND devices are also exposed through the MTD subsystem, usually /dev/mtdblock0.
        if [ -e "/dev/mtdblock0" ]; then
            echo "/dev/mtdblock0"
        else
            echo ""
        fi
        ;;
    ufs)
        echo "/dev/sda"
        ;;
    *)
        echo ""
        ;;
    esac
}

# Derive the base (whole-disk) device from a rootfs device path.
get_base_device() {
    local dev=$1
    case $dev in
    "/dev/mmcblk0"*)
        echo "/dev/mmcblk0"
        ;;
    "/dev/mmcblk2"*)
        echo "/dev/mmcblk2"
        ;;
    "/dev/sda"*)
        echo "/dev/sda"
        ;;
    "/dev/nvme0n1"*)
        echo "/dev/nvme0n1"
        ;;
    *)
        echo ""
        ;;
    esac
}

# Determine the target device: when boot_mode is set and points to a device
# different from the rootfs, prefer the boot device; otherwise fall back to
# the rootfs device.
TARGET_DEVICE=""
if [ -n "$BOOT_MODE" ]; then
    BOOT_DEVICE=$(get_boot_device_from_mode "$BOOT_MODE")
    if [ -n "$BOOT_DEVICE" ] && [ -n "$ROOT" ]; then
        ROOT_BASE_DEVICE=$(get_base_device "$ROOT")
        if [ "$BOOT_DEVICE" != "$ROOT_BASE_DEVICE" ]; then
            TARGET_DEVICE="$BOOT_DEVICE"
            echo "Boot device ($BOOT_MODE -> $BOOT_DEVICE) differs from rootfs device ($ROOT_BASE_DEVICE), using boot device."
        else
            TARGET_DEVICE="$ROOT_BASE_DEVICE"
            echo "Boot device matches rootfs device, using $TARGET_DEVICE."
        fi
    elif [ -n "$BOOT_DEVICE" ]; then
        TARGET_DEVICE="$BOOT_DEVICE"
        echo "Using boot device from boot_mode=$BOOT_MODE: $TARGET_DEVICE"
    else
        echo "Unsupported boot_mode=$BOOT_MODE, unable to determine boot device."
        exit 1
    fi
fi

# If no target device was determined yet (no boot_mode or parsing failed),
# fall back to the original logic based on the rootfs device.
if [ -z "$TARGET_DEVICE" ] && [ -n "$ROOT" ]; then
    TARGET_DEVICE=$(get_base_device "$ROOT")
fi

# Detect UEFI boot early.
IS_UEFI=0
if [ -d "/sys/firmware/efi" ]; then
    IS_UEFI=1
    echo "UEFI boot detected, skipping u-boot proper (u-boot.itb) installation."
fi

# When booted via UEFI without an explicit boot_mode, fall back to a
# NOR + storage combination if NOR flash is present. This matches the
# common layout where NOR holds FSBL/bootinfo/env while SSD/UFS/eMMC
# holds the rootfs.
if [ "$IS_UEFI" -eq 1 ] && [ -z "$BOOT_MODE" ] && [ -f "/proc/mtd" ]; then
    echo "UEFI boot without boot_mode, NOR flash detected, falling back to NOR boot device."
    TARGET_DEVICE="/dev/mtdblock0"
    BOOT_MODE="nor"
fi

if [ -n "$TARGET_DEVICE" ]; then
    case $TARGET_DEVICE in
    "/dev/mmcblk0")
        BOOTINFO_FILE=bootinfo_block.bin
        BOOTINFO=/dev/mmcblk0
        BOOTINFO_SEEK=$((1024 * 1024))
        FSBL=/dev/mmcblk0
        FSBL_SEEK=$((1536 * 1024))
        ENV=/dev/mmcblk0
        ENV_SEEK=$((640 * 1024))
        UBOOT=/dev/mmcblk0
        # Offset expressed in kilobytes.
        UBOOT_SEEK=8192
        ;;
    "/dev/mmcblk2")
        BOOTINFO_FILE=bootinfo_block.bin
        BOOTINFO=/dev/mmcblk2
        BOOTINFO_SEEK=$((1024 * 1024))
        FSBL=/dev/mmcblk2
        FSBL_SEEK=$((1536 * 1024))
        ENV=/dev/mmcblk2
        ENV_SEEK=$((640 * 1024))
        UBOOT=/dev/mmcblk2
        # Offset expressed in kilobytes.
        UBOOT_SEEK=8192
        ;;
    "/dev/sda")
        BOOTINFO_FILE=bootinfo_block.bin
        BOOTINFO=/dev/sda
        BOOTINFO_SEEK=$((1024 * 1024))
        FSBL=/dev/sda
        FSBL_SEEK=$((1536 * 1024))
        ENV=/dev/sda
        ENV_SEEK=$((640 * 1024))
        UBOOT=/dev/sda
        # Offset expressed in kilobytes.
        UBOOT_SEEK=8192
        ;;
    "/dev/mtdblock0")
        if [ "$BOOT_MODE" = "nand" ]; then
            BOOTINFO_FILE=bootinfo_spinand.bin
        else
            BOOTINFO_FILE=bootinfo_spinor.bin
        fi

        # Parse /proc/mtd to learn the partition layout.
        if [ ! -f "/proc/mtd" ]; then
            echo "Error: /proc/mtd not found, cannot determine MTD partition layout"
            exit 1
        fi

        # Read the mtd0 partition name.
        MTD0_NAME=$(grep '^mtd0:' /proc/mtd | awk -F'"' '{print $2}')

        if [ "$MTD0_NAME" = "bootinfo" ]; then
            # mtd0 is the bootinfo partition: each component has its own
            # partition, write by partition number.
            echo "Detected MTD partition layout: independent partitions"
            BOOTINFO=/dev/mtdblock0
            BOOTINFO_SEEK=0
            FSBL=/dev/mtdblock1
            FSBL_SEEK=0
            ENV=/dev/mtdblock2
            ENV_SEEK=0
            UBOOT=/dev/mtdblock5
            # Offset expressed in kilobytes.
            UBOOT_SEEK=0
        else
            # mtd0 is the whole device: write with byte offsets.
            echo "Detected MTD partition layout: single device with offsets (mtd0=$MTD0_NAME)"
            BOOTINFO=/dev/mtdblock0
            BOOTINFO_SEEK=0
            FSBL=/dev/mtdblock0
            FSBL_SEEK=$((128 * 1024))
            ENV=/dev/mtdblock0
            ENV_SEEK=$((640 * 1024))
            UBOOT=/dev/mtdblock0
            # Offset expressed in kilobytes.
            UBOOT_SEEK=2112
        fi
        ;;
    "/dev/nvme0n1")
        # NVMe + NOR combination: inspect the MTD partition layout.
        if [ -f "/proc/mtd" ]; then
            # MTD present, boot from NOR.
            MTD0_NAME=$(grep '^mtd0:' /proc/mtd | awk -F'"' '{print $2}')

            if [ "$MTD0_NAME" = "bootinfo" ]; then
                # mtd0 is the bootinfo partition, independent partitions layout.
                BOOTINFO_FILE=bootinfo_spinor.bin
                BOOTINFO=/dev/mtdblock0
                BOOTINFO_SEEK=0
                FSBL=/dev/mtdblock1
                FSBL_SEEK=0
                ENV=/dev/mtdblock2
                ENV_SEEK=0
                UBOOT=/dev/mtdblock5
                # Offset expressed in kilobytes.
                UBOOT_SEEK=0
            else
                # mtd0 is the whole device, write with byte offsets.
                BOOTINFO_FILE=bootinfo_spinor.bin
                BOOTINFO=/dev/mtdblock0
                BOOTINFO_SEEK=0
                FSBL=/dev/mtdblock0
                FSBL_SEEK=$((128 * 1024))
                ENV=/dev/mtdblock0
                ENV_SEEK=$((640 * 1024))
                UBOOT=/dev/mtdblock0
                # Offset expressed in kilobytes.
                UBOOT_SEEK=2112
            fi
        else
            # No MTD, boot from eMMC.
            BOOTINFO_FILE=bootinfo_block.bin
            BOOTINFO=/dev/mmcblk2
            BOOTINFO_SEEK=$((1024 * 1024))
            FSBL=/dev/mmcblk2
            FSBL_SEEK=$((1536 * 1024))
            ENV=/dev/mmcblk2
            ENV_SEEK=$((640 * 1024))
            UBOOT=/dev/mmcblk2
            # Offset expressed in kilobytes.
            UBOOT_SEEK=2112
        fi
        ;;
    *)
        echo "Unsupported target device=$TARGET_DEVICE"
        exit 1
        ;;
    esac
else
    echo "Unable to determine target device (missing root= or boot_mode= in cmdline)"
    exit 1
fi

BIN_DIR="/usr/lib/u-boot/$target"
