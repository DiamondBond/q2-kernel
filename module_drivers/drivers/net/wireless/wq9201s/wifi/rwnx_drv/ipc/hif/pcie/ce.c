/** @file ce.c
 *
 *  @brief This file contains CopyEngine related functions.
 *
 * Copyright (C) 2016-2023, WuQi Ltd.
 *
 * This software file (the "File") is distributed by WuQi Ltd.
 * Under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 *
 */

#include "pcie.h"
#include "wq_log.h"

/*
 * private hw_reg api
*/
#define REG_FIELD_GET(name, dword) ((dword & name##_MASK) >> name##_OFFSET)
#define REG_FIELD_SET(name, dword, value)                                      \
	do {                                                                   \
		(dword) &= ~name##_MASK;                                       \
		(dword) |= (((value) << name##_OFFSET) & name##_MASK);         \
	} while (0)

#define WQ_PCIE_RD32 wq_pcie_read32
#define WQ_PCIE_WR32 wq_pcie_write32

#define WQ_PCIE_REG_FIELD_RD(wq_pcie, offset, name)                            \
	({                                                                     \
		u32 val = WQ_PCIE_RD32(wq_pcie, offset);                       \
		REG_FIELD_GET(name, val);                                      \
	})

#define WQ_PCIE_REG_FIELD_WR(wq_pcie, offset, name, val)                       \
	do {                                                                   \
		u32 _val = WQ_PCIE_RD32(wq_pcie, offset);                      \
		REG_FIELD_SET(name, _val, val);                                \
		WQ_PCIE_WR32(wq_pcie, offset, _val);                           \
	} while (0)

#define WQ_PCIE_MAY_RD_ERR 0xffffffff

/*
 * private hw_ce api
*/
#include "ce_reg.h"
#include "host_reg_base.h"
#include "wifi_ahb_reg.h"

#define HW_CE_CHN_CFG_DEFAULT 0x00001000
#define HW_CE_CHN_CFG_COMMON 0x2018107e

#define HW_CE_RING_DEPTH_LOG2_MAX 13
#define HW_CE_RING_DEPTH_LOG2_INVALID (HW_CE_RING_DEPTH_LOG2_MAX + 1)

#define HW_CE_DESC_FLAG_BUF_LENGTH_OFFSET 0
#define HW_CE_DESC_FLAG_BUF_LENGTH_MASK 0x000fffff

#define HW_CE_CON_REG_DUMP_WORD_SIZE 48
#define HW_CE_CHN_REG_DUMP_WORD_SIZE 40

#define CE_DBG_BUS_SEL_MAX 16
#define CE_DBG_BUS_SEL_SRC_DESC_WORD0 2
#define CE_DBG_BUS_SEL_SRC_DESC_WORD1 3
#define CE_DBG_BUS_SEL_DST_DESC_WORD0 4
#define CE_DBG_BUS_SEL_DST_DESC_WORD1 5
#define CE_DBG_BUS_SEL_CHN_IDLE_STATUS 11

#define HW_CE_CHN_STATE_REG_DUMP_WORD_SIZE 16
#define HW_CE_CHN_INTERRUPT_REG_DUMP_WORD_SIZE 8
#define HW_CE_CHN_DBG_BUS_REG_DUMP_WORD_SIZE CE_DBG_BUS_SEL_MAX

#ifdef CONFIG_WAR_CE_INTR_FAST_THAN_DATA
#define WQ_CE_DESC_FLAG_METADATA_PRI 0xa0000000
#define WQ_CE_RECV_METADATA_WAIT_TIMEOUT 10000000
#endif

enum { CE_CHN0_INT1 = 145,
       CE_CHN1_INT1 = 169,
       CE_CHN2_INT1 = 182,
       CE_CHN3_INT1,
       CE_CHN4_INT1,
       CE_CHN5_INT1,
       CE_CHN6_INT1,
       CE_CHN7_INT1,
       CE_CHN8_INT1,
       CE_CHN9_INT1,
       CE_CHN10_INT1,
       CE_CHN11_INT1,
};

static const u32 ce_chn_to_irq_vec_map[CE_CHN_MAX] = {
	CE_CHN0_INT1, CE_CHN1_INT1, CE_CHN2_INT1,  CE_CHN3_INT1,
	CE_CHN4_INT1, CE_CHN5_INT1, CE_CHN6_INT1,  CE_CHN7_INT1,
	CE_CHN8_INT1, CE_CHN9_INT1, CE_CHN10_INT1, CE_CHN11_INT1,
};

#define CE_CHN_REG_OFFSET_BASE CFG_DMA_CHN_RX_LINK_ADDR_ADDR
#define CE_INT_REG_OFFSET_BASE CFG_DMA_CHN_RX_INT_RAW_ADDR

#define CE_REG_GROUPS_INTERVAL 0x40

#define CE_REG(chn, reg)                                                       \
	(HOST_W_CE_BASEADDR + (reg) + (chn)*CE_REG_GROUPS_INTERVAL)

#define WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, retval)                            \
	_wq_ce_ann_pcie_read_failed(wq_pcie, retval, __func__, __LINE__)

/*
 * Announce PCIe bus read may failed
 */
static void _wq_ce_ann_pcie_read_failed(struct wq_pcie *wq_pcie, u32 retval,
					const char *func, int line)
{
	WQ_DBG(DM_GENERIC, DL_ERR,
	       "%s, %d: PCIe bus read may failed, return 0x%08x\n", func, line,
	       retval);
	(void)wq_pcie_show_bus_info(wq_pcie);
	BUG_ON(1);
}

/**
 * @brief: set CE channel rx configure value
 * @param: CE channel UUID
*/
static inline void hw_ce_chn_rx_cfg_set(struct wq_pcie *wq_pcie,
					CE_CHN_UUID chn, u32 val)
{
	WQ_PCIE_WR32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_CFG_ADDR), val);
}

/**
 * @brief: clear the channel rx
 * @param: CE channel UUID
*/
static inline void hw_ce_chn_rx_clear(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	WQ_PCIE_REG_FIELD_WR(wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_CFG_ADDR),
			     CHN_RX_RING_CLR, 1);
}

/**
 * @brief: get CE channel rx ring cfg
 * @param:
        @chn: CE channel UUID
        @low: low water thrs
        @high: high water thrs
        @depth_log2: log2 of ring depth
*/
static inline void hw_ce_chn_rx_ring_cfg_get(struct wq_pcie *wq_pcie,
					     CE_CHN_UUID chn, u8 *depth_log2,
					     u16 *high, u16 *low)
{
	u32 val;

	val = WQ_PCIE_RD32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_RING_CFG_ADDR));

	if (WQ_PCIE_MAY_RD_ERR == val)
		WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, val);

	if (depth_log2 != NULL)
		*depth_log2 =
			(u8)REG_FIELD_GET(CHN_RX_RING_FIFO_DEPTH_LOG2, val);

	if (high != NULL)
		*high = (u8)REG_FIELD_GET(CHN_RX_RING_HIGH_WATER_THRS, val);

	if (low != NULL)
		*low = (u8)REG_FIELD_GET(CHN_RX_RING_LOW_WATER_THRS, val);
}

/**
 * @brief: set CE channel rx ring configure
 * @param:
        @chn: CE channel UUID
        @low: low water thrs
        @high: high water thrs
        @depth_log2: log2 of ring depth
*/
static inline void hw_ce_chn_rx_ring_cfg_set(struct wq_pcie *wq_pcie,
					     CE_CHN_UUID chn, u8 depth_log2,
					     u16 high, u16 low)
{
	u32 val = 0;

	REG_FIELD_SET(CHN_RX_RING_FIFO_DEPTH_LOG2, val, (u32)depth_log2);
	REG_FIELD_SET(CHN_RX_RING_HIGH_WATER_THRS, val, (u32)high);
	REG_FIELD_SET(CHN_RX_RING_LOW_WATER_THRS, val, (u32)low);

	WQ_PCIE_WR32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_RING_CFG_ADDR), val);
}

/**
 * @brief: set CE channel rx ring desc ring address
 * @param:
 *      @chn: CE channel UUID
 *      @rx_link_addr: rx ring desc ring address
*/
static inline void hw_ce_chn_rx_link_addr_set(struct wq_pcie *wq_pcie,
					      CE_CHN_UUID chn, u32 rx_link_addr)
{
	WQ_PCIE_WR32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_LINK_ADDR_ADDR),
		     rx_link_addr);
}

/**
 * @brief: set CE channel tx configure value
 * @param: CE channel UUID
*/
static inline void hw_ce_chn_tx_cfg_set(struct wq_pcie *wq_pcie,
					CE_CHN_UUID chn, u32 val)
{
	WQ_PCIE_WR32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_CFG_ADDR), val);
}

/**
 * @brief: clear the channel tx
 * @param: CE channel UUID
*/
static inline void hw_ce_chn_tx_clear(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	WQ_PCIE_REG_FIELD_WR(wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_CFG_ADDR),
			     CHN_TX_RING_CLR, 1);
}

/**
 * @brief: get CE channel tx ring cfg
 * @param:
        @chn: CE channel UUID
        @low: low water thrs
        @high: high water thrs
        @depth_log2: log2 of ring depth
*/
static inline void hw_ce_chn_tx_ring_cfg_get(struct wq_pcie *wq_pcie,
					     CE_CHN_UUID chn, u8 *depth_log2,
					     u16 *high, u16 *low)
{
	u32 val;

	val = WQ_PCIE_RD32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_RING_CFG_ADDR));

	if (WQ_PCIE_MAY_RD_ERR == val)
		WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, val);

	if (depth_log2 != NULL)
		*depth_log2 =
			(u8)REG_FIELD_GET(CHN_TX_RING_FIFO_DEPTH_LOG2, val);

	if (high != NULL)
		*high = (u8)REG_FIELD_GET(CHN_TX_RING_HIGH_WATER_THRS, val);

	if (low != NULL)
		*low = (u8)REG_FIELD_GET(CHN_TX_RING_LOW_WATER_THRS, val);
}

