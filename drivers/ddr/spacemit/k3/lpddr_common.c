// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Spacemit
 */

#include "k3_ddr.h"

void lpddr_training_table_init(uint32_t ddrc_base, const phy_init_config* train_table[],
	const ddr_phy_reg_config* override_table, ddr_phy_reg_config* io_table)
{
	uint32_t reg_base, reg_offset, phy_value;
	unsigned long DPHY_BASE = ddrc_base + 0x800000;
	volatile uint32_t* phy_reg = (uint32_t*)DPHY_BASE;
	uint16_t* phy_data;
	const phy_init_config* sub_table;
	int i, j, k0, k1;
	bool need_table_check = false, need_io_check = false;

	for (i = 0, k0 = 0, k1 = 0; NULL != train_table[i]; i++) {
		sub_table = train_table[i];
		reg_base = sub_table->base;

		if ((NULL != override_table)
			&& ((override_table[k0].offset & ~0x7FFF) == (reg_base & ~0x7FFF))) {
			need_table_check = true;
		} else {
			need_table_check = false;
		}

		if ((NULL != io_table)
			&& ((io_table[k1].offset & ~0x7FFF) == (reg_base & ~0x7FFF))) {
			need_io_check = true;
		} else {
			need_io_check = false;
		}

		if (sub_table->is_linear_increase) {
			phy_data = (uint16_t*)sub_table->sequence;
			for (j = 0; j < sub_table->count; j++) {
				reg_offset = reg_base + j;
				phy_value = phy_data[j];

				if (need_table_check && (override_table[k0].offset == reg_offset)) {
					phy_value = override_table[k0++].value;
					if ((override_table[k0].offset & ~0x7FFF) != (reg_base & ~0x7FFF)) {
						need_table_check = false;
					}
					// skip the PHY setting when value is 0xdeadbeef
					if (DDR_CONFIG_BYPASS_MAGIC == phy_value) {
						continue;
					}
					pr_debug("Overriding PHY register 0x%x with value 0x%x @%d\n",
						reg_offset, phy_value, k0);
				}

				// configuration in IO table has higher priority
				if (need_io_check && (io_table[k1].offset == reg_offset)) {
					phy_value = io_table[k1++].value;
					if ((io_table[k1].offset & ~0x7FFF) != (reg_base & ~0x7FFF)) {
						need_io_check = false;
					}
					pr_debug("Overriding CSR 0x%x with value 0x%x @%d\n",
						reg_offset, phy_value, k1);
				}

				phy_reg[reg_offset] = phy_value;
			}
		} else {
			for (j = 0; j < sub_table->count; j++) {
				reg_offset = reg_base + sub_table->sequence[j].a.offset;
				phy_value = sub_table->sequence[j].a.value;
				if (need_table_check && (override_table[k0].offset == reg_offset)) {
					phy_value = override_table[k0++].value;
					if ((override_table[k0].offset & ~0x7FFF) != (reg_base & ~0x7FFF)) {
						need_table_check = false;
					}
					// skip the PHY setting when value is 0xdeadbeef
					if (DDR_CONFIG_BYPASS_MAGIC == phy_value) {
						continue;
					}
					pr_debug("Overriding PHY register 0x%x with value 0x%x @%d\n",
						reg_offset, phy_value, k0);
				}

				// configuration in IO table has higher priority
				if (need_io_check && (io_table[k1].offset == reg_offset)) {
					phy_value = io_table[k1++].value;
					if ((io_table[k1].offset & ~0x7FFF) != (reg_base & ~0x7FFF)) {
						need_io_check = false;
					}
					pr_debug("Overriding CSR 0x%x with value 0x%x @%d\n",
						reg_offset, phy_value, k1);
				}

				phy_reg[reg_offset] = phy_value;
			}
		}
	}
}

#if TRAINING_DEBUG
void translate_streaming(uint32_t* d)
{
}
#endif

