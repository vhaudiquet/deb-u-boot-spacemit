// SPDX-License-Identifier: GPL-2.0+

#include <common.h>
#include <fdtdec.h>
#include <image.h>
#include <malloc.h>
#include <dm/ofnode.h>
#include <linux/ioport.h>
#include <linux/libfdt.h>
#include <tee/optee.h>

#define SBI_MPXY_MBOX_CELLS		3
#define SBI_MPXY_MSGPROTO_TEE_ID	0x00000002

static int generate_optee_node_from_mpxy(ofnode src_node, void *fdt)
{
	int fw_off, node_off, mbox_target_off, ret, cleanup_ret;
	bool fw_created = false;
	bool optee_created = false;
	bool phandle_created = false;
	u32 target_phandle;
	u32 *ids;
	u32 *mboxes_prop = NULL;
	size_t mboxes_size;
	int ids_size, num_ids, i;

	mbox_target_off = fdt_node_offset_by_compatible(fdt, -1, "riscv,sbi-mpxy-mbox");
	if (mbox_target_off < 0) {
		printf("WARNING: OP-TEE MPXY mailbox not found in target FDT\n");
		return 0;
	}

	ids_size = ofnode_read_size(src_node, "riscv,sbi-mpxy-channel-id");
	if (ids_size <= 0 || ids_size % sizeof(u32))
		return -FDT_ERR_BADVALUE;

	num_ids = ids_size / sizeof(u32);
	if ((size_t)num_ids > INT_MAX / sizeof(u32) /
	    SBI_MPXY_MBOX_CELLS)
		return -FDT_ERR_BADVALUE;

	mboxes_size = (size_t)num_ids * SBI_MPXY_MBOX_CELLS * sizeof(u32);
	if (mboxes_size > fdt_totalsize(fdt))
		return -FDT_ERR_BADVALUE;

	ids = malloc(ids_size);
	if (!ids)
		return -FDT_ERR_NOSPACE;

	ret = ofnode_read_u32_array(src_node, "riscv,sbi-mpxy-channel-id", ids, num_ids);
	if (ret) {
		free(ids);
		return -FDT_ERR_BADVALUE;
	}

	mboxes_prop = malloc(mboxes_size);
	if (!mboxes_prop) {
		free(ids);
		return -FDT_ERR_NOSPACE;
	}

	target_phandle = fdt_get_phandle(fdt, mbox_target_off);
	if (!target_phandle) {
		ret = fdt_generate_phandle(fdt, &target_phandle);
		if (ret < 0)
			goto err_cleanup;

		ret = fdt_setprop_u32(fdt, mbox_target_off, "phandle", target_phandle);
		if (ret < 0)
			goto err_cleanup;
		phandle_created = true;
	}

	for (i = 0; i < num_ids; i++) {
		int base = i * SBI_MPXY_MBOX_CELLS;

		mboxes_prop[base] = cpu_to_fdt32(target_phandle);
		mboxes_prop[base + 1] = cpu_to_fdt32(ids[i]);
		mboxes_prop[base + 2] = cpu_to_fdt32(SBI_MPXY_MSGPROTO_TEE_ID);
	}

	fw_off = fdt_path_offset(fdt, "/firmware");
	if (fw_off < 0) {
		fw_off = fdt_add_subnode(fdt, 0, "firmware");
		if (fw_off < 0) {
			ret = fw_off;
			goto err_cleanup;
		}
		fw_created = true;
	}

	node_off = fdt_add_subnode(fdt, fw_off, "optee");
	if (node_off < 0) {
		ret = node_off;
		goto err_cleanup;
	}
	optee_created = true;

	ret = fdt_setprop_string(fdt, node_off, "compatible", "linaro,optee-tz");
	if (ret < 0)
		goto err_cleanup;

	ret = fdt_setprop(fdt, node_off, "mboxes",
			  mboxes_prop, mboxes_size);
	if (ret < 0)
		goto err_cleanup;

	free(ids);
	free(mboxes_prop);
	return 0;

err_cleanup:
	free(ids);
	free(mboxes_prop);

	if (optee_created) {
		node_off = fdt_path_offset(fdt, "/firmware/optee");
		if (node_off >= 0) {
			cleanup_ret = fdt_del_node(fdt, node_off);
			if (cleanup_ret < 0)
				printf("Failed to remove incomplete OP-TEE node: %s\n",
				       fdt_strerror(cleanup_ret));
		}
	}

	if (fw_created) {
		fw_off = fdt_path_offset(fdt, "/firmware");
		if (fw_off >= 0) {
			cleanup_ret = fdt_del_node(fdt, fw_off);
			if (cleanup_ret < 0)
				printf("Failed to remove incomplete firmware node: %s\n",
				       fdt_strerror(cleanup_ret));
		}
	}

	if (phandle_created) {
		mbox_target_off = fdt_node_offset_by_compatible(fdt, -1, "riscv,sbi-mpxy-mbox");
		if (mbox_target_off >= 0) {
			cleanup_ret = fdt_delprop(fdt, mbox_target_off, "phandle");
			if (cleanup_ret < 0)
				printf("Failed to remove generated mbox phandle: %s\n",
				       fdt_strerror(cleanup_ret));
		}
	}

	return ret;
}

int riscv_optee_copy_fdt_nodes(void *new_blob)
{
	ofnode node;
	int ret;

	ret = fdt_check_header(new_blob);
	if (ret)
		return ret;

	node = ofnode_by_compatible(ofnode_null(), "riscv,sbi-mpxy-opteed");
	if (!ofnode_valid(node) || !ofnode_is_available(node)) {
		debug("No enabled sbi-mpxy-opteed configuration found in U-Boot.\n");
		return 0;
	}

	/*
	 * Do not proceed if the target dt already has an OP-TEE node.
	 * In this case assume that the system knows better somehow,
	 * so do not interfere.
	 */
	if (fdt_node_offset_by_compatible(new_blob, -1,
					  "linaro,optee-tz") >= 0) {
		debug("OP-TEE Device Tree node already exists in target\n");
		return 0;
	}

	ret = generate_optee_node_from_mpxy(node, new_blob);
	if (ret < 0) {
		printf("Failed to add OP-TEE firmware node\n");
		return ret;
	}

	return 0;
}
