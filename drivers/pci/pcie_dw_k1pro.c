// SPDX-License-Identifier: GPL-2.0+
/*
 * Spacemit DesignWare based PCIe host controller driver
 *
 * Copyright (c) 2023, spacemit Corporation.
 *
 */
#include <asm/io.h>
#include <asm-generic/gpio.h>
#include <clk.h>
#include <common.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <generic-phy.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include <pci.h>
#include <pci_ep.h>
#include <pci_ids.h>
#include <regmap.h>
#include <reset.h>
#include <syscon.h>

#include "pcie_dw_common.h"

/* Doorbell Interface */
#define DBI_OFFSET			0x0
#define DBI_SIZE			0x1000

#define PL_OFFSET			0x700

#define PHY_DEBUG_R0			(PL_OFFSET + 0x28)

#define PHY_DEBUG_R1			(PL_OFFSET + 0x2c)
#define PHY_DEBUG_R1_LINK_UP		(0x1 << 4)
#define PHY_DEBUG_R1_LINK_IN_TRAINING	(0x1 << 29)

#define PCIE_MISC_CONTROL_1		0x8bc
#define DBI_RO_WR_EN			BIT(0)

#define PCIE_CAP_BASE			0x70
#define PCI_CONFIG(r)			(DBI_OFFSET + (r))
#define PCIE_CAPABILITIES(r)		PCI_CONFIG(PCIE_CAP_BASE + (r))

/* Link capability */
#define PF0_PCIE_CAP_LINK_CAP		PCIE_CAPABILITIES(0xc)
#define PCIE_LINK_CAP_MAX_SPEED_MASK	0xf
#define PCIE_LINK_CAP_MAX_SPEED_GEN1	BIT(0)
#define PCIE_LINK_CAP_MAX_SPEED_GEN2	BIT(1)
#define PCIE_LINK_CAP_MAX_SPEED_GEN3	BIT(2)
#define PCIE_LINK_CAP_MAX_SPEED_GEN4	BIT(3)

/* app control registers */
#define  PCIE_APP_TYPE		0x000
#define  DEVICE_TYPE_RC		0x4
#define  DEVICE_TYPE_EP		0

#define  PCIE_APP_CTL		0x004
#define  CTL_LTSSM_ENABLE	BIT(0)

#define  PCIE_APP_STATE		0x200
#define  LTSSM_STATE_MASK	GENMASK(5, 0)

struct pcie_spacemit {
	/* Must be first member of the struct */
	struct pcie_dw dw;

	/* private control regs */
	void __iomem *priv_base;

	/* reset, clock resources */
	struct clk clock;
	struct reset_ctl_bulk resets;
};

enum pcie_spacemit_devtype {
	SM_PCIE_UNKNOWN_TYPE = 0,
	SM_PCIE_ENDPOINT_TYPE = 1,
	SM_PCIE_HOST_TYPE = 3
};

static enum pcie_spacemit_devtype pcie_spacemit_get_devtype(struct pcie_spacemit *sm)
{
	u32 val;

	val = readl(sm->priv_base + PCIE_APP_TYPE);
	switch (val) {
	case DEVICE_TYPE_RC:
		return SM_PCIE_HOST_TYPE;
	case DEVICE_TYPE_EP:
		return SM_PCIE_ENDPOINT_TYPE;
	default:
		return SM_PCIE_UNKNOWN_TYPE;
	}
}

static int pcie_spacemit_check_link(struct pcie_spacemit *sm)
{
	u32 val;

	val = readl(sm->priv_base + PCIE_APP_STATE);
	return !!(val & LTSSM_STATE_MASK);
}

static void pcie_spacemit_force_gen1(struct pcie_spacemit *sm)
{
	u32 val, linkcap;

	/*
	 * Force Gen1 operation when starting the link. In case the link is
	 * started in Gen2 mode, there is a possibility the devices on the
	 * bus will not be detected at all. This happens with PCIe switches.
	 */

	/* ctrl_ro_wr_enable */
	val = readl(sm->dw.dbi_base + PCIE_MISC_CONTROL_1);
	val |= DBI_RO_WR_EN;
	writel(val, sm->dw.dbi_base + PCIE_MISC_CONTROL_1);

	/* configure link cap */
	linkcap = readl(sm->dw.dbi_base + PF0_PCIE_CAP_LINK_CAP);
	linkcap |= PCIE_LINK_CAP_MAX_SPEED_MASK;
	writel(linkcap, sm->dw.dbi_base + PF0_PCIE_CAP_LINK_CAP);

	/* ctrl_ro_wr_disable */
	val &= ~DBI_RO_WR_EN;
	writel(val, sm->dw.dbi_base + PCIE_MISC_CONTROL_1);
}