/**
 * @brief: set CE channel tx ring configure
 * @param:
        @chn: CE channel UUID
        @low: low water thrs
        @high: high water thrs
        @depth_log2: log2 of ring depth
*/
static inline void hw_ce_chn_tx_ring_cfg_set(struct wq_pcie *wq_pcie,
					     CE_CHN_UUID chn, u8 depth_log2,
					     u16 high, u16 low)
{
	u32 val = 0;

	REG_FIELD_SET(CHN_TX_RING_FIFO_DEPTH_LOG2, val, (u32)depth_log2);
	REG_FIELD_SET(CHN_TX_RING_HIGH_WATER_THRS, val, (u32)high);
	REG_FIELD_SET(CHN_TX_RING_LOW_WATER_THRS, val, (u32)low);

	WQ_PCIE_WR32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_RING_CFG_ADDR), val);
}

/**
 * @brief: set CE channel tx ring desc ring address
 * @param: CE channel UUID
*/
static inline void hw_ce_chn_tx_link_addr_set(struct wq_pcie *wq_pcie,
					      CE_CHN_UUID chn, u32 tx_link_addr)
{
	WQ_PCIE_WR32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_LINK_ADDR_ADDR),
		     tx_link_addr);
}

/**
 * @brief: get CE channel rx interrupt st register word
 * @param: CE channel UUID
 */
static inline u32 hw_ce_chn_rx_int_st_get(struct wq_pcie *wq_pcie,
					  CE_CHN_UUID chn)
{
	u32 val;

	val = WQ_PCIE_RD32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_INT_ST_ADDR));

	if (WQ_PCIE_MAY_RD_ERR == val)
		WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, val);

	return val;
}

static inline u32 hw_ce_chn_rx_int_raw_get(struct wq_pcie *wq_pcie,
					   CE_CHN_UUID chn)
{
	u32 val;

	val = WQ_PCIE_RD32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_INT_RAW_ADDR));

	if (WQ_PCIE_MAY_RD_ERR == val)
		WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, val);

	return val;
}

/**
 * @brief: set CE channel rx interrupt clr register word
 * @param:
 *      @chn: CE channel UUID
 *      @rx_int_clr: register value to write
 */
static inline void hw_ce_chn_rx_int_clr_set(struct wq_pcie *wq_pcie,
					    CE_CHN_UUID chn, u32 rx_int_clr)
{
	WQ_PCIE_WR32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_INT_CLR_ADDR),
		     rx_int_clr);
}

/**
 * @brief: get CE channel rx interrupt ena register word
 * @param: CE channel UUID
 */
static inline u32 hw_ce_chn_rx_int_ena_get(struct wq_pcie *wq_pcie,
					   CE_CHN_UUID chn)
{
	u32 val;

	val = WQ_PCIE_RD32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_INT_ENA_ADDR));

	if (WQ_PCIE_MAY_RD_ERR == val)
		WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, val);

	return val;
}

/**
 * @brief: set CE channel rx interrupt ena register word
 * @param: CE channel UUID
 */
static inline void hw_ce_chn_rx_int_ena_set(struct wq_pcie *wq_pcie,
					    CE_CHN_UUID chn, u32 rx_int_ena)
{
	WQ_PCIE_WR32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_INT_ENA_ADDR),
		     rx_int_ena);
}

/**
 * @brief: increase CE channel rx ring write pointer
 * @param:
 *      @chn: CE channel UUID
 *      @rx_widx_incr: incr value to set
*/
static inline void hw_ce_chn_rx_widx_incr_set(struct wq_pcie *wq_pcie,
					      CE_CHN_UUID chn, u32 rx_widx_incr)
{
	WQ_PCIE_WR32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_IDX_INCR_CFG_ADDR),
		     rx_widx_incr | (1u << CHN_RX_RING_WIDX_INCR_POS_OFFSET));
}

/**
 * @brief: get CE channel rx ring hardware read pointer
 * @param: CE channel UUID
*/
static inline u16 hw_ce_chn_rx_ridx_get(struct wq_pcie *wq_pcie,
					CE_CHN_UUID chn)
{
	u32 val;

	val = WQ_PCIE_RD32(wq_pcie,
			   CE_REG(chn, CFG_DMA_CHN_RX_RING_FIFO_ST0_ADDR));

	if (WQ_PCIE_MAY_RD_ERR == val)
		WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, val);

	return (u16)REG_FIELD_GET(CHN_RX_RING_FIFO_RD_IDX, val);
}

/**
 * @brief: get CE channel tx interrupt st register word
 * @param: CE channel UUID
 */
static inline u32 hw_ce_chn_tx_int_st_get(struct wq_pcie *wq_pcie,
					  CE_CHN_UUID chn)
{
	u32 val;

	val = WQ_PCIE_RD32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_INT_ST_ADDR));

	if (WQ_PCIE_MAY_RD_ERR == val)
		WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, val);

	return val;
}

static inline u32 hw_ce_chn_tx_int_raw_get(struct wq_pcie *wq_pcie,
					   CE_CHN_UUID chn)
{
	u32 val;

	val = WQ_PCIE_RD32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_INT_RAW_ADDR));

	if (WQ_PCIE_MAY_RD_ERR == val)
		WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, val);

	return val;
}

/**
 * @brief: set CE channel tx interrupt clr register word
 * @param:
 *      @chn: CE channel UUID
 *      @tx_int_clr: register value to write
 */
static inline void hw_ce_chn_tx_int_clr_set(struct wq_pcie *wq_pcie,
					    CE_CHN_UUID chn, u32 tx_int_clr)
{
	WQ_PCIE_WR32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_INT_CLR_ADDR),
		     tx_int_clr);
}

/**
 * @brief: get CE channel tx interrupt ena register word
 * @param: CE channel UUID
 */
static inline u32 hw_ce_chn_tx_int_ena_get(struct wq_pcie *wq_pcie,
					   CE_CHN_UUID chn)
{
	u32 val;

	val = WQ_PCIE_RD32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_INT_ENA_ADDR));

	if (WQ_PCIE_MAY_RD_ERR == val)
		WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, val);

	return val;
}

/**
 * @brief: set CE channel tx interrupt ena register word
 * @param: CE channel UUID
 */
static inline void hw_ce_chn_tx_int_ena_set(struct wq_pcie *wq_pcie,
					    CE_CHN_UUID chn, u32 tx_int_ena)
{
	WQ_PCIE_WR32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_INT_ENA_ADDR),
		     tx_int_ena);
}

/**
 * @brief: increase CE channel tx ring write pointer
 * @param:
 *      @chn: CE channel UUID
 *      @tx_widx_incr: incr value to set
*/
static inline void hw_ce_chn_tx_widx_incr_set(struct wq_pcie *wq_pcie,
					      CE_CHN_UUID chn, u32 tx_widx_incr)
{
	WQ_PCIE_WR32(wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_IDX_INCR_CFG_ADDR),
		     tx_widx_incr | (1u << CHN_TX_RING_WIDX_INCR_POS_OFFSET));
}

/**
 * @brief: get CE channel tx ring hardware read pointer
 * @param: CE channel UUID
*/
static inline u16 hw_ce_chn_tx_ridx_get(struct wq_pcie *wq_pcie,
					CE_CHN_UUID chn)
{
	u32 val;

	val = WQ_PCIE_RD32(wq_pcie,
			   CE_REG(chn, CFG_DMA_CHN_TX_RING_FIFO_ST0_ADDR));

	if (WQ_PCIE_MAY_RD_ERR == val)
		WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, val);

	return (u16)REG_FIELD_GET(CHN_TX_RING_FIFO_RD_IDX, val);
}

static inline u16 hw_ce_chn_tx_hw_num_get(struct wq_pcie *wq_pcie,
					  CE_CHN_UUID chn)
{
	return (u16)WQ_PCIE_REG_FIELD_RD(
		wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_RING_FIFO_ST0_ADDR),
		CHN_TX_RING_FIFO_DATA_NUM);
}

static inline u16 hw_ce_chn_rx_hw_num_get(struct wq_pcie *wq_pcie,
					  CE_CHN_UUID chn)
{
	return (u16)WQ_PCIE_REG_FIELD_RD(
		wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_RING_FIFO_ST0_ADDR),
		CHN_RX_RING_FIFO_DATA_NUM);
}

#ifdef CE_DEBUG
/**
 * @brief: get CE channel dbg bus info
 * @param: CE channel UUID
*/
static inline u32 hw_ce_chn_info_dbg_bus_get(struct wq_pcie *wq_pcie,
					     CE_CHN_UUID chn,
					     CE_DBG_BUS_SEL_IDX sel)
{
	WQ_PCIE_REG_FIELD_WR(wq_pcie, CE_REG(chn, CFG_DMA_CHN_COM_CFG_ADDR),
			     DMA_CHN_DBG_BUS_SEL, (u32)sel);
	return WQ_PCIE_REG_FIELD_RD(
		wq_pcie, CE_REG(chn, CFG_DMA_CHN_INFO_DBG_BUS_ST_ADDR),
		CHN_INFO_DBG_BUS);
}
#endif // CE_DEBUG

static inline u16 hw_ce_chn_rx_widx_get(struct wq_pcie *wq_pcie,
					CE_CHN_UUID chn)
{
	u32 val;

	val = WQ_PCIE_RD32(wq_pcie,
			   CE_REG(chn, CFG_DMA_CHN_RX_RING_FIFO_ST1_ADDR));

	if (WQ_PCIE_MAY_RD_ERR == val)
		WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, val);

	return (u16)REG_FIELD_GET(CHN_RX_RING_FIFO_WR_IDX, val);
}

static inline u16 hw_ce_chn_tx_widx_get(struct wq_pcie *wq_pcie,
					CE_CHN_UUID chn)
{
	u32 val;

	val = WQ_PCIE_RD32(wq_pcie,
			   CE_REG(chn, CFG_DMA_CHN_TX_RING_FIFO_ST1_ADDR));

	if (WQ_PCIE_MAY_RD_ERR == val)
		WQ_CE_ANN_PCIE_READ_FAILED(wq_pcie, val);

	return (u16)REG_FIELD_GET(CHN_TX_RING_FIFO_WR_IDX, val);
}

static void hw_ce_con_reg_dump(struct wq_pcie *wq_pcie,
			       CE_CON_ID con __attribute__((unused)),
			       u32 word[HW_CE_CON_REG_DUMP_WORD_SIZE])
{
	u32 beg, end;

	beg = HOST_W_CE_BASEADDR;

	end = HOST_W_CE_BASEADDR + sizeof(u32) * HW_CE_CON_REG_DUMP_WORD_SIZE;

	while (beg < end) {
		*word++ = wq_pcie_read32(wq_pcie, beg);
		beg += sizeof(u32);
	}
}

