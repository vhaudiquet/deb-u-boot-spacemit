// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2001
 * Denis Peter, MPL AG Switzerland
 */

/*
 * SCSI support.
 */
#include <common.h>
#include <blk.h>
#include <command.h>
#include <errno.h>
#include <linux/err.h>
#include <malloc.h>
#include <memalign.h>
#include <part.h>
#include <scsi.h>
#include <watchdog.h>

static int scsi_curr_dev; /* current device */
#define SCSI_WIPE_BUF_BYTES	(128 * 1024)

static int scsi_get_desc(int dev, struct blk_desc **descp)
{
	int ret;

	ret = scsi_scan(false);
	if (ret) {
		printf("SCSI scan failed (%d)\n", ret);
		return ret;
	}

	*descp = blk_get_dev("scsi", dev);
	if (!*descp) {
		printf("No SCSI device found at dev %d\n", dev);
		return -ENODEV;
	}

	return 0;
}

static int scsi_validate_range(struct blk_desc *desc, lbaint_t start,
			       lbaint_t blkcnt)
{
	if (!blkcnt) {
		printf("Block count is 0\n");
		return -EINVAL;
	}
	if (start >= desc->lba || blkcnt > desc->lba - start) {
		printf("Block range out of device: start " LBAFU
		       ", count " LBAFU ", device " LBAFU "\n",
		       start, blkcnt, desc->lba);
		return -ERANGE;
	}

	return 0;
}

static int scsi_calc_blocks(struct blk_desc *desc, ulong offset, ulong len,
			    lbaint_t *startp, lbaint_t *blkcntp)
{
	ulong blksz = desc->blksz;
	lbaint_t start;
	lbaint_t blkcnt;

	if (!blksz) {
		printf("Invalid block size\n");
		return -EINVAL;
	}
	if (!len) {
		printf("Length is 0\n");
		return -EINVAL;
	}
	if ((offset % blksz) || (len % blksz)) {
		printf("Offset/length must be aligned to %lu bytes\n", blksz);
		return -EINVAL;
	}

	start = offset / blksz;
	blkcnt = len / blksz;

	if (scsi_validate_range(desc, start, blkcnt))
		return -ERANGE;

	*startp = start;
	*blkcntp = blkcnt;
	return 0;
}

static int scsi_zero_blocks(struct blk_desc *desc, lbaint_t start,
			    lbaint_t blkcnt)
{
	ulong blksz = desc->blksz;
	ulong buf_bytes = SCSI_WIPE_BUF_BYTES;
	lbaint_t chunk_blks;
	void *buf;
	lbaint_t remaining = blkcnt;
	lbaint_t blk = start;
	unsigned long n;

	if (!blksz) {
		printf("Invalid block size\n");
		return CMD_RET_FAILURE;
	}

	if (buf_bytes < blksz)
		buf_bytes = blksz;

	chunk_blks = buf_bytes / blksz;
	if (!chunk_blks)
		chunk_blks = 1;
	buf_bytes = (ulong)chunk_blks * blksz;

	buf = malloc_cache_aligned(buf_bytes);
	if (!buf) {
		printf("Failed to allocate %lu bytes for wipe buffer\n",
		       buf_bytes);
		return CMD_RET_FAILURE;
	}
	memset(buf, 0, buf_bytes);

	while (remaining) {
		lbaint_t cur = remaining > chunk_blks ? chunk_blks : remaining;

		n = blk_dwrite(desc, blk, cur, buf);
		if (IS_ERR_VALUE(n) || n != cur) {
			if (IS_ERR_VALUE(n))
				printf("SCSI write failed (%ld)\n", (long)n);
			else
				printf("SCSI write failed at block " LBAFU "\n",
				       blk);
			free(buf);
			return CMD_RET_FAILURE;
		}

		blk += cur;
		remaining -= cur;
		WATCHDOG_RESET();
	}

	free(buf);
	return CMD_RET_SUCCESS;
}

