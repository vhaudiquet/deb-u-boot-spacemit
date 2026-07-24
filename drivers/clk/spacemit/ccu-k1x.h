// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, spacemit Corporation.
 *
 */

#ifndef _CCU_SPACEMIT_K1X_H_
#define _CCU_SPACEMIT_K1X_H_

#include <clk.h>
#include <clk-uclass.h>


/* u-boot-spl would used this clk */
enum {
	CLK_PLL1_2457P6_SPL = 0,
	CLK_PLL1_D2_SPL,
	CLK_PLL1_D4_SPL,
	CLK_PLL1_D6_SPL,
	CLK_PLL1_D8_SPL,
	CLK_PLL1_D23_SPL,
	CLK_PLL1_102P4_SPL,
	CLK_PLL1_409P6_SPL,
	CLK_PLL1_204P8_SPL,
	CLK_PLL1_31P5_SPL,
	CLK_PLL1_1228_SPL,
	CLK_TWSI6_SPL,
	CLK_TWSI8_SPL,
	CLK_SDH_AXI_SPL,
	CLK_SDH0_SPL,
	CLK_SDH2_SPL,
	CLK_USB_P1_SPL,
	CLK_USB_AXI_SPL,
	CLK_USB30_SPL,
	CLK_QSPI_SPL,
	CLK_QSPI_BUS_SPL,
	CLK_AES_SPL,

	CLK_PMUA_ACLK_SPL,
	CLK_APB_SPL,

	CLK_VCTCXO_24_SPL,
	CLK_VCTCXO_3_SPL,
	CLK_VCTCXO_1_SPL,
	CLK_PLL1_SPL,
	CLK_32K_SPL,
	CLK_DUMMY_SPL,

	CLK_MAX_NO_SPL,
};

ulong transfer_clk_id_to_spl(ulong id);

#endif /* _CCU_SPACEMIT_K1X_H_ */