static void hw_ce_chn_reg_dump(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			       uint32_t word[HW_CE_CHN_REG_DUMP_WORD_SIZE])
{
	u8 sel;
	u32 beg, end;

	beg = HOST_W_CE_BASEADDR + CE_CHN_REG_OFFSET_BASE +
	      CE_REG_GROUPS_INTERVAL * chn;

	end = beg + sizeof(u32) * HW_CE_CHN_STATE_REG_DUMP_WORD_SIZE;

	while (beg < end) {
		*word++ = wq_pcie_read32(wq_pcie, beg);
		beg += sizeof(u32);
	}

	beg = HOST_W_CE_BASEADDR + CE_INT_REG_OFFSET_BASE +
	      CE_REG_GROUPS_INTERVAL * chn;

	end = beg + sizeof(u32) * HW_CE_CHN_INTERRUPT_REG_DUMP_WORD_SIZE;

	while (beg < end) {
		*word++ = wq_pcie_read32(wq_pcie, beg);
		beg += sizeof(u32);
	}

	for (sel = 0; sel < CE_DBG_BUS_SEL_MAX; ++sel) {
		WQ_PCIE_REG_FIELD_WR(wq_pcie,
				     CE_REG(chn, CFG_DMA_CHN_COM_CFG_ADDR),
				     DMA_CHN_DBG_BUS_SEL, (u32)sel);
		*word++ = WQ_PCIE_REG_FIELD_RD(
			wq_pcie, CE_REG(chn, CFG_DMA_CHN_INFO_DBG_BUS_ST_ADDR),
			CHN_INFO_DBG_BUS);
	}
}

static inline __attribute__((always_inline)) bool
wq_ce_is_controller_init(struct wq_pcie *wq_pcie)
{
	u32 offset = HOST_W_AHB_REG_BASEADDR + CFG_WIFI_AHB_GLB_GEN0_ADDR;
	return !!WQ_PCIE_REG_FIELD_RD(wq_pcie, offset, WIFI_CE_EB);
}

/*
 * private & public wq_ce api
*/
#define CE_ATTR_FLAG_VALID 0x00002000

#define CE_DESC_FLAG_SRC_VALID 0xffc00000
#define CE_DESC_FLAG_DST_VALID 0x01000000

#define CE_DIR_SIDE(wq_pcie, chn, dir) ((&wq_pcie->ce_states[chn].src)[dir])

#define CE_CHN_DIR_LEGAL(chn, dir) ((chn) < CE_CHN_MAX && (dir) < 2)
#define CE_DIR_EVT_LEGAL(dir, evt)                                             \
	((WQ_CE_CHN_SRC == (dir) && (evt) < CE_SRC_INT_EVT_MAX) ||             \
	 (WQ_CE_CHN_DST == (dir) && (evt) < CE_DST_INT_EVT_MAX))

#define CE_RING_DELTA(depth_mask, srt, end)                                    \
	((u16)(((end) - (srt)) & (depth_mask)))
#define CE_RING_IDX_INCR(depth_mask, idx, step)                                \
	((u16)(((idx) + (step)) & (depth_mask)))
#define CE_RING_IDX_DECR(depth_mask, idx, step)                                \
	((u16)(((idx) - (step)) & (depth_mask)))

#define CE_FIFO_SW_LFO(ring)                                                   \
	(CE_RING_DELTA((ring)->depth_mask, (ring)->sw_widx,                        \
		       *(volatile const u16 *)&(ring)->sw_ridx - 1))

#define CE_FIFO_SW_UNPROCESS(ring)                                             \
		(CE_RING_IDX_DECR((ring)->depth_mask, (ring)->sw_widx, 				   \
				   *(volatile const u16 *)&(ring)->sw_ridx))

#define CE_FIFO_SW_FULL(ring) (CE_FIFO_SW_LFO(ring) == 0)

#define CE_DUMP(fmt, ...) WQ_DBG(DM_GENERIC, DL_ERR, fmt, ##__VA_ARGS__)

