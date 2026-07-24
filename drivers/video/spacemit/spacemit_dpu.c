// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Spacemit Co., Ltd.
 *
 */

#include <common.h>
#include <display.h>
#include <cpu_func.h>
#include <dm.h>
#include <dm/uclass.h>
#include <dm/device.h>
#include <dm/uclass-internal.h>
#include <dm/device-internal.h>
#include <dm/lists.h>

#include <regmap.h>
#include <syscon.h>
#include <video.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/delay.h>

#include <power-domain-uclass.h>
#include <power-domain.h>
#include <clk.h>
#include <video_bridge.h>
#include <power/pmic.h>
#include <panel.h>
#include <edid.h>

#include "spacemit_dpu.h"

extern bool is_video_connected;

DECLARE_GLOBAL_DATA_PTR;

struct fb_info fbi = {0};

static void dpu_init(struct spacemit_mode_modeinfo *spacemit_mode, ulong fbbase, void __iomem *dpu_base_addr, int dpu_type)
{
	unsigned int vsync, hsync, vbp, vfp, hbp, hfp, vsp, hsp;

	vsync = spacemit_mode->vsync_len & 0x3ff;
	hsync = spacemit_mode->hsync_len & 0x3ff;
	vbp = spacemit_mode->upper_margin & 0xfff;
	vfp = spacemit_mode->lower_margin & 0xfff;
	hbp = spacemit_mode->left_margin & 0xfff;
	hfp = spacemit_mode->right_margin & 0xfff;
	vsp = spacemit_mode->hsync_invert ? 0 : 1;
	hsp = spacemit_mode->hsync_invert ? 0 : 1;

	pr_debug("dpu_write hbp %d, hfp %d hsync %d vsp %d \n", hbp, hfp, hsync, vsp);
	pr_debug("dpu_write vbp %d, vfp %d vsync %d hsp %d \n", vbp, vfp, vsync, hsp);

	dpu_writel(dpu_base_addr, 0x30000, 0x1);

	dpu_writel(dpu_base_addr, 0x5120C, hfp << 16);
	dpu_writel(dpu_base_addr, 0x51210, (hbp << 16) | hsync);
	dpu_writel(dpu_base_addr, 0x51214, (vsync << 16) | vfp);
	dpu_writel(dpu_base_addr, 0x51218, (spacemit_mode->xres << 16) | vbp);
	dpu_writel(dpu_base_addr, 0x5121C, spacemit_mode->yres);
	dpu_writel(dpu_base_addr, 0x51200, 0x184);
	dpu_writel(dpu_base_addr, 0x51208, spacemit_mode->pix_fmt_out << 12);
	dpu_writel(dpu_base_addr, 0x5123c, 0x1);

	dpu_writel(dpu_base_addr, 0x1284, 0x840);

	dpu_writel(dpu_base_addr, 0x1000, 0xf00217c); 
	dpu_writel(dpu_base_addr, 0x1024, (unsigned int)(fbbase & 0xffffffff));
	dpu_writel(dpu_base_addr, 0x1028, (unsigned int)(fbbase >> 32));
	dpu_writel(dpu_base_addr, 0x103c, spacemit_mode->xres * 4);
	dpu_writel(dpu_base_addr, 0x1040, spacemit_mode->xres | (spacemit_mode->yres << 16));
	dpu_writel(dpu_base_addr, 0x1044, 0x0);
	dpu_writel(dpu_base_addr, 0x1048, (spacemit_mode->xres - 1) | ((spacemit_mode->yres-1) << 16));
	dpu_writel(dpu_base_addr, 0x1074, 0x8);
	/* supports up to 3840x2160 */
	dpu_writel(dpu_base_addr, 0x107c, 0x100001e0);

	dpu_writel(dpu_base_addr, 0x30004, (spacemit_mode->xres | (spacemit_mode->yres << 16)));
	dpu_writel(dpu_base_addr, 0x30020, ((spacemit_mode->xres-1) << 16));
	dpu_writel(dpu_base_addr, 0x30024, ((spacemit_mode->yres-1) << 16));
	dpu_writel(dpu_base_addr, 0x30038, 0xff03);
	dpu_writel(dpu_base_addr, 0x30300, 0x0);
	dpu_writel(dpu_base_addr, 0x30334, (spacemit_mode->xres | (spacemit_mode->yres << 16)));

	dpu_writel(dpu_base_addr, 0x340, 0x1040001);

	dpu_writel(dpu_base_addr, 0x34c, 0x821);

	dpu_writel(dpu_base_addr, 0x348, 0x1);
	dpu_writel(dpu_base_addr, 0x350, 0x1);
}

