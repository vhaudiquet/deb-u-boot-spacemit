// SPDX-License-Identifier: GPL-2.0+
/*
 * rtl8169.c : U-Boot driver for the RealTek RTL8169
 *
 * Masami Komiya (mkomiya@sonare.it)
 *
 * Most part is taken from r8169.c of etherboot
 *
 */

/**************************************************************************
*    r8169.c: Etherboot device driver for the RealTek RTL-8169 Gigabit
*    Written 2003 by Timothy Legge <tlegge@rogers.com>
*
*    Portions of this code based on:
*	r8169.c: A RealTek RTL-8169 Gigabit Ethernet driver
*		for Linux kernel 2.4.x.
*
*    Written 2002 ShuChen <shuchen@realtek.com.tw>
*	  See Linux Driver for full information
*
*    Linux Driver Version 1.27a, 10.02.2002
*
*    Thanks to:
*	Jean Chen of RealTek Semiconductor Corp. for
*	providing the evaluation NIC used to develop
*	this driver.  RealTek's support for Etherboot
*	is appreciated.
*
*    REVISION HISTORY:
*    ================
*
*    v1.0	11-26-2003	timlegge	Initial port of Linux driver
*    v1.5	01-17-2004	timlegge	Initial driver output cleanup
*    v1.6	05-26-2026	Spacemit	Refactor to fit current U-Boot
*
*    Indent Options: indent -kr -i8
***************************************************************************/
/*
 * 26 August 2006 Mihai Georgian <u-boot@linuxnotincluded.org.uk>
 * Modified to use le32_to_cpu and cpu_to_le32 properly
 */
#include <cpu_func.h>
#include <dm.h>
#include <errno.h>
#include <log.h>
#include <malloc.h>
#include <memalign.h>
#include <net.h>
#include <asm/cache.h>
#include <asm/io.h>
#include <pci.h>
#include <linux/delay.h>
#include <linux/printk.h>

#undef DEBUG_RTL8169
#undef DEBUG_RTL8169_TX
#undef DEBUG_RTL8169_RX

#define drv_version "v1.6"
#define drv_date "05-26-2026"

/* Condensed operations for readability. */
#define currticks()	get_timer(0)

/* MAC address length*/
#define MAC_ADDR_LEN	6

#define RX_FIFO_THRESH	7	/* 7 means NO threshold, Rx buffer level before first PCI xfer.	 */
#define RX_DMA_BURST	6	/* Maximum PCI burst, '6' is 1024 */
#define TX_DMA_BURST	6	/* Maximum PCI burst, '6' is 1024 */
#define EarlyTxThld	0x3F	/* 0x3F means NO early transmit */
#define RxPacketMaxSize 0x0800	/* Maximum size supported is 16K-1 */
#define InterFrameGap	0x03	/* 3 means InterFrameGap = the shortest one */

#define NUM_TX_DESC	1	/* Number of Tx descriptor registers */
#define NUM_RX_DESC	128

#define RX_BUF_SIZE	1536	/* Rx Buffer size */
#define RX_COPY_BUF_SIZE RX_BUF_SIZE

#define TX_TIMEOUT  (6*HZ)

/* write/read MMIO register. Notice: {read,write}[wl] do the necessary swapping */
#define RTL_W8(priv, reg, val8)\
	writeb((val8), (void *)((priv)->iobase + (reg)))
#define RTL_W16(priv, reg, val16)\
	writew((val16), (void *)((priv)->iobase + (reg)))
#define RTL_W32(priv, reg, val32)\
	writel((val32), (void *)((priv)->iobase + (reg)))
#define RTL_R8(priv, reg)\
	readb((void *)((priv)->iobase + (reg)))
#define RTL_R16(priv, reg)\
	readw((void *)((priv)->iobase + (reg)))
#define RTL_R32(priv, reg)\
	readl((void *)((priv)->iobase + (reg)))

enum RTL8169_registers {
	MAC0 = 0,		/* Ethernet hardware address. */
	MAR0 = 8,		/* Multicast filter. */
	TxDescStartAddrLow = 0x20,
	TxDescStartAddrHigh = 0x24,
	TxHDescStartAddrLow = 0x28,
	TxHDescStartAddrHigh = 0x2c,
	FLASH = 0x30,
	ERSR = 0x36,
	ChipCmd = 0x37,
	TxPoll_8169 = 0x38,
	IntrMask_8169 = 0x3C,
	IntrStatus_8169 = 0x3E,
	TxConfig = 0x40,
	RxConfig = 0x44,
	RxMissed = 0x4C,
	Cfg9346 = 0x50,
	Config0 = 0x51,
	Config1 = 0x52,
	Config2 = 0x53,
	Config3 = 0x54,
	Config4 = 0x55,
	Config5 = 0x56,
	MultiIntr = 0x5C,
	PHYAR = 0x60,
	TBICSR = 0x64,
	TBI_ANAR = 0x68,
	TBI_LPAR = 0x6A,
	PHYstatus = 0x6C,
	RxMaxSize = 0xDA,
	CPlusCmd = 0xE0,
	RxDescStartAddrLow = 0xE4,
	RxDescStartAddrHigh = 0xE8,
	EarlyTxThres = 0xEC,
	FuncEvent = 0xF0,
	FuncEventMask = 0xF4,
	FuncPresetState = 0xF8,
	FuncForceEvent = 0xFC,
};

enum RTL8125_registers {
	IntrMask_8125 = 0x38,
	IntrStatus_8125 = 0x3C,
	TxPoll_8125 = 0x90,
};