static int pcie_spacemit_wait_for_link(struct pcie_spacemit *sm)
{
	u32 val;
	int timeout;

	/* Wait for the link to train */
	mdelay(20);
	timeout = 80;

	do {
		mdelay(1);
	} while (--timeout && !pcie_spacemit_check_link(sm));

	val = readl(sm->priv_base + PCIE_APP_STATE);
	if (!(val & LTSSM_STATE_MASK)) {
		printf("Failed to negotiate PCIe link!\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int pcie_spacemit_start_link(struct pcie_spacemit *sm)
{
	u32 val;

	if (pcie_spacemit_check_link(sm))
		return -EALREADY;

	pcie_spacemit_force_gen1(sm);

	val = readl(sm->priv_base + PCIE_APP_CTL);
	val |= CTL_LTSSM_ENABLE;
	writel(val, sm->priv_base + PCIE_APP_CTL);
	return 0;
}

static int pcie_spacemit_init_port(struct udevice *dev,
				 enum pcie_spacemit_devtype mode)
{
	struct pcie_spacemit *sm = dev_get_priv(dev);
	int ret;

	/* pcie reset */
	ret = reset_deassert_bulk(&sm->resets);
	if (ret < 0) {
		dev_err(dev, "failed to deassert resets");
		return -EINVAL;
	}

	/* enable pcie clk */
	clk_enable(&sm->clock);

	/* Set desired mode while core is not operational */
	if (mode == SM_PCIE_HOST_TYPE)
		writel(DEVICE_TYPE_RC,
		       sm->priv_base + PCIE_APP_TYPE);
	else
		writel(DEVICE_TYPE_EP,
		       sm->priv_base + PCIE_APP_TYPE);

	/* Confirm desired mode from operational core */
	if (pcie_spacemit_get_devtype(sm) != mode)
		return -EINVAL;

	pcie_dw_setup_host(&sm->dw);

	if (pcie_spacemit_start_link(sm) == -EALREADY)
		dev_info(dev, "PCIe link is already up\n");
	else if (pcie_spacemit_wait_for_link(sm) == -ETIMEDOUT)
		return -ETIMEDOUT;

	return 0;
}

static int pcie_spacemit_probe(struct udevice *dev)
{
	struct pcie_spacemit *sm = dev_get_priv(dev);
	struct udevice *parent = pci_get_controller(dev);
	struct pci_controller *hose = dev_get_uclass_priv(parent);
	int err;

	sm->dw.first_busno = dev_seq(dev);
	sm->dw.dev = dev;

	err = pcie_spacemit_init_port(dev, SM_PCIE_HOST_TYPE);
	if (err) {
		dev_err(dev, "Failed to init port.\n");
		return err;
	}

	printf("PCIE-%d: Link up (Gen%d-x%d, Bus%d)\n",
	       dev_seq(dev), pcie_dw_get_link_speed(&sm->dw),
	       pcie_dw_get_link_width(&sm->dw),
	       hose->first_busno);

	return pcie_dw_prog_outbound_atu_unroll(&sm->dw,
						PCIE_ATU_REGION_INDEX0,
						PCIE_ATU_TYPE_MEM,
						sm->dw.mem.phys_start,
						sm->dw.mem.bus_start,
						sm->dw.mem.size);
}

static void __iomem *get_fdt_addr(struct udevice *dev, const char *name)
{
	fdt_addr_t addr;

	addr = dev_read_addr_name(dev, name);

	return (addr == FDT_ADDR_T_NONE) ? NULL : (void __iomem *)addr;
}

static int pcie_spacemit_of_to_plat(struct udevice *dev)
{
	struct pcie_spacemit *sm = dev_get_priv(dev);
	int err;

	/* get designware DBI base addr */
	sm->dw.dbi_base = get_fdt_addr(dev, "dbi");
	if (!sm->dw.dbi_base)
		return -EINVAL;

	/* get private control base addr */
	sm->priv_base = get_fdt_addr(dev, "app");
	if (!sm->priv_base)
		return -EINVAL;

	err = clk_get_by_index(dev, 0, &sm->clock);
	if (err) {
		dev_warn(dev, "It has no clk: %d\n", err);
	}

	err = reset_get_bulk(dev, &sm->resets);
	if (err) {
		dev_err(dev, "reset_get_bulk(reset) failed: %d\n", err);
		return err;
	}

	return 0;
}

static const struct dm_pci_ops pcie_spacemit_ops = {
	.read_config	= pcie_dw_read_config,
	.write_config	= pcie_dw_write_config,
};

static const struct udevice_id pcie_spacemit_ids[] = {
	{ .compatible = "spacemit,k1-pro-pcie" },
	{}
};

U_BOOT_DRIVER(pcie_spacemit) = {
	.name		= "pcie_spacemit",
	.id		= UCLASS_PCI,
	.of_match	= pcie_spacemit_ids,
	.ops		= &pcie_spacemit_ops,
	.of_to_plat	= pcie_spacemit_of_to_plat,
	.probe		= pcie_spacemit_probe,
	.priv_auto	= sizeof(struct pcie_spacemit),
};
