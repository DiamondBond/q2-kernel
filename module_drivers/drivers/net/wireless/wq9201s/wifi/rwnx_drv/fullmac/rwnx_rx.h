/**
 ******************************************************************************
 *
 * @file rwnx_rx.h
 *
 * Copyright (C) RivieraWaves 2012-2020
 *
 ******************************************************************************
 */
#ifndef _RWNX_RX_H_
#define _RWNX_RX_H_

#include <linux/workqueue.h>
#include "hal_desc.h"

enum rx_status_bits {
	/// The buffer can be forwarded to the networking stack
	RX_STAT_FORWARD = 1 << 0,
	/// A new buffer has to be allocated
	RX_STAT_ALLOC = 1 << 1,
	/// The buffer has to be deleted
	RX_STAT_DELETE = 1 << 2,
	/// The length of the buffer has to be updated
	RX_STAT_LEN_UPDATE = 1 << 3,
	/// The length in the Ethernet header has to be updated
	RX_STAT_ETH_LEN_UPDATE = 1 << 4,
	/// Simple copy
	RX_STAT_COPY = 1 << 5,
	/// Spurious frame (inform upper layer and discard)
	RX_STAT_SPURIOUS = 1 << 6,
	/// packet for monitor interface
	RX_STAT_MONITOR = 1 << 7,
};

#ifdef CONFIG_RWNX_MON_DATA
#define RX_MACHDR_BACKUP_LEN 64

/// MAC header backup descriptor
struct mon_machdrdesc {
	/// Length of the buffer
	u32 buf_len;
	/// Buffer containing mac header, LLC and SNAP
	u8 buffer[RX_MACHDR_BACKUP_LEN];
};
#endif

/**
 * struct rwnx_defer_rx - Defer rx buffer processing
 *
 * @skb: List of deferred buffers
 * @work: work to defer processing of this buffer
 */
struct rwnx_defer_rx {
	struct sk_buff_head sk_list;
	struct work_struct work;
};

/**
 * struct rwnx_defer_rx_cb - Control buffer for deferred buffers
 *
 * @vif: VIF that received the buffer
 */
struct rwnx_defer_rx_cb {
	struct rwnx_vif *vif;
};

u8 rwnx_rxdataind(struct rwnx_hw *rwnx_hw, struct sk_buff *skb);
u8 rwnx_rxdataind_ll(struct rwnx_hw *rwnx_hw, struct sk_buff *skb);

struct rwnx_vif *rwnx_rx_get_vif(struct rwnx_hw *rwnx_hw, int vif_idx);

bool rwnx_rx_data_skb(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
		      struct sk_buff *skb, struct hw_rxhdr *rxhdr);

void rwnx_rx_deferred(struct work_struct *ws);
void rwnx_rx_defer_skb(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
		       struct sk_buff *skb);

bool check_roc_ignore_nego_req(uint8_t *frame_ctrl, int len, int set_roc,
			       unsigned int roc_duration);
void rwnx_rx_cntrl_any(struct rwnx_hw *rwnx_hw, struct sk_buff *skb,
                                    struct hw_rxhdr *hw_rxhdr);
#endif /* _RWNX_RX_H_ */