enum RTL8169_register_content {
	/*InterruptStatusBits */
	SYSErr = 0x8000,
	PCSTimeout = 0x4000,
	SWInt = 0x0100,
	TxDescUnavail = 0x80,
	RxFIFOOver = 0x40,
	RxUnderrun = 0x20,
	RxOverflow = 0x10,
	TxErr = 0x08,
	TxOK = 0x04,
	RxErr = 0x02,
	RxOK = 0x01,

	/*RxStatusDesc */
	RxRES = 0x00200000,
	RxCRC = 0x00080000,
	RxRUNT = 0x00100000,
	RxRWT = 0x00400000,

	/*ChipCmdBits */
	CmdReset = 0x10,
	CmdRxEnb = 0x08,
	CmdTxEnb = 0x04,
	RxBufEmpty = 0x01,

	/*Cfg9346Bits */
	Cfg9346_Lock = 0x00,
	Cfg9346_Unlock = 0xC0,

	/*rx_mode_bits */
	AcceptErr = 0x20,
	AcceptRunt = 0x10,
	AcceptBroadcast = 0x08,
	AcceptMulticast = 0x04,
	AcceptMyPhys = 0x02,
	AcceptAllPhys = 0x01,

	/*RxConfigBits */
	RxCfgFIFOShift = 13,
	RxCfgDMAShift = 8,

	/*TxConfigBits */
	TxInterFrameGapShift = 24,
	TxDMAShift = 8,		/* DMA burst value (0-7) is shift this many bits */

	/*rtl8169_PHYstatus */
	TBI_Enable = 0x80,
	TxFlowCtrl = 0x40,
	RxFlowCtrl = 0x20,
	_1000bpsF = 0x10,
	_100bps = 0x08,
	_10bps = 0x04,
	LinkStatus = 0x02,
	FullDup = 0x01,

	/*GIGABIT_PHY_registers */
	PHY_CTRL_REG = 0,
	PHY_STAT_REG = 1,
	PHY_AUTO_NEGO_REG = 4,
	PHY_1000_CTRL_REG = 9,

	/*GIGABIT_PHY_REG_BIT */
	PHY_Restart_Auto_Nego = 0x0200,
	PHY_Enable_Auto_Nego = 0x1000,

	/* PHY_STAT_REG = 1; */
	PHY_Auto_Nego_Comp = 0x0020,

	/* PHY_AUTO_NEGO_REG = 4; */
	PHY_Cap_10_Half = 0x0020,
	PHY_Cap_10_Full = 0x0040,
	PHY_Cap_100_Half = 0x0080,
	PHY_Cap_100_Full = 0x0100,

	/* PHY_1000_CTRL_REG = 9; */
	PHY_Cap_1000_Full = 0x0200,

	PHY_Cap_Null = 0x0,

	/*_MediaType*/
	_10_Half = 0x01,
	_10_Full = 0x02,
	_100_Half = 0x04,
	_100_Full = 0x08,
	_1000_Full = 0x10,

	/*_TBICSRBit*/
	TBILinkOK = 0x02000000,

	/* FuncEvent/Misc */
	RxDv_Gated_En = 0x80000,
};

static struct {
	const char *name;
	u8 version;		/* depend on RTL8169 docs */
	u32 RxConfigMask;	/* should clear the bits supported by this chip */
} rtl_chip_info[] = {
	{"RTL-8169", 0x00, 0xff7e1880,},
	{"RTL-8169", 0x04, 0xff7e1880,},
	{"RTL-8169", 0x00, 0xff7e1880,},
	{"RTL-8169s/8110s",	0x02, 0xff7e1880,},
	{"RTL-8169s/8110s",	0x04, 0xff7e1880,},
	{"RTL-8169sb/8110sb",	0x10, 0xff7e1880,},
	{"RTL-8169sc/8110sc",	0x18, 0xff7e1880,},
	{"RTL-8168b/8111sb",	0x30, 0xff7e1880,},
	{"RTL-8168b/8111sb",	0x38, 0xff7e1880,},
	{"RTL-8168c/8111c",	0x3c, 0xff7e1880,},
	{"RTL-8168d/8111d",	0x28, 0xff7e1880,},
	{"RTL-8168evl/8111evl",	0x2e, 0xff7e1880,},
	{"RTL-8168/8111g",	0x4c, 0xff7e1880,},
	{"RTL-8101e",		0x34, 0xff7e1880,},
	{"RTL-8100e",		0x32, 0xff7e1880,},
	{"RTL-8168h/8111h",	0x54, 0xff7e1880,},
	{"RTL-8125B",		0x64, 0xff7e1880,},
	{"RTL-8125d",		0x6a, 0xff7e5880,},
};

enum _DescStatusBit {
	OWNbit = 0x80000000,
	EORbit = 0x40000000,
	FSbit = 0x20000000,
	LSbit = 0x10000000,
};

struct TxDesc {
	u32 status;
	u32 vlan_tag;
	u32 buf_addr;
	u32 buf_Haddr;
};

struct RxDesc {
	u32 status;
	u32 vlan_tag;
	u32 buf_addr;
	u32 buf_Haddr;
};

#define RTL8169_DESC_SIZE 16

#if ARCH_DMA_MINALIGN > 256
#  define RTL8169_ALIGN ARCH_DMA_MINALIGN
#else
#  define RTL8169_ALIGN 256
#endif

