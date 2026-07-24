// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Device Tree support for Spacemit SoCs
 *
 * Copyright (c) 2023 Spacemit Inc.
 */
#include <dt-bindings/clock/k1pro-clock.h>
#include <linux/clk-provider.h>
#include <common.h>
#include <clk.h>
#include <clk-uclass.h>
#include <dm.h>
#include <log.h>
#include <asm/io.h>
#include "ccu-k1pro.h"

static struct clk* clks[CLK_MAX_NO] = {};
//parents for mux
static const char *clk_mcu_src[]       = { "osc_clk_24m", "pll_clk_400m", "in_clk_32k", };
static const char *pll_clk_sys_src[]   = { "osc_clk_24m", "pll_sys", };
static const char *pll_clk_i2s_src[]   = { "osc_clk_24m", "pll_i2s", };
static const char *pll_clk_gmac_src[]  = { "osc_clk_24m", "pll_gmac", };
static const char *clk_sys_src[]       = { "pll_clk_sys", "pll_clk_gmac", };
static const char *clk_core_cpu0_src[] = { "osc_clk_24m", "pll_cpu0", "pll_dfs", };
static const char *clk_core_cpu1_src[] = { "osc_clk_24m", "pll_cpu1", "pll_dfs", };
static const char *clk_cci_src[]       = { "osc_clk_24m", "pll_clk_sys", "pll_clk_gmac", };
static const char *clk_cci_apb_src[]   = { "osc_clk_24m", "pll_clk_400m", };