#define WQ_CE_LOG(fmt, ...)                                                    \
	WQ_DBG(DM_GENERIC, DL_ERR, "%s, %d: " fmt, __func__, __LINE__,         \
	       ##__VA_ARGS__)

#ifdef CE_DEBUG
volatile u32 ce_send_completed_count[CE_CHN_MAX];
volatile u32 ce_recv_completed_count[CE_CHN_MAX];
volatile u32 ce_src_int_statistics[CE_CHN_MAX][CE_SRC_INT_EVT_MAX];
volatile u32 ce_dst_int_statistics[CE_CHN_MAX][CE_DST_INT_EVT_MAX];
#endif

int wq_ce_isr(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	const struct ce_state *state;
	wq_ce_int_cb_t send_cb, recv_cb;

	if (NULL == wq_pcie || chn >= CE_CHN_MAX)
		return -EINVAL;

	state = &wq_pcie->ce_states[chn];

	if (state->src != NULL) {
		CE_SRC_INT_EVENT evt;

		u32 rx_int_sts = hw_ce_chn_rx_int_st_get(wq_pcie, chn);
		hw_ce_chn_rx_int_clr_set(wq_pcie, chn, rx_int_sts);

		for (evt = 0; evt < CE_SRC_INT_EVT_MAX; ++evt) {
			if ((rx_int_sts & (1u << evt)) &&
			    (send_cb = state->src->int_cbs[evt]) != NULL) {
				send_cb(wq_pcie, chn);
#ifdef CE_DEBUG
				ce_src_int_statistics[chn][evt]++;
#endif
			}
		}
	}

	if (state->dst != NULL) {
		CE_DST_INT_EVENT evt;

		u32 tx_int_sts = hw_ce_chn_tx_int_st_get(wq_pcie, chn);
		hw_ce_chn_tx_int_clr_set(wq_pcie, chn, tx_int_sts);

		for (evt = 0; evt < CE_DST_INT_EVT_MAX; ++evt) {
			if ((tx_int_sts & (1u << evt)) &&
			    (recv_cb = state->dst->int_cbs[evt]) != NULL) {
				recv_cb(wq_pcie, chn);

#ifdef CE_DEBUG
				ce_dst_int_statistics[chn][evt]++;
#endif
			}
		}
	}

	return 0;
}

/**
 * @brief: get CE channel interrupt vector
 * @param: CE channel UUID
*/
u32 wq_ce_get_irq_vector_from_chn(CE_CHN_UUID chn)
{
	if (chn >= CE_CHN_MAX)
		return 0;

	return ce_chn_to_irq_vec_map[chn];
}

/**
 * @brief: get CE channel UUID from a interrupt vector
 * @param: CE interrupt vector
*/
CE_CHN_UUID wq_ce_get_chn_from_irq_vec(u32 vector)
{
#define IRQ_VEC_TO_CHN(i)                                                      \
	case CE_CHN##i##_INT1:                                                 \
		return i;                                                      \
		break;

	switch (vector) {
		IRQ_VEC_TO_CHN(0)
		IRQ_VEC_TO_CHN(1)
		IRQ_VEC_TO_CHN(2)
		IRQ_VEC_TO_CHN(3)
		IRQ_VEC_TO_CHN(4)
		IRQ_VEC_TO_CHN(5)
		IRQ_VEC_TO_CHN(6)
		IRQ_VEC_TO_CHN(7)
		IRQ_VEC_TO_CHN(8)
		IRQ_VEC_TO_CHN(9)
		IRQ_VEC_TO_CHN(10)
		IRQ_VEC_TO_CHN(11)
	default:
		return CE_CHN_MAX;
	}
}

static inline void ce_src_hardware_deinit(struct wq_pcie *wq_pcie,
					  CE_CHN_UUID chn)
{
	hw_ce_chn_rx_int_ena_set(wq_pcie, chn, 0);
	hw_ce_chn_rx_link_addr_set(wq_pcie, chn, 0);
#ifdef CE_CHN_RING_CLR_BUG_FIXED
	hw_ce_chn_rx_clear(wq_pcie, chn);
	hw_ce_chn_rx_ring_cfg_set(wq_pcie, chn, 1, 1, 0);
#else
	{
		u8 depth_log2;
		u16 depth_mask;

		hw_ce_chn_rx_ring_cfg_get(wq_pcie, chn, &depth_log2, NULL,
					  NULL);
		depth_mask = (u16)((1u << depth_log2) - 1);
		hw_ce_chn_rx_ring_cfg_set(wq_pcie, chn, depth_log2, depth_mask,
					  0);
	}
#endif // CE_CHN_RING_CLR_BUG_FIXED
	hw_ce_chn_rx_int_clr_set(wq_pcie, chn, (1u << CE_SRC_INT_EVT_MAX) - 1);
	hw_ce_chn_rx_cfg_set(wq_pcie, chn, HW_CE_CHN_CFG_DEFAULT);
}

static inline void ce_dst_hardware_deinit(struct wq_pcie *wq_pcie,
					  CE_CHN_UUID chn)
{
	hw_ce_chn_tx_int_ena_set(wq_pcie, chn, 0);
	hw_ce_chn_tx_link_addr_set(wq_pcie, chn, 0);
#ifdef CE_CHN_RING_CLR_BUG_FIXED
	hw_ce_chn_tx_clear(wq_pcie, chn);
	hw_ce_chn_tx_ring_cfg_set(wq_pcie, chn, 1, 1, 0);
#else
	{
		u8 depth_log2;
		u16 depth_mask;

		hw_ce_chn_tx_ring_cfg_get(wq_pcie, chn, &depth_log2, NULL,
					  NULL);
		depth_mask = (u16)((1u << depth_log2) - 1);
		hw_ce_chn_tx_ring_cfg_set(wq_pcie, chn, depth_log2, depth_mask,
					  0);
	}
#endif // CE_CHN_RING_CLR_BUG_FIXED
	hw_ce_chn_tx_int_clr_set(wq_pcie, chn, (1u << CE_DST_INT_EVT_MAX) - 1);
	hw_ce_chn_tx_cfg_set(wq_pcie, chn, HW_CE_CHN_CFG_DEFAULT);
}

static inline void ce_src_hardware_init(struct wq_pcie *wq_pcie,
					CE_CHN_UUID chn, u32 chn_cfg,
					const struct ce_ring *src_ring)
{
	ce_src_hardware_deinit(wq_pcie, chn);
	hw_ce_chn_rx_cfg_set(wq_pcie, chn, chn_cfg);
	hw_ce_chn_rx_ring_cfg_set(wq_pcie, chn, src_ring->depth_log2,
				  src_ring->depth_mask, 0);
	hw_ce_chn_rx_link_addr_set(wq_pcie, chn,
				   (u32)src_ring->desc_base_phys_addr);
}

static inline void ce_dst_hardware_init(struct wq_pcie *wq_pcie,
					CE_CHN_UUID chn, u32 chn_cfg,
					const struct ce_ring *dst_ring)
{
	ce_dst_hardware_deinit(wq_pcie, chn);
	hw_ce_chn_tx_cfg_set(wq_pcie, chn, chn_cfg);
	hw_ce_chn_tx_ring_cfg_set(wq_pcie, chn, dst_ring->depth_log2,
				  dst_ring->depth_mask, 0);
	hw_ce_chn_tx_link_addr_set(wq_pcie, chn,
				   (u32)dst_ring->desc_base_phys_addr);
}

static u8 ce_depth_roundup_pow_of_two(u16 depth)
{
	u8 depth_log2;

	if (depth < 2 || (depth & (depth - 1)) != 0)
		return HW_CE_RING_DEPTH_LOG2_INVALID;

	for (depth_log2 = 0; depth_log2 <= HW_CE_RING_DEPTH_LOG2_MAX &&
			     (1u << depth_log2) < depth;
	     ++depth_log2)
		; // null statement

	return depth_log2;
}

static struct ce_side *ce_side_alloc(struct wq_pcie *wq_pcie, u16 depth)
{
	u8 depth_log2;
	size_t nbytes;
	struct ce_side *side;
	struct ce_ring *ring;
	int ret;

	depth_log2 = ce_depth_roundup_pow_of_two(depth);

	if (HW_CE_RING_DEPTH_LOG2_INVALID == depth_log2)
		return NULL;

	nbytes = sizeof(struct ce_side) + sizeof(void *) * depth;

	if (NULL == (side = kmalloc(nbytes, GFP_ATOMIC)))
		return NULL;

	(void)memset(side, 0, nbytes);
	spin_lock_init(&side->lock);

	ring = &side->ring;
	ring->depth_log2 = depth_log2;
	ring->depth_mask = depth - 1;
	q_stats_init(&ring->stats, depth);

	ret = wq_map_consistent(wq_pcie, (u8 **)&ring->desc_base_virt_addr,
				&ring->desc_base_phys_addr,
				(u32)(sizeof(struct hw_ce_desc) * depth));

	if (ret != 0) {
		kfree(side);
		return NULL;
	}

	return side;
}

static void ce_side_free(struct wq_pcie *wq_pcie, struct ce_side **side)
{
	struct ce_side *_side = *side;

	if (_side != NULL) {
		struct ce_ring *ring = &_side->ring;
		u16 depth = (u16)(1u << ring->depth_log2);

		if (ring->desc_base_virt_addr != NULL &&
		    ring->desc_base_phys_addr != 0 && depth != 0)
			wq_unmap_consistent(
				wq_pcie, (u8 **)&ring->desc_base_virt_addr,
				&ring->desc_base_phys_addr,
				(u32)(sizeof(struct hw_ce_desc) * depth));

		kfree(_side);
		*side = NULL;
	}
}

static int ce_src_side_init(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			    u32 chn_cfg, u16 depth)
{
	u8 depth_log2;
	struct ce_side *src_side;

	hw_ce_chn_rx_ring_cfg_get(wq_pcie, chn, &depth_log2, NULL, NULL);

	if (0 == depth_log2) {
		WQ_CE_LOG("CE AHB CLK may not have been reset yet\n");
		return -EPERM;
	}

#ifndef CE_CHN_RING_CLR_BUG_FIXED
	if (((1u << depth_log2) > depth)) {
		CE_DUMP("%s, %d: hardware depth_log2 = %d, software deliver "
			"src_depth = %d\n",
			__func__, __LINE__, (int)depth_log2, (int)depth);
		wq_ce_chn_dump(wq_pcie, chn);
		return -EINVAL;
	}
#endif

	if (NULL == (src_side = ce_side_alloc(wq_pcie, depth)))
		return -ENOMEM;

	wq_pcie->ce_states[chn].src = src_side;
	src_side->irq_vec = ce_chn_to_irq_vec_map[chn];
	wq_pcie_irq_mask(wq_pcie, src_side->irq_vec);

#ifndef CE_CHN_RING_CLR_BUG_FIXED
	{
		u16 start_idx = hw_ce_chn_rx_ridx_get(wq_pcie, chn);

		src_side->ring.sw_widx = start_idx;
		src_side->ring.hw_ridx = start_idx;
		src_side->ring.sw_ridx = start_idx;
	}
#endif

	ce_src_hardware_init(wq_pcie, chn, chn_cfg, &src_side->ring);
	return 0;
}

static void ce_src_side_deinit(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	struct ce_side *src_side;
	ce_src_hardware_deinit(wq_pcie, chn);

	if ((src_side = wq_pcie->ce_states[chn].src) != NULL) {
		wq_pcie_irq_mask(wq_pcie, src_side->irq_vec);
		ce_side_free(wq_pcie, &wq_pcie->ce_states[chn].src);
	}
}

static int ce_dst_side_init(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			    u32 chn_cfg, u16 depth)
{
	u8 depth_log2;
	struct ce_side *dst_side;

	hw_ce_chn_tx_ring_cfg_get(wq_pcie, chn, &depth_log2, NULL, NULL);

	if (0 == depth_log2) {
		WQ_CE_LOG("CE AHB CLK may not have been reset yet\n");
		return -EPERM;
	}

#ifndef CE_CHN_RING_CLR_BUG_FIXED
	if (((1u << depth_log2) > depth)) {
		CE_DUMP("%s, %d: hardware depth_log2 = %d, software deliver "
			"dst_depth = %d\n",
			__func__, __LINE__, (int)depth_log2, (int)depth);
		wq_ce_chn_dump(wq_pcie, chn);
		return -EINVAL;
	}
#endif

	if (NULL == (dst_side = ce_side_alloc(wq_pcie, depth)))
		return -ENOMEM;

	wq_pcie->ce_states[chn].dst = dst_side;
	dst_side->irq_vec = ce_chn_to_irq_vec_map[chn];
	wq_pcie_irq_mask(wq_pcie, dst_side->irq_vec);

#ifndef CE_CHN_RING_CLR_BUG_FIXED
	{
		u16 start_idx = hw_ce_chn_tx_ridx_get(wq_pcie, chn);
		dst_side->ring.sw_widx = start_idx;
		dst_side->ring.hw_ridx = start_idx;
		dst_side->ring.sw_ridx = start_idx;
	}
#endif

	ce_dst_hardware_init(wq_pcie, chn, chn_cfg, &dst_side->ring);
	return 0;
}

static void ce_dst_side_deinit(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	struct ce_side *dst_side;
	ce_dst_hardware_deinit(wq_pcie, chn);

	if ((dst_side = wq_pcie->ce_states[chn].dst) != NULL) {
		wq_pcie_irq_mask(wq_pcie, dst_side->irq_vec);
		ce_side_free(wq_pcie, &wq_pcie->ce_states[chn].dst);
	}
}

static inline bool ce_attr_assign_legal(const wq_ce_attr_assign_t *attr_assign)
{
	if (!(0 < attr_assign->src_sz_max &&
	      attr_assign->src_sz_max <= WQ_CE_SRC_SZ_MAX_UPLIMIT))
		return false;

	if (attr_assign->src_depth > WQ_CE_FIFO_DEPTH_MAX ||
	    attr_assign->dst_depth > WQ_CE_FIFO_DEPTH_MAX)
		return false;

	return (attr_assign->src_depth > 0 || attr_assign->dst_depth > 0);
}

/**
 * @brief: init src/dst configures for CE channel
 * @param: CE channel UUID
*/
int wq_ce_chn_init(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		   const wq_ce_attr_assign_t *attr_assign)
{
	int ret;
	u32 attr_flags;
	const wq_ce_attr_t *attr;

	if (NULL == wq_pcie || chn >= CE_CHN_MAX || NULL == attr_assign) {
		CE_DUMP("%s, %d: wq_pcie = 0x%lx, chn = %d, "
			"attr_assign = 0x%lx\n",
			__func__, __LINE__, (unsigned long)wq_pcie, (int)chn,
			(unsigned long)attr_assign);
		return -EINVAL;
	}

	attr = wq_ce_attr_get(wq_pcie, chn);

	if (attr->flags != 0 || attr->src_sz_max != 0 || attr->src_depth != 0 ||
	    attr->dst_depth != 0) {
		CE_DUMP("%s, %d: flags = 0x%x, src_sz_max = %u, "
			"src_depth = %d, dst_depth = %d\n",
			__func__, __LINE__, attr->flags, attr->src_sz_max,
			(int)attr->src_depth, (int)attr->dst_depth);
		return -EINVAL;
	}

	if (!wq_ce_is_controller_init(wq_pcie)) {
		WQ_CE_LOG("CE AHB CLK may not have been enable yet\n");
		return -EPERM;
	}

	attr_flags = attr_assign->flags & CE_ATTR_FLAG_VALID;

	if (attr_assign->src_depth > 0) {
		if ((ret = ce_src_side_init(wq_pcie, chn,
					    HW_CE_CHN_CFG_COMMON | attr_flags,
					    attr_assign->src_depth)) < 0)
			return ret;
	}

	if (attr_assign->dst_depth > 0) {
		if ((ret = ce_dst_side_init(wq_pcie, chn,
					    HW_CE_CHN_CFG_COMMON | attr_flags,
					    attr_assign->dst_depth)) < 0) {
			ce_src_side_deinit(wq_pcie, chn);
			return ret;
		}
	}

	wq_pcie->ce_attrs[chn] = *(const wq_ce_attr_t *)attr_assign;
	return 0;
}

/**
 * @brief: deinitialize channel
 * @param: CE channel UUID
*/
int wq_ce_chn_deinit(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	struct ce_state *state;
	wq_ce_attr_t *attr;

	if (NULL == wq_pcie || chn >= CE_CHN_MAX)
		return -EINVAL;

	attr = &wq_pcie->ce_attrs[chn];
	state = &wq_pcie->ce_states[chn];

	if (state->src != NULL) {
		wq_ce_irq_mask(wq_pcie, chn, WQ_CE_CHN_SRC);
		ce_src_side_deinit(wq_pcie, chn);
	}

	if (state->dst != NULL) {
		wq_ce_irq_mask(wq_pcie, chn, WQ_CE_CHN_DST);
		ce_dst_side_deinit(wq_pcie, chn);
	}

	(void)memset(attr, 0, sizeof(wq_ce_attr_t));
	return 0;
}

/**
 * @brief: get CE channel table
 * @param: CE channel UUID
*/
const wq_ce_attr_t *wq_ce_attr_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	(void)wq_pcie;

	if (chn >= CE_CHN_MAX)
		return NULL;

	return &wq_pcie->ce_attrs[chn];
}