/*
 * Warn if the cache-line size is larger than the descriptor size. In such
 * cases the driver will likely fail because the CPU needs to flush the cache
 * when requeuing RX buffers, therefore descriptors written by the hardware
 * may be discarded.
 *
 * This can be fixed by defining CONFIG_SYS_NONCACHED_MEMORY which will cause
 * the driver to allocate descriptors from a pool of non-cached memory.
 *
 * Hardware maintain D-cache coherency in RISC-V architecture.
 */
#if RTL8169_DESC_SIZE < ARCH_DMA_MINALIGN
#if !defined(CONFIG_SYS_NONCACHED_MEMORY) && \
	!CONFIG_IS_ENABLED(SYS_DCACHE_OFF) && !defined(CONFIG_X86) && !defined(CONFIG_RISCV)
#warning cache-line size is larger than descriptor size
#endif
#endif

struct rtl8169_private {
	ulong iobase;
	int chipset;
	unsigned long cur_rx;	/* Index into the Rx descriptor buffer of next Rx pkt. */
	unsigned long cur_tx;	/* Index into the Tx descriptor buffer of next Rx pkt. */
	struct TxDesc *TxDescArray;	/* Index of 256-alignment Tx Descriptor buffer */
	struct RxDesc *RxDescArray;	/* Index of 256-alignment Rx Descriptor buffer */

	unsigned char *RxBufferRing[NUM_RX_DESC];	/* Index of Rx Buffer array */
	unsigned char *Tx_skbuff[NUM_TX_DESC];

	u8 *txb;
	u8 *rxb;
	u8 *rxdata;
};

static const unsigned int rtl8169_rx_config =
    (RX_FIFO_THRESH << RxCfgFIFOShift) | (RX_DMA_BURST << RxCfgDMAShift);

static struct pci_device_id supported[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8125) },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8161) },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8167) },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8168) },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8169) },
	{}
};

static void mdio_write(struct rtl8169_private *priv, int RegAddr, int value)
{
	int i;

	RTL_W32(priv, PHYAR, 0x80000000 | (RegAddr & 0xFF) << 16 | value);
	udelay(1000);

	for (i = 2000; i > 0; i--) {
		/* Check if the RTL8169 has completed writing to the specified MII register */
		if (!(RTL_R32(priv, PHYAR) & 0x80000000)) {
			break;
		} else {
			udelay(100);
		}
	}
}

static int mdio_read(struct rtl8169_private *priv, int RegAddr)
{
	int i, value = -1;

	RTL_W32(priv, PHYAR, 0x0 | (RegAddr & 0xFF) << 16);
	udelay(1000);

	for (i = 2000; i > 0; i--) {
		/* Check if the RTL8169 has completed retrieving data from the specified MII register */
		if (RTL_R32(priv, PHYAR) & 0x80000000) {
			value = (int) (RTL_R32(priv, PHYAR) & 0xFFFF);
			break;
		} else {
			udelay(100);
		}
	}
	return value;
}

static int rtl8169_init_board(struct rtl8169_private *priv, const char *name)
{
	int i;
	u32 tmp;

#ifdef DEBUG_RTL8169
	printf ("%s\n", __FUNCTION__);
#endif

	/* Soft reset the chip. */
	RTL_W8(priv, ChipCmd, CmdReset);

	/* Check that the chip has finished the reset. */
	for (i = 1000; i > 0; i--)
		if ((RTL_R8(priv, ChipCmd) & CmdReset) == 0)
			break;
		else
			udelay(10);

	/* identify chip attached to board */
	tmp = RTL_R32(priv, TxConfig);
	tmp = ((tmp & 0x7c000000) + ((tmp & 0x00800000) << 2)) >> 24;

	for (i = ARRAY_SIZE(rtl_chip_info) - 1; i >= 0; i--){
		if (tmp == rtl_chip_info[i].version) {
			priv->chipset = i;
			goto match;
		}
	}

	/* if unknown chip, assume array element #0, original RTL-8169 in this case */
	printf("PCI device %s: unknown chip version, assuming RTL-8169\n",
	       name);
	printf("PCI device: TxConfig = 0x%lX\n", (unsigned long) RTL_R32(priv, TxConfig));
	priv->chipset = 0;

match:
	return 0;
}

/*
 * TX and RX descriptors are 16 bytes. This causes problems with the cache
 * maintenance on CPUs where the cache-line size exceeds the size of these
 * descriptors. What will happen is that when the driver receives a packet
 * it will be immediately requeued for the hardware to reuse. The CPU will
 * therefore need to flush the cache-line containing the descriptor, which
 * will cause all other descriptors in the same cache-line to be flushed
 * along with it. If one of those descriptors had been written to by the
 * device those changes (and the associated packet) will be lost.
 *
 * To work around this, we make use of non-cached memory if available. If
 * descriptors are mapped uncached there's no need to manually flush them
 * or invalidate them.
 *
 * Note that this only applies to descriptors. The packet data buffers do
 * not have the same constraints since they are 1536 bytes large, so they
 * are unlikely to share cache-lines.
 */
static void *rtl_alloc_descs(unsigned int num)
{
	size_t size = num * RTL8169_DESC_SIZE;

#ifdef CONFIG_SYS_NONCACHED_MEMORY
	return (void *)noncached_alloc(size, RTL8169_ALIGN);
#else
	return memalign(RTL8169_ALIGN, size);
#endif
}

static void rtl_free_desc(void *desc)
{
#ifndef CONFIG_SYS_NONCACHED_MEMORY
	if (desc)
		free(desc);
#endif
}

