/** @file ce.h
 *
 *  @brief This file contains definitions for CopyEngine driver.
 *
 * Copyright (C) 2016-2023, WuQi Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */

#ifndef _CE_H_
#define _CE_H_

// CE: CopyEngine

#include <linux/types.h>

#include "utils.h"

/**
 * CE basic define
*/
#ifndef CE_BASIC_DEF
#define CE_BASIC_DEF

// #define CE_DEBUG

/**
 * CE controller and channel maxnum
*/
#define CE_CON_MAX 1
#define CE_CHN_MAX 12

/**
 * CE attr channel configuration
*/
#define CE_ATTR_FLAG_DATA_WORD_ORDER_OFFSET 13
#define CE_ATTR_FLAG_DATA_WORD_ORDER_MASK 0x00002000

#define CE_ATTR_FLAG_DATA_WORD_ORDER 0x00002000

/**
 * CE descript configuration
*/
#define CE_DESC_FLAG_METADATA_OFFSET 28
#define CE_DESC_FLAG_METADATA_MASK 0xf0000000
#define CE_DESC_FLAG_CRC_EB_OFFSET 27
#define CE_DESC_FLAG_CRC_EB_MASK 0x08000000
#define CE_DESC_FLAG_LAST_EB_OFFSET 26
#define CE_DESC_FLAG_LAST_EB_MASK 0x04000000
#define CE_DESC_FLAG_GATHER_EB_OFFSET 25
#define CE_DESC_FLAG_GATHER_EB_MASK 0x02000000
#define CE_DESC_FLAG_INT_EB_OFFSET 24
#define CE_DESC_FLAG_INT_EB_MASK 0x01000000
#define CE_DESC_FLAG_CRC_MODE_OFFSET 22
#define CE_DESC_FLAG_CRC_MODE_MASK 0x00c00000
#define CE_DESC_FLAG_STATUS_OFFSET 20
#define CE_DESC_FLAG_STATUS_MASK 0x00300000

#define CE_DESC_FLAG_CRC_MODE_8 0x00000000
#define CE_DESC_FLAG_CRC_MODE_16 0x00400000
#define CE_DESC_FLAG_CRC_MODE_24 0x00800000
#define CE_DESC_FLAG_CRC_MODE_32 0x00c00000
#define CE_DESC_FLAG_CRC_EB 0x08000000
#define CE_DESC_FLAG_GATHER_EB 0x02000000
#define CE_DESC_FLAG_INT_EB 0x01000000

#define CE_WHETHER_SUPPORT_CRC(chn) ((chn) < 6) // 0~5 crc ability

/* public enum define */

/**
 * CE channel src interrupt event
*/
typedef enum {
	CE_SRC_INT_RING_LOW_WATER,
	CE_SRC_INT_RING_HIGH_WATER,
	CE_SRC_INT_RING_OVERFLOW,
	CE_SRC_INT_CURR_DESC,
	CE_SRC_INT_ACK,
	CE_SRC_INT_BUF_LEN_INVALID,
	CE_SRC_INT_CURR_DESC_PRE,
	CE_SRC_INT_EVT_MAX,
} CE_SRC_INT_EVENT;

/**
 * CE channel dst interrupt event
*/
typedef enum {
	CE_DST_INT_RING_LOW_WATER,
	CE_DST_INT_RING_HIGH_WATER,
	CE_DST_INT_RING_OVERFLOW,
	CE_DST_INT_CURR_DESC,
	CE_DST_INT_ACK,
	CE_DST_INT_BUFFER_MLEN_VIO,
	CE_DST_INT_CURR_DESC_PRE,
	CE_DST_INT_EVT_MAX,
} CE_DST_INT_EVENT;

typedef u8 CE_CON_ID;

/**
 * CE channel UUID
 * In implementation, ce0 has 12 channels for WIFI to use.
*/
typedef u8 CE_CHN_UUID;

#endif // CE_BASIC_DEF

struct wq_pcie;

#define WQ_CE_FIFO_DEPTH_MAX 8192
#define WQ_CE_SRC_SZ_MAX_UPLIMIT 1048575