/**
 * @brief: get low and high watermarks
 * @param: CE channel UUID
*/
int wq_ce_watermarks_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			 WQ_CE_CHN_DIR dir, u16 *high, u16 *low)
{
	if (!(wq_pcie != NULL && CE_CHN_DIR_LEGAL(chn, dir) &&
	      (CE_DIR_SIDE(wq_pcie, chn, dir) != NULL)))
		return -EINVAL;

	if (WQ_CE_CHN_SRC == dir)
		hw_ce_chn_rx_ring_cfg_get(wq_pcie, chn, NULL, high, low);
	else
		hw_ce_chn_tx_ring_cfg_get(wq_pcie, chn, NULL, high, low);

	return 0;
}

/**
 * @brief: set low and high watermarks
 * @param: CE channel UUID
*/
int wq_ce_watermarks_set(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			 WQ_CE_CHN_DIR dir, u16 high, u16 low)
{
	const struct ce_side *side;
	const struct ce_ring *ring;

	if (!(wq_pcie != NULL && CE_CHN_DIR_LEGAL(chn, dir) &&
	      (side = CE_DIR_SIDE(wq_pcie, chn, dir)) != NULL))
		return -EINVAL;

	ring = &side->ring;

	if (!(low <= high && high <= ring->depth_mask))
		return -EINVAL;

	if (WQ_CE_CHN_SRC == dir)
		hw_ce_chn_rx_ring_cfg_set(wq_pcie, chn, ring->depth_log2, high,
					  low);
	else
		hw_ce_chn_tx_ring_cfg_set(wq_pcie, chn, ring->depth_log2, high,
					  low);

	return 0;
}

/**
 * @brief: get CE channel interrupt callback
 * @param: CE channel UUID
*/
int wq_ce_int_cb_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		     WQ_CE_CHN_DIR dir, u8 evt, wq_ce_int_cb_t *cb)
{
	const struct ce_side *side;

	if (!(wq_pcie != NULL && CE_CHN_DIR_LEGAL(chn, dir) &&
	      (side = CE_DIR_SIDE(wq_pcie, chn, dir)) != NULL &&
	      CE_DIR_EVT_LEGAL(dir, evt)))
		return -EINVAL;

	if (cb != NULL)
		*cb = side->int_cbs[evt];

	return 0;
}

/**
 * @brief: set CE channel interrupt callback
 * @param: CE channel UUID
*/
int wq_ce_int_cb_set(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		     WQ_CE_CHN_DIR dir, u8 evt, wq_ce_int_cb_t cb)
{
	struct ce_side *side;

	if (!(wq_pcie != NULL && CE_CHN_DIR_LEGAL(chn, dir) &&
	      (side = CE_DIR_SIDE(wq_pcie, chn, dir)) != NULL &&
	      CE_DIR_EVT_LEGAL(dir, evt)))
		return -EINVAL;

	side->int_cbs[evt] = cb;
	return 0;
}

/**
 * @brief: get CE channel interrupt enas
 * @param: CE channel UUID
*/
int wq_ce_int_ena_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		      WQ_CE_CHN_DIR dir, u8 evt, bool *ena)
{
	u32 int_enas;

	if (!(wq_pcie != NULL && CE_CHN_DIR_LEGAL(chn, dir) &&
	      (CE_DIR_SIDE(wq_pcie, chn, dir)) != NULL &&
	      CE_DIR_EVT_LEGAL(dir, evt)))
		return -EINVAL;

	if (WQ_CE_CHN_SRC == dir)
		int_enas = hw_ce_chn_rx_int_ena_get(wq_pcie, chn);
	else
		int_enas = hw_ce_chn_tx_int_ena_get(wq_pcie, chn);

	if (ena != NULL)
		*ena = (bool)((int_enas & (1u << evt)) != 0);

	return 0;
}

/**
 * @brief: set CE channel interrupt enas
 * @param: CE channel UUID
*/
int wq_ce_int_ena_set(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		      WQ_CE_CHN_DIR dir, u8 evt, bool ena)
{
	u32 int_enas;

	if (!(wq_pcie != NULL && CE_CHN_DIR_LEGAL(chn, dir) &&
	      (CE_DIR_SIDE(wq_pcie, chn, dir)) != NULL &&
	      CE_DIR_EVT_LEGAL(dir, evt)))
		return -EINVAL;

	if (WQ_CE_CHN_SRC == dir)
		int_enas = hw_ce_chn_rx_int_ena_get(wq_pcie, chn);
	else
		int_enas = hw_ce_chn_tx_int_ena_get(wq_pcie, chn);

	if (ena)
		int_enas |= (1u << evt);
	else
		int_enas &= ~(1u << evt);

	if (WQ_CE_CHN_SRC == dir)
		hw_ce_chn_rx_int_ena_set(wq_pcie, chn, int_enas);
	else
		hw_ce_chn_tx_int_ena_set(wq_pcie, chn, int_enas);

	return 0;
}

/**
 * @brief: channel irq unmask
 * @param: CE channel UUID
*/
int wq_ce_irq_unmask(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		     WQ_CE_CHN_DIR dir)
{
	const struct ce_side *side;

	if (!(wq_pcie != NULL && CE_CHN_DIR_LEGAL(chn, dir) &&
	      (side = CE_DIR_SIDE(wq_pcie, chn, dir)) != NULL))
		return -EINVAL;

	wq_pcie_irq_unmask(wq_pcie, side->irq_vec);
	return 0;
}

/**
 * @brief: channel irq mask
 * @param: CE channel UUID
*/
int wq_ce_irq_mask(struct wq_pcie *wq_pcie, CE_CHN_UUID chn, WQ_CE_CHN_DIR dir)
{
	const struct ce_side *side;

	if (!(wq_pcie != NULL && CE_CHN_DIR_LEGAL(chn, dir) &&
	      (side = CE_DIR_SIDE(wq_pcie, chn, dir)) != NULL))
		return -EINVAL;

	wq_pcie_irq_mask(wq_pcie, side->irq_vec);
	return 0;
}

static void wq_ce_src_ring_sw_idx_sync(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	struct ce_ring *src_ring;
	u16 hw_widx;
	u16 src_depth;

	src_ring = &wq_pcie->ce_states[chn].src->ring;

	if (src_ring->widx_incr)
		return;

	if (src_ring->sw_widx != src_ring->hw_ridx ||
	    src_ring->hw_ridx != src_ring->sw_ridx)
		return;

	hw_widx = hw_ce_chn_rx_widx_get(wq_pcie, chn);
	src_depth = (u16)(1u << src_ring->depth_log2);

	if (hw_widx >= src_depth) {
		WQ_CE_LOG("chn=%d, hw_widx=%d, src_depth=%d\n", (int)chn,
			  (int)hw_widx, (int)src_depth);
		wq_ce_everything_dump(wq_pcie);
		BUG_ON(1);
	}

	if (src_ring->sw_widx != hw_widx) {
		if (hw_widx) {
			WQ_CE_LOG("chn=%d, sw_widx=%d, hw_widx=%d\n", (int)chn,
				  (int)src_ring->sw_widx, (int)hw_widx);
			wq_ce_everything_dump(wq_pcie);
			BUG_ON(1);
		}
		src_ring->sw_widx = src_ring->hw_ridx = src_ring->sw_ridx = 0;
	}
}

/* sw high watermark depth*0.9, if over high watermark, return -ENOBUFS */
static int is_wq_ce_tx_over_sw_watermark(struct ce_ring *src_ring, u8 chan)
{
	if (WQ_PCIE_CE_CH_PKT_TX == chan || WQ_PCIE_CE_CH_RAW_TX == chan) {
		u32 depth = src_ring->depth_mask + 1;
		u32 water_mark = depth * 9 / 10;
		if (CE_FIFO_SW_UNPROCESS(src_ring) > water_mark) {
			src_ring->is_flow_ctrl = 1;
			WQ_DBG(DM_GENERIC, DL_VRB, "is_wq_ce_tx_over_sw_watermark chan:%d, mark=%d, cur:%d\n",
				chan, water_mark, CE_FIFO_SW_UNPROCESS(src_ring));
			return -ENOBUFS;
		}
	}

	return 0;
}

/* sw low watermark depth*0.7, if under low watermark, return 1 */
static int is_wq_ce_txdone_src_under_sw_watermark(struct ce_ring *src_ring, u8 chan)
{
	if (WQ_PCIE_CE_CH_PKT_TX == chan || WQ_PCIE_CE_CH_RAW_TX == chan) {
		u32 depth = src_ring->depth_mask + 1;
		u32 water_mark = depth * 7 / 10;
		if (src_ring->is_flow_ctrl && CE_FIFO_SW_UNPROCESS(src_ring) < water_mark) {
			src_ring->is_flow_ctrl = 0;
			WQ_DBG(DM_GENERIC, DL_VRB, "is_wq_ce_txdone_src_under_sw_watermark=%d, cur:%d\n",
				water_mark, CE_FIFO_SW_UNPROCESS(src_ring));
			return 1;
		}
	}

	return 0;
}