static void rtl_free_buffers(struct rtl8169_private *priv)
{
	if (priv->txb) {
		free(priv->txb);
		priv->txb = NULL;
	}

	if (priv->rxb) {
		free(priv->rxb);
		priv->rxb = NULL;
	}

	if (priv->rxdata) {
		free(priv->rxdata);
		priv->rxdata = NULL;
	}
}

static int rtl_alloc_buffers(struct rtl8169_private *priv)
{
	priv->txb = memalign(RTL8169_ALIGN,
			       NUM_TX_DESC * RX_BUF_SIZE);
	if (!priv->txb)
		return -ENOMEM;

	priv->rxb = memalign(RTL8169_ALIGN,
			       NUM_RX_DESC * RX_BUF_SIZE);
	if (!priv->rxb)
		goto err_free_txb;

	priv->rxdata = memalign(ARCH_DMA_MINALIGN, RX_COPY_BUF_SIZE);
	if (!priv->rxdata)
		goto err_free_rxb;

	return 0;

err_free_rxb:
	free(priv->rxb);
	priv->rxb = NULL;
err_free_txb:
	free(priv->txb);
	priv->txb = NULL;
	return -ENOMEM;
}

static inline u32 rtl_lo32(phys_addr_t addr)
{
	return (u32)addr;
}

static inline u32 rtl_hi32(phys_addr_t addr)
{
	return (u32)((u64)addr >> 32);
}

static inline void rtl_set_desc_addr(u32 *lo, u32 *hi, phys_addr_t addr)
{
	*lo = cpu_to_le32(rtl_lo32(addr));
	*hi = cpu_to_le32(rtl_hi32(addr));
}

/*
 * Cache maintenance functions. These are simple wrappers around the more
 * general purpose flush_cache() and invalidate_dcache_range() functions.
 */

static void rtl_inval_rx_desc(struct RxDesc *desc)
{
#ifndef CONFIG_SYS_NONCACHED_MEMORY
	unsigned long start = (unsigned long)desc & ~(ARCH_DMA_MINALIGN - 1);
	unsigned long end = ALIGN(start + sizeof(*desc), ARCH_DMA_MINALIGN);

	invalidate_dcache_range(start, end);
#endif
}

static void rtl_flush_rx_desc(struct RxDesc *desc)
{
#ifndef CONFIG_SYS_NONCACHED_MEMORY
	unsigned long start = (unsigned long)desc & ~(ARCH_DMA_MINALIGN - 1);
	unsigned long end = ALIGN((unsigned long)desc + sizeof(*desc),
				  ARCH_DMA_MINALIGN);

	flush_cache(start, end - start);
#endif
}

static void rtl_inval_tx_desc(struct TxDesc *desc)
{
#ifndef CONFIG_SYS_NONCACHED_MEMORY
	unsigned long start = (unsigned long)desc & ~(ARCH_DMA_MINALIGN - 1);
	unsigned long end = ALIGN(start + sizeof(*desc), ARCH_DMA_MINALIGN);

	invalidate_dcache_range(start, end);
#endif
}

static void rtl_flush_tx_desc(struct TxDesc *desc)
{
#ifndef CONFIG_SYS_NONCACHED_MEMORY
	unsigned long start = (unsigned long)desc & ~(ARCH_DMA_MINALIGN - 1);
	unsigned long end = ALIGN((unsigned long)desc + sizeof(*desc),
				  ARCH_DMA_MINALIGN);

	flush_cache(start, end - start);
#endif
}

static void rtl_inval_buffer(void *buf, size_t size)
{
	unsigned long start = (unsigned long)buf & ~(ARCH_DMA_MINALIGN - 1);
	unsigned long end = ALIGN((unsigned long)buf + size, ARCH_DMA_MINALIGN);

	invalidate_dcache_range(start, end);
}

static void rtl_flush_buffer(void *buf, size_t size)
{
	unsigned long start = (unsigned long)buf & ~(ARCH_DMA_MINALIGN - 1);
	unsigned long end = ALIGN((unsigned long)buf + size, ARCH_DMA_MINALIGN);

	flush_cache(start, end - start);
}

static void rtl_requeue_rx_desc(struct udevice *dev,
				 struct rtl8169_private *priv, int entry)
{
	struct RxDesc *desc = &priv->RxDescArray[entry];
	phys_addr_t dma;

	/*
	 * RX descriptors are only 16 bytes. Several descriptors may share one
	 * cache line. Invalidate the cache line before updating this descriptor
	 * so that flushing it later will not write back stale neighbour
	 * descriptors over hardware updates.
	 */
	rtl_inval_rx_desc(desc);

	dma = dm_pci_mem_to_phys(dev,
		(pci_addr_t)(unsigned long)priv->RxBufferRing[entry]);

	rtl_set_desc_addr(&desc->buf_addr, &desc->buf_Haddr, dma);

	desc->vlan_tag = 0;

	/*
	 * Status contains OWNbit. It must be written after buffer address fields
	 * are prepared.
	 */
	if (entry == NUM_RX_DESC - 1)
		desc->status = cpu_to_le32((OWNbit | EORbit) + RX_BUF_SIZE);
	else
		desc->status = cpu_to_le32(OWNbit + RX_BUF_SIZE);

