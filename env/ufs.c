// SPDX-License-Identifier: GPL-2.0+
/*
 * UFS Environment driver
 * Based on env/mmc.c
 */

#include <common.h>
#include <asm/global_data.h>
#include <command.h>
#include <env.h>
#include <env_internal.h>
#include <linux/stddef.h>
#include <malloc.h>
#include <memalign.h>
#include <part.h>
#include <search.h>
#include <errno.h>
#include <scsi.h>
#include <blk.h>

DECLARE_GLOBAL_DATA_PTR;

/* UFS device number for environment storage */
#ifndef CONFIG_SYS_UFS_ENV_DEV
#define CONFIG_SYS_UFS_ENV_DEV 0
#endif

__weak int ufs_get_env_dev(void)
{
	return CONFIG_SYS_UFS_ENV_DEV;
}

__weak int ufs_get_env_addr(struct blk_desc *desc, u32 *env_addr)
{
	*env_addr = CONFIG_ENV_OFFSET;
	return 0;
}

__weak int board_scsi_scan_once(bool verbose)
{
	return scsi_scan(verbose);
}

static struct blk_desc *init_ufs_for_env(void)
{
	struct blk_desc *desc;
	int dev = ufs_get_env_dev();

#ifdef CONFIG_SPL_BUILD
	static bool scsi_scanned;

	/*
	 * SPL may call env_load() multiple times on UFS path. Reuse the
	 * existing SCSI enumeration once it is available.
	 */
	if (!scsi_scanned) {
		board_scsi_scan_once(false);
		scsi_scanned = true;
	}
#else
	board_scsi_scan_once(false);
#endif

	desc = blk_get_dev("scsi", dev);
#ifdef CONFIG_SPL_BUILD
	if (!desc) {
		/* Retry once: first scan can happen before UFS is fully ready. */
		scsi_scan(false);
		desc = blk_get_dev("scsi", dev);
	}
#endif
	if (!desc) {
		pr_err("No UFS device found at dev %d\n", dev);
		return NULL;
	}

	return desc;
}

#if defined(CONFIG_CMD_SAVEENV) && !defined(CONFIG_SPL_BUILD)
static inline int write_env(struct blk_desc *desc, unsigned long size,
			    unsigned long offset, const void *buffer)
{
	uint blk_start, blk_cnt, n;

	blk_start = ALIGN(offset, desc->blksz) / desc->blksz;
	blk_cnt = ALIGN(size, desc->blksz) / desc->blksz;

	n = blk_dwrite(desc, blk_start, blk_cnt, (u_char *)buffer);

	return (n == blk_cnt) ? 0 : -1;
}

static int env_ufs_save(void)
{
	ALLOC_CACHE_ALIGN_BUFFER(env_t, env_new, 1);
	struct blk_desc *desc;
	u32 offset;
	int ret;
	int dev = ufs_get_env_dev();

	desc = init_ufs_for_env();
	if (!desc) {
		pr_err("UFS init failed\n");
		return 1;
	}

	ret = env_export(env_new);
	if (ret)
		return ret;

	if (ufs_get_env_addr(desc, &offset)) {
		ret = 1;
		return ret;
	}

	pr_info("Writing to UFS(%d)... ", dev);
	if (write_env(desc, CONFIG_ENV_SIZE, offset, (u_char *)env_new)) {
		puts("failed\n");
		return 1;
	}

	puts("done\n");
	return 0;
}

static inline int erase_env(struct blk_desc *desc, unsigned long size,
			    unsigned long offset)
{
	uint blk_start, blk_cnt, n;

	blk_start = ALIGN(offset, desc->blksz) / desc->blksz;
	blk_cnt = ALIGN(size, desc->blksz) / desc->blksz;

	n = blk_derase(desc, blk_start, blk_cnt);
	pr_info("%d blocks erased at 0x%x: %s\n", n, blk_start,
	       (n == blk_cnt) ? "OK" : "ERROR");

	return (n == blk_cnt) ? 0 : 1;
}

static int env_ufs_erase(void)
{
	struct blk_desc *desc;
	u32 offset;
	int ret;

	desc = init_ufs_for_env();
	if (!desc) {
		pr_err("UFS init failed\n");
		return 1;
	}

	if (ufs_get_env_addr(desc, &offset)) {
		return 1;
	}

	pr_info("\n");
	ret = erase_env(desc, CONFIG_ENV_SIZE, offset);

	return ret;
}
#endif /* CONFIG_CMD_SAVEENV && !CONFIG_SPL_BUILD */

static inline int read_env(struct blk_desc *desc, unsigned long size,
			   unsigned long offset, const void *buffer)
{
	uint blk_start, blk_cnt, n;

	blk_start = ALIGN(offset, desc->blksz) / desc->blksz;
	blk_cnt = ALIGN(size, desc->blksz) / desc->blksz;

	n = blk_dread(desc, blk_start, blk_cnt, (uchar *)buffer);

	return (n == blk_cnt) ? 0 : -1;
}

static int env_ufs_load(void)
{
#if !defined(ENV_IS_EMBEDDED)
	char *buf;
	struct blk_desc *desc;
	u32 offset;
	int ret;
	const char *errmsg = NULL;
	env_t *ep = NULL;

	buf = malloc_cache_aligned(CONFIG_ENV_SIZE);
	if (!buf) {
		errmsg = "env malloc failed";
		ret = -ENOMEM;
		goto err;
	}

	desc = init_ufs_for_env();
	if (!desc) {
		errmsg = "UFS init failed";
		ret = -EIO;
		goto err;
	}

	if (ufs_get_env_addr(desc, &offset)) {
		ret = -EIO;
		goto err;
	}

	if (read_env(desc, CONFIG_ENV_SIZE, offset, buf)) {
		errmsg = "UFS read failed";
		ret = -EIO;
		goto err;
	}

	ret = env_import(buf, 1, H_EXTERNAL);
	if (!ret) {
		ep = (env_t *)buf;
		gd->env_addr = (ulong)&ep->data;
	}

	return ret;

err:
	if (ret)
		env_set_default(errmsg, 0);
#endif
	return ret;
}

U_BOOT_ENV_LOCATION(ufs) = {
	.location	= ENVL_UFS,
	ENV_NAME("UFS")
	.load		= env_ufs_load,
#ifndef CONFIG_SPL_BUILD
	.save		= env_save_ptr(env_ufs_save),
	.erase		= ENV_ERASE_PTR(env_ufs_erase)
#endif
};