static struct ccu_clk_data ccu_clocks[] =  {
    {PLL_CPU0, CLK_TYPE_PLL_INT, "pll_cpu0", "osc_clk_24m", NULL, 1, CPU0_PLL_CTRL_REG000, 0, 0, 0, 0, 0},
    {PLL_CPU1, CLK_TYPE_PLL_INT, "pll_cpu1", "osc_clk_24m", NULL, 1, CPU1_PLL_CTRL_REG008, 0, 0, 0, 0, 0},
    {PLL_DFS, CLK_TYPE_PLL_INT, "pll_dfs", "osc_clk_24m", NULL, 1, DFS_PLL_CTRL_REG010, 0, 0, 0, 0, 0},
    {PLL_DDR, CLK_TYPE_PLL_FRAC, "pll_ddr", "osc_clk_24m", NULL, 1, DDR_PLL_CTRL_REG018, 0, 0, 0, 0, 0},
    {PLL_SYS, CLK_TYPE_PLL_INT, "pll_sys", "osc_clk_24m", NULL, 1, SYS_PLL_CTRL_REG024, 0, 0, 0, 0, 0},
    {PLL_AUD, CLK_TYPE_PLL_FRAC, "pll_i2s", "osc_clk_24m", NULL, 1, AUDIO_PLL_CTRL_REG034, 0, 0, 0, 0, 0},
    {PLL_GMAC, CLK_TYPE_PLL_INT, "pll_gmac", "osc_clk_24m", NULL, 1, GMAC_PLL_CTRL_REG02C, 0, 0, 0, 0, 0},
    {PLL_CLK_SYS, CLK_TYPE_MUX, "pll_clk_sys", NULL, pll_clk_sys_src, 2, SYS_SUB_CTRL_REG200, 0, 0, 0, 0, 0},
    {PLL_CLK_GMAC, CLK_TYPE_MUX, "pll_clk_gmac", NULL, pll_clk_gmac_src, 2, USB_GMAC_SUB_CTRL_REG700, 0, 0, 0, 0, 0},
    {PLL_CLK_I2S, CLK_TYPE_MUX, "pll_clk_i2s", NULL, pll_clk_i2s_src, 2, SYS_SUB_CTRL_REG200, 2, 0, 0, 0, 0},
    {CLK_SYS, CLK_TYPE_MUX, "clk_sys", NULL, clk_sys_src, 2, SYS_SUB_CTRL_REG200, 1, 0, 0, 0, 0},
    {PLL_CLK_400M, CLK_TYPE_DIVIDER, "pll_clk_400m", "clk_sys", NULL, 1, SYS_SUB_CTRL_REG200, 8, 4, 0, 0, 0},
    {OSC_CLK_24M_DIV60, CLK_TYPE_FIXED_FACTOR, "osc_clk_24m_div60", "osc_clk_24m", NULL, 0, 0, 0, 0, 0, 1, 60},
    {OSC_CLK_400K, CLK_TYPE_GATE, "osc_clk_400k", "osc_clk_24m_div60", NULL, 1, MEM_SUB_EN_REG304, 2, 0, 0, 0, 0},
    {OSC_CLK_200K, CLK_TYPE_FIXED_FACTOR, "osc_clk_200k", "osc_clk_24m", NULL, 0, 0, 0, 0, 0, 1, 120},
    {PLL_CLK_400M_2MEM, CLK_TYPE_GATE, "pll_clk_400m_2mem", "pll_clk_400m", NULL, 1, MEM_SUB_EN_REG304, 1, 0, 0, 0, 0},
    {PLL_CLK_DDR, CLK_TYPE_GATE, "pll_clk_ddr", "pll_ddr", NULL, 1, MBUS_DDR_SUB_EN_REG804, 0, 0, 0, 0, 0},
    {PLL_CLK_50M, CLK_TYPE_FIXED_FACTOR, "pll_clk_50m", "pll_clk_400m", NULL, 0, 0, 0, 0, 0, 1, 8},
    {PLL_CLK_10M, CLK_TYPE_FIXED_FACTOR, "pll_clk_10m", "pll_clk_400m", NULL, 0, 0, 0, 0, 0, 1, 40},
    {CLK_MEM_SYS_DIV, CLK_TYPE_DIVIDER, "clk_mem_sys_div", "pll_clk_sys", NULL, 1, MEM_SUB_CTRL_REG300, 8, 4, 0, 0, 0},
    {CLK_MEM_SYS, CLK_TYPE_GATE, "clk_mem_sys", "clk_mem_sys_div", NULL, 1, MEM_SUB_EN_REG304, 0, 0, 0, 0, 0},
    {CLK_VPU_SYS_DIV, CLK_TYPE_DIVIDER, "clk_vpu_sys_div", "pll_clk_sys", NULL, 1, VPU_SUB_CTRL_REG500, 8, 4, 0, 0, 0},
    {CLK_VPU_SYS, CLK_TYPE_GATE, "clk_vpu_sys", "clk_vpu_sys_div", NULL, 1, VPU_SUB_EN_REG504, 0, 0, 0, 0, 0},
    {CLK_SEC_SYS_DIV, CLK_TYPE_DIVIDER, "clk_sec_sys_div", "pll_clk_sys", NULL, 1, SEC_SUB_CTRL_REG600, 8, 4, 0, 0, 0},
    {CLK_SEC_SYS, CLK_TYPE_GATE, "clk_sec_sys", "clk_sec_sys_div", NULL, 1, SEC_SUB_EN_REG604, 0, 0, 0, 0, 0},
    {CLK_SERDES_SYS_DIV, CLK_TYPE_DIVIDER, "clk_serdes_sys_div", "pll_clk_sys", NULL, 1, SERDES_SUB_CTRL_REG400, 8, 4, 0, 0, 0},
    {CLK_SERDES_SYS, CLK_TYPE_GATE, "clk_serdes_sys", "clk_serdes_sys_div", NULL, 1, SERDES_SUB_EN_REG404, 0, 0, 0, 0, 0},
    {CLK_USB_SYS_DIV, CLK_TYPE_DIVIDER, "clk_usb_sys_div", "pll_clk_sys", NULL, 1, USB_GMAC_SUB_CTRL_REG700, 8, 4, 0, 0, 0},
    {CLK_USB_SYS, CLK_TYPE_GATE, "clk_usb_sys", "clk_usb_sys_div", NULL, 1, USB_GMAC_SUB_EN_REG704, 0, 0, 0, 0, 0},
    {CLK_MBUS_SYS_DIV, CLK_TYPE_DIVIDER, "clk_mbus_sys_div", "pll_clk_sys", NULL, 1, MBUS_DDR_SUB_CTRL_REG800, 8, 4, 0, 0, 0},
    {CLK_MBUS_SYS, CLK_TYPE_GATE, "clk_mbus_sys", "clk_mbus_sys_div", NULL, 1, MBUS_DDR_SUB_EN_REG804, 1, 0, 0, 0, 0},
    {PLL_CLK_250_50M_DIV, CLK_TYPE_DIVIDER, "pll_clk_250_50m_div", "pll_clk_gmac", NULL, 1, USB_GMAC_SUB_CTRL_REG700, 12, 6, 0, 0, 0},
    {PLL_CLK_250_50M, CLK_TYPE_GATE, "pll_clk_250_50m", "pll_clk_250_50m_div", NULL, 1, USB_GMAC_SUB_EN_REG704, 6, 0, 0, 0, 0},
    //cpu
    {CLK_CORE_CPU0_SRC, CLK_TYPE_MUX, "clk_core_cpu0_src", NULL, clk_core_cpu0_src, 3, CPU_SUB_CPU0_CTRL_REG100, 0, 2, 0, 0, 0},
    {CLK_CORE_CPU0, CLK_TYPE_GATE, "clk_core_cpu0", "clk_core_cpu0_src", NULL, 1, CPU_SUB_CPU0_EN_REG104, 0, 0, 0, 0, 0},
    {CLK_ADB_CPU0, CLK_TYPE_DIVIDER, "clk_adb_cpu0", "clk_core_cpu0", NULL, 1, CPU_SUB_CPU0_CTRL_REG100, 8, 2, 0, 0, 0},
    {CLK_LLP_CPU0, CLK_TYPE_DIVIDER, "clk_llp_cpu0", "clk_core_cpu0", NULL, 1, CPU_SUB_CPU0_CTRL_REG100, 10, 2, 0, 0, 0},
    {CLK_CPU2MBUS, CLK_TYPE_GATE, "clk_cpu2mbus", "clk_llp_cpu0", NULL, 1, CPU_SUB_EN_REG114, 16, 0, 0, 0, 0},
    {CLK_CORE_CPU1_SRC, CLK_TYPE_MUX, "clk_core_cpu1_src", NULL, clk_core_cpu1_src, 3, CPU_SUB_CPU1_CTRL_REG108, 0, 2, 0, 0, 0},
    {CLK_CORE_CPU1, CLK_TYPE_GATE, "clk_core_cpu1", "clk_core_cpu1_src", NULL, 1, CPU_SUB_CPU1_EN_REG10C, 0, 0, 0, 0, 0},
    {CLK_ADB_CPU1, CLK_TYPE_DIVIDER, "clk_adb_cpu1", "clk_core_cpu1", NULL, 1, CPU_SUB_CPU1_CTRL_REG108, 8, 2, 0, 0, 0},
    {CLK_LLP_CPU1, CLK_TYPE_DIVIDER, "clk_llp_cpu1", "clk_core_cpu1", NULL, 1, CPU_SUB_CPU1_CTRL_REG108, 10, 2, 0, 0, 0},
    {CLK_CCI_SRC, CLK_TYPE_MUX, "clk_cci_src", NULL, clk_cci_src, 3, CPU_SUB_CTRL_REG110, 1, 2, 0, 0, 0},
    {CLK_CCI_SRC_EN, CLK_TYPE_GATE, "clk_cci_src_en", "clk_cci_src", NULL, 1, CPU_SUB_EN_REG114, 3, 0, 0, 0, 0},
    {CLK_CPU_CCI, CLK_TYPE_DIVIDER, "clk_cpu_cci", "clk_cci_src_en", NULL, 1, CPU_SUB_CTRL_REG110, 11, 3, 0, 0, 0},
    {CLK_CPU_NIC, CLK_TYPE_DIVIDER, "clk_cpu_nic", "clk_cci_src_en", NULL, 1, CPU_SUB_CTRL_REG110, 14, 3, 0, 0, 0},
    {CLK_CPU_CCI_EN, CLK_TYPE_GATE, "clk_cpu_cci_en", "clk_cpu_cci", NULL, 1, CPU_SUB_EN_REG114, 4, 0, 0, 0, 0},
    {CLK_CCI_APB_SRC, CLK_TYPE_MUX, "clk_cci_apb_src", NULL, clk_cci_apb_src, 2, CPU_SUB_CTRL_REG110, 0, 0, 0, 0, 0},
    {CLK_CCI_APB_SRC_EN, CLK_TYPE_GATE, "clk_cci_apb_src_en", "clk_cci_apb_src", NULL, 1, CPU_SUB_EN_REG114, 0, 0, 0, 0, 0},
    {CLK_CCI_APB, CLK_TYPE_DIVIDER, "clk_cci_apb", "clk_cci_apb_src_en", NULL, 1, CPU_SUB_CTRL_REG110, 8, 3, 0, 0, 0},
    {CLK_TDT_DMI, CLK_TYPE_GATE, "clk_tdt_dmi", "clk_cci_apb", NULL, 1, CPU_SUB_EN_REG114, 1, 0, 0, 0, 0},
    {CLK_SYS_DEBUG, CLK_TYPE_GATE, "clk_sys_debug", "clk_cci_apb", NULL, 1, CPU_SUB_EN_REG114, 2, 0, 0, 0, 0},
    {CLK_APB_APU0, CLK_TYPE_GATE, "clk_apb_cpu0", "clk_cci_apb", NULL, 1, CPU_SUB_CPU0_EN_REG104, 1, 0, 0, 0, 0},
    {CLK_APB_APU1, CLK_TYPE_GATE, "clk_apb_cpu1", "clk_cci_apb", NULL, 1, CPU_SUB_CPU1_EN_REG10C, 1, 0, 0, 0, 0},
    {CLK_SERDES2CPU_NIC, CLK_TYPE_GATE, "clk_cci_apb_src_en", "clk_serdes_sys", NULL, 1, CPU_SUB_EN_REG114, 5, 0, 0, 0, 0},
    //sys
    {CLK_SYS_AHB, CLK_TYPE_DIVIDER, "clk_sys_ahb", "pll_clk_400m", NULL, 1, SYS_SUB_CTRL_REG200, 12, 2, 0, 0, 0},
    {CLK_SYS_APB, CLK_TYPE_DIVIDER, "clk_sys_apb", "pll_clk_400m", NULL, 1, SYS_SUB_CTRL_REG200, 14, 2, 0, 0, 0},
    {CLK_DMAC, CLK_TYPE_GATE, "clk_dmac", "clk_sys_ahb", NULL, 1, SYS_SUB_EN_REG204, 2, 0, 0, 0, 0},
    {CLK_GPIO, CLK_TYPE_GATE, "clk_gpio", "clk_sys_apb", NULL, 1, SYS_SUB_EN_REG204, 3, 0, 0, 0, 0},
    {CLK_WDT, CLK_TYPE_GATE, "clk_wdt", "clk_sys_apb", NULL, 1, SYS_SUB_EN_REG204, 4, 0, 0, 0, 0},
    {CLK_PWM, CLK_TYPE_GATE, "clk_pwm", "clk_sys_apb", NULL, 1, SYS_SUB_EN_REG204, 5, 0, 0, 0, 0},
    {CLK_BMU, CLK_TYPE_GATE, "clk_bmu", "clk_sys_apb", NULL, 1, SYS_SUB_EN_REG204, 6, 0, 0, 0, 0},
    {CLK_SYSREG, CLK_TYPE_GATE, "clk_sysreg", "clk_sys_apb", NULL, 1, SYS_SUB_EN_REG204, 7, 0, 0, 0, 0},
    {CLK_UART0, CLK_TYPE_GATE, "clk_uart0", "clk_sys_apb", NULL, 1, SYS_SUB_UART_EN_REG214, 0, 0, 0, 0, 0},
    {CLK_UART1, CLK_TYPE_GATE, "clk_uart1", "clk_sys_apb", NULL, 1, SYS_SUB_UART_EN_REG214, 1, 0, 0, 0, 0},
    {CLK_UART2, CLK_TYPE_GATE, "clk_uart2", "clk_sys_apb", NULL, 1, SYS_SUB_UART_EN_REG214, 2, 0, 0, 0, 0},
    {CLK_UART3, CLK_TYPE_GATE, "clk_uart3", "clk_sys_apb", NULL, 1, SYS_SUB_UART_EN_REG214, 3, 0, 0, 0, 0},
    {CLK_UART4, CLK_TYPE_GATE, "clk_uart4", "clk_sys_apb", NULL, 1, SYS_SUB_UART_EN_REG214, 4, 0, 0, 0, 0},
    {CLK_I2C0, CLK_TYPE_GATE, "clk_i2c0", "clk_sys_apb", NULL, 1, SYS_SUB_I2C_EN_REG21C, 0, 0, 0, 0, 0},
    {CLK_I2C1, CLK_TYPE_GATE, "clk_i2c1", "clk_sys_apb", NULL, 1, SYS_SUB_I2C_EN_REG21C, 1, 0, 0, 0, 0},
    {CLK_I2C2, CLK_TYPE_GATE, "clk_i2c2", "clk_sys_apb", NULL, 1, SYS_SUB_I2C_EN_REG21C, 2, 0, 0, 0, 0},
    {CLK_I2C3, CLK_TYPE_GATE, "clk_i2c3", "clk_sys_apb", NULL, 1, SYS_SUB_I2C_EN_REG21C, 3, 0, 0, 0, 0},
    {CLK_I2C4, CLK_TYPE_GATE, "clk_i2c4", "clk_sys_apb", NULL, 1, SYS_SUB_I2C_EN_REG21C, 4, 0, 0, 0, 0},
    {CLK_TIMER0, CLK_TYPE_GATE, "clk_timer0", "clk_sys_apb", NULL, 1, SYS_SUB_TIMER_EN_REG224, 0, 0, 0, 0, 0},
    {CLK_QSPI0, CLK_TYPE_DIVIDER, "clk_qspi0", "pll_clk_400m", NULL, 1, SYS_SUB_QSPI_CTRL_REG208, 8, 2, 0, 0, 0},
    {CLK_QSPI1, CLK_TYPE_DIVIDER, "clk_qspi1", "pll_clk_400m", NULL, 1, SYS_SUB_QSPI_CTRL_REG208, 10, 2, 0, 0, 0},
    {CLK_QSPI2, CLK_TYPE_DIVIDER, "clk_qspi2", "pll_clk_400m", NULL, 1, SYS_SUB_QSPI_CTRL_REG208, 12, 2, 0, 0, 0},
    {CLK_QSPI0_EN, CLK_TYPE_GATE, "clk_qspi0_en", "clk_qspi0", NULL, 1, SYS_SUB_QSPI_EN_REG20C, 0, 0, 0, 0, 0},
    {CLK_QSPI1_EN, CLK_TYPE_GATE, "clk_qspi1_en", "clk_qspi1", NULL, 1, SYS_SUB_QSPI_EN_REG20C, 1, 0, 0, 0, 0},
    {CLK_QSPI2_EN, CLK_TYPE_GATE, "clk_qspi2_en", "clk_qspi2", NULL, 1, SYS_SUB_QSPI_EN_REG20C, 2, 0, 0, 0, 0},
    {CLK_CAN, CLK_TYPE_DIVIDER, "clk_can", "pll_clk_400m", NULL, 1, SYS_SUB_CAN_CTRL_REG228, 8, 4, 0, 0, 0},
    {CLK_CAN_EN, CLK_TYPE_GATE, "clk_can_en", "clk_can", NULL, 1, SYS_SUB_CAN_EN_REG22C, 0, 0, 0, 0, 0},
    {CLK_I2S0_EN, CLK_TYPE_GATE, "clk_i2s0_en", "pll_clk_i2s", NULL, 1, SYS_SUB_I2S_EN_REG234, 0, 0, 0, 0, 0},
    {CLK_I2S1_EN, CLK_TYPE_GATE, "clk_i2s1_en", "pll_clk_i2s", NULL, 1, SYS_SUB_I2S_EN_REG234, 1, 0, 0, 0, 0},
    {CLK_I2S0_MCLK, CLK_TYPE_DIVIDER, "clk_i2s0_mclk", "clk_i2s0_en", NULL, 1, SYS_SUB_I2S_CTRL_REG230, 8, 5, 0, 0, 0},
    {CLK_I2S1_MCLK, CLK_TYPE_DIVIDER, "clk_i2s1_mclk", "clk_i2s1_en", NULL, 1, SYS_SUB_I2S_CTRL_REG230, 16, 5, 0, 0, 0},
    {CLK_I2S0_BCLK, CLK_TYPE_DIVIDER, "clk_i2s0_bclk", "clk_i2s0_mclk", NULL, 1, SYS_SUB_I2S_CTRL_REG230, 13, 3, 0, 0, 0},
    {CLK_I2S1_BCLK, CLK_TYPE_DIVIDER, "clk_i2s1_bclk", "clk_i2s1_mclk", NULL, 1, SYS_SUB_I2S_CTRL_REG230, 21, 3, 0, 0, 0},
    {CLK_I2S0_MCLK_OUT, CLK_TYPE_GATE, "clk_i2s0_mclk_out", "clk_i2s0_mclk", NULL, 1, SYS_SUB_I2S_EN_REG234, 16, 0, 0, 0, 0},
    {CLK_I2S1_MCLK_OUT, CLK_TYPE_GATE, "clk_i2s1_mclk_out", "clk_i2s1_mclk", NULL, 1, SYS_SUB_I2S_EN_REG234, 17, 0, 0, 0, 0},
    //mem
    {CLK_MEM_HCLK, CLK_TYPE_FIXED_FACTOR, "clk_mem_hclk", "clk_sys_ahb", NULL, 0, 0, 0, 0, 0, 1, 2},
    {CLK_SDIO_HCLK, CLK_TYPE_GATE, "clk_sdio_hclk", "clk_mem_hclk", NULL, 1, MEM_SUB_EN_REG304, 3, 0, 0, 0, 0},
    {CLK_EMMC_HCLK, CLK_TYPE_GATE, "clk_emmc_hclk", "clk_mem_hclk", NULL, 1, MEM_SUB_EN_REG304, 4, 0, 0, 0, 0},
    {CLK_MEM_AHB, CLK_TYPE_GATE, "clk_mem_ahb", "clk_sys_ahb", NULL, 1, MEM_SUB_EN_REG304, 5, 0, 0, 0, 0},
    //serdes
    {CLK_SERDES_AXI, CLK_TYPE_FIXED_FACTOR, "clk_serdes_axi", "clk_serdes_sys", NULL, 0, 0, 0, 0, 0, 1, 2},
    {CLK_SERDES_APB, CLK_TYPE_FIXED_FACTOR, "clk_serdes_apb", "clk_serdes_sys", NULL, 0, 0, 0, 0, 0, 1, 4},
    {CLK_SERDES2MBUS, CLK_TYPE_GATE, "clk_serdes2mbus", "clk_serdes_axi", NULL, 1, SERDES_SUB_EN_REG404, 1, 0, 0, 0, 0},
    {CLK_PCIE0_EN, CLK_TYPE_GATE, "clk_pcie0_en", "clk_serdes_sys", NULL, 1, SERDES_SUB_PCIE_EN_REG40C, 0, 0, 0, 0, 0},
    {CLK_PCIE1_EN, CLK_TYPE_GATE, "clk_pcie1_en", "clk_serdes_sys", NULL, 1, SERDES_SUB_PCIE_EN_REG40C, 1, 0, 0, 0, 0},
    {CLK_PCIE2_EN, CLK_TYPE_GATE, "clk_pcie2_en", "clk_serdes_sys", NULL, 1, SERDES_SUB_PCIE_EN_REG40C, 2, 0, 0, 0, 0},
    {CLK_PCIE3_EN, CLK_TYPE_GATE, "clk_pcie3_en", "clk_serdes_sys", NULL, 1, SERDES_SUB_PCIE_EN_REG40C, 3, 0, 0, 0, 0},
    {CLK_PCIE4_EN, CLK_TYPE_GATE, "clk_pcie4_en", "clk_serdes_sys", NULL, 1, SERDES_SUB_PCIE_EN_REG40C, 4, 0, 0, 0, 0},
    {CLK_PCIE5_EN, CLK_TYPE_GATE, "clk_pcie5_en", "clk_serdes_sys", NULL, 1, SERDES_SUB_PCIE_EN_REG40C, 5, 0, 0, 0, 0},
    {CLK_SATA_EN, CLK_TYPE_GATE, "clk_sata_en", "clk_serdes_sys", NULL, 1, SERDES_SUB_SATA_EN_REG414, 0, 0, 0, 0, 0},
    {CLK_SATA0_EN, CLK_TYPE_GATE, "clk_sata0_en", "clk_serdes_sys", NULL, 1, SERDES_SUB_SATA_EN_REG414, 1, 0, 0, 0, 0},
    {CLK_SATA1_EN, CLK_TYPE_GATE, "clk_sata1_en", "clk_serdes_sys", NULL, 1, SERDES_SUB_SATA_EN_REG414, 2, 0, 0, 0, 0},
    {CLK_SATA2_EN, CLK_TYPE_GATE, "clk_sata2_en", "clk_serdes_sys", NULL, 1, SERDES_SUB_SATA_EN_REG414, 3, 0, 0, 0, 0},
    {CLK_SATA3_EN, CLK_TYPE_GATE, "clk_sata3_en", "clk_serdes_sys", NULL, 1, SERDES_SUB_SATA_EN_REG414, 4, 0, 0, 0, 0},
    {CLK_USB31_BUS_CLK_EARLY0, CLK_TYPE_GATE, "clk_usb31_bus_clk_early0", "clk_serdes_axi", NULL, 1, SERDES_SUB_USB_EN_REG41C, 0, 0, 0, 0, 0},
    {CLK_USB31_BUS_CLK_EARLY1, CLK_TYPE_GATE, "clk_usb31_bus_clk_early1", "clk_serdes_axi", NULL, 1, SERDES_SUB_USB_EN_REG41C, 1, 0, 0, 0, 0},
    {CLK_XGMII_PCLK0, CLK_TYPE_GATE, "clk_xgmii_pclk0", "clk_serdes_apb", NULL, 1, SERDES_SUB_XGMII_EN_REG424, 0, 0, 0, 0, 0},
    {CLK_XGMII_PCLK1, CLK_TYPE_GATE, "clk_xgmii_pclk1", "clk_serdes_apb", NULL, 1, SERDES_SUB_XGMII_EN_REG424, 1, 0, 0, 0, 0},
    {CLK_COMBO_PHY_PCLK0, CLK_TYPE_GATE, "clk_combo_phy_pclk0", "clk_serdes_apb", NULL, 1, SERDES_SUB_PHY_EN_REG42C, 0, 0, 0, 0, 0},
    {CLK_COMBO_PHY_PCLK1, CLK_TYPE_GATE, "clk_combo_phy_pclk1", "clk_serdes_apb", NULL, 1, SERDES_SUB_PHY_EN_REG42C, 1, 0, 0, 0, 0},
    //vpu
    {CLK_VPU2VBUS, CLK_TYPE_GATE, "clk_vpu2vbus", "clk_vpu_sys", NULL, 1, VPU_SUB_EN_REG504, 2, 0, 0, 0, 0},
    //sec
    {CLK_SEC_AHB, CLK_TYPE_GATE, "clk_sec_ahb", "clk_sys_ahb", NULL, 1, SEC_SUB_EN_REG604, 6, 0, 0, 0, 0},
    {CLK_SEC_SYS_DIV2, CLK_TYPE_FIXED_FACTOR, "clk_sec_sys_div2", "clk_sec_sys", NULL, 0, 0, 0, 0, 0, 1, 2},
    {CLK_SEC_APB, CLK_TYPE_FIXED_FACTOR, "clk_sec_apb", "clk_sec_ahb", NULL, 0, 0, 0, 0, 0, 1, 4},
    {CLK_SEC_TRNG, CLK_TYPE_GATE, "clk_sec_trng", "clk_sec_sys_div2", NULL, 1, SEC_SUB_EN_REG604, 1, 0, 0, 0, 0},
    {CLK_SEC_PKE, CLK_TYPE_GATE, "clk_sec_pke", "clk_sec_sys_div2", NULL, 1, SEC_SUB_EN_REG604, 2, 0, 0, 0, 0},
    {CLK_SEC_HASH, CLK_TYPE_GATE, "clk_sec_hash", "clk_sec_sys_div2", NULL, 1, SEC_SUB_EN_REG604, 3, 0, 0, 0, 0},
    {CLK_SEC_SKE, CLK_TYPE_GATE, "clk_sec_ske", "clk_sec_sys_div2", NULL, 1, SEC_SUB_EN_REG604, 4, 0, 0, 0, 0},
    {CLK_SEC_EFUSE, CLK_TYPE_GATE, "clk_sec_efuse", "clk_sec_apb", NULL, 1, SEC_SUB_EN_REG604, 5, 0, 0, 0, 0},
    //gmac
    {CLK_USB_AHB, CLK_TYPE_GATE, "clk_usb_ahb", "clk_sys_ahb", NULL, 1, USB_GMAC_SUB_EN_REG704, 7, 0, 0, 0, 0},
    {CLK_USB_AHB_DIV2, CLK_TYPE_FIXED_FACTOR, "clk_usb_ahb_div2", "clk_usb_ahb", NULL, 0, 0, 0, 0, 0, 1, 2},
    {CLK_USB_SYS_DIV2, CLK_TYPE_FIXED_FACTOR, "clk_usb_sys_div2", "clk_usb_sys", NULL, 0, 0, 0, 0, 0, 1, 2},
    {CLK_USB20_OTG_EN, CLK_TYPE_GATE, "clk_usb20_otg_en", "clk_usb_sys_div2", NULL, 1, USB_GMAC_SUB_EN_REG704, 1, 0, 0, 0, 0},
    {CLK_USB20_HOST_EN, CLK_TYPE_GATE, "clk_usb20_host_en", "clk_usb_sys_div2", NULL, 1, USB_GMAC_SUB_EN_REG704, 2, 0, 0, 0, 0},
    {CLK_USB31_DRD_EN, CLK_TYPE_GATE, "clk_usb31_drd_en", "osc_clk_24m", NULL, 1, USB_GMAC_SUB_EN_REG704, 3, 0, 0, 0, 0},
    {CLK_GMAC_CSR, CLK_TYPE_GATE, "clk_gmac_csr", "clk_usb_ahb_div2", NULL, 1, USB_GMAC_SUB_EN_REG704, 6, 0, 0, 0, 0},
    {CLK_USB2VBUS, CLK_TYPE_GATE, "clk_usb2vbus", "clk_usb_sys", NULL, 1, USB_GMAC_SUB_EN_REG704, 16, 0, 0, 0, 0},
    {CLK_GMAC_PHYCLK_OUT, CLK_TYPE_GATE, "clk_gmac_phyclk_out", "clk_dummy", NULL, 0, USB_GMAC_SUB_EN_REG704, 17, 0, 0, 0, 0},
    //ddr_mbus
    {CLK_DDRC_APB, CLK_TYPE_GATE, "clk_ddrc_apb", "clk_sys_apb", NULL, 1, MBUS_DDR_SUB_EN_REG804, 2, 0, 0, 0, 0},
	//mcu
    {CLK_MCU_SRC, CLK_TYPE_MUX, "clk_mcu_src", NULL, clk_mcu_src, 3, MCU_SUB_CTRL_REG000, 0, 2, 0, 0, 0},
    {CLK_MCU_SYS, CLK_TYPE_GATE, "clk_mcu_sys", "clk_mcu_src", NULL, 1, MCU_SUB_EN_REG004, 0, 0, 0, 0, 0},
    {CLK_MCU_AHB, CLK_TYPE_DIVIDER, "clk_mcu_ahb", "clk_mcu_sys", NULL, 1, MCU_SUB_CTRL_REG000, 8, 2, 0, 0, 0},
    {CLK_MCU_APB, CLK_TYPE_DIVIDER, "clk_mcu_apb", "clk_mcu_sys", NULL, 1, MCU_SUB_CTRL_REG000, 10, 2, 0, 0, 0},
    {CLK_MBOX, CLK_TYPE_GATE, "clk_mbox", "clk_mcu_apb", NULL, 1, MCU_SUB_EN_REG004, 11, 0, 0, 0, 0},
    {CLK_SPINLOCK, CLK_TYPE_GATE, "clk_spinlock", "clk_mcu_apb", NULL, 1, MCU_SUB_EN_REG004, 12, 0, 0, 0, 0},
    {CLK_MCU2MBUS, CLK_TYPE_GATE, "clk_mcu2mbus", "clk_mcu_ahb", NULL, 0, MCU_SUB_EN_REG004, 16, 0, 0, 0, 0},
};