static int wq_ce_send_nolock(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			     void *send_context, dma_addr_t phys_addr,
			     u32 nbytes, u32 flags)
{
	struct ce_side *src_side;
	u32 src_sz_max;
	struct ce_ring *src_ring;
	bool gather_eb;
	struct hw_ce_desc *src_desc;

	if (!(wq_pcie != NULL && chn < CE_CHN_MAX &&
	      (src_side = wq_pcie->ce_states[chn].src) != NULL))
		return -EINVAL;

	/* pci_wake_up */
	wq_ce_src_ring_sw_idx_sync(wq_pcie, chn);

	src_sz_max = wq_pcie->ce_attrs[chn].src_sz_max;

	if (0 == phys_addr || 0 == nbytes || nbytes > src_sz_max)
		return -EINVAL;

	if ((flags & CE_DESC_FLAG_CRC_EB_MASK) == CE_DESC_FLAG_CRC_EB &&
	    !CE_WHETHER_SUPPORT_CRC(chn))
		return -EINVAL;

	src_ring = &src_side->ring;

	if (CE_FIFO_SW_FULL(src_ring))
		return -ENOSPC;

	gather_eb = ((flags & CE_DESC_FLAG_GATHER_EB_MASK) ==
		     CE_DESC_FLAG_GATHER_EB);

	if (gather_eb) {
		if (src_ring->widx_incr + 1 == src_ring->depth_mask)
			return -EINVAL;
		// above: gather counter does not allow to make fifo full
		if (nbytes >= src_sz_max - src_ring->gather_nbytes)
			return -EINVAL;
		src_ring->gather_nbytes += nbytes;
	} else {
		if (nbytes > src_sz_max - src_ring->gather_nbytes)
			return -EINVAL;
		src_ring->gather_nbytes = 0;
	}

	src_ring->transfer_context[src_ring->sw_widx] = send_context;
	src_desc = src_ring->desc_base_virt_addr + src_ring->sw_widx;

	src_desc->flags =
		cpu_to_le32((flags & CE_DESC_FLAG_SRC_VALID) | nbytes);
	src_desc->phys_addr = cpu_to_le32((u32)phys_addr);

	src_ring->sw_widx =
		CE_RING_IDX_INCR(src_ring->depth_mask, src_ring->sw_widx, 1);
	src_ring->widx_incr++;

	if (!gather_eb) {
		hw_ce_chn_rx_widx_incr_set(wq_pcie, chn, src_ring->widx_incr);
		src_ring->widx_incr = 0;
	}

	q_stats_tx(&src_ring->stats, 1);

	return is_wq_ce_tx_over_sw_watermark(src_ring, chn);
}

/**
 * @brief: send entry once
 * @param: CE channel UUID
*/
int wq_ce_send(struct wq_pcie *wq_pcie, CE_CHN_UUID chn, void *send_context,
	       dma_addr_t phys_addr, u32 nbytes, u32 flags)
{
	struct ce_side *src_side;
	unsigned long iflags;
	int ret;

	if (!(wq_pcie != NULL && chn < CE_CHN_MAX &&
	      (src_side = wq_pcie->ce_states[chn].src) != NULL))
		return -EINVAL;

	spin_lock_irqsave(&src_side->lock, iflags);
	ret = wq_ce_send_nolock(wq_pcie, chn, send_context, phys_addr, nbytes,
				flags);
	spin_unlock_irqrestore(&src_side->lock, iflags);

	return ret;
}

static int wq_ce_send_completed_next_nolock(struct wq_pcie *wq_pcie,
					    CE_CHN_UUID chn,
					    void **send_context,
					    dma_addr_t *phys_addr, u32 *nbytes,
					    u32 *flags)
{
	struct ce_side *src_side;
	struct ce_ring *src_ring;
	struct hw_ce_desc *src_desc;

	if (!(wq_pcie != NULL && chn < CE_CHN_MAX &&
	      (src_side = wq_pcie->ce_states[chn].src) != NULL))
		return -EINVAL;

	src_ring = &src_side->ring;

	if (src_ring->sw_ridx == src_ring->hw_ridx) {
		src_ring->hw_ridx = hw_ce_chn_rx_ridx_get(wq_pcie, chn);
		if (src_ring->sw_ridx == src_ring->hw_ridx)
			return -ENODATA;
	}

	if (send_context != NULL)
		*send_context = src_ring->transfer_context[src_ring->sw_ridx];

	src_ring->transfer_context[src_ring->sw_ridx] = NULL;
	src_desc = src_ring->desc_base_virt_addr + src_ring->sw_ridx;

	if (phys_addr != NULL)
		*phys_addr = le32_to_cpu(src_desc->phys_addr);

	if (nbytes != NULL)
		*nbytes = (le32_to_cpu(src_desc->flags) &
			   HW_CE_DESC_FLAG_BUF_LENGTH_MASK) >>
			  HW_CE_DESC_FLAG_BUF_LENGTH_OFFSET;

	if (flags != NULL)
		*flags = (le32_to_cpu(src_desc->flags) &
			  ~HW_CE_DESC_FLAG_BUF_LENGTH_MASK);

	(void)memset(src_desc, 0, sizeof(struct hw_ce_desc));
	src_ring->sw_ridx =
		CE_RING_IDX_INCR(src_ring->depth_mask, src_ring->sw_ridx, 1);

	q_stats_txdone(&src_ring->stats, 1);

#ifdef CE_DEBUG
	ce_send_completed_count[chn]++;
#endif // CE_DEBUG
	return is_wq_ce_txdone_src_under_sw_watermark(src_ring, chn);
}

/**
 * @brief: iterator to get completed entries
 * @param: CE channel UUID
*/
int wq_ce_send_completed_next(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			      void **send_context, dma_addr_t *phys_addr,
			      u32 *nbytes, u32 *flags)
{
	struct ce_side *src_side;
	unsigned long iflags;
	int ret;

	if (!(wq_pcie != NULL && chn < CE_CHN_MAX &&
	      (src_side = wq_pcie->ce_states[chn].src) != NULL))
		return -EINVAL;

	spin_lock_irqsave(&src_side->lock, iflags);
	ret = wq_ce_send_completed_next_nolock(wq_pcie, chn, send_context,
					       phys_addr, nbytes, flags);
	spin_unlock_irqrestore(&src_side->lock, iflags);

	return ret;
}

static void wq_ce_recv_dummy_move(struct ce_ring *dst_ring)
{
	u16 sw_widx, mv_widx;
	void **transfer_context;
	struct hw_ce_desc *desc_base;

	sw_widx = dst_ring->sw_widx;
	mv_widx = CE_RING_IDX_INCR(dst_ring->depth_mask, sw_widx, 1);

	transfer_context = dst_ring->transfer_context;
	transfer_context[mv_widx] = transfer_context[sw_widx];

	desc_base = dst_ring->desc_base_virt_addr;
	desc_base[mv_widx] = desc_base[sw_widx];
}

static int wq_ce_recv_nolock(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			     void *recv_context, dma_addr_t phys_addr,
			     u32 nbytes, u32 flags)
{
	struct ce_side *dst_side;
	u32 src_sz_max;
	struct ce_ring *dst_ring;
	struct hw_ce_desc *dst_desc;

	if (!(wq_pcie != NULL && chn < CE_CHN_MAX &&
	      (dst_side = wq_pcie->ce_states[chn].dst) != NULL))
		return -EINVAL;

	src_sz_max = wq_pcie->ce_attrs[chn].src_sz_max;

	if (0 == phys_addr || nbytes < src_sz_max)
		return -EINVAL;

	if ((flags & CE_DESC_FLAG_CRC_EB_MASK) == CE_DESC_FLAG_CRC_EB) {
		if (!CE_WHETHER_SUPPORT_CRC(chn))
			return -EINVAL;

		if (src_sz_max > WQ_CE_SRC_SZ_MAX_IGNORE_DST) {
			u8 crc_byte_width =
				((flags & CE_DESC_FLAG_CRC_MODE_MASK) >>
				 CE_DESC_FLAG_CRC_MODE_OFFSET) +
				1;
			if (nbytes < src_sz_max + crc_byte_width)
				return -EINVAL;
		}
	}

	dst_ring = &dst_side->ring;

	if (CE_FIFO_SW_FULL(dst_ring))
		return -ENOSPC;

	wq_ce_recv_dummy_move(dst_ring);

	dst_ring->transfer_context[dst_ring->sw_widx] = recv_context;
	dst_desc = dst_ring->desc_base_virt_addr + dst_ring->sw_widx;

	dst_desc->flags =
		cpu_to_le32((flags & CE_DESC_FLAG_DST_VALID) | nbytes);
	dst_desc->phys_addr = cpu_to_le32((u32)phys_addr);

	dst_ring->sw_widx =
		CE_RING_IDX_INCR(dst_ring->depth_mask, dst_ring->sw_widx, 1);
	hw_ce_chn_tx_widx_incr_set(wq_pcie, chn, 1);

	q_stats_rx_refill(&dst_ring->stats, 1);

	return 0;
}

/**
 * @brief: recv once
 * @param: CE channel UUID
*/
int wq_ce_recv(struct wq_pcie *wq_pcie, CE_CHN_UUID chn, void *recv_context,
	       dma_addr_t phys_addr, u32 nbytes, u32 flags)
{
	struct ce_side *dst_side;
	unsigned long iflags;
	int ret;

	if (!(wq_pcie != NULL && chn < CE_CHN_MAX &&
	      (dst_side = wq_pcie->ce_states[chn].dst) != NULL))
		return -EINVAL;

	spin_lock_irqsave(&dst_side->lock, iflags);
	ret = wq_ce_recv_nolock(wq_pcie, chn, recv_context, phys_addr, nbytes,
				flags);
	spin_unlock_irqrestore(&dst_side->lock, iflags);

	return ret;
}

static void wq_ce_dst_ring_sw_idx_sync(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	struct ce_ring *dst_ring;
	u16 hw_widx;
	u16 dst_depth;

	dst_ring = &wq_pcie->ce_states[chn].dst->ring;

	if (!CE_FIFO_SW_FULL(dst_ring))
		return;

	hw_widx = hw_ce_chn_tx_widx_get(wq_pcie, chn);
	dst_depth = (u16)(1u << dst_ring->depth_log2);

	if (hw_widx >= dst_depth) {
		WQ_CE_LOG("chn=%d, hw_widx=%d, dst_depth=%d\n", (int)chn,
			  (int)hw_widx, (int)dst_depth);
		wq_ce_everything_dump(wq_pcie);
		BUG_ON(1);
	}

	if (dst_ring->sw_widx != hw_widx) {
		if (hw_widx != dst_ring->depth_mask) {
			WQ_CE_LOG("chn=%d, sw_widx=%d, hw_widx=%d\n", (int)chn,
				  (int)dst_ring->sw_widx, (int)hw_widx);
			wq_ce_everything_dump(wq_pcie);
			BUG_ON(1);
		}
		dst_ring->sw_widx = dst_ring->depth_mask;
		dst_ring->hw_ridx = dst_ring->sw_ridx = 0;
	}
}