	rtl_flush_rx_desc(desc);
}
/**************************************************************************
RECV - Receive a frame
***************************************************************************/
static int rtl_recv_common(struct udevice *dev, struct rtl8169_private *priv,
			   uchar **packetp)
{
	struct pci_child_plat *pplat = dev_get_parent_plat(dev);
	int cur_rx;
	int length = 0;
	u32 status;

#ifdef DEBUG_RTL8169_RX
	printf("%s\n", __FUNCTION__);
#endif

	cur_rx = priv->cur_rx;

	rtl_inval_rx_desc(&priv->RxDescArray[cur_rx]);

	status = le32_to_cpu(priv->RxDescArray[cur_rx].status);

	if ((status & OWNbit) == 0) {
		if (!(status & RxRES)) {
			length = (int)(status & 0x00001FFF) - 4;

			if (length > 0 && length <= RX_BUF_SIZE) {
				rtl_inval_buffer(priv->RxBufferRing[cur_rx], length);
				memcpy(priv->rxdata, priv->RxBufferRing[cur_rx], length);
				*packetp = priv->rxdata;
			} else {
				debug("rtl8169: invalid RX length %d, status=0x%08x\n",
				      length, status);
				length = 0;
			}

			rtl_requeue_rx_desc(dev, priv, cur_rx);
		} else {
			debug("rtl8169: RX descriptor error, status=0x%08x\n",
			      status);

			/*
			 * Drop the bad packet, but always return the descriptor
			 * to hardware. Otherwise long/burst transfers can lose
			 * RX descriptors and eventually stall.
			 */
			rtl_requeue_rx_desc(dev, priv, cur_rx);
			length = 0;
		}

		cur_rx = (cur_rx + 1) % NUM_RX_DESC;
		priv->cur_rx = cur_rx;

		return length;
	} else {
		u32 IntrStatus = IntrStatus_8169;
		ushort sts;

		if (pplat->device == 0x8125)
			IntrStatus = IntrStatus_8125;

		sts = RTL_R16(priv, IntrStatus);
		RTL_W16(priv, IntrStatus, sts & ~(TxErr | RxErr | SYSErr));
		udelay(100);
	}

	priv->cur_rx = cur_rx;
	return 0;
}

int rtl8169_eth_recv(struct udevice *dev, int flags, uchar **packetp)
{
	struct rtl8169_private *priv = dev_get_priv(dev);

	return rtl_recv_common(dev, priv, packetp);
}

#define HZ 1000
static void rtl8169_restart_hw(struct udevice *dev,
			       struct rtl8169_private *priv);
/**************************************************************************
SEND - Transmit a frame
***************************************************************************/
static int rtl_send_common(struct udevice *dev, struct rtl8169_private *priv,
			   void *packet, int length)
{
	/* send the packet to destination */

	struct pci_child_plat *pplat = dev_get_parent_plat(dev);
	u32 to;
	u8 *ptxb;
	int entry = priv->cur_tx % NUM_TX_DESC;
	u32 len = length;
	int ret;
	phys_addr_t dma;

#ifdef DEBUG_RTL8169_TX
	int stime = currticks();

	printf("%s\n", __FUNCTION__);
	printf("sending %d bytes\n", len);
#endif

	/* point to the current txb in case multiple tx_rings are used */
	if (length <= 0 || length > RX_BUF_SIZE)
		return -EMSGSIZE;

	ptxb = priv->Tx_skbuff[entry];
	memcpy(ptxb, (char *)packet, (int)length);

	while (len < ETH_ZLEN)
		ptxb[len++] = '\0';

	rtl_flush_buffer(ptxb, len);

	dma = dm_pci_mem_to_phys(dev, (pci_addr_t)(unsigned long)ptxb);
	rtl_set_desc_addr(&priv->TxDescArray[entry].buf_addr,
			  &priv->TxDescArray[entry].buf_Haddr,
			  dma);

	if (entry != (NUM_TX_DESC - 1)) {
		priv->TxDescArray[entry].status =
			cpu_to_le32((OWNbit | FSbit | LSbit) |
				    ((len > ETH_ZLEN) ? len : ETH_ZLEN));
	} else {
		priv->TxDescArray[entry].status =
			cpu_to_le32((OWNbit | EORbit | FSbit | LSbit) |
				    ((len > ETH_ZLEN) ? len : ETH_ZLEN));
	}

	rtl_flush_tx_desc(&priv->TxDescArray[entry]);

	if (pplat->device == 0x8125)
		RTL_W8(priv, TxPoll_8125, 0x1);	/* set polling bit */
	else
		RTL_W8(priv, TxPoll_8169, 0x40);	/* set polling bit */

	priv->cur_tx++;
	to = currticks() + TX_TIMEOUT;

	do {
		rtl_inval_tx_desc(&priv->TxDescArray[entry]);
	} while ((le32_to_cpu(priv->TxDescArray[entry].status) & OWNbit) &&
		 (currticks() < to));	/* wait */

	if (currticks() >= to) {
#ifdef DEBUG_RTL8169_TX
		puts("tx timeout/error\n");
		printf("%s elapsed time : %lu\n", __func__, currticks() - stime);
#endif
		rtl8169_restart_hw(dev, priv);
		ret = -ETIMEDOUT;
	} else {
#ifdef DEBUG_RTL8169_TX
		puts("tx done\n");
#endif
		ret = 0;
	}

	/* Delay to make net console (nc) work properly */
	udelay(20);
	return ret;
}

int rtl8169_eth_send(struct udevice *dev, void *packet, int length)
{
	struct rtl8169_private *priv = dev_get_priv(dev);

	return rtl_send_common(dev, priv, packet, length);
}