static inline const struct clk_ops *ccu_clk_dev_ops(struct udevice *dev)
{
	return (const struct clk_ops *)dev->driver->ops;
}

ulong ccu_clk_get_rate(struct clk *clk)
{
	const struct clk_ops *ops;
	struct clk *c = clks[clk->id];
	if (!clk_valid(c))
		return 0;
	ops = ccu_clk_dev_ops(c->dev);
	if(ops->get_rate)
		return ops->get_rate(c);
	return 0;
}

ulong ccu_clk_set_rate(struct clk *clk, unsigned long rate)
{
	const struct clk_ops *ops;
	struct clk *c = clks[clk->id];
	if (!clk_valid(c))
		return 0;
	ops = ccu_clk_dev_ops(c->dev);
	if(ops->set_rate)
		return ops->set_rate(c, rate);
	return 0;
}

int ccu_clk_set_parent(struct clk *clk, struct clk *parent)
{
	const struct clk_ops *ops;
	struct clk *c = clks[clk->id];
	struct clk *p = clks[parent->id];
	if (!clk_valid(c))
		return 0;
	ops = ccu_clk_dev_ops(c->dev);
	if(ops->set_parent)
		return ops->set_parent(c, p);
	return 0;
}

int ccu_clk_enable(struct clk *clk)
{
	const struct clk_ops *ops;
	struct clk *c = clks[clk->id];
	if (!clk_valid(c))
		return 0;
	ops = ccu_clk_dev_ops(c->dev);
	if(ops->enable)
		return ops->enable(c);
	return 0;
}