static int spacemit_panel_init(void)
{
	struct video_tx_device *tx = NULL;
	int modes_num = 0;

	tx = find_video_tx();
	if (!tx) {
		pr_info("probe: failed to find video tx\n");
		return -1;
	}
	fbi.tx = tx;

	modes_num = video_tx_get_modes(fbi.tx, &fbi.mode);
	if (!modes_num) {
		pr_info("can't get videomode num\n");
		return -1;
	}

	video_tx_dpms(fbi.tx, DPMS_ON);

	return 0;
}

static int spacemit_display_init(struct udevice *dev, ulong fbbase, ofnode ep_node)
{
	// struct video_uc_plat *uc_plat = dev_get_uclass_plat(dev);
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	// struct spacemit_dpu_priv *priv = dev_get_priv(dev);
	struct display_timing timing;
	int dpu_type, remote_dpu_id, dpu_id;
	struct udevice *disp;
	int ret;
	u32 remote_phandle;
	ofnode remote;
	const char *compat;
	struct display_plat *disp_uc_plat;
	struct udevice *panel = NULL;
	void __iomem *dpu_base_addr;

	struct spacemit_mode_modeinfo *spacemit_mode = NULL;

	pr_debug("%s(%s, 0x%lx, %s)\n", __func__,
			  dev_read_name(dev), fbbase, ofnode_get_name(ep_node));

	ret = ofnode_read_u32(ep_node, "remote-endpoint", &remote_phandle);
	if (ret)
		return ret;

	remote = ofnode_get_by_phandle(remote_phandle);
	if (!ofnode_valid(remote))
		return -EINVAL;
	remote_dpu_id = ofnode_read_u32_default(remote, "reg", -1);
	uc_priv->bpix = VIDEO_BPP32;
	pr_debug("remote_dpu_id  %d\n", remote_dpu_id);

	while (ofnode_valid(remote)) {
		remote = ofnode_get_parent(remote);
		if (!ofnode_valid(remote)) {
			pr_debug("%s(%s): no UCLASS_DISPLAY for remote-endpoint\n",
			      __func__, dev_read_name(dev));
			return -EINVAL;
		}
		pr_debug("%s(dev: %s, 0x%lx,remote node: %s)\n", __func__,
		  dev_read_name(dev), fbbase, ofnode_get_name(remote));

		uclass_find_device_by_ofnode(UCLASS_DISPLAY, remote, &disp);
		if (disp)
			break;

	};

	ret = ofnode_read_u32(remote, "dpu-id", &dpu_id);
	if (ret) {
		pr_warn("%s(%s): Failed to read dpu-id property, using default\n",
			__func__, dev_read_name(dev));
		dpu_id = 0;
	}

	pr_debug("dpu_id: %d\n", dpu_id);
	compat = ofnode_get_property(remote, "compatible", NULL);
	if (!compat) {
		pr_info("%s(%s): Failed to find compatible property\n",
		      __func__, dev_read_name(dev));
		return -EINVAL;
	}

	if (strstr(compat, "mipi")) {
		dpu_type = DPU_MODE_MIPI;
	} else if (strstr(compat, "hdmi")) {
		dpu_type = DPU_MODE_HDMI;
	} else if (strstr(compat, "dp")) {
		dpu_type = DPU_MODE_DP;
	} else if (strstr(compat, "edp")) {
		dpu_type = DPU_MODE_DP;
	} else {
		pr_info("%s(%s): Failed to find dpu mode for %s\n",
			__func__, dev_read_name(dev), compat);
		return -EINVAL;
	}

	pr_debug("dpu_type %d,compat = %s\n", dpu_type, compat);
	uc_priv->xsize = 1920;
	uc_priv->ysize = 1080;

	if (dpu_id == 0) {
		dpu_base_addr = (void __iomem *)0xc0340000;
	} else if (dpu_id == 1) {
		dpu_base_addr = (void __iomem *)0xc0440000;
	} else {
		pr_err("%s(%s): Unsupported dpu_id %d\n",
			__func__, dev_read_name(dev), dpu_id);
		return -EINVAL;
	}

	if (dpu_type == DPU_MODE_DP) {
		disp_uc_plat = dev_get_uclass_plat(disp);

		pr_info("Found device '%s', disp_uc_priv=%p\n", disp->name, disp_uc_plat);

		disp_uc_plat->source_id = remote_dpu_id;
		disp_uc_plat->src_dev = dev;

		ret = device_probe(disp);
		if (ret) {
			pr_info("%s: device '%s' display won't probe (ret=%d)\n",
			  __func__, dev->name, ret);
			return ret;
		}

		ret = display_read_timing(disp, &timing);
		if (ret) {
			pr_info("%s: Failed to read timings\n", __func__);
			return ret;
		}

		if (timing.hactive.typ > 3840) {
			pr_info("%s: no support the resolution of %dx%d\n", __func__, timing.hactive.typ, timing.vactive.typ);
			return -EINVAL;
		}

		uc_priv->xsize = timing.hactive.typ;
		uc_priv->ysize = timing.vactive.typ;

		pr_info("fb=%lx, size=%dx%d\n", fbbase, uc_priv->xsize, uc_priv->ysize);

		memset((void *)fbbase, 0, uc_priv->xsize * uc_priv->ysize * VNBYTES(uc_priv->bpix));
		flush_cache(fbbase, uc_priv->xsize * uc_priv->ysize * VNBYTES(uc_priv->bpix));

		ret = display_enable(disp, 1 << VIDEO_BPP32, &timing);
		if (ret) {
			pr_info("%s: Failed to enable display\n", __func__);
			return ret;
		}

		spacemit_mode = &fbi.mode;
		spacemit_mode->pix_fmt_out = OUTFMT_RGB888;
		spacemit_mode->pixclock_freq = timing.pixelclock.typ;
		spacemit_mode->left_margin = timing.hback_porch.typ;
		spacemit_mode->right_margin = timing.hfront_porch.typ;
		spacemit_mode->hsync_len = timing.hsync_len.typ;
		spacemit_mode->upper_margin = timing.vback_porch.typ;
		spacemit_mode->lower_margin = timing.vfront_porch.typ;
		spacemit_mode->vsync_len = timing.vsync_len.typ;

		spacemit_mode->xres = timing.hactive.typ;
		spacemit_mode->yres = timing.vactive.typ;
		spacemit_mode->hsync_invert = 1;
		spacemit_mode->hsync_invert = 1;

		dpu_init(spacemit_mode, fbbase, dpu_base_addr, dpu_type);

		return 0;
	} else if (dpu_type == DPU_MODE_MIPI) {

		struct video_tx_device *video_tx;

		disp_uc_plat = dev_get_uclass_plat(disp);

		pr_info("Found device '%s', disp_uc_priv=%p\n", disp->name, disp_uc_plat);

		disp_uc_plat->source_id = remote_dpu_id;
		disp_uc_plat->src_dev = dev;

		ret = device_probe(disp);
		if (ret) {
			pr_info("%s: device '%s' display won't probe (ret=%d)\n",
			  __func__, dev->name, ret);
			return ret;
		}

		ret = display_read_timing(disp, &timing);
		if (ret) {
			pr_info("%s: Failed to read timings\n", __func__);
			return ret;
		}

		ret = display_enable(disp, 1 << VIDEO_BPP32, &timing);
		if (ret) {
			pr_info("%s: Failed to display enable\n", __func__);
			return ret;
		}

		ret = spacemit_panel_init();
		if (ret) {
			pr_info("%s: Failed to init panel\n", __func__);
			is_video_connected = false;
			return ret;
		}
		is_video_connected = true;

		spacemit_mode = &fbi.mode;
		uc_priv->xsize = spacemit_mode->xres;
		uc_priv->ysize = spacemit_mode->yres;

		pr_info("fb=%lx, size=%dx%d\n", fbbase, uc_priv->xsize, uc_priv->ysize);

		memset((void *)fbbase, 0, uc_priv->xsize * uc_priv->ysize * VNBYTES(uc_priv->bpix));
		flush_cache(fbbase, uc_priv->xsize * uc_priv->ysize * VNBYTES(uc_priv->bpix));

		pr_debug("%s: panel type %d\n", __func__, fbi.tx->panel_type);

		if (fbi.tx->panel_type == LCD_MIPI) {
			dpu_init(spacemit_mode, fbbase, dpu_base_addr, dpu_type);
			video_tx_esd_check(fbi.tx);
			video_tx = fbi.tx;
			video_tx->driver->bl_enable(video_tx, true);

		} else if (fbi.tx->panel_type == LCD_EDP){
			dpu_init(spacemit_mode, fbbase, dpu_base_addr, dpu_type);
			video_tx_reset(fbi.tx);
			video_tx = fbi.tx;

			ret = uclass_first_device_err(UCLASS_PANEL, &panel);
			if (ret) {
				if (ret != -ENODEV)
					pr_info("panel device error %d\n", ret);

				return ret;
			}

			ret = panel_get_display_timing(panel, &timing);
			if (ret) {
				pr_info("%s: timing error: %d\n", __func__, ret);
			}

			ret = panel_enable_backlight(panel);
			if (ret) {
				pr_info("%s: backlight error: %d\n", __func__, ret);
				return ret;
			}

			video_tx->driver->bl_enable(video_tx, true);
		} else if (fbi.tx->panel_type == LCD_DPI ){
			dpu_init(spacemit_mode, fbbase, dpu_base_addr, dpu_type);
			video_tx_reset(fbi.tx);
			video_tx = fbi.tx;

			ret = uclass_first_device_err(UCLASS_PANEL, &panel);
			if (ret) {
				if (ret != -ENODEV)
					pr_info("panel device error %d\n", ret);

				return ret;
			}

			ret = panel_get_display_timing(panel, &timing);
			if (ret) {
				pr_info("%s: timing error: %d\n", __func__, ret);
			}

			ret = panel_enable_backlight(panel);
			if (ret) {
				pr_info("%s: backlight error: %d\n", __func__, ret);
				return ret;
			}

		} else {
			pr_info("%s: Failed to find panel\n", __func__);
		}

		return 0;
	}

	return 0;
}