static int do_scsi_erase(int dev, lbaint_t blk, lbaint_t cnt)
{
	struct blk_desc *desc;
	unsigned long n;
	int ret;

	ret = scsi_get_desc(dev, &desc);
	if (ret)
		return CMD_RET_FAILURE;
	ret = scsi_validate_range(desc, blk, cnt);
	if (ret)
		return CMD_RET_FAILURE;

	printf("\nSCSI erase: dev # %d, block # " LBAFU ", count " LBAFU
	       " ... ", dev, blk, cnt);

	n = blk_derase(desc, blk, cnt);
	if (IS_ERR_VALUE(n)) {
		long err = (long)n;

		if (err == -ENOSYS) {
			printf("not supported, zero-writing ... ");
			ret = scsi_zero_blocks(desc, blk, cnt);
			printf("%s\n", ret == CMD_RET_SUCCESS ? "OK" : "ERROR");
			return ret;
		}

		printf("failed (%ld)\n", err);
		return CMD_RET_FAILURE;
	}

	printf(LBAFU " blocks erased: %s\n", (lbaint_t)n,
	       (n == cnt) ? "OK" : "ERROR");
	return (n == cnt) ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

static int do_scsi_wipe(int dev)
{
	struct blk_desc *desc;
	lbaint_t cnt;
	int ret;

	ret = scsi_get_desc(dev, &desc);
	if (ret)
		return CMD_RET_FAILURE;

	cnt = desc->lba;
	if (!cnt) {
		printf("SCSI device %d has 0 blocks\n", dev);
		return CMD_RET_FAILURE;
	}
	ret = scsi_validate_range(desc, 0, cnt);
	if (ret)
		return CMD_RET_FAILURE;

	printf("\nSCSI wipe: dev # %d, block # 0, count " LBAFU " ... ",
	       dev, cnt);
	ret = scsi_zero_blocks(desc, 0, cnt);
	printf("%s\n", ret == CMD_RET_SUCCESS ? "OK" : "ERROR");

	return ret;
}

static int do_scsi_readb(int dev, ulong addr, ulong offset, ulong len)
{
	struct blk_desc *desc;
	lbaint_t blk;
	lbaint_t cnt;
	unsigned long n;
	int ret;

	ret = scsi_get_desc(dev, &desc);
	if (ret)
		return CMD_RET_FAILURE;
	ret = scsi_calc_blocks(desc, offset, len, &blk, &cnt);
	if (ret)
		return CMD_RET_FAILURE;

	printf("\nSCSI readb: dev # %d, offset 0x%lx, len 0x%lx (blk "
	       LBAFU ", cnt " LBAFU ") ... ", dev, offset, len, blk, cnt);

	n = blk_dread(desc, blk, cnt, (void *)addr);
	if (IS_ERR_VALUE(n)) {
		printf("failed (%ld)\n", (long)n);
		return CMD_RET_FAILURE;
	}

	printf(LBAFU " blocks read: %s\n", (lbaint_t)n,
	       (n == cnt) ? "OK" : "ERROR");
	return (n == cnt) ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

static int do_scsi_writeb(int dev, ulong addr, ulong offset, ulong len)
{
	struct blk_desc *desc;
	lbaint_t blk;
	lbaint_t cnt;
	unsigned long n;
	int ret;

	ret = scsi_get_desc(dev, &desc);
	if (ret)
		return CMD_RET_FAILURE;
	ret = scsi_calc_blocks(desc, offset, len, &blk, &cnt);
	if (ret)
		return CMD_RET_FAILURE;

	printf("\nSCSI writeb: dev # %d, offset 0x%lx, len 0x%lx (blk "
	       LBAFU ", cnt " LBAFU ") ... ", dev, offset, len, blk, cnt);

	n = blk_dwrite(desc, blk, cnt, (void *)addr);
	if (IS_ERR_VALUE(n)) {
		printf("failed (%ld)\n", (long)n);
		return CMD_RET_FAILURE;
	}

	printf(LBAFU " blocks written: %s\n", (lbaint_t)n,
	       (n == cnt) ? "OK" : "ERROR");
	return (n == cnt) ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

static int do_scsi_eraseb(int dev, ulong offset, ulong len)
{
	struct blk_desc *desc;
	lbaint_t blk;
	lbaint_t cnt;
	int ret;

	ret = scsi_get_desc(dev, &desc);
	if (ret)
		return CMD_RET_FAILURE;
	ret = scsi_calc_blocks(desc, offset, len, &blk, &cnt);
	if (ret)
		return CMD_RET_FAILURE;

	return do_scsi_erase(dev, blk, cnt);
}

/*
 * scsi boot command intepreter. Derived from diskboot
 */
static int do_scsiboot(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	return common_diskboot(cmdtp, "scsi", argc, argv);
}

/*
 * scsi command intepreter
 */
static int do_scsi(struct cmd_tbl *cmdtp, int flag, int argc,
		   char *const argv[])
{
	int ret;

	if (argc >= 2) {
		if (strcmp(argv[1], "erase") == 0) {
			int dev;
			lbaint_t blk;
			lbaint_t cnt;

			if (argc == 4) {
				dev = scsi_curr_dev;
				blk = hextoul(argv[2], NULL);
				cnt = hextoul(argv[3], NULL);
			} else if (argc == 5) {
				dev = dectoul(argv[2], NULL);
				blk = hextoul(argv[3], NULL);
				cnt = hextoul(argv[4], NULL);
			} else {
				return CMD_RET_USAGE;
			}

			return do_scsi_erase(dev, blk, cnt);
		}
		if (strcmp(argv[1], "wipe") == 0) {
			int dev;

			if (argc == 2)
				dev = scsi_curr_dev;
			else if (argc == 3)
				dev = dectoul(argv[2], NULL);
			else
				return CMD_RET_USAGE;

			return do_scsi_wipe(dev);
		}
		if (strcmp(argv[1], "readb") == 0) {
			int dev;
			ulong addr;
			ulong offset;
			ulong len;

			if (argc == 5) {
				dev = scsi_curr_dev;
				addr = hextoul(argv[2], NULL);
				offset = hextoul(argv[3], NULL);
				len = hextoul(argv[4], NULL);
			} else if (argc == 6) {
				dev = dectoul(argv[2], NULL);
				addr = hextoul(argv[3], NULL);
				offset = hextoul(argv[4], NULL);
				len = hextoul(argv[5], NULL);
			} else {
				return CMD_RET_USAGE;
			}

			return do_scsi_readb(dev, addr, offset, len);
		}
		if (strcmp(argv[1], "writeb") == 0) {
			int dev;
			ulong addr;
			ulong offset;
			ulong len;

			if (argc == 5) {
				dev = scsi_curr_dev;
				addr = hextoul(argv[2], NULL);
				offset = hextoul(argv[3], NULL);
				len = hextoul(argv[4], NULL);
			} else if (argc == 6) {
				dev = dectoul(argv[2], NULL);
				addr = hextoul(argv[3], NULL);
				offset = hextoul(argv[4], NULL);
				len = hextoul(argv[5], NULL);
			} else {
				return CMD_RET_USAGE;
			}

			return do_scsi_writeb(dev, addr, offset, len);
		}
		if (strcmp(argv[1], "eraseb") == 0) {
			int dev;
			ulong offset;
			ulong len;

			if (argc == 4) {
				dev = scsi_curr_dev;
				offset = hextoul(argv[2], NULL);
				len = hextoul(argv[3], NULL);
			} else if (argc == 5) {
				dev = dectoul(argv[2], NULL);
				offset = hextoul(argv[3], NULL);
				len = hextoul(argv[4], NULL);
			} else {
				return CMD_RET_USAGE;
			}

			return do_scsi_eraseb(dev, offset, len);
		}
	}

	if (argc == 2) {
		if (strncmp(argv[1], "res", 3) == 0) {
			printf("\nReset SCSI\n");
#ifndef CONFIG_DM_SCSI
			scsi_bus_reset(NULL);
#endif
			ret = scsi_scan(true);
			if (ret)
				return CMD_RET_FAILURE;
			return ret;
		}
		if (strncmp(argv[1], "scan", 4) == 0) {
			ret = scsi_scan(true);
			if (ret)
				return CMD_RET_FAILURE;
			return ret;
		}
	}

	return blk_common_cmd(argc, argv, IF_TYPE_SCSI, &scsi_curr_dev);
}

U_BOOT_CMD(
	scsi, 6, 1, do_scsi,
	"SCSI sub-system",
	"reset - reset SCSI controller\n"
	"scsi info  - show available SCSI devices\n"
	"scsi scan  - (re-)scan SCSI bus\n"
	"scsi device [dev] - show or set current device\n"
	"scsi part [dev] - print partition table of one or all SCSI devices\n"
	"scsi erase blk# cnt - erase `cnt' blocks starting at block `blk#'\n"
	"scsi erase dev blk# cnt - erase `cnt' blocks on device `dev'\n"
	"scsi wipe [dev] - zero-fill the whole device\n"
	"scsi readb addr off len - read bytes at offset `off' (aligned) into `addr'\n"
	"scsi readb dev addr off len - read bytes on device `dev'\n"
	"scsi writeb addr off len - write bytes at offset `off' (aligned) from `addr'\n"
	"scsi writeb dev addr off len - write bytes on device `dev'\n"
	"scsi eraseb off len - erase bytes at offset `off' (aligned)\n"
	"scsi eraseb dev off len - erase bytes on device `dev'\n"
	"scsi read addr blk# cnt - read `cnt' blocks starting at block `blk#'\n"
	"     to memory address `addr'\n"
	"scsi write addr blk# cnt - write `cnt' blocks starting at block\n"
	"     `blk#' from memory address `addr'"
);

U_BOOT_CMD(
	scsiboot, 3, 1, do_scsiboot,
	"boot from SCSI device",
	"loadAddr dev:part"
);
