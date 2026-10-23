#ifndef _WQ_IPC_H
#define _WQ_IPC_H

#include "fw_api/wifi/htc/htc_v0.h"
#include "fw_api/wifi/mac/dp_tx.h"
#include "rwnx_defs.h"

#include <linux/skbuff.h>

#define HEADROOM_HIF_HTC                                                       \
	(u32)(hif_get_hdr_sz(wq_core_get()) + sizeof(struct wq_htc_v0))
#define HEADROOM_TXDESC (u32)(sizeof(struct txdesc_host))
#define HEADROOM_HIF_HTC_TXDESC (u32)(HEADROOM_HIF_HTC + HEADROOM_TXDESC)
#define TAILROOM_HIF (u32)(WQ_HIF_TRAILER_SPACE_RSVD)
#define BUNDLE_HDR_LEN (u32)(sizeof(struct rwnx_tx_bundle_head))

struct wq_core;
struct wq_skb_txcb;

#define MAX_MSDU_TXDONE_ENTRIES 4096
struct macdone_entry {
	struct list_head entry;
	addr32 addr;
};

struct msdu_txdone {
	struct sk_buff_head hifdone;
	struct {
		struct list_head head;
		struct list_head free;
		struct macdone_entry pool[MAX_MSDU_TXDONE_ENTRIES];
	} macdone;
};

struct wq_ipc {
	/* htc TX done, wait for firmware confirm (MAC TX done) */
	struct sk_buff_head mgmt_txdone;

	struct msdu_txdone msdu;
};

int wq_ipc_init(struct wq_core *core);
void wq_ipc_deinit(struct wq_core *core);

int wq_tx_skb_dma_map(struct wq_core *core, struct sk_buff *skb,
		      struct wq_skb_txcb *txcb);
void wq_tx_skb_dma_unmap(struct wq_core *core, struct sk_buff *skb);

void wq_tx_skb_free_any(struct wq_core *core, struct sk_buff *skb);

int wq_ipc_tx_msg(struct wq_core *core, bool is_pwr_cmd, u8 *cmd_buf,
		  u32 cmd_len);

int wq_ipc_tx_pkt_bundle(struct wq_core *core, u8 hw_txq,
			 struct sk_buff_head *skbq);
int wq_ipc_tx_pkt_bundle_usb(struct wq_core *core, u8 hw_txq,
			     struct sk_buff_head *skbq);
void wq_ipc_txq_ring_free(struct wq_core *core);
void wq_ipc_txq_ring_2task(struct wq_core *core);
void wq_ipc_txq_ring_start_timer(struct wq_core *core);

static inline int wq_ipc_tx_pkt(struct wq_core *core, u8 hw_txq,
				struct sk_buff *skb)
{
	struct sk_buff_head skbq;

	__skb_queue_head_init(&skbq);
	__skb_queue_tail(&skbq, skb);
	return wq_ipc_tx_pkt_bundle(core, hw_txq, &skbq);
}

int wq_ipc_tx_pkt_done_pre(struct wq_core *core, struct sk_buff *skb, int status);
int wq_ipc_tx_msg_done(struct htc_q *q, struct sk_buff *skb);
int wq_ipc_tx_pkt_done(struct htc_q *q, struct sk_buff *skb);

void wq_ipc_mgmt_txdone_clean(struct wq_core *core);
void wq_ipc_msdu_txdone_clean(struct wq_core *core);

int wq_ipc_msdu_tx_is_all_done(struct wq_core *core);

int __wq_ipc_rx_pkt(struct rwnx_hw *hw, struct sk_buff *skb);

#endif /* _WQ_IPC_H */