int ccu_clk_disable(struct clk *clk)
{
	const struct clk_ops *ops;
	struct clk *c = clks[clk->id];
	if (!clk_valid(c))
		return 0;
	ops = ccu_clk_dev_ops(c->dev);
	if(ops->disable)
		return ops->disable(c);
	return 0;
}

const struct clk_ops ccu_clk_ops = {
	.set_rate = ccu_clk_set_rate,
	.get_rate = ccu_clk_get_rate,
	.set_parent = ccu_clk_set_parent,
	.enable = ccu_clk_enable,
	.disable = ccu_clk_disable,
};

static inline void ccu_clk_dm(ulong id, struct clk *clk)
{
	if (!IS_ERR(clk)){
		clk->id = id;
		clks[id] = clk;
	}
}

static int ccu_clk_probe(struct udevice *dev)
{
	void __iomem *reg_base;
	void __iomem *mcu_reg_base;
	struct clk osc_24m, clk_32k, clk_dummy;
	int i;

	reg_base = (void __iomem *)dev_remap_addr_index(dev, 0);
	mcu_reg_base = (void __iomem *)dev_remap_addr_index(dev, 1);
	if (!reg_base || !mcu_reg_base)
		return -ENOMEM;

	clk_get_by_name(dev, "clk_dummy", &clk_dummy);
	ccu_clk_dm(CLK_DUMMY, dev_get_clk_ptr(clk_dummy.dev));
	clk_get_by_name(dev, "osc_clk_24m", &osc_24m);
	ccu_clk_dm(OSC_CLK_24M, dev_get_clk_ptr(osc_24m.dev));
	clk_get_by_name(dev, "in_clk_32k", &clk_32k);
	ccu_clk_dm(IN_CLK_32K, dev_get_clk_ptr(clk_32k.dev));

	//register clocks
	for (i = 0; i < ARRAY_SIZE(ccu_clocks); i++) {
		const struct ccu_clk_data *ccd = &ccu_clocks[i];
		void __iomem *reg = reg_base;
		if (ccd->clk_index >= CLK_MCU_SRC){
			reg = mcu_reg_base;
		};

		switch(ccd->clk_type){
			case CLK_TYPE_GATE:
				ccu_clk_dm(ccd->clk_index, clk_register_gate(NULL, ccd->name, ccd->parent_name, 0,
					reg + ccd->reg, ccd->shift, 0, NULL));
				break;
			case CLK_TYPE_DIVIDER:
				ccu_clk_dm(ccd->clk_index, clk_register_divider(NULL, ccd->name, ccd->parent_name, 0,
					reg + ccd->reg, ccd->shift, ccd->width, 0));
				break;
			case CLK_TYPE_MUX:
				ccu_clk_dm(ccd->clk_index, clk_register_mux(NULL, ccd->name, ccd->parent_names, ccd->num_parents, 0,
					reg + ccd->reg, ccd->shift, ccd->width, 0));
				break;
			case CLK_TYPE_FIXED_FACTOR:
				ccu_clk_dm(ccd->clk_index, clk_register_fixed_factor(NULL, ccd->name, ccd->parent_name, 0,
					ccd->factor_mult, ccd->factor_div));
				break;
			case CLK_TYPE_PLL_INT:
			case CLK_TYPE_PLL_FRAC:
				ccu_clk_dm(ccd->clk_index, k1pro_clk_register_pllxx(ccd->name, ccd->parent_name,
					reg_base + ccd->reg, ccd->clk_type));
				break;
			default:
				break;
		}
	}
	return 0;
}

static const struct udevice_id ccu_clk_ids[] = {
	{ .compatible = "spacemit,k1pro-ccu" },
	{ },
};

U_BOOT_DRIVER(spacemit_k1pro_clk) = {
	.name = "k1pro-ccu",
	.id = UCLASS_CLK,
	.of_match = ccu_clk_ids,
	.ops = &ccu_clk_ops,
	.probe = ccu_clk_probe,
	.flags = DM_FLAG_PRE_RELOC,
};