static int wq_ce_recv_completed_next_nolock(struct wq_pcie *wq_pcie,
					    CE_CHN_UUID chn,
					    void **recv_context,
					    dma_addr_t *phys_addr, u32 *nbytes,
					    u32 *flags)
{
	struct ce_side *dst_side;
	struct ce_ring *dst_ring;
	struct hw_ce_desc *dst_desc;
#ifdef CONFIG_WAR_CE_INTR_FAST_THAN_DATA
	u32 _flags;
	unsigned long timeout;
#endif

	if (!(wq_pcie != NULL && chn < CE_CHN_MAX &&
	      (dst_side = wq_pcie->ce_states[chn].dst) != NULL))
		return -EINVAL;

	/* pci_wake_up */
	wq_ce_dst_ring_sw_idx_sync(wq_pcie, chn);

	dst_ring = &dst_side->ring;

	if (dst_ring->sw_ridx == dst_ring->hw_ridx) {
		dst_ring->hw_ridx = hw_ce_chn_tx_ridx_get(wq_pcie, chn);
		if (dst_ring->sw_ridx == dst_ring->hw_ridx)
			return -ENODATA;
	}

	if (recv_context != NULL)
		*recv_context = dst_ring->transfer_context[dst_ring->sw_ridx];

	dst_ring->transfer_context[dst_ring->sw_ridx] = NULL;
	dst_desc = dst_ring->desc_base_virt_addr + dst_ring->sw_ridx;

#ifdef CONFIG_WAR_CE_INTR_FAST_THAN_DATA
	timeout = 0;
	do {
		_flags = le32_to_cpu(*(volatile u32 *)&dst_desc->flags);
		if ((_flags & CE_DESC_FLAG_METADATA_MASK) ==
		    WQ_CE_DESC_FLAG_METADATA_PRI)
			break;
		if (++timeout >= WQ_CE_RECV_METADATA_WAIT_TIMEOUT)
			break;
	} while (1);

	if (timeout >= WQ_CE_RECV_METADATA_WAIT_TIMEOUT) {
		WQ_CE_LOG("ce chn:%d recv wait desc metadata timeout "
				"flags:0x%08x\n",
				chn, _flags);
		BUG_ON(1);
	}
#endif

	if (phys_addr != NULL)
		*phys_addr = le32_to_cpu(dst_desc->phys_addr);

	if (nbytes != NULL)
		*nbytes = (le32_to_cpu(dst_desc->flags) &
			   HW_CE_DESC_FLAG_BUF_LENGTH_MASK) >>
			  HW_CE_DESC_FLAG_BUF_LENGTH_OFFSET;

	if (flags != NULL)
		*flags = (le32_to_cpu(dst_desc->flags) &
			  ~HW_CE_DESC_FLAG_BUF_LENGTH_MASK);

	(void)memset(dst_desc, 0, sizeof(struct hw_ce_desc));
	dst_ring->sw_ridx =
		CE_RING_IDX_INCR(dst_ring->depth_mask, dst_ring->sw_ridx, 1);

	q_stats_rx(&dst_ring->stats, 1);

#ifdef CE_DEBUG
	ce_recv_completed_count[chn]++;
#endif // CE_DEBUG
	return 0;
}

/**
 * @brief: iterator to get completed entries
 * @param: CE channel UUID
*/
int wq_ce_recv_completed_next(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			      void **recv_context, dma_addr_t *phys_addr,
			      u32 *nbytes, u32 *flags)
{
	struct ce_side *dst_side;
	unsigned long iflags;
	int ret;

	if (!(wq_pcie != NULL && chn < CE_CHN_MAX &&
	      (dst_side = wq_pcie->ce_states[chn].dst) != NULL))
		return -EINVAL;

	spin_lock_irqsave(&dst_side->lock, iflags);
	ret = wq_ce_recv_completed_next_nolock(wq_pcie, chn, recv_context,
					       phys_addr, nbytes, flags);
	spin_unlock_irqrestore(&dst_side->lock, iflags);

	return ret;
}

/**
 * @brief: get ring fifo state
 * @param: CE channel UUID
*/
int wq_ce_fifo_lfo_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		       WQ_CE_CHN_DIR dir, u16 *lfo)
{
	const struct ce_side *side;

	if (!(wq_pcie != NULL && CE_CHN_DIR_LEGAL(chn, dir) &&
	      (side = CE_DIR_SIDE(wq_pcie, chn, dir)) != NULL))
		return -EINVAL;

	if (lfo != NULL)
		*lfo = CE_FIFO_SW_LFO(&side->ring);

	return 0;
}

/**
 * @brief: get ring fifo accurate gather nbytes
 * @param: CE channel UUID
*/
int wq_ce_send_gather_statistics_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
				     u16 *gather_count, u32 *gather_nbytes)
{
	struct ce_side *src_side;
	struct ce_ring *src_ring;
	unsigned long iflags;

	if (!(wq_pcie != NULL && chn < CE_CHN_MAX &&
	      (src_side = wq_pcie->ce_states[chn].src) != NULL))
		return -EINVAL;

	src_ring = &src_side->ring;
	spin_lock_irqsave(&src_side->lock, iflags);

	if (gather_count != NULL)
		*gather_count = src_ring->widx_incr;

	if (gather_nbytes != 0)
		*gather_nbytes = src_ring->gather_nbytes;

	spin_unlock_irqrestore(&src_side->lock, iflags);
	return 0;
}

void wq_ce_chn_ring_dump(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
#ifdef WQ_STATS
	struct ce_ring *ring = NULL;
	u16 hw_ridx;
	u16 hw_pending;

	if (wq_pcie->ce_states[chn].src) {
		ring = &wq_pcie->ce_states[chn].src->ring;
		hw_ridx = hw_ce_chn_rx_ridx_get(wq_pcie, chn);
		hw_pending = hw_ce_chn_rx_hw_num_get(wq_pcie, chn);
	}

	if (wq_pcie->ce_states[chn].dst) {
		ring = &wq_pcie->ce_states[chn].dst->ring;
		hw_ridx = hw_ce_chn_tx_ridx_get(wq_pcie, chn);
		hw_pending = hw_ce_chn_tx_hw_num_get(wq_pcie, chn);
	}

	if (!ring)
		return;

	WQ_DBG(DM_TRBUS, DL_WRN,
	       "CE[%2u] %4d/%4u: sw/hw: %4u/%4u, in - out: %8d = %8u - %8u\n",
	       chn, ring->stats.max, ring->depth_mask,
	       (hw_ridx - ring->sw_ridx) & ring->depth_mask, hw_pending,
	       q_stats_n(&ring->stats), ring->stats.in, ring->stats.out);
	q_stats_reset(&ring->stats);
#endif
}

#ifdef CE_DEBUG
/**
 * @brief: debug channel desc word
 * @param: CE channel UUID
*/
/*
int wq_ce_info_dbg_bus_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn, u32 words[CE_DBG_BUS_SEL_MAX])
{
    CE_DBG_BUS_SEL_IDX sel;

    if (wq_pcie != NULL && chn < CE_CHN_MAX) {
        for (sel = 0; sel < CE_DBG_BUS_SEL_MAX; ++sel)
            words[sel] = hw_ce_chn_info_dbg_bus_get(wq_pcie, chn, sel);
        (void)hw_ce_chn_info_dbg_bus_get(wq_pcie, chn, 0);
        return 0;
    }

    return -EINVAL;
}
*/
#endif // CE_DEBUG

int wq_ce_intr_ena_set(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		       WQ_CE_CHN_DIR dir, u32 ena)
{
	if (!wq_pcie || chn >= CE_CHN_MAX)
		return -EINVAL;

	if (WQ_CE_CHN_SRC == dir)
		hw_ce_chn_rx_int_ena_set(wq_pcie, chn, ena);
	else if (WQ_CE_CHN_DST == dir)
		hw_ce_chn_tx_int_ena_set(wq_pcie, chn, ena);

	return 0;
}

int wq_ce_stop(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	int timeout;
	u32 dbg_bus, status;

	if (chn >= CE_CHN_MAX)
		return -EINVAL;

	WQ_PCIE_REG_FIELD_WR(wq_pcie, CE_REG(chn, CFG_DMA_CHN_RX_CFG_ADDR),
			     CHN_RX_EB, 0);
	WQ_PCIE_REG_FIELD_WR(wq_pcie, CE_REG(chn, CFG_DMA_CHN_TX_CFG_ADDR),
			     CHN_TX_EB, 0);

	WQ_PCIE_REG_FIELD_WR(wq_pcie, CE_REG(chn, CFG_DMA_CHN_COM_CFG_ADDR),
			     DMA_CHN_STOP, 1);
	WQ_PCIE_REG_FIELD_WR(wq_pcie, CE_REG(chn, CFG_DMA_CHN_COM_CFG_ADDR),
			     DMA_CHN_DBG_BUS_SEL,
			     CE_DBG_BUS_SEL_CHN_IDLE_STATUS);

	timeout = 0;

	do {
		BUG_ON(timeout >= 1000);
		++timeout;

		nop();

		dbg_bus = WQ_PCIE_REG_FIELD_RD(
			wq_pcie, CE_REG(chn, CFG_DMA_CHN_INFO_DBG_BUS_ST_ADDR),
			CHN_INFO_DBG_BUS);
		status = dbg_bus & 0x000f000f;
		/*
		 * Hardware spec for wait copyengine channel idle:
		 *    chn_info_dbg_bus(sel=11):
		 *         src_status[19:16] -> 0
		 *         dst_status[3:0] -> 0 or 8
		 */
	} while (status && status != 0x8);

	WQ_PCIE_REG_FIELD_WR(wq_pcie, CE_REG(chn, CFG_DMA_CHN_COM_CFG_ADDR),
			     DMA_CHN_SOFT_RESET, 1);
	WQ_PCIE_REG_FIELD_WR(wq_pcie, CE_REG(chn, CFG_DMA_CHN_COM_CFG_ADDR),
			     DMA_CHN_SOFT_RESET, 0);

	WQ_PCIE_REG_FIELD_WR(wq_pcie, CE_REG(chn, CFG_DMA_CHN_COM_CFG_ADDR),
			     DMA_CHN_STOP, 0);

	return 0;
}