#define WQ_CE_SRC_SZ_MAX_IGNORE_SRC WQ_CE_SRC_SZ_MAX_UPLIMIT
#define WQ_CE_SRC_SZ_MAX_IGNORE_DST 1

#define WQ_CE_SRC_SZ_MAX(wq_pcie, chn)                                         \
	(wq_ce_attr_get(wq_pcie, chn)->src_sz_max)
#define WQ_CE_CHN_INITED(wq_pcie, chn)                                         \
	({                                                                     \
		const wq_ce_attr_t *attr = wq_ce_attr_get(wq_pcie, chn);       \
		attr->src_depth > 0 || attr->dst_depth > 0;                    \
	})

#define WQ_CE_SRC_FIFO_DEPTH(wq_pcie, chn)                                     \
	(wq_ce_attr_get(wq_pcie, chn)->src_depth)
#define WQ_CE_DST_FIFO_DEPTH(wq_pcie, chn)                                     \
	(wq_ce_attr_get(wq_pcie, chn)->dst_depth)

#define WQ_CE_FIFO_LFO(wq_pcie, chn, dir)                                      \
	({                                                                     \
		u16 lfo;                                                       \
		(void)wq_ce_fifo_lfo_get(wq_pcie, chn, dir, &lfo);             \
		lfo;                                                           \
	})
#define WQ_CE_FIFO_FULL(wq_pcie, chn, dir)                                     \
	(WQ_CE_FIFO_LFO(wq_pcie, chn, dir) == 0)

#define CE_ANYSIDE_INT_EVT_MAX CE_SRC_INT_EVT_MAX

typedef enum {
	WQ_CE_CHN_SRC,
	WQ_CE_CHN_DST,
} WQ_CE_CHN_DIR;

typedef void (*wq_ce_int_cb_t)(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);

typedef struct {
	u32 flags;
	u32 src_sz_max;
	u16 src_depth;
	u16 dst_depth;
	void (*desc_completed)(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);
} wq_ce_attr_t;

typedef wq_ce_attr_t wq_ce_attr_assign_t;

/**
 * src/dst desc structure
*/
struct hw_ce_desc {
	__le32 flags;
	__le32 phys_addr;
};

/**
 * src/dst ring information includes software managed
 * read/write pointer, ring structure and all event interrupt callbacks
*/
struct ce_ring {
	u16 depth_log2;
	u16 depth_mask; // used by WQ_CE_RING_IDX_INCR
	u16 sw_widx;
	u16 widx_incr; // for send gather
	u32 gather_nbytes;
	u16 sw_ridx; // ring index: sw_ridx <= hw_ridx <= hw_widx <= sw_widx
	u16 hw_ridx; // cache copy from hardware
	u32 is_flow_ctrl;
	Q_STATS(stats)

	struct hw_ce_desc *desc_base_virt_addr; // virtual address of desc_ring
	dma_addr_t desc_base_phys_addr; // physical address of desc_ring
	void *transfer_context[0];
};

struct ce_side {
	spinlock_t lock;
	u32 irq_vec;
	wq_ce_int_cb_t int_cbs[CE_ANYSIDE_INT_EVT_MAX];
	struct ce_ring ring;
};

struct ce_state {
	struct ce_side *src;
	struct ce_side *dst;
};

/**
 * public wq_ce interface
*/
int wq_ce_isr(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);
u32 wq_ce_get_irq_vector_from_chn(CE_CHN_UUID chn);
CE_CHN_UUID wq_ce_get_chn_from_irq_vec(u32 vector);

/**
 * @brief: init src/dst configures for CE channel
 * @param: CE channel UUID
*/
int wq_ce_chn_init(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		   const wq_ce_attr_assign_t *attr_assign);

/**
 * @brief: deinitialize channel
 * @param: CE channel UUID
*/
int wq_ce_chn_deinit(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);

/**
 * @brief: get CE channel table
 * @param: CE channel UUID
*/
const wq_ce_attr_t *wq_ce_attr_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);

/**
 * @brief: get low and high watermarks
 * @param: CE channel UUID
*/
int wq_ce_watermarks_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			 WQ_CE_CHN_DIR dir, u16 *high, u16 *low);

