// SPDX-License-Identifier: (GPL-2.0+ or MIT)

#ifndef _DT_BINDINGS_CLK_SPACEMIT_K1PRO_H_
#define _DT_BINDINGS_CLK_SPACEMIT_K1PRO_H_

#define CLK_DUMMY       0
#define OSC_CLK_24M     1
#define IN_CLK_32K      2

#define PLL_CPU0        3
#define PLL_CPU1        4
#define PLL_DFS         5
#define PLL_DDR         6
#define PLL_SYS         7
#define PLL_AUD         8
#define PLL_GMAC        9

#define PLL_CLK_SYS     10
#define PLL_CLK_GMAC    11
#define PLL_CLK_I2S     12
#define CLK_SYS         13
#define PLL_CLK_400M    14

#define OSC_CLK_24M_DIV60   15
#define OSC_CLK_400K        16
#define OSC_CLK_200K        17
#define PLL_CLK_400M_2MEM   18
#define PLL_CLK_DDR         19
#define PLL_CLK_50M         20
#define PLL_CLK_10M         21
#define CLK_MEM_SYS_DIV     22
#define CLK_MEM_SYS         23
#define CLK_VPU_SYS_DIV     24
#define CLK_VPU_SYS         25
#define CLK_SEC_SYS_DIV     26
#define CLK_SEC_SYS         27
#define CLK_SERDES_SYS_DIV  28
#define CLK_SERDES_SYS      29
#define CLK_USB_SYS_DIV     30
#define CLK_USB_SYS         31
#define CLK_MBUS_SYS_DIV    32
#define CLK_MBUS_SYS        33
#define PLL_CLK_250_50M_DIV 34
#define PLL_CLK_250_50M     35

//cpu
#define CLK_CORE_CPU0_SRC   36
#define	CLK_CORE_CPU0       37
#define	CLK_ADB_CPU0        38
#define CLK_LLP_CPU0        39
#define	CLK_CPU2MBUS        40

#define CLK_CORE_CPU1_SRC   41
#define	CLK_CORE_CPU1       42
#define	CLK_ADB_CPU1        43
#define CLK_LLP_CPU1        44

#define	CLK_CCI_SRC         45
#define	CLK_CCI_SRC_EN      46
#define	CLK_CPU_CCI         47
#define	CLK_CPU_NIC         48
#define	CLK_CPU_CCI_EN      49

#define	CLK_CCI_APB_SRC     50
#define	CLK_CCI_APB_SRC_EN  51
#define	CLK_CCI_APB         52
#define	CLK_TDT_DMI         53
#define	CLK_SYS_DEBUG       54
#define	CLK_APB_APU0        55
#define	CLK_APB_APU1        56

#define	CLK_SERDES2CPU_NIC  57

//sys
#define CLK_SYS_AHB   58
#define CLK_SYS_APB   59
#define CLK_DMAC      60
#define CLK_GPIO      61
#define CLK_WDT       62
#define CLK_PWM       63
#define CLK_BMU       64
#define CLK_SYSREG    65
#define CLK_UART0     66
#define CLK_UART1     67
#define CLK_UART2     68
#define CLK_UART3     69
#define CLK_UART4     70
#define CLK_I2C0      71
#define CLK_I2C1      72
#define CLK_I2C2      73
#define CLK_I2C3      74
#define CLK_I2C4      75
#define CLK_TIMER0    76
#define CLK_TIMER1    77
#define CLK_QSPI0     78
#define CLK_QSPI1     79
#define CLK_QSPI2     80
#define CLK_QSPI0_EN  81
#define CLK_QSPI1_EN  82
#define CLK_QSPI2_EN  82
#define CLK_CAN       84
#define CLK_CAN_EN    85
#define CLK_I2S0_EN   86
#define CLK_I2S1_EN   87
#define CLK_I2S0_MCLK 88
#define CLK_I2S1_MCLK 89
#define CLK_I2S0_BCLK 90
#define CLK_I2S1_BCLK 91
#define CLK_I2S0_MCLK_OUT 92
#define CLK_I2S1_MCLK_OUT 93
//mem
#define CLK_SDIO_DIV    94
#define CLK_EMMC_DIV    95
#define CLK_TMCLK_10M   96
#define CLK_SDIO_SEL    97
#define CLK_EMMC_SEL    98
#define CLK_MEM_HCLK    99
#define CLK_SDIO_HCLK   100
#define CLK_SDIO_ACLK   101
#define CLK_SDIO_TMCLK  102
#define CLK_SDIO_CCLK   103
#define CLK_EMMC_HCLK   104
#define CLK_EMMC_ACLK   105
#define CLK_EMMC_TMCLK  106
#define CLK_EMMC_CCLK   107
#define CLK_MEM_AHB     108
//serdes
#define CLK_SERDES_AXI  109
#define CLK_SERDES_APB  110
#define CLK_SERDES2MBUS 111
#define CLK_PCIE0_EN    112
#define CLK_PCIE1_EN    113
#define CLK_PCIE2_EN    114
#define CLK_PCIE3_EN    115
#define CLK_PCIE4_EN    116
#define CLK_PCIE5_EN    117
#define CLK_SATA_EN     118
#define CLK_SATA0_EN    119
#define CLK_SATA1_EN    120
#define CLK_SATA2_EN    121
#define CLK_SATA3_EN    122
#define CLK_USB31_BUS_CLK_EARLY0  123
#define CLK_USB31_BUS_CLK_EARLY1  124
#define CLK_XGMII_PCLK0      125
#define CLK_XGMII_PCLK1      126
#define CLK_COMBO_PHY_PCLK0  127
#define CLK_COMBO_PHY_PCLK1  128
//vpu
#define CLK_VPU           129
#define CLK_VPU2VBUS      130
//sec
#define CLK_SEC_AHB       131
#define CLK_SEC_SYS_DIV2  132
#define CLK_SEC_APB       133
#define CLK_SEC_TRNG      134
#define CLK_SEC_PKE       135
#define CLK_SEC_HASH      136
#define CLK_SEC_SKE       137
#define CLK_SEC_EFUSE     138
//gmac
#define CLK_USB_AHB       139
#define CLK_USB_AHB_DIV2  140
#define CLK_USB_SYS_DIV2  141
#define CLK_USB20_OTG_EN  142
#define CLK_USB20_HOST_EN 143
#define CLK_USB31_DRD_EN  144

#define CLK_GMAC_CSR            147
#define CLK_USB2VBUS            148
#define CLK_GMAC_PHYCLK_OUT     149
//mbus
#define CLK_DDRC_APB   150
//mcu
#define CLK_MCU_SRC    151
#define CLK_MCU_SYS    152
#define CLK_MCU2MBUS   153
#define CLK_MCU_AHB    154
#define CLK_MCU_APB    155
#define CLK_MBOX       156
#define CLK_SPINLOCK   157

#define CLK_MAX_NO     158
#endif /* _DT_BINDINGS_CLK_SPACEMIT_K1PRO_H_ */