static void wq_ce_desc_push(struct hw_ce_desc *desc, dma_addr_t phys_addr,
			    u32 nbytes, u32 flags, u32 mask)
{
	desc->flags = cpu_to_le32((flags & mask) | nbytes);
	desc->phys_addr = cpu_to_le32((u32)phys_addr);
}

static void wq_ce_desc_pop(const struct hw_ce_desc *desc, dma_addr_t *phys_addr,
			   u32 *nbytes, u32 *flags)
{
	if (phys_addr != NULL)
		*phys_addr = le32_to_cpu(desc->phys_addr);

	if (nbytes != NULL)
		*nbytes = (le32_to_cpu(desc->flags) &
			   HW_CE_DESC_FLAG_BUF_LENGTH_MASK) >>
			  HW_CE_DESC_FLAG_BUF_LENGTH_OFFSET;

	if (flags != NULL)
		*flags = (le32_to_cpu(desc->flags) &
			  ~HW_CE_DESC_FLAG_BUF_LENGTH_MASK);
}

int wq_ce_send_cancel_next(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			   void **send_context, dma_addr_t *phys_addr,
			   u32 *nbytes, u32 *flags)
{
	struct ce_side *src_side;
	struct ce_ring *src_ring;
	struct hw_ce_desc *src_desc;

	if (!(wq_pcie != NULL && chn < CE_CHN_MAX &&
	      (src_side = wq_pcie->ce_states[chn].src) != NULL))
		return -EINVAL;

	src_ring = &src_side->ring;

	if (src_ring->sw_ridx == src_ring->sw_widx)
		return -ENODATA;

	if (send_context != NULL)
		*send_context = src_ring->transfer_context[src_ring->sw_ridx];

	src_ring->transfer_context[src_ring->sw_ridx] = NULL;
	src_desc = src_ring->desc_base_virt_addr + src_ring->sw_ridx;

	wq_ce_desc_pop(src_desc, phys_addr, nbytes, flags);

	(void)memset(src_desc, 0, sizeof(struct hw_ce_desc));

	src_ring->sw_ridx =
		CE_RING_IDX_INCR(src_ring->depth_mask, src_ring->sw_ridx, 1);

	return 0;
}

int wq_ce_recv_revoke_next(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			   void **recv_context, dma_addr_t *phys_addr,
			   u32 *nbytes, u32 *flags)
{
	struct ce_side *dst_side;
	struct ce_ring *dst_ring;
	struct hw_ce_desc *dst_desc;

	if (!(wq_pcie != NULL && chn < CE_CHN_MAX &&
	      (dst_side = wq_pcie->ce_states[chn].dst) != NULL))
		return -EINVAL;

	dst_ring = &dst_side->ring;

	if (dst_ring->sw_ridx == dst_ring->sw_widx)
		return -ENODATA;

	if (recv_context != NULL)
		*recv_context = dst_ring->transfer_context[dst_ring->sw_ridx];

	dst_ring->transfer_context[dst_ring->sw_ridx] = NULL;
	dst_desc = dst_ring->desc_base_virt_addr + dst_ring->sw_ridx;

	wq_ce_desc_pop(dst_desc, phys_addr, nbytes, flags);

	(void)memset(dst_desc, 0, sizeof(struct hw_ce_desc));

	dst_ring->sw_ridx =
		CE_RING_IDX_INCR(dst_ring->depth_mask, dst_ring->sw_ridx, 1);

	return 0;
}

void wq_ce_recv_dummy_push(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			   void *recv_context, dma_addr_t phys_addr, u32 nbytes,
			   u32 flags)
{
	struct ce_side *dst_side;
	struct ce_ring *dst_ring;
	struct hw_ce_desc *dst_desc;

	dst_side = wq_pcie->ce_states[chn].dst;
	dst_ring = &dst_side->ring;

	dst_ring->transfer_context[dst_ring->sw_widx] = recv_context;
	dst_desc = dst_ring->desc_base_virt_addr + dst_ring->sw_widx;

	wq_ce_desc_push(dst_desc, phys_addr, nbytes, flags,
			CE_DESC_FLAG_DST_VALID);
}

void wq_ce_recv_dummy_pop(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			  void **recv_context, dma_addr_t *phys_addr,
			  u32 *nbytes, u32 *flags)
{
	struct ce_side *dst_side;
	struct ce_ring *dst_ring;
	struct hw_ce_desc *dst_desc;

	dst_side = wq_pcie->ce_states[chn].dst;
	dst_ring = &dst_side->ring;

	if (recv_context != NULL)
		*recv_context = dst_ring->transfer_context[dst_ring->sw_widx];

	dst_ring->transfer_context[dst_ring->sw_widx] = NULL;
	dst_desc = dst_ring->desc_base_virt_addr + dst_ring->sw_widx;

	wq_ce_desc_pop(dst_desc, phys_addr, nbytes, flags);

	(void)memset(dst_desc, 0, sizeof(struct hw_ce_desc));
}

static void wq_ce_word_dump(uint32_t *word, size_t size)
{
	size_t i;

	for (i = 0; i < size; i += 8) {
		CE_DUMP("0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			word[i], word[i + 1], word[i + 2], word[i + 3],
			word[i + 4], word[i + 5], word[i + 6], word[i + 7]);
	}
}

static void wq_ce_con_reg_dump(struct wq_pcie *wq_pcie, CE_CON_ID con)
{
	uint32_t word[HW_CE_CON_REG_DUMP_WORD_SIZE];

	hw_ce_con_reg_dump(wq_pcie, con, word);

	CE_DUMP("CopyEngine controller %d dump:\n", (int)con);

	wq_ce_word_dump(word, HW_CE_CON_REG_DUMP_WORD_SIZE);
}

static void wq_ce_chn_reg_dump(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	uint32_t word[HW_CE_CHN_REG_DUMP_WORD_SIZE];

	hw_ce_chn_reg_dump(wq_pcie, chn, word);

	CE_DUMP("CopyEngine channel %d dump:\n", (int)chn);

	wq_ce_word_dump(word, HW_CE_CHN_REG_DUMP_WORD_SIZE);
}

void wq_ce_chn_dump(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	wq_ce_chn_reg_dump(wq_pcie, chn);
}

static void wq_ce_con_dump(struct wq_pcie *wq_pcie, CE_CON_ID con)
{
	CE_CHN_UUID chn, chn_beg, chn_end;

	wq_ce_con_reg_dump(wq_pcie, con);

	chn_beg = 0;
	chn_end = CE_CHN_MAX;

	for (chn = chn_beg; chn < chn_end; ++chn)
		wq_ce_chn_dump(wq_pcie, chn);
}

void wq_ce_everything_dump(struct wq_pcie *wq_pcie)
{
	CE_CON_ID con;

	for (con = 0; con < CE_CON_MAX; ++con)
		wq_ce_con_dump(wq_pcie, con);
}

void wq_ce_dump_chan_dbg_sts(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	u8 src_depth_log2;
	u16 src_hw_ridx, src_hw_widx;
	u32 src_intr_ena, src_intr_raw, src_intr_st;

	u8 dst_depth_log2;
	u16 dst_hw_ridx, dst_hw_widx;
	u32 dst_intr_ena, dst_intr_raw, dst_intr_st;

	u16 src_sw_ridx, src_sw_widx;
	u16 dst_sw_ridx, dst_sw_widx;

	bool irq_status;

	if (chn >= CE_CHN_MAX)
		return;

	src_sw_ridx = src_sw_widx = dst_sw_ridx = dst_sw_widx = 0xffff;

	hw_ce_chn_rx_ring_cfg_get(wq_pcie, chn, &src_depth_log2, NULL, NULL);

	src_hw_ridx = hw_ce_chn_rx_ridx_get(wq_pcie, chn);
	src_hw_widx = hw_ce_chn_rx_widx_get(wq_pcie, chn);

	src_intr_ena = hw_ce_chn_rx_int_ena_get(wq_pcie, chn);
	src_intr_raw = hw_ce_chn_rx_int_raw_get(wq_pcie, chn);
	src_intr_st = hw_ce_chn_rx_int_st_get(wq_pcie, chn);

	hw_ce_chn_tx_ring_cfg_get(wq_pcie, chn, &dst_depth_log2, NULL, NULL);

	dst_hw_ridx = hw_ce_chn_tx_ridx_get(wq_pcie, chn);
	dst_hw_widx = hw_ce_chn_tx_widx_get(wq_pcie, chn);

	dst_intr_ena = hw_ce_chn_tx_int_ena_get(wq_pcie, chn);
	dst_intr_raw = hw_ce_chn_tx_int_raw_get(wq_pcie, chn);
	dst_intr_st = hw_ce_chn_tx_int_st_get(wq_pcie, chn);

	if (wq_pcie->ce_states[chn].src) {
		src_sw_ridx = wq_pcie->ce_states[chn].src->ring.sw_ridx;
		src_sw_widx = wq_pcie->ce_states[chn].src->ring.sw_widx;
	}

	if (wq_pcie->ce_states[chn].dst) {
		dst_sw_ridx = wq_pcie->ce_states[chn].dst->ring.sw_ridx;
		dst_sw_widx = wq_pcie->ce_states[chn].dst->ring.sw_widx;
	}

	irq_status = wq_pcie_irq_status_get(wq_pcie, ce_chn_to_irq_vec_map[chn]);

	WQ_DBG(DM_GENERIC, DL_ERR,
	       "ce:chn:%d,hw:%u,%d,%d,0x%x,0x%x,0x%x,%u,%d,%d,0x%x,0x%x,0x%x,"
	       "sw:%d,%d,%d,%d,%d\n",
	       chn, (1u << src_depth_log2), src_hw_ridx, src_hw_widx,
	       src_intr_ena, src_intr_raw, src_intr_st, (1u << dst_depth_log2),
	       dst_hw_ridx, dst_hw_widx, dst_intr_ena, dst_intr_raw,
	       dst_intr_st, src_sw_ridx, src_sw_widx, dst_sw_ridx, dst_sw_widx, irq_status);
}