/**
 * @brief: set low and high watermarks
 * @param: CE channel UUID
*/
int wq_ce_watermarks_set(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			 WQ_CE_CHN_DIR dir, u16 high, u16 low);

/**
 * @brief: get CE channel interrupt callback
 * @param: CE channel UUID
*/
int wq_ce_int_cb_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		     WQ_CE_CHN_DIR dir, u8 evt, wq_ce_int_cb_t *cb);

/**
 * @brief: set CE channel interrupt callback
 * @param: CE channel UUID
*/
int wq_ce_int_cb_set(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		     WQ_CE_CHN_DIR dir, u8 evt, wq_ce_int_cb_t cb);

/**
 * @brief: get CE channel interrupt enas
 * @param: CE channel UUID
*/
int wq_ce_int_ena_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		      WQ_CE_CHN_DIR dir, u8 evt, bool *ena);

/**
 * @brief: set CE channel interrupt enas
 * @param: CE channel UUID
*/
int wq_ce_int_ena_set(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		      WQ_CE_CHN_DIR dir, u8 evt, bool ena);

/**
 * @brief: channel irq unmask
 * @param: CE channel UUID
*/
int wq_ce_irq_unmask(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		     WQ_CE_CHN_DIR dir);

/**
 * @brief: channel irq mask
 * @param: CE channel UUID
*/
int wq_ce_irq_mask(struct wq_pcie *wq_pcie, CE_CHN_UUID chn, WQ_CE_CHN_DIR dir);

/**
 * @brief: send once
 * @param: CE channel UUID
*/
int wq_ce_send(struct wq_pcie *wq_pcie, CE_CHN_UUID chn, void *send_context,
	       dma_addr_t phys_addr, u32 nbytes, u32 flags);

/**
 * @brief: iterator to get completed entries
 * @param: CE channel UUID
*/
int wq_ce_send_completed_next(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			      void **send_context, dma_addr_t *phys_addr,
			      u32 *nbytes, u32 *flags);

/**
 * @brief: recv once
 * @param: CE channel UUID
*/
int wq_ce_recv(struct wq_pcie *wq_pcie, CE_CHN_UUID chn, void *recv_context,
	       dma_addr_t phys_addr, u32 nbytes, u32 flags);

/**
 * @brief: iterator to get completed entries
 * @param: CE channel UUID
*/
int wq_ce_recv_completed_next(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			      void **recv_context, dma_addr_t *phys_addr,
			      u32 *nbytes, u32 *flags);

/**
 * @brief: get ring fifo state
 * @param: CE channel UUID
*/
int wq_ce_fifo_lfo_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		       WQ_CE_CHN_DIR dir, u16 *lfo);

/**
 * @brief: get ring fifo accurate gather nbytes
 * @param: CE channel UUID
*/
int wq_ce_send_gather_statistics_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
				     u16 *gather_count, u32 *gather_nbytes);

void wq_ce_chn_ring_dump(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);

#ifdef CE_DEBUG
/**
 * @brief: debug channel desc word
 * @param: CE channel UUID
*/
/*
int wq_ce_info_dbg_bus_get(struct wq_pcie *wq_pcie, CE_CHN_UUID chn, u32 words[CE_DBG_BUS_SEL_MAX]);
*/
#endif // CE_DEBUG

int wq_ce_intr_ena_set(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
		       WQ_CE_CHN_DIR dir, u32 ena);

int wq_ce_stop(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);

int wq_ce_send_cancel_next(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			   void **send_context, dma_addr_t *phys_addr,
			   u32 *nbytes, u32 *flags);

int wq_ce_recv_revoke_next(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			   void **recv_context, dma_addr_t *phys_addr,
			   u32 *nbytes, u32 *flags);

void wq_ce_recv_dummy_push(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			   void *recv_context, dma_addr_t phys_addr, u32 nbytes,
			   u32 flags);

void wq_ce_recv_dummy_pop(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
			  void **recv_context, dma_addr_t *phys_addr,
			  u32 *nbytes, u32 *flags);

void wq_ce_everything_dump(struct wq_pcie *wq_pcie);
void wq_ce_chn_dump(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);

void wq_ce_dump_chan_dbg_sts(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);

#endif // _CE_H_