void accept_message(uint32_t dphy_base)
{
	volatile uint32_t* dphy_reg = (volatile uint32_t*)(size_t)dphy_base;
	uint32_t read_data;

	dphy_reg[0x000d0031] = 0x00000000;
	read_data = dphy_reg[0x000d0004];
	while ((read_data & 0x00000001) != 0x00000001) {
		read_data = dphy_reg[0x000d0004];
	}
	dphy_reg[0x000d0031] = 0x00000001;
}

uint32_t major_message_all(uint32_t dphy_base)
{
	volatile uint32_t* dphy_reg = (volatile uint32_t*)(size_t)dphy_base;
	uint32_t read_data;
	uint32_t i;
	uint32_t j;
	uint32_t cnt = 0x1000;
#if TRAINING_DEBUG
	uint32_t read_data1;
	static uint32_t dmsg[50];
#endif

	read_data = dphy_reg[0x000d0004];
	while ((read_data & 0x00000001) != 0x00000000) {
		read_data = dphy_reg[0x000d0004];
	}
	read_data = dphy_reg[0x000d0032];

	while ((read_data & 0x000000ff) != 0x00000007) {
		if ((read_data & 0x000000ff) == 0x00000008) {
			accept_message(dphy_base);

			read_data = dphy_reg[0x000d0004];
			while ((read_data & 0x00000001) != 0x00000000) {
				read_data = dphy_reg[0x000d0004];
			}
			j = dphy_reg[0x000d0032];
			i = 0;
			while (i <= j) {

				read_data = dphy_reg[0x000d0032];
#if TRAINING_DEBUG
				read_data1 = dphy_reg[0x000d0034];
				if (i < ARRAY_SIZE(dmsg)) {
					dmsg[i] = (read_data1 << 16) | read_data;
					// LogMsg(0,"read dmsg 0x%08X\n",dmsg[i]);
				}
				if (i == j) {
					translate_streaming(dmsg);
				}
#endif
				accept_message(dphy_base);
				i++;
				read_data = dphy_reg[0x000d0004];
				while ((read_data & 0x00000001) != 0x00000000) {
					read_data = dphy_reg[0x000d0004];
				}
			}

		} else {
			LogMsg(0, "== Training major message ==\n");
			LogMsg(0, "%02x\n", read_data);

			if (read_data == 0xff) {
				dphy_reg[0xd0099] = 0x1;
				while (cnt--)
					;
				dphy_reg[0xd0000] = 0x0;
				read_data = dphy_reg[0x200c9];
				LogMsg(0, "plllockstatus is %02x\n", read_data);
				return read_data;
			}
			// while(cnt--);
			LogMsg(0, "============================\n");
			accept_message(dphy_base);
			read_data = dphy_reg[0x000d0004];
			while ((read_data & 0x00000001) != 0x00000000) {
				read_data = dphy_reg[0x000d0004];
			}
		}
		read_data = dphy_reg[0x000d0032];
	}

	LogMsg(0, "== Training major message ==\n");
	LogMsg(0, "%02x\n", read_data);
	LogMsg(0, "============================\n");

	accept_message(dphy_base);

	return 0;
}