static int spacemit_dpu_probe(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);
	struct spacemit_dpu_priv *priv = dev_get_priv(dev);
	// struct udevice *udev;
	ofnode port, node;
	int ret;

	priv->regs_crtc0 = dev_remap_addr_name(dev, "crtc0");
	if (!priv->regs_crtc0)
		return -EINVAL;

	priv->regs_crtc1 = dev_remap_addr_name(dev, "crtc1");
	if (!priv->regs_crtc1)
		return -EINVAL;

	port = dev_read_subnode(dev, "port");
	if (!ofnode_valid(port)) {
		pr_info("%s(%s): 'port' subnode not found\n",
		      __func__, dev_read_name(dev));
		return -EINVAL;
	}

	/*
	for (uclass_find_first_device(UCLASS_VIDEO, &udev);
		udev;
		uclass_find_next_device(&udev)) {
		pr_info("%s:video device %s \n", __func__, udev->name);
	}

	for (uclass_find_first_device(UCLASS_DISPLAY, &udev);
		udev;
		uclass_find_next_device(&udev)) {

		pr_info("%s:display device %s\n", __func__, udev->name);
	}

	for (uclass_find_first_device(UCLASS_VIDEO_BRIDGE, &udev);
		udev;
		uclass_find_next_device(&udev)) {

		pr_info("%s:bridge device %s\n", __func__, udev->name);
	}

	for (uclass_find_first_device(UCLASS_PANEL, &udev);
		udev;
		uclass_find_next_device(&udev)) {

		pr_info("%s:panel device %s\n", __func__, udev->name);
	}
	*/

	for (node = ofnode_first_subnode(port);
		ofnode_valid(node);
		node = dev_read_next_subnode(node)) {

		ret = spacemit_display_init(dev, plat->base, node);
		if (ret)
			pr_debug("Device failed: ret=%d\n", ret);
		if (!ret)
			break;
	}

	video_set_flush_dcache(dev, 1);

	return 0;
}