static void rtl8169_set_rx_mode(struct rtl8169_private *priv)
{
	u32 mc_filter[2];	/* Multicast hash filter */
	int rx_mode;
	u32 tmp = 0;

#ifdef DEBUG_RTL8169
	printf ("%s\n", __FUNCTION__);
#endif

	/* IFF_ALLMULTI */
	/* Too many to filter perfectly -- accept all multicasts. */
	rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
	mc_filter[1] = mc_filter[0] = 0xffffffff;

	tmp = rtl8169_rx_config | rx_mode | (RTL_R32(priv, RxConfig) &
				   rtl_chip_info[priv->chipset].RxConfigMask);

	RTL_W32(priv, RxConfig, tmp);
	RTL_W32(priv, MAR0 + 0, mc_filter[0]);
	RTL_W32(priv, MAR0 + 4, mc_filter[1]);
}

static void rtl8169_hw_start(struct udevice *dev, struct rtl8169_private *priv)
{
	u32 i;
	phys_addr_t txd;
	phys_addr_t rxd;

#ifdef DEBUG_RTL8169
	int stime = currticks();

	printf("%s\n", __FUNCTION__);
#endif

#if 0
	/* Soft reset the chip. */
	RTL_W8(priv, ChipCmd, CmdReset);

	/* Check that the chip has finished the reset. */
	for (i = 1000; i > 0; i--) {
		if ((RTL_R8(priv, ChipCmd) & CmdReset) == 0)
			break;
		else
			udelay(10);
	}
#endif

	RTL_W8(priv, Cfg9346, Cfg9346_Unlock);

	/* RTL-8169sb/8110sb or previous version */
	if (priv->chipset <= 5)
		RTL_W8(priv, ChipCmd, CmdTxEnb | CmdRxEnb);

	RTL_W8(priv, EarlyTxThres, EarlyTxThld);

	/* For gigabit rtl8169 */
	RTL_W16(priv, RxMaxSize, RxPacketMaxSize);

	/* Set Rx Config register */
	i = rtl8169_rx_config | (RTL_R32(priv, RxConfig) &
				 rtl_chip_info[priv->chipset].RxConfigMask);
	RTL_W32(priv, RxConfig, i);

	/* Set DMA burst size and Interframe Gap Time */
	RTL_W32(priv, TxConfig, (TX_DMA_BURST << TxDMAShift) |
				(InterFrameGap << TxInterFrameGapShift));

	priv->cur_rx = 0;

	txd = dm_pci_mem_to_phys(dev,
		(pci_addr_t)(unsigned long)priv->TxDescArray);
	rxd = dm_pci_mem_to_phys(dev,
		(pci_addr_t)(unsigned long)priv->RxDescArray);

	RTL_W32(priv, TxDescStartAddrLow, rtl_lo32(txd));
	RTL_W32(priv, TxDescStartAddrHigh, rtl_hi32(txd));

	RTL_W32(priv, RxDescStartAddrLow, rtl_lo32(rxd));
	RTL_W32(priv, RxDescStartAddrHigh, rtl_hi32(rxd));

	/* RTL-8169sc/8110sc or later version */
	if (priv->chipset > 5)
		RTL_W8(priv, ChipCmd, CmdTxEnb | CmdRxEnb);

	RTL_W8(priv, Cfg9346, Cfg9346_Lock);
	udelay(10);

	RTL_W32(priv, RxMissed, 0);

	rtl8169_set_rx_mode(priv);

	/* no early-rx interrupts */
	RTL_W16(priv, MultiIntr, RTL_R16(priv, MultiIntr) & 0xF000);

#ifdef DEBUG_RTL8169
	printf("%s elapsed time : %lu\n", __func__, currticks() - stime);
#endif
}

static void rtl8169_init_ring(struct udevice *dev, struct rtl8169_private *priv)
{
	int i;

#ifdef DEBUG_RTL8169
	int stime = currticks();

	printf("%s\n", __FUNCTION__);
#endif

	priv->cur_rx = 0;
	priv->cur_tx = 0;

	memset(priv->TxDescArray, 0x0, NUM_TX_DESC * sizeof(struct TxDesc));
	memset(priv->RxDescArray, 0x0, NUM_RX_DESC * sizeof(struct RxDesc));

	for (i = 0; i < NUM_TX_DESC; i++)
		priv->Tx_skbuff[i] = &priv->txb[i * RX_BUF_SIZE];

	for (i = 0; i < NUM_RX_DESC; i++) {
		priv->RxBufferRing[i] = &priv->rxb[i * RX_BUF_SIZE];
		rtl_requeue_rx_desc(dev, priv, i);
	}

#ifdef DEBUG_RTL8169
	printf("%s elapsed time : %lu\n", __func__, currticks() - stime);
#endif
}

static void rtl8169_common_start(struct udevice *dev, unsigned char *enetaddr)
{
	struct rtl8169_private *priv = dev_get_priv(dev);
	int i;

#ifdef DEBUG_RTL8169
	int stime = currticks();

	printf("%s\n", __FUNCTION__);
#endif

	rtl8169_init_ring(dev, priv);
	rtl8169_hw_start(dev, priv);

	/* Construct a perfect filter frame with the mac address as first match
	 * and broadcast for all others */
	for (i = 0; i < 192; i++)
		priv->txb[i] = 0xFF;

	priv->txb[0] = enetaddr[0];
	priv->txb[1] = enetaddr[1];
	priv->txb[2] = enetaddr[2];
	priv->txb[3] = enetaddr[3];
	priv->txb[4] = enetaddr[4];
	priv->txb[5] = enetaddr[5];

#ifdef DEBUG_RTL8169
	printf("%s elapsed time : %lu\n", __func__, currticks() - stime);
#endif
}