static void init_ddr_clock(uint32_t DDRC_BASE, uint32_t data_rate_mtps)
{
	uint32_t read_data;
	uint32_t CFG_BASE = DDRC_BASE + 0x600000;
	volatile uint32_t* cfg_reg = (volatile uint32_t*)(size_t)CFG_BASE;

	if (5500 == data_rate_mtps) {
		/* DPLL 2750MHz*/
		cfg_reg[0x8 / 4] = 0x0b3912aa;
		cfg_reg[0x10 / 4] = 0xa0558b8b;
		cfg_reg[0xc / 4] |= (0x1 << 22) | (0x1 << 16) | (0xff) | (0xab << 8);
	} else if (6000 == data_rate_mtps) {
		/* DPLL 3000MHz*/
		cfg_reg[0x8 / 4] = 0x0b3e2000;
		cfg_reg[0x10 / 4] = 0xa0558c8c;
		cfg_reg[0xc / 4] |= (0x1 << 22) | (0x1 << 16) | (0xff) | (0x00 << 8);
	} else {
		/* DPLL 3200MHz*/
		cfg_reg[0xc / 4] |= (0x1 << 22) | (0x1 << 16) | (0xff);
	}

	read_data = cfg_reg[0x1c / 4];
	while ((read_data & 0x00000001) != 0x1) {
		read_data = cfg_reg[0x1c / 4];
	}
	// clear frequency divider
	cfg_reg[0x18 / 4] &= ~(0x3f << 16);

	if (1066 == data_rate_mtps) {
		cfg_reg[0x18 / 4] |= (0x1 << 19) | (0x7 << 16); // sel 2, div 8
	} else if (4266 == data_rate_mtps) {
		cfg_reg[0x18 / 4] |= (0x1 << 19) | (0x1 << 16); // sel 2, div 2
		// cfg_reg[0x18 / 4] |= (0x2 << 19) | (0x1 << 16); // sel 2, div 2 3200mbps
	} else if (5120 == data_rate_mtps) {
		cfg_reg[0x18 / 4] |= (0x7 << 19) | (0x0 << 16); // sel 3, div 1 5120mbps
	} else {
		cfg_reg[0x18 / 4] |= (0x2 << 19) | (0x0 << 16); // sel 3, div 1 6400mbps
		// cfg_reg[0x18 / 4] |= (0x1 << 19) | (0x1 << 16); // sel 3, div 1
	}

	// initial frequency change
	cfg_reg[0x18 / 4] |= (1 << 25);
	LogMsg(0, "read 6400 reg 0x%08X 0x%08X\n", CFG_BASE + 0x18, cfg_reg[0x18 / 4]);
	cfg_reg[0x18 / 4] = cfg_reg[0x18 / 4];
	LogMsg(0, "check setting reg 0x%08X 0x%08X\n", 0xD4282CE8, cfg_reg[0x18 / 4]);
	read_data = cfg_reg[0x18 / 4];
	while ((read_data & 0x2000000) != 0x0) {
		read_data = cfg_reg[0x18 / 4];
	}
	cfg_reg[0x18 / 4] |= 0x1;
}

static void init_snps_lp45(unsigned DDRC_BASE, ddr_part_info* part_info,
	ddr_boot_mode ddr_mode, ddr_training_info_t* training_info)
{
	init_ddr_clock(DDRC_BASE, part_info->data_rate_mtps);

	if (DDR_TYPE_LPDDR5 == part_info->type) {
#ifdef CONFIG_K3_DDR_LPDDR5
		init_snps_lp5_ddrc(DDRC_BASE, part_info, ddr_mode, training_info);
#endif
	} else if (DDR_TYPE_LPDDR4X == part_info->type) {
#ifdef CONFIG_K3_DDR_LPDDR4X
		init_snps_lp4x_ddrc(DDRC_BASE, part_info, ddr_mode, training_info);
#endif
	}
}

void lpddr_init_prepare(ddr_part_info* part_info, ddr_boot_mode ddr_mode)
{
	// ddr para and training firmware need to be initialized before training
	if (DDR_TYPE_LPDDR5 == part_info->type) {
#ifdef CONFIG_K3_DDR_LPDDR5
		build_lpddr5_io_para(get_ddr_default_io_para(DDR_TYPE_LPDDR5), part_info);
		if (DDR_QUICKBOOT_MODE != ddr_mode) {
			// during first boot, MUST do fully training
			lp5_training_prepare();
		}
#endif
	} else if (DDR_TYPE_LPDDR4X == part_info->type) {
#ifdef CONFIG_K3_DDR_LPDDR4X
		build_lpddr4x_io_para(get_ddr_default_io_para(DDR_TYPE_LPDDR4X), part_info);
		if (DDR_QUICKBOOT_MODE != ddr_mode) {
			// during first boot, MUST do fully training
			lp4x_training_prepare();
		}
#endif
	}
}

void lpddr_silicon_init(uint64_t ddrc_reg_base, ddr_part_info* part_info,
	ddr_boot_mode ddr_mode, ddr_training_info_t* training_info)
{
	LogMsg(0, "=== start init_lpddr() ===\n");
	init_snps_lp45(ddrc_reg_base, part_info, ddr_mode, training_info);
	LogMsg(0, "=== finish init_lpddr() ===\n");
}