static int spacemit_dpu_remove(struct udevice *dev)
{
	return 0;
}

struct spacemit_dpu_driverdata dpu_driverdata = {
	.features = DPU_FEATURE_OUTPUT_10BIT,
};

static const struct udevice_id spacemit_dc_ids[] = {
	{ .compatible = "spacemit,dpu",
	  .data = (ulong)&dpu_driverdata },
	{ }
};

static const struct video_ops spacemit_dpu_ops = {
};

int spacemit_dpu_bind(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);

	pr_debug("%s,%d,plat->size = %d plat->base 0x%lx\n",__func__,__LINE__,plat->size, plat->base);

	plat->size = 4 * (CONFIG_VIDEO_SPACEMIT_MAX_XRES *
			  CONFIG_VIDEO_SPACEMIT_MAX_YRES);

	return 0;
}

U_BOOT_DRIVER(spacemit_dpu) = {
	.name	= "spacemit_dpu",
	.id	= UCLASS_VIDEO,
	.of_match = spacemit_dc_ids,
	.ops	= &spacemit_dpu_ops,
	.bind	= spacemit_dpu_bind,
	.probe	= spacemit_dpu_probe,
	.remove = spacemit_dpu_remove,
	.priv_auto	= sizeof(struct spacemit_dpu_priv),
};