static int rtl8169_eth_start(struct udevice *dev)
{
	struct eth_pdata *plat = dev_get_plat(dev);

	rtl8169_common_start(dev, plat->enetaddr);

	return 0;
}

static void rtl_halt_common(struct udevice *dev)
{
	struct rtl8169_private *priv = dev_get_priv(dev);
	struct pci_child_plat *pplat = dev_get_parent_plat(dev);
	int i;

#ifdef DEBUG_RTL8169
	printf ("%s\n", __FUNCTION__);
#endif

	/* Stop the chip's Tx and Rx DMA processes. */
	RTL_W8(priv, ChipCmd, 0x00);

	/* Disable interrupts by clearing the interrupt mask. */
	if (pplat->device == 0x8125)
		RTL_W16(priv, IntrMask_8125, 0x0000);
	else
		RTL_W16(priv, IntrMask_8169, 0x0000);

	RTL_W32(priv, RxMissed, 0);

	for (i = 0; i < NUM_RX_DESC; i++) {
		priv->RxBufferRing[i] = NULL;
	}
}

void rtl8169_eth_stop(struct udevice *dev)
{
	rtl_halt_common(dev);
}

static int rtl8169_write_hwaddr(struct udevice *dev)
{
	struct rtl8169_private *priv = dev_get_priv(dev);
	struct eth_pdata *plat = dev_get_plat(dev);
	unsigned int i;

	RTL_W8(priv, Cfg9346, Cfg9346_Unlock);

	for (i = 0; i < MAC_ADDR_LEN; i++)
		RTL_W8(priv, MAC0 + i, plat->enetaddr[i]);

	RTL_W8(priv, Cfg9346, Cfg9346_Lock);

	return 0;
}

static void rtl8169_restart_hw(struct udevice *dev,
			       struct rtl8169_private *priv)
{
	rtl_halt_common(dev);
	rtl8169_init_ring(dev, priv);
	rtl8169_hw_start(dev, priv);
}

/**************************************************************************
INIT - Look for an adapter, this routine's visible to the outside
***************************************************************************/

static int rtl_init(struct udevice *dev)
{
	struct rtl8169_private *priv = dev_get_priv(dev);
	struct eth_pdata *plat = dev_get_plat(dev);
	unsigned char *enetaddr = plat->enetaddr;
	int i, rc;
	int option = -1, Cap10_100 = 0, Cap1000 = 0;

#ifdef DEBUG_RTL8169
	printf ("%s\n", __FUNCTION__);
#endif
	rc = rtl_alloc_buffers(priv);
	if (rc)
		return rc;

	rc = rtl8169_init_board(priv, dev->name);
	if (rc)
		goto err_free_buffers;

	/* Get MAC address.  FIXME: read EEPROM */
	for (i = 0; i < MAC_ADDR_LEN; i++)
		enetaddr[i] = RTL_R8(priv, MAC0 + i);

#ifdef DEBUG_RTL8169
	printf("chipset = %d\n", priv->chipset);
	printf("MAC Address");
	for (i = 0; i < MAC_ADDR_LEN; i++)
		printf(":%02x", enetaddr[i]);
	putc('\n');
#endif

#ifdef DEBUG_RTL8169
	/* Print out some hardware info */
	printf("%s: at ioaddr 0x%lx\n", dev->name, priv->iobase);
#endif

	/* if TBI is not endbled */
	if (!(RTL_R8(priv, PHYstatus) & TBI_Enable)) {
		int val = mdio_read(priv, PHY_AUTO_NEGO_REG);

		/* Force RTL8169 in 10/100/1000 Full/Half mode. */
		if (option > 0) {
#ifdef DEBUG_RTL8169
			printf("%s: Force-mode Enabled.\n", dev->name);
#endif
			Cap10_100 = 0, Cap1000 = 0;
			switch (option) {
			case _10_Half:
				Cap10_100 = PHY_Cap_10_Half;
				Cap1000 = PHY_Cap_Null;
				break;
			case _10_Full:
				Cap10_100 = PHY_Cap_10_Full;
				Cap1000 = PHY_Cap_Null;
				break;
			case _100_Half:
				Cap10_100 = PHY_Cap_100_Half;
				Cap1000 = PHY_Cap_Null;
				break;
			case _100_Full:
				Cap10_100 = PHY_Cap_100_Full;
				Cap1000 = PHY_Cap_Null;
				break;
			case _1000_Full:
				Cap10_100 = PHY_Cap_Null;
				Cap1000 = PHY_Cap_1000_Full;
				break;
			default:
				break;
			}
			mdio_write(priv, PHY_AUTO_NEGO_REG, Cap10_100 | (val & 0x1F));	/* leave PHY_AUTO_NEGO_REG bit4:0 unchanged */
			mdio_write(priv, PHY_1000_CTRL_REG, Cap1000);
		} else {
#ifdef DEBUG_RTL8169
			printf("%s: Auto-negotiation Enabled.\n",
			       dev->name);
#endif
			/* enable 10/100 Full/Half Mode, leave PHY_AUTO_NEGO_REG bit4:0 unchanged */
			mdio_write(priv, PHY_AUTO_NEGO_REG,
				   PHY_Cap_10_Half | PHY_Cap_10_Full |
				   PHY_Cap_100_Half | PHY_Cap_100_Full |
				   (val & 0x1F));

			/* enable 1000 Full Mode */
			mdio_write(priv, PHY_1000_CTRL_REG, PHY_Cap_1000_Full);

		}

		/* Enable auto-negotiation and restart auto-nigotiation */
		mdio_write(priv, PHY_CTRL_REG,
			   PHY_Enable_Auto_Nego | PHY_Restart_Auto_Nego);
		udelay(100);

		/* wait for auto-negotiation process */
		for (i = 10000; i > 0; i--) {
			/* check if auto-negotiation complete */
			if (mdio_read(priv, PHY_STAT_REG) & PHY_Auto_Nego_Comp) {
				udelay(100);
				option = RTL_R8(priv, PHYstatus);
				if (option & _1000bpsF) {
#ifdef DEBUG_RTL8169
					printf("%s: 1000Mbps Full-duplex operation.\n",
					       dev->name);
#endif
				} else {
#ifdef DEBUG_RTL8169
					printf("%s: %sMbps %s-duplex operation.\n",
					       dev->name,
					       (option & _100bps) ? "100" :
					       "10",
					       (option & FullDup) ? "Full" :
					       "Half");
#endif
				}
				break;
			} else {
				udelay(100);
			}
		}		/* end for-loop to wait for auto-negotiation process */

	} else {
		udelay(100);
#ifdef DEBUG_RTL8169
		printf
		    ("%s: 1000Mbps Full-duplex operation, TBI Link %s!\n",
		     dev->name,
		     (RTL_R32(priv, TBICSR) & TBILinkOK) ? "OK" : "Failed");
#endif
	}

	priv->RxDescArray = rtl_alloc_descs(NUM_RX_DESC);
	if (!priv->RxDescArray) {
		rc = -ENOMEM;
		goto err_free_buffers;
	}

	priv->TxDescArray = rtl_alloc_descs(NUM_TX_DESC);
	if (!priv->TxDescArray) {
		rc = -ENOMEM;
		goto err_free_rx_desc;
	}

	return 0;

err_free_rx_desc:
	rtl_free_desc(priv->RxDescArray);
	priv->RxDescArray = NULL;
err_free_buffers:
	rtl_free_buffers(priv);
	return rc;
}

static int rtl8169_eth_probe(struct udevice *dev)
{
	struct pci_child_plat *pplat = dev_get_parent_plat(dev);
	struct rtl8169_private *priv = dev_get_priv(dev);
	int region;
	int ret;
	u32 val;

	/*
	 * Keep the old static-BSS semantics. Some early/SPL paths are less
	 * forgiving if driver-private memory is not already zeroed before probe.
	 */
	memset(priv, 0, sizeof(*priv));

	switch (pplat->device) {
	case 0x8125:
	case 0x8161:
	case 0x8168:
		region = 2;
		break;
	default:
		region = 1;
		break;
	}

	priv->iobase = (ulong)dm_pci_map_bar(dev,
					     PCI_BASE_ADDRESS_0 + region * 4,
					     0, 0,
					     PCI_REGION_TYPE, PCI_REGION_MEM);
	if (!priv->iobase)
		return -ENODEV;

	debug("rtl8169: REALTEK RTL8169 @0x%lx\n", priv->iobase);
	ret = rtl_init(dev);
	if (ret < 0) {
		printf(pr_fmt("failed to initialize card: %d\n"), ret);
		return ret;
	}

	/*
	 * WAR for DHCP failure after rebooting from kernel.
	 * Clear RxDv_Gated_En bit which was set by kernel driver.
	 * Without this, U-Boot can't get an IP via DHCP.
	 * Register (FuncEvent, aka MISC) and RXDV_GATED_EN bit are from
	 * the r8169.c kernel driver.
	 */

	val = RTL_R32(priv, FuncEvent);
	debug("%s: FuncEvent/Misc (0xF0) = 0x%08X\n", __func__, val);
	val &= ~RxDv_Gated_En;
	RTL_W32(priv, FuncEvent, val);

	return 0;
}

static void rtl_deinit(struct udevice *dev)
{
	struct rtl8169_private *priv = dev_get_priv(dev);

	rtl_halt_common(dev);

	rtl_free_desc(priv->TxDescArray);
	priv->TxDescArray = NULL;

	rtl_free_desc(priv->RxDescArray);
	priv->RxDescArray = NULL;

	rtl_free_buffers(priv);
}

static int rtl8169_eth_remove(struct udevice *dev)
{
	rtl_deinit(dev);
	return 0;
}

static const struct eth_ops rtl8169_eth_ops = {
	.start	= rtl8169_eth_start,
	.send	= rtl8169_eth_send,
	.recv	= rtl8169_eth_recv,
	.stop	= rtl8169_eth_stop,
	.write_hwaddr = rtl8169_write_hwaddr,
};

static const struct udevice_id rtl8169_eth_ids[] = {
	{ .compatible = "realtek,rtl8169" },
	{ }
};

U_BOOT_DRIVER(eth_rtl8169) = {
	.name	= "eth_rtl8169",
	.id	= UCLASS_ETH,
	.of_match = rtl8169_eth_ids,
	.probe	= rtl8169_eth_probe,
	.remove	= rtl8169_eth_remove,
	.ops	= &rtl8169_eth_ops,
	.priv_auto	= sizeof(struct rtl8169_private),
	.plat_auto	= sizeof(struct eth_pdata),
};

U_BOOT_PCI_DEVICE(eth_rtl8169, supported);