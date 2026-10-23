/**
 ******************************************************************************
 *
 * @file rwnx_txq.c
 *
 * Copyright (C) RivieraWaves 2016-2020
 *
 ******************************************************************************
 */
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <net/sock.h>

#include "rwnx_defs.h"
#include "rwnx_tx.h"
#include "rwnx_events.h"
#include "hif_api.h"
#include "wq_ipc.h"
#include "wq_log.h"
#include "usb.h"
#include "sdio.h"
#include "wq_profiling.h"
#include "ieee80211_extap.h"

#define NX_TXDESC_CNT0 8
#define NX_TXDESC_CNT1 64
#define NX_TXDESC_CNT2 64
#define NX_TXDESC_CNT3 32
#if NX_TXQ_CNT == 5
#define NX_TXDESC_CNT4 8
#endif

u16 NX_REMOTE_STA_MAX;
u8 NX_VIRT_DEV_MAX;
u16 NX_NB_TXQ;
u16 NX_FIRST_VIF_TXQ_IDX;
u16 NX_FIRST_BCMC_TXQ_IDX;
u16 NX_FIRST_UNK_TXQ_IDX;
u16 NX_OFF_CHAN_TXQ_IDX;
u16 NX_NB_NDEV_TXQ;
u16 NX_BCMC_TXQ_NDEV_IDX;

void global_data_init(u8 vif_max, u16 sta_max)
{
    NX_NB_TXQ = ((NX_NB_TXQ_PER_STA * sta_max) + (NX_NB_TXQ_PER_VIF * vif_max) + 1);
    NX_FIRST_VIF_TXQ_IDX = (sta_max * NX_NB_TXQ_PER_STA);
    NX_FIRST_BCMC_TXQ_IDX = NX_FIRST_VIF_TXQ_IDX;
    NX_FIRST_UNK_TXQ_IDX = (NX_FIRST_BCMC_TXQ_IDX + vif_max);
    NX_OFF_CHAN_TXQ_IDX = (NX_FIRST_VIF_TXQ_IDX + (vif_max * NX_NB_TXQ_PER_VIF));
    NX_NB_NDEV_TXQ = ((NX_NB_TID_PER_STA * sta_max) + 1);
    NX_BCMC_TXQ_NDEV_IDX = (NX_NB_TID_PER_STA * sta_max);
}

static const int nx_txdesc_cnt[] = {
	NX_TXDESC_CNT0, NX_TXDESC_CNT1, NX_TXDESC_CNT2, NX_TXDESC_CNT3,
#if NX_TXQ_CNT == 5
	NX_TXDESC_CNT4,
#endif
};

/******************************************************************************
 * Utils functions
 *****************************************************************************/
const int nx_tid_prio[NX_NB_TID_PER_STA] = { 7, 6, 5, 4, 3, 0, 2, 1 };

static inline int rwnx_txq_sta_idx(struct rwnx_sta *sta, u8 tid)
{
	if (is_multicast_sta(sta->sta_idx))
		return NX_FIRST_VIF_TXQ_IDX + sta->vif_idx;
	else
		return (sta->sta_idx * NX_NB_TXQ_PER_STA) + tid;
}

static inline int rwnx_txq_vif_idx(struct rwnx_vif *vif, u8 type)
{
	return NX_FIRST_VIF_TXQ_IDX + master_vif_idx(vif) +
	       (type * NX_VIRT_DEV_MAX);
}

struct rwnx_txq *rwnx_txq_sta_get(struct rwnx_sta *sta, u8 tid,
				  struct rwnx_hw *rwnx_hw)
{
	if (tid >= NX_NB_TXQ_PER_STA)
		tid = 0;

	if (rwnx_txq_sta_idx(sta, tid) >= NX_NB_TXQ) {
		WQ_DBG(DM_TX, DL_ERR, "%s: sta_idx:%d, txqidx:%d, txqmax:%d",
		       __func__, sta->sta_idx, rwnx_txq_sta_idx(sta, tid),
		       NX_NB_TXQ);
		BUG_ON(1);
	}

	return &rwnx_hw->txq[rwnx_txq_sta_idx(sta, tid)];
}

struct rwnx_txq *rwnx_txq_vif_get(struct rwnx_vif *vif, u8 type)
{
	if (type > NX_UNK_TXQ_TYPE)
		type = NX_BCMC_TXQ_TYPE;

	if (rwnx_txq_vif_idx(vif, type) >= NX_NB_TXQ) {
		WQ_DBG(DM_TX, DL_ERR, "%s: vif_idx:%d, txqidx:%d, txqmax:%d",
		       __func__, vif->vif_index, rwnx_txq_vif_idx(vif, type),
		       NX_NB_TXQ);
		BUG_ON(1);
	}

	return &vif->rwnx_hw->txq[rwnx_txq_vif_idx(vif, type)];
}

static inline struct rwnx_sta *rwnx_txq_2_sta(struct rwnx_txq *txq)
{
	return txq->sta;
}

/**
 * rwnx_txq_skb_ready - Check if skb are available for the txq
 *
 * @txq: Pointer on txq
 * @return True if there are buffer ready to be pushed on this txq,
 * false otherwise
 */
static inline bool rwnx_txq_skb_ready(struct rwnx_txq *txq)
{
#ifdef CONFIG_MAC80211_TXQ
	if (txq->nb_ready_mac80211 != NOT_MAC80211_TXQ)
		return ((txq->nb_ready_mac80211 > 0) ||
			!skb_queue_empty(&txq->sk_list));
	else
#endif
		return (!skb_queue_empty(&txq->sk_list) ||
			!skb_queue_empty(&txq->bundle.list) ||
			!skb_queue_empty(&txq->sk_ack_list));
}

static enum hrtimer_restart rwnx_txq_trampoline(struct hrtimer *timer);
static enum hrtimer_restart rwnx_txq_ack_trampoline(struct hrtimer *timer);

/******************************************************************************
 * Init/Deinit functions
 *****************************************************************************/
/**
 * rwnx_txq_init - Initialize a TX queue
 *
 * @txq: TX queue to be initialized
 * @idx: TX queue index
 * @status: TX queue initial status
 * @hwq: Associated HW queue
 * @ndev: Net device this queue belongs to
 *        (may be null for non netdev txq)
 *
 * Each queue is initialized with the credit of @NX_TXQ_INITIAL_CREDITS.
 */
static void rwnx_txq_init(struct rwnx_txq *txq, int idx, u8 status,
			  struct rwnx_hwq *hwq, int tid, struct rwnx_sta *sta,
			  struct net_device *ndev)
{
	int i;

	txq->idx = idx;
	txq->status = status;
	txq->credits = NX_TXQ_INITIAL_CREDITS;
	txq->pkt_sent = 0;

	/* mark this txq that's not pushed into hwq */
	txq->sched_list.next = LIST_POISON1;
	txq->sched_list.prev = LIST_POISON2;

	skb_queue_head_init(&txq->sk_list);
	skb_queue_head_init(&txq->sk_ack_list);
	txq->nb_retry = 0;
	txq->hwq = hwq;
	txq->sta = sta;
	for (i = 0; i < CONFIG_USER_MAX; i++)
		txq->pkt_pushed[i] = 0;
	txq->push_limit = 0;
	txq->tid = tid;
#ifdef CONFIG_MAC80211_TXQ
	txq->nb_ready_mac80211 = 0;
#endif
	txq->ps_id = LEGACY_PS_ID;
	if (idx < NX_FIRST_VIF_TXQ_IDX) {
		int sta_idx = sta->sta_idx;
		int tid = idx - (sta_idx * NX_NB_TXQ_PER_STA);
		if (tid < NX_NB_TID_PER_STA)
			txq->ndev_idx = NX_STA_NDEV_IDX(tid, sta_idx);
		else
			txq->ndev_idx = NDEV_NO_TXQ;
	} else if (idx < NX_FIRST_UNK_TXQ_IDX) {
		txq->ndev_idx = NX_BCMC_TXQ_NDEV_IDX;
	} else {
		txq->ndev_idx = NDEV_NO_TXQ;
	}
	txq->ndev = ndev;
#ifdef CONFIG_RWNX_AMSDUS_TX
#if defined(CONFIG_HIGH_RES_TIMERS) && defined(USE_HRT)
	hrtimer_init(&txq->amsdu_timer, CLOCK_MONOTONIC,
		HRTIMER_MODE_REL);
	txq->amsdu_timer.function = rwnx_tx_amsdu_timeout_cb;
	txq->txq_in_amsdu_list = false;
#else
	timer_setup(&txq->amsdu_timer, rwnx_tx_amsdu_timeout_cb, 0);
	txq->amsdu_len = 0;
#endif
#endif /* CONFIG_RWNX_AMSDUS_TX */

	q_stats_init(&txq->stats, 0);

	atomic_set(&txq->sending, 0);
	skb_queue_head_init(&txq->bundle.list);
	hrtimer_init(&txq->bundle.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	txq->bundle.timer.function = rwnx_txq_trampoline;
	/* for ack timer */
	hrtimer_init(&txq->bundle.ack_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	txq->bundle.ack_timer.function = rwnx_txq_ack_trampoline;
}

/**
 * rwnx_txq_drop_skb - Drop the buffer skb from the TX queue
 *
 * @rwnx_hw:      Driver main data
 * @txq:          TX queue
 * @skb:          skb packet that should be dropped.
 *
 */
static void rwnx_txq_drop_skb(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq,
			      struct sk_buff *skb)
{
	skb_unlink(skb, &txq->sk_list);
	wq_tx_skb_free_any(rwnx_hw->core, skb);
}

/**
 * rwnx_txq_drop_skb - Drop the buffer skb from the TX queue
 *
 * @rwnx_hw:      Driver main data
 * @txq:          TX queue
 * @skb:          skb packet that should be dropped.
 *
 */
static void rwnx_txq_drop_ack_skb(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq,
				  struct sk_buff *skb)
{
	skb_unlink(skb, &txq->sk_ack_list);
	wq_tx_skb_free_any(rwnx_hw->core, skb);
}

/**
 * rwnx_txq_flush - Flush all buffers queued for a TXQ
 *
 * @rwnx_hw: main driver data
 * @txq: txq to flush
 */
void rwnx_txq_flush(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq)
{
	int i, pushed = 0;
	struct sk_buff *skb;

	while ((skb = skb_peek(&txq->sk_list))) {
		rwnx_txq_tx_done_pre(rwnx_hw, WQ_SKB_TXCB(skb)->txq_idx);
		rwnx_txq_drop_skb(rwnx_hw, txq, skb);
	}

	for (i = 0; i < CONFIG_USER_MAX; i++) {
		pushed += txq->pkt_pushed[i];
	}

	if (pushed)
		dev_warn(rwnx_hw->dev, "TXQ[%d]: %d skb still pushed to the FW",
			 txq->idx, pushed);
}

/**
 * rwnx_txq_deinit - De-initialize a TX queue
 *
 * @rwnx_hw: Driver main data
 * @txq: TX queue to be de-initialized
 * Any buffer stuck in a queue will be freed.
 */
static void rwnx_txq_deinit(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq)
{
	struct sk_buff *skb;

	if (txq->idx == TXQ_INACTIVE)
		return;

	spin_lock_bh(&rwnx_hw->tx_lock);
	rwnx_txq_del_from_hw_list(txq);

	hrtimer_cancel(&txq->bundle.timer);
	while ((skb = __skb_dequeue(&txq->bundle.list))) {
		rwnx_txq_tx_done_pre(rwnx_hw, WQ_SKB_TXCB(skb)->txq_idx);
		wq_tx_skb_free_any(rwnx_hw->core, skb);
	}

	hrtimer_cancel(&txq->bundle.ack_timer);
	if (!skb_queue_empty(&txq->sk_ack_list)) {
		while ((skb = __skb_dequeue(&txq->sk_ack_list)) != NULL) {
			/* add buffer in the sk_list */
			skb_queue_tail(&txq->sk_list, skb);
		}
	}

	rwnx_amsdu_tx_drain(txq);
	txq->idx = TXQ_INACTIVE;
	spin_unlock_bh(&rwnx_hw->tx_lock);

	rwnx_txq_flush(rwnx_hw, txq);
}

/**
 * rwnx_txq_vif_init - Initialize all TXQ linked to a vif
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: Pointer on VIF
 * @status: Intial txq status
 *
 * Softmac : 1 VIF TXQ per HWQ
 *
 * Fullmac : 1 VIF TXQ for BC/MC
 *           1 VIF TXQ for MGMT to unknown STA
 */
void rwnx_txq_vif_init(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
		       u8 status)
{
	struct rwnx_txq *txq;
	int idx;

	txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
	idx = rwnx_txq_vif_idx(rwnx_vif, NX_BCMC_TXQ_TYPE);
	rwnx_txq_init(txq, idx, status, &rwnx_hw->hwq[RWNX_HWQ_BE], 0,
		      &rwnx_hw->sta_table[rwnx_vif->ap.bcmc_index],
		      rwnx_vif->ndev);

	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	idx = rwnx_txq_vif_idx(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_init(txq, idx, status, &rwnx_hw->hwq[RWNX_HWQ_VO], TID_MGT,
		      NULL, rwnx_vif->ndev);
}

/**
 * rwnx_txq_vif_deinit - Deinitialize all TXQ linked to a vif
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_vif_deinit(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	struct rwnx_txq *txq;

	txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
	rwnx_txq_deinit(rwnx_hw, txq);

	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_deinit(rwnx_hw, txq);

	rwnx_mgmt_tx_cb_list_deinit(rwnx_hw);
}

/**
 * rwnx_txq_sta_init - Initialize TX queues for a STA
 *
 * @rwnx_hw: Main driver data
 * @rwnx_sta: STA for which tx queues need to be initialized
 * @status: Intial txq status
 *
 * This function initialize all the TXQ associated to a STA.
 * Softmac : 1 TXQ per TID
 *
 * Fullmac : 1 TXQ per TID (limited to 8)
 *           1 TXQ for MGMT
 */
void rwnx_txq_sta_init(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta,
		       u8 status)
{
	struct rwnx_txq *txq;
	int tid, idx;
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[rwnx_sta->vif_idx];
	idx = rwnx_txq_sta_idx(rwnx_sta, 0);

	foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw)
	{
		rwnx_txq_init(txq, idx, status,
			      &rwnx_hw->hwq[rwnx_tid2hwq[tid]], tid, rwnx_sta,
			      rwnx_vif->ndev);
		txq->ps_id = rwnx_sta->uapsd_tids & (1 << tid) ? UAPSD_ID :
								 LEGACY_PS_ID;
		idx++;
	}

	rwnx_ipc_sta_buffer_init(rwnx_hw, rwnx_sta->sta_idx);
}

/**
 * rwnx_txq_sta_deinit - Deinitialize TX queues for a STA
 *
 * @rwnx_hw: Main driver data
 * @rwnx_sta: STA for which tx queues need to be deinitialized
 */
void rwnx_txq_sta_deinit(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta)
{
	struct rwnx_txq *txq;
	int tid;

	foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw)
	{
		rwnx_txq_deinit(rwnx_hw, txq);
	}
}

/**
 * rwnx_txq_unk_vif_init - Initialize TXQ for unknown STA linked to a vif
 *
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_unk_vif_init(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_txq *txq;
	int idx;

	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	idx = rwnx_txq_vif_idx(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_init(txq, idx, 0, &rwnx_hw->hwq[RWNX_HWQ_VO], TID_MGT, NULL,
		      rwnx_vif->ndev);
}

/**
 * rwnx_txq_unk_vif_deinit - Deinitialize TXQ for unknown STA linked to a vif
 *
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_unk_vif_deinit(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_txq *txq;

	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_deinit(rwnx_vif->rwnx_hw, txq);
}

/**
 * rwnx_txq_offchan_init - Initialize TX queue for the transmission on a offchannel
 *
 * @vif: Interface for which the queue has to be initialized
 *
 * NOTE: Offchannel txq is only active for the duration of the ROC
 */
void rwnx_txq_offchan_init(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_txq *txq;

	txq = &rwnx_hw->txq[NX_OFF_CHAN_TXQ_IDX];
	rwnx_txq_init(txq, NX_OFF_CHAN_TXQ_IDX, RWNX_TXQ_STOP_CHAN,
		      &rwnx_hw->hwq[RWNX_HWQ_VO], TID_MGT, NULL,
		      rwnx_vif->ndev);
}

/**
 * rwnx_deinit_offchan_txq - Deinitialize TX queue for offchannel
 *
 * @vif: Interface that manages the STA
 *
 * This function deintialize txq for one STA.
 * Any buffer stuck in a queue will be freed.
 */
void rwnx_txq_offchan_deinit(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_txq *txq;

	txq = &rwnx_vif->rwnx_hw->txq[NX_OFF_CHAN_TXQ_IDX];
	rwnx_txq_deinit(rwnx_vif->rwnx_hw, txq);
}

/**
 * rwnx_txq_tdls_vif_init - Initialize TXQ vif for TDLS
 *
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_tdls_vif_init(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;

	if (!(rwnx_hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return;

	rwnx_txq_unk_vif_init(rwnx_vif);
}

/**
 * rwnx_txq_tdls_vif_deinit - Deinitialize TXQ vif for TDLS
 *
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_tdls_vif_deinit(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;

	if (!(rwnx_hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return;

	rwnx_txq_unk_vif_deinit(rwnx_vif);
}

static void rwnx_txq_subqueue_try_to_wake(struct rwnx_hw *rwnx_hw,
					  struct rwnx_txq *txq)
{
	/* restart netdev queue if number no more queued buffer */
	if (unlikely(txq->status & RWNX_TXQ_NDEV_FLOW_CTRL) &&
		atomic_read(&txq->sending) < rwnx_hw->txq_restart_threshlod) {
		txq->status &= ~RWNX_TXQ_NDEV_FLOW_CTRL;
		netif_wake_subqueue(txq->ndev, txq->ndev_idx);
		trace_txq_flowctrl_restart(txq);
		printk_ratelimited(KERN_INFO "%s: netif_wake_subqueue(%d)\n",
				   __func__, txq->idx);
		PROFILING_CLR(SW_PROF_FLOW_CTRL);
	}
}

/**
 * rwnx_txq_drop_old_traffic - Drop pkt queued for too long in a TXQ
 *
 * @txq: TXQ to process
 * @rwnx_hw: Driver main data
 * @skb_timeout: Max queue duration, in jiffies, for this queue
 * @dropped: Updated to inidicate if at least one skb was dropped
 *
 * @return Whether there is still pkt queued in this queue.
 */
static bool rwnx_txq_drop_old_traffic(struct rwnx_txq *txq,
				      struct rwnx_hw *rwnx_hw,
				      unsigned long skb_timeout, bool *dropped)
{
	struct sk_buff *skb, *skb_next;
	bool pkt_queued = false;

	if (txq->idx == TXQ_INACTIVE)
		return false;

	spin_lock(&rwnx_hw->tx_lock);

	skb_queue_walk_safe(&txq->sk_list, skb, skb_next)
	{
		if (!time_after((unsigned long)jiffies,
				WQ_SKB_TXCB(skb)->jiffies + skb_timeout)) {
			pkt_queued = true;
			break;
		}

		*dropped = true;
		WQ_DBG(DM_TX, DL_ERR,
		       "%s: txqidx:%d, txqtid:%d, qid:%d, inhost:%d, pktlen:%d, "
		       "txqidx:%d, pktcls:0x%x\n",
		       __func__, txq->idx, txq->tid, WQ_SKB_TXCB(skb)->qid,
		       WQ_SKB_TXCB(skb)->msdu_in_host,
		       WQ_SKB_TXCB(skb)->pkt_len, WQ_SKB_TXCB(skb)->txq_idx,
		       WQ_SKB_TXCB(skb)->pkt_cls);
		//dump_bytes(DL_WRN, "dropped skb:", skb->data, skb->len);
		rwnx_txq_tx_done_pre(rwnx_hw, WQ_SKB_TXCB(skb)->txq_idx);
		rwnx_txq_drop_skb(rwnx_hw, txq, skb);

		if (txq->sta && txq->sta->ps.active) {
			txq->sta->ps.pkt_ready[txq->ps_id]--;
			if (txq->sta->ps.pkt_ready[txq->ps_id] == 0)
				rwnx_set_traffic_status(rwnx_hw, txq->sta,
							false, txq->ps_id);

			// drop packet during PS service period ...
			if (txq->sta->ps.sp_cnt[txq->ps_id]) {
				txq->sta->ps.sp_cnt[txq->ps_id]--;
				if (txq->push_limit)
					txq->push_limit--;
				if (WARN(((txq->ps_id == UAPSD_ID) &&
					  (txq->sta->ps.sp_cnt[txq->ps_id] ==
					   0)),
					 "Drop last packet of UAPSD service period")) {
					// TODO: inform FW to end SP
				}
			}
			trace_ps_drop(txq->sta);
		}
	}

	skb_queue_walk_safe(&txq->sk_ack_list, skb, skb_next)
	{
		if (!time_after((unsigned long)jiffies,
				WQ_SKB_TXCB(skb)->jiffies + skb_timeout)) {
			pkt_queued = true;
			break;
		}

		*dropped = true;
		WQ_DBG(DM_TX, DL_ERR,
		       "%s: ack txqidx:%d, txqtid:%d, qid:%d, inhost:%d, pktlen:%d, "
		       "txqidx:%d, pktcls:0x%x\n",
		       __func__, txq->idx, txq->tid, WQ_SKB_TXCB(skb)->qid,
		       WQ_SKB_TXCB(skb)->msdu_in_host,
		       WQ_SKB_TXCB(skb)->pkt_len, WQ_SKB_TXCB(skb)->txq_idx,
		       WQ_SKB_TXCB(skb)->pkt_cls);
		dump_bytes(DL_WRN, "dropped ack skb:", skb->data, skb->len);
		rwnx_txq_tx_done_pre(rwnx_hw, WQ_SKB_TXCB(skb)->txq_idx);
		rwnx_txq_drop_ack_skb(rwnx_hw, txq, skb);
	}

	if (!rwnx_txq_skb_ready(txq)) {
		rwnx_txq_del_from_hw_list(txq);
		txq->pkt_sent = 0;
	}

	spin_unlock(&rwnx_hw->tx_lock);

	rwnx_txq_subqueue_try_to_wake(rwnx_hw, txq);

	return pkt_queued;
}

/**
 * rwnx_txq_drop_ap_vif_old_traffic - Drop pkt queued for too long in TXQs
 * linked to an "AP" vif (AP, MESH, P2P_GO)
 *
 * @vif: Vif to process
 * @return Whether there is still pkt queued in any TXQ.
 */
static bool rwnx_txq_drop_ap_vif_old_traffic(struct rwnx_vif *vif)
{
	struct rwnx_sta *sta;
	unsigned long timeout = (vif->ap.bcn_interval * HZ * 3) >> 10;
	bool pkt_queued = false;
	bool pkt_dropped_bcmc = false, pkt_dropped_unk = false, pkt_dropped_tx = false;

	// Should never be needed but still check VIF queues
	rwnx_txq_drop_old_traffic(rwnx_txq_vif_get(vif, NX_BCMC_TXQ_TYPE),
				  vif->rwnx_hw, RWNX_TXQ_MAX_QUEUE_JIFFIES,
				  &pkt_dropped_bcmc);
	rwnx_txq_drop_old_traffic(rwnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE),
				  vif->rwnx_hw, RWNX_TXQ_MAX_QUEUE_JIFFIES,
				  &pkt_dropped_unk);
	if (pkt_dropped_bcmc ||  pkt_dropped_unk) {
		if (vif->rwnx_hw->core->hif_ops->hif == WQ_HIF_USB) {
			WQ_DBG(DM_TX, DL_ERR, "[auto]msg:Dropped packet in BCMC/UNK queue");
		} else {
			WARN(true, "Dropped packet in BCMC/UNK queue");
		}
	}

	list_for_each_entry (sta, &vif->ap.sta_list, list) {
		struct rwnx_txq *txq;
		int tid;
		foreach_sta_txq(sta, txq, tid, vif->rwnx_hw)
		{
			pkt_queued |= rwnx_txq_drop_old_traffic(
				txq, vif->rwnx_hw,
				timeout * sta->listen_interval, &pkt_dropped_tx);
		}
	}

	return pkt_queued;
}

/**
 * rwnx_txq_drop_sta_vif_old_traffic - Drop pkt queued for too long in TXQs
 * linked to a "STA" vif. In theory this should not be required as there is no
 * case where traffic can accumulate in a STA interface.
 *
 * @vif: Vif to process
 * @return Whether there is still pkt queued in any TXQ.
 */
static bool rwnx_txq_drop_sta_vif_old_traffic(struct rwnx_vif *vif)
{
	struct rwnx_txq *txq;
	bool pkt_queued = false, pkt_dropped = false;
	int tid;

	if (vif->tdls_status == TDLS_LINK_ACTIVE) {
		txq = rwnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE);
		pkt_queued |=
			rwnx_txq_drop_old_traffic(txq, vif->rwnx_hw,
						  RWNX_TXQ_MAX_QUEUE_JIFFIES,
						  &pkt_dropped);
		foreach_sta_txq(vif->sta.tdls_sta, txq, tid, vif->rwnx_hw)
		{
			pkt_queued |= rwnx_txq_drop_old_traffic(
				txq, vif->rwnx_hw, RWNX_TXQ_MAX_QUEUE_JIFFIES,
				&pkt_dropped);
		}
	}

	if (vif->sta.ap) {
		foreach_sta_txq(vif->sta.ap, txq, tid, vif->rwnx_hw)
		{
			pkt_queued |= rwnx_txq_drop_old_traffic(
				txq, vif->rwnx_hw, RWNX_TXQ_MAX_QUEUE_JIFFIES,
				&pkt_dropped);
		}
	}

	if (pkt_dropped)
		netdev_warn(vif->ndev, "Dropped packet in STA interface TXQs");
	return pkt_queued;
}

/**
 * rwnx_txq_cleanup_timer_cb - callack for TXQ cleaup timer
 * Used to prevent pkt to accumulate in TXQ. The main use case is for AP
 * interface with client in Power Save mode but just in case all TXQs are
 * checked.
 *
 * @t: timer structure
 */
static void rwnx_txq_cleanup_timer_cb(struct timer_list *t)
{
	struct rwnx_hw *rwnx_hw = from_timer(rwnx_hw, t, txq_cleanup);
	struct rwnx_vif *vif;
	bool pkt_queue = false;

	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		switch (RWNX_VIF_TYPE(vif)) {
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_P2P_GO:
		case NL80211_IFTYPE_MESH_POINT:
			pkt_queue |= rwnx_txq_drop_ap_vif_old_traffic(vif);
			break;
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_P2P_CLIENT:
			pkt_queue |= rwnx_txq_drop_sta_vif_old_traffic(vif);
			break;
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
		default:
			continue;
		}
	}

	if (pkt_queue)
		mod_timer(t, jiffies + RWNX_TXQ_CLEANUP_INTERVAL);
}

/**
 * rwnx_txq_start_cleanup_timer - Start 'cleanup' timer if not started
 *
 * @rwnx_hw: Driver main data
 */
void rwnx_txq_start_cleanup_timer(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	if (sta && !is_multicast_sta(sta->sta_idx) &&
	    !timer_pending(&rwnx_hw->txq_cleanup))
		mod_timer(&rwnx_hw->txq_cleanup,
			  jiffies + RWNX_TXQ_CLEANUP_INTERVAL);
}

int rwnx_txq_malloc(struct rwnx_hw *rwnx_hw, u16 num)
{
    rwnx_hw->txq = (struct rwnx_txq *)kzalloc(sizeof(struct rwnx_txq) * num, GFP_KERNEL);
    if (rwnx_hw->txq == NULL) {
        return -ENOMEM;
    }
    return 0;
}

void rwnx_txq_free(struct rwnx_hw *rwnx_hw)
{
    if (rwnx_hw->txq != NULL) {
        kfree(rwnx_hw->txq);
        rwnx_hw->txq = NULL;
    }
}

/**
 * rwnx_txq_prepare - Global initialization of txq
 *
 * @rwnx_hw: Driver main data
 */
void rwnx_txq_prepare(struct rwnx_hw *rwnx_hw)
{
	int i;

	for (i = 0; i < NX_NB_TXQ; i++) {
		rwnx_hw->txq[i].idx = TXQ_INACTIVE;
	}

	timer_setup(&rwnx_hw->txq_cleanup, rwnx_txq_cleanup_timer_cb, 0);
}

/******************************************************************************
 * Start/Stop functions
 *****************************************************************************/
/**
 * rwnx_txq_add_to_hw_list - Add TX queue to a HW queue schedule list.
 *
 * @txq: TX queue to add
 *
 * Add the TX queue if not already present in the HW queue list.
 * To be called with tx_lock hold
 */
void rwnx_txq_add_to_hw_list(struct rwnx_txq *txq)
{
	if (!rwnx_txq_is_scheduled(txq)) {
		trace_txq_add_to_hw(txq);
		txq->status |= RWNX_TXQ_IN_HWQ_LIST;
		/* For BCMC TxQ, we insert to the head */
		if (txq->idx >= NX_FIRST_VIF_TXQ_IDX)
			list_add(&txq->sched_list, &txq->hwq->list);
		else
			list_add_tail(&txq->sched_list, &txq->hwq->list);
		txq->hwq->need_processing = true;
	}
}

/**
 * rwnx_txq_del_from_hw_list - Delete TX queue from a HW queue schedule list.
 *
 * @txq: TX queue to delete
 *
 * Remove the TX queue from the HW queue list if present.
 * To be called with tx_lock hold
 */
void rwnx_txq_del_from_hw_list(struct rwnx_txq *txq)
{
	if (rwnx_txq_is_scheduled(txq)) {
		trace_txq_del_from_hw(txq);
		txq->status &= ~RWNX_TXQ_IN_HWQ_LIST;
		list_del(&txq->sched_list);
	}
}

/**
 * rwnx_txq_start - Try to Start one TX queue
 *
 * @txq: TX queue to start
 * @reason: reason why the TX queue is started (among RWNX_TXQ_STOP_xxx)
 *
 * Re-start the TX queue for one reason.
 * If after this the txq is no longer stopped and some buffers are ready,
 * the TX queue is also added to HW queue list.
 * To be called with tx_lock hold
 */
void rwnx_txq_start(struct rwnx_txq *txq, u16 reason)
{
	BUG_ON(txq == NULL);
	if (txq->idx != TXQ_INACTIVE && (txq->status & reason)) {
		trace_txq_start(txq, reason);
		txq->status &= ~reason;
		if (!rwnx_txq_is_stopped(txq) && rwnx_txq_skb_ready(txq)) {
			rwnx_txq_add_to_hw_list(txq);
			tasklet_schedule(&txq->hwq->tasklet);
		}
	}
}

/**
 * rwnx_txq_stop - Stop one TX queue
 *
 * @txq: TX queue to stop
 * @reason: reason why the TX queue is stopped (among RWNX_TXQ_STOP_xxx)
 *
 * Stop the TX queue. It will remove the TX queue from HW queue list
 * To be called with tx_lock hold
 */
void rwnx_txq_stop(struct rwnx_txq *txq, u16 reason)
{
	BUG_ON(txq == NULL);
	if (txq->idx != TXQ_INACTIVE) {
		trace_txq_stop(txq, reason);
		txq->status |= reason;
		rwnx_txq_del_from_hw_list(txq);
	}
}

/**
 * rwnx_txq_sta_start - Start all the TX queue linked to a STA
 *
 * @sta: STA whose TX queues must be re-started
 * @reason: Reason why the TX queue are restarted (among RWNX_TXQ_STOP_xxx)
 * @rwnx_hw: Driver main data
 *
 * This function will re-start all the TX queues of the STA for the reason
 * specified. It can be :
 * - RWNX_TXQ_STOP_STA_PS: the STA is no longer in power save mode
 * - RWNX_TXQ_STOP_VIF_PS: the VIF is in power save mode (p2p absence)
 * - RWNX_TXQ_STOP_CHAN: the STA's VIF is now on the current active channel
 *
 * Any TX queue with buffer ready and not Stopped for other reasons, will be
 * added to the HW queue list
 * To be called with tx_lock hold
 */
void rwnx_txq_sta_start(struct rwnx_sta *rwnx_sta, u16 reason,
			struct rwnx_hw *rwnx_hw)
{
	struct rwnx_txq *txq;
	int tid;

	trace_txq_sta_start(rwnx_sta->sta_idx);

	foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw)
	{
		rwnx_txq_start(txq, reason);
	}
}

/**
 * rwnx_stop_sta_txq - Stop all the TX queue linked to a STA
 *
 * @sta: STA whose TX queues must be stopped
 * @reason: Reason why the TX queue are stopped (among RWNX_TX_STOP_xxx)
 * @rwnx_hw: Driver main data
 *
 * This function will stop all the TX queues of the STA for the reason
 * specified. It can be :
 * - RWNX_TXQ_STOP_STA_PS: the STA is in power save mode
 * - RWNX_TXQ_STOP_VIF_PS: the VIF is in power save mode (p2p absence)
 * - RWNX_TXQ_STOP_CHAN: the STA's VIF is not on the current active channel
 *
 * Any TX queue present in a HW queue list will be removed from this list.
 * To be called with tx_lock hold
 */
void rwnx_txq_sta_stop(struct rwnx_sta *rwnx_sta, u16 reason,
		       struct rwnx_hw *rwnx_hw)
{
	struct rwnx_txq *txq;
	int tid;

	if (!rwnx_sta)
		return;

	trace_txq_sta_stop(rwnx_sta->sta_idx);
	foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw)
	{
		rwnx_txq_stop(txq, reason);
	}
}

void rwnx_txq_tdls_sta_start(struct rwnx_vif *rwnx_vif, u16 reason,
			     struct rwnx_hw *rwnx_hw)
{
	trace_txq_vif_start(rwnx_vif->vif_index);
	spin_lock_bh(&rwnx_hw->tx_lock);

	if (rwnx_vif->sta.tdls_sta)
		rwnx_txq_sta_start(rwnx_vif->sta.tdls_sta, reason, rwnx_hw);

	spin_unlock_bh(&rwnx_hw->tx_lock);
}

void rwnx_txq_tdls_sta_stop(struct rwnx_vif *rwnx_vif, u16 reason,
			    struct rwnx_hw *rwnx_hw)
{
	trace_txq_vif_stop(rwnx_vif->vif_index);

	spin_lock_bh(&rwnx_hw->tx_lock);

	if (rwnx_vif->sta.tdls_sta)
		rwnx_txq_sta_stop(rwnx_vif->sta.tdls_sta, reason, rwnx_hw);

	spin_unlock_bh(&rwnx_hw->tx_lock);
}

static inline void
rwnx_txq_vif_for_each_sta(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			  void (*f)(struct rwnx_sta *, u16, struct rwnx_hw *),
			  u16 reason)
{
	switch (RWNX_VIF_TYPE(rwnx_vif)) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT: {
		if (rwnx_vif->tdls_status == TDLS_LINK_ACTIVE)
			f(rwnx_vif->sta.tdls_sta, reason, rwnx_hw);
		if (rwnx_vif->sta.ap != NULL)
			f(rwnx_vif->sta.ap, reason, rwnx_hw);
		else
			WQ_DBG(DM_TX, DL_ERR, "rwnx_vif->sta.ap == NULL\n");

		break;
	}
	case NL80211_IFTYPE_AP_VLAN:
		rwnx_vif = rwnx_vif->ap_vlan.master;
		fallthrough;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_P2P_GO: {
		struct rwnx_sta *sta;
		list_for_each_entry (sta, &rwnx_vif->ap.sta_list, list) {
			f(sta, reason, rwnx_hw);
		}
		break;
	}
	default:
		BUG();
		break;
	}
}

/**
 * rwnx_txq_vif_start - START TX queues of all STA associated to the vif
 *                      and vif's TXQ
 *
 * @vif: Interface to start
 * @reason: Start reason (RWNX_TXQ_STOP_CHAN or RWNX_TXQ_STOP_VIF_PS)
 * @rwnx_hw: Driver main data
 *
 * Iterate over all the STA associated to the vif and re-start them for the
 * reason @reason
 * Take tx_lock
 */
void rwnx_txq_vif_start(struct rwnx_vif *rwnx_vif, u16 reason,
			struct rwnx_hw *rwnx_hw)
{
	struct rwnx_txq *txq;

	trace_txq_vif_start(rwnx_vif->vif_index);

	spin_lock_bh(&rwnx_hw->tx_lock);

	//Reject if monitor interface
	if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_MONITOR)
		goto end;

	if (rwnx_vif->roc_tdls && rwnx_vif->sta.tdls_sta &&
	    rwnx_vif->sta.tdls_sta->tdls.chsw_en) {
		rwnx_txq_sta_start(rwnx_vif->sta.tdls_sta, reason, rwnx_hw);
	}
	if (!rwnx_vif->roc_tdls) {
		rwnx_txq_vif_for_each_sta(rwnx_hw, rwnx_vif, rwnx_txq_sta_start,
					  reason);
	}

	txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
	rwnx_txq_start(txq, reason);
	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_start(txq, reason);

end:
	spin_unlock_bh(&rwnx_hw->tx_lock);
}

/**
 * rwnx_txq_vif_stop - STOP TX queues of all STA associated to the vif
 *
 * @vif: Interface to stop
 * @arg: Stop reason (RWNX_TXQ_STOP_CHAN or RWNX_TXQ_STOP_VIF_PS)
 * @rwnx_hw: Driver main data
 *
 * Iterate over all the STA associated to the vif and stop them for the
 * reason RWNX_TXQ_STOP_CHAN or RWNX_TXQ_STOP_VIF_PS
 * Take tx_lock
 */
void rwnx_txq_vif_stop(struct rwnx_vif *rwnx_vif, u16 reason,
		       struct rwnx_hw *rwnx_hw)
{
	struct rwnx_txq *txq;

	trace_txq_vif_stop(rwnx_vif->vif_index);
	spin_lock_bh(&rwnx_hw->tx_lock);

	//Reject if monitor interface
	if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_MONITOR)
		goto end;

	rwnx_txq_vif_for_each_sta(rwnx_hw, rwnx_vif, rwnx_txq_sta_stop, reason);

	txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
	rwnx_txq_stop(txq, reason);
	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_stop(txq, reason);

end:
	spin_unlock_bh(&rwnx_hw->tx_lock);
}

/**
 * rwnx_start_offchan_txq - START TX queue for offchannel frame
 *
 * @rwnx_hw: Driver main data
 */
void rwnx_txq_offchan_start(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_txq *txq;

	txq = &rwnx_hw->txq[NX_OFF_CHAN_TXQ_IDX];
	spin_lock_bh(&rwnx_hw->tx_lock);
	rwnx_txq_start(txq, RWNX_TXQ_STOP_CHAN);
	spin_unlock_bh(&rwnx_hw->tx_lock);
}

/**
 * rwnx_switch_vif_sta_txq - Associate TXQ linked to a STA to a new vif
 *
 * @sta: STA whose txq must be switched
 * @old_vif: Vif currently associated to the STA (may no longer be active)
 * @new_vif: vif which should be associated to the STA for now on
 *
 * This function will switch the vif (i.e. the netdev) associated to all STA's
 * TXQ. This is used when AP_VLAN interface are created.
 * If one STA is associated to an AP_vlan vif, it will be moved from the master
 * AP vif to the AP_vlan vif.
 * If an AP_vlan vif is removed, then STA will be moved back to mastert AP vif.
 *
 */
void rwnx_txq_sta_switch_vif(struct rwnx_sta *sta, struct rwnx_vif *old_vif,
			     struct rwnx_vif *new_vif)
{
	struct rwnx_hw *rwnx_hw = new_vif->rwnx_hw;
	struct rwnx_txq *txq;
	int i;

	/* start TXQ on the new interface, and update ndev field in txq */
	if (!netif_carrier_ok(new_vif->ndev))
		netif_carrier_on(new_vif->ndev);
	txq = rwnx_txq_sta_get(sta, 0, rwnx_hw);
	for (i = 0; i < NX_NB_TID_PER_STA; i++, txq++) {
		txq->ndev = new_vif->ndev;
		netif_wake_subqueue(txq->ndev, txq->ndev_idx);
	}
}

static void rwnx_txq_q_to_list(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq, struct sk_buff *skb,
			       u32 expiers_ns)
{
	struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);

	if (!expiers_ns || (txcb->pkt_cls & (BIT(WQ_PKT_CLS_ICMP) | BIT(WQ_PKT_CLS_DHCP) |
		BIT(WQ_PKT_CLS_EAPOL) | BIT(WQ_PKT_CLS_ARP)))) {
		/* add buffer in the sk_list */
		skb_queue_tail(&txq->sk_list, skb);
		return;
	}

	/* TODO：remove later.
	 * This is to work around for peak TPUT and limited pair when iperf running,
	 * because sometimes the host's socket memory is under 64k and may be all queued in the ac hwq of host side,
	 * the 1 pair iperf TX would be lower than normal case. So we need to adjust the bundle time for PCIE,
	 * for max wait time, or max pkt cnt for waiting.
	 * We will need a better way for this in the future,
	 * as we should not have any active delay in the data path.
	 */
	if (rwnx_hw->rx_throughput > rwnx_hw->tx_throughput) {
		/* maybe tcp rx, keep the input expiers_ns untouched */
	} else if (rwnx_hw->tx_throughput > PCIE_MAX_BUNDLE_WAIT_TIME_H_TPUT_THRD) {
		expiers_ns = NSEC_PER_USEC * PCIE_MAX_BUNDLE_WAIT_TIME_H_US;
	} else {
		expiers_ns = NSEC_PER_USEC * PCIE_MAX_BUNDLE_WAIT_TIME_L_US;
	}

	if (!hrtimer_is_queued(&txq->bundle.ack_timer)) {
		/* ack timer or bundle timer(shared with tcp ack timer) is not start or timeout */
		if (!skb_queue_empty(&txq->sk_ack_list)) {
			/* ack list has value, currently share with pcie bundle delay list */
			struct sk_buff *skb_tmp;
			while ((skb_tmp = __skb_dequeue(&txq->sk_ack_list)) != NULL) {
				/* add buffer in the sk_list */
				skb_queue_tail(&txq->sk_list, skb_tmp);
			}
		}
		/* add current skb after bundle list if any */
		skb_queue_tail(&txq->sk_ack_list, skb);
		/* update the next ack timeout value from now + expiers_ns */
		txq->bundle.ack_deadline = ktime_add_ns(ktime_get_boottime(), expiers_ns);
		hrtimer_start_range_ns(&txq->bundle.ack_timer,
				ktime_set(0, expiers_ns + 50),
				50, HRTIMER_MODE_REL);
	} else {
		/* waiting for TCP ack delay or bundle delay */
		skb_queue_tail(&txq->sk_ack_list, skb);
		if (skb_queue_len(&txq->sk_ack_list) >= PCIE_MAX_BUNDLE_MPDU_PER_PPDU_CNT) {
			/* if the ppdu cnt is reach the max cnt, then we need to send it too */
			hrtimer_cancel(&txq->bundle.ack_timer);
			expiers_ns = 1;
			txq->bundle.ack_deadline = ktime_add_ns(ktime_get_boottime(), expiers_ns);
			hrtimer_start_range_ns(&txq->bundle.ack_timer,
					ktime_set(0, expiers_ns + 50),
					50, HRTIMER_MODE_REL);
		}
	}
}

/******************************************************************************
 * TXQ queue/schedule functions
 *****************************************************************************/
/**
 * rwnx_txq_queue_skb - Queue a buffer in a TX queue
 *
 * @rwnx_hw: Driver main data
 * @txq: TX Queue in which the buffer must be added
 * @skb: Buffer to queue
 *
 * @return: Return 1 if txq has been added to hwq list, 0 otherwise
 *
 * Add a buffer in the buffer list of the TX queue
 * and add this TX queue in the HW queue list if the txq is not stopped.
 *
 * If the STA is in PS mode and this is the first packet queued for this txq
 * update TIM.
 *
 * To be called with tx_lock hold
 */
int rwnx_txq_queue_skb(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq,
		       struct sk_buff *skb)
{
	u32 expires_ns = rwnx_hw->core->config.tx_bundle_expire_ns;
	u32 ack_expires_ns = rwnx_hw->core->config.tx_tcpack_expire_ns;
	int sending = atomic_inc_return(&txq->sending);
	int wait = 0;

	atomic_inc(&rwnx_hw->sending);
	q_stats_tx(&txq->stats, 1);
	q_stats_tx(&rwnx_hw->tx_stats, 1);

	if (unlikely(txq->sta && txq->sta->ps.active)) {
		txq->sta->ps.pkt_ready[txq->ps_id]++;
		trace_ps_queue(txq->sta);

		if (txq->sta->ps.pkt_ready[txq->ps_id] == 1) {
			rwnx_set_traffic_status(rwnx_hw, txq->sta, true,
						txq->ps_id);
		}
	}

	if (expires_ns) {
		wait = 1;
		if (hrtimer_is_queued(&txq->bundle.timer)) {
			hrtimer_cancel(&txq->bundle.timer);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			if (ktime_after(ktime_get_boottime(),
					txq->bundle.deadline)) {
#else
			if (ktime_compare(ktime_get_boottime(),
					txq->bundle.deadline) > 0) {
#endif
				wait = 0;
			}
		}

		if (wait) {
			txq->bundle.deadline =
				ktime_add_ns(ktime_get_boottime(), expires_ns);
			hrtimer_start_range_ns(&txq->bundle.timer,
					       ktime_set(0, expires_ns + 50),
					       50, HRTIMER_MODE_REL);
		}
	}

	/* queue to tx list */
	rwnx_txq_q_to_list(rwnx_hw, txq, skb, ack_expires_ns);
	rwnx_txq_start_cleanup_timer(rwnx_hw, txq->sta);
	trace_txq_queue_skb(skb, txq, false);

	/* If too many buffer are queued for this TXQ stop netdev queue */
	if ((txq->ndev_idx != NDEV_NO_TXQ) &&
	    sending > rwnx_hw->txq_stop_threshlod) {
		PROFILING_SET(SW_PROF_FLOW_CTRL);
		wq_ipc_txq_ring_2task(rwnx_hw->core);
		txq->status |= RWNX_TXQ_NDEV_FLOW_CTRL;
		netif_stop_subqueue(txq->ndev, txq->ndev_idx);
		trace_txq_flowctrl_stop(txq);
		printk_ratelimited(KERN_INFO "%s: netif_stop_subqueue(%d)\n",
				   __func__, txq->idx);
	}

	/* add it in the hwq list if not stopped and not yet present */
	if (!rwnx_txq_is_stopped(txq)) {
		rwnx_txq_add_to_hw_list(txq);
		return !wait;
	}

	return 0;
}

/******************************************************************************
 * HWQ processing
 *****************************************************************************/
static inline bool rwnx_txq_take_mu_lock(struct rwnx_hw *rwnx_hw)
{
	bool res = false;
#ifdef CONFIG_RWNX_MUMIMO_TX
	if (rwnx_hw->mod_params.mutx)
		res = (down_trylock(&rwnx_hw->mu.lock) == 0);
#endif /* CONFIG_RWNX_MUMIMO_TX */
	return res;
}

static inline void rwnx_txq_release_mu_lock(struct rwnx_hw *rwnx_hw)
{
#ifdef CONFIG_RWNX_MUMIMO_TX
	up(&rwnx_hw->mu.lock);
#endif /* CONFIG_RWNX_MUMIMO_TX */
}

static inline void rwnx_txq_set_mu_info(struct rwnx_hw *rwnx_hw,
					struct rwnx_txq *txq, int group_id,
					int pos)
{
#ifdef CONFIG_RWNX_MUMIMO_TX
	trace_txq_select_mu_group(txq, group_id, pos);
	if (group_id) {
		txq->mumimo_info = group_id | (pos << 6);
		rwnx_mu_set_active_group(rwnx_hw, group_id);
	} else
		txq->mumimo_info = 0;
#endif /* CONFIG_RWNX_MUMIMO_TX */
}

static inline s8 rwnx_txq_get_credits(struct rwnx_txq *txq)
{
	s8 cred = txq->credits;
	/* if destination is in PS mode, push_limit indicates the maximum
       number of packet that can be pushed on this txq. */
	if (txq->push_limit && (cred > txq->push_limit)) {
		cred = txq->push_limit;
	}
	return cred;
}

/**
 * skb_queue_extract - Extract buffer from skb list
 *
 * @list: List of skb to extract from
 * @head: List of skb to append to
 * @nb_elt: Number of skb to extract
 *
 * extract the first @nb_elt of @list and append them to @head
 * It is assume that:
 * - @list contains more that @nb_elt
 * - There is no need to take @list nor @head lock to modify them
 */
static inline void skb_queue_extract(struct sk_buff_head *list,
				     struct sk_buff_head *head, int nb_elt)
{
	int i;
	struct sk_buff *first, *last, *ptr;

	first = ptr = list->next;
	for (i = 0; i < nb_elt; i++) {
		ptr = ptr->next;
	}
	last = ptr->prev;

	/* unlink nb_elt in list */
	list->qlen -= nb_elt;
	list->next = ptr;
	ptr->prev = (struct sk_buff *)list;

	/* append nb_elt at end of head */
	head->qlen += nb_elt;
	last->next = (struct sk_buff *)head;
	head->prev->next = first;
	first->prev = head->prev;
	head->prev = last;
}

#ifdef CONFIG_MAC80211_TXQ
/**
 * rwnx_txq_mac80211_dequeue - Dequeue buffer from mac80211 txq and
 *                             add them to push list
 *
 * @rwnx_hw: Main driver data
 * @sk_list: List of buffer to push (initialized without lock)
 * @txq: TXQ to dequeue buffers from
 * @max: Max number of buffer to dequeue
 *
 * Dequeue buffer from mac80211 txq, prepare them for transmission and chain them
 * to the list of buffer to push.
 *
 * @return true if no more buffer are queued in mac80211 txq and false otherwise.
 */
static bool rwnx_txq_mac80211_dequeue(struct rwnx_hw *rwnx_hw,
				      struct sk_buff_head *sk_list,
				      struct rwnx_txq *txq, int max)
{
	struct ieee80211_txq *mac_txq;
	struct sk_buff *skb;
	unsigned long mac_txq_len;

	if (txq->nb_ready_mac80211 == NOT_MAC80211_TXQ)
		return true;

	mac_txq = container_of((void *)txq, struct ieee80211_txq, drv_priv);

	for (; max > 0; max--) {
		skb = rwnx_tx_dequeue_prep(rwnx_hw, mac_txq);
		if (skb == NULL)
			return true;

		__skb_queue_tail(sk_list, skb);
	}

	/* re-read mac80211 txq current length.
       It is mainly for debug purpose to trace dropped packet. There is no
       problems to have nb_ready_mac80211 != actual mac80211 txq length */
	ieee80211_txq_get_depth(mac_txq, &mac_txq_len, NULL);
	if (txq->nb_ready_mac80211 > mac_txq_len)
		trace_txq_drop(txq, txq->nb_ready_mac80211 - mac_txq_len);
	txq->nb_ready_mac80211 = mac_txq_len;

	return (txq->nb_ready_mac80211 == 0);
}
#endif

static inline void rwnx_txq_acklist_to_txlist(struct rwnx_txq *txq)
{
	struct sk_buff *skb;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	if (ktime_after(ktime_get_boottime(), txq->bundle.ack_deadline) &&
	    !skb_queue_empty(&txq->sk_ack_list)) {
#else
	if (ktime_compare(ktime_get_boottime(), txq->bundle.ack_deadline) > 0 &&
	    !skb_queue_empty(&txq->sk_ack_list)) {
#endif
		while ((skb = __skb_dequeue(&txq->sk_ack_list)) != NULL) {
			/* add buffer in the sk_list */
			skb_queue_tail(&txq->sk_list, skb);
		}
	}
}

/**
 * rwnx_txq_get_skb_to_push - Get list of buffer to push for one txq
 *
 * @rwnx_hw: main driver data
 * @hwq: HWQ on wich buffers will be pushed
 * @txq: TXQ to get buffers from
 * @user: user postion to use
 * @sk_list_push: list to update
 *
 *
 * This function will returned a list of buffer to push for one txq.
 * It will take into account the number of credit of the HWQ for this user
 * position and TXQ (and push_limit).
 * This allow to get a list that can be pushed without having to test for
 * hwq/txq status after each push
 *
 * If a MU group has been selected for this txq, it will also update the
 * counter for the group
 *
 * @return true if txq no longer have buffer ready after the ones returned.
 *         false otherwise
 */
static bool rwnx_txq_get_skb_to_push(struct rwnx_hw *rwnx_hw,
				     struct rwnx_hwq *hwq, struct rwnx_txq *txq,
				     int user,
				     struct sk_buff_head *sk_list_push)
{
	struct sk_buff *skb;
	int credits = 0;
	bool res = false;

	__skb_queue_head_init(sk_list_push);

	/* for TCP ACK */
	rwnx_txq_acklist_to_txlist(txq);

	while ((skb = skb_peek(&txq->sk_list)) != NULL) {
		struct txdesc_host *txdesc_host =
			(struct txdesc_host *)skb->data;
		struct hostdesc *host = &txdesc_host->api.host;

		if (rwnx_get_dev_credit(rwnx_hw, hwq->id, host->vif_idx, 0,
					&host->via_grp_id,
					&host->via_type_id)) {

			struct rwnx_vif *rwnx_vif;
			struct credit_mgmt *crdt_mgmt = &rwnx_hw->crdt_mgmt;
			u8 type_id, limit;

			rwnx_vif = rwnx_hw->vif_table[host->vif_idx];
			type_id = crdt_mgmt->credit_grp[rwnx_vif->crdt_gid].type[hwq->id];
			limit = crdt_mgmt->credit_grp[0].size[type_id]
				+ crdt_mgmt->credit_grp[1].size[type_id];

			__skb_unlink(skb, &txq->sk_list);
			__skb_queue_tail(sk_list_push, skb);
			credits++;

			limit = crdt_mgmt->credit_grp[0].size[type_id]
				+ crdt_mgmt->credit_grp[1].size[type_id];

			/* AP mode has multiple STAs accessing it,
			 * Limit the number of credits used by per txq_sk_list.
			 * limit = cur_ac initial allocate credit_num.
			 */
			if (rwnx_hw->core->hif_ops->hif == WQ_HIF_USB &&
			    rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP &&
			    (atomic_read(&rwnx_vif->ap.sta_num) > 1) &&
			    credits > limit) {
				WQ_DBG(DM_TX, DL_INF, "get_credit:%d > curr AC[%d] "
					"available limit:%d, sta_num:%d\n",
				credits, type_id, limit,
				atomic_read(&rwnx_vif->ap.sta_num));
				break;
			}

			if (txq->push_limit && (credits == txq->push_limit)) {
				res = true;
				break;
			}
		} else {
			break;
		}
	}

	if (skb_queue_len(&txq->sk_list) == 0) {
		res = true;
	}

	rwnx_mu_set_active_sta(rwnx_hw, rwnx_txq_2_sta(txq), credits);

	return res;
}

static bool rwnx_txq_get_skb_to_push_usb(struct rwnx_hw *rwnx_hw,
					 struct rwnx_hwq *hwq,
					 struct rwnx_txq *txq, int user,
					 struct sk_buff_head *sk_list_push)
{
	bool res = false;
	struct wq_usb *wq_usb =
		container_of(rwnx_hw->core, struct wq_usb, core);
	bool usb_bus_congested = false;
	bool txq_pkt_exceed_out_bundle = false;

	//if the number of URBs used exceeds the WQ_PKTOUT_CONGEST_THRESHOLD, it means that the USB Bus is busy
	if ((WQ_PKTOUT_URB_NUM - wq_usb->pools.pktout.list.num) >=
	    WQ_PKTOUT_CONGEST_THRESHOLD) {
		usb_bus_congested = true;
	}
	if (skb_queue_len(&txq->sk_list) >= THRESHOLD_USB_OUT_BUNDLE_MAX) {
		txq_pkt_exceed_out_bundle = true;
	}

	//if USB bus is not congested, schedule as much as possible
	//or if txq reaching threshold, schedule a bundle with THRESHOLD_USB_OUT_BUNDLE_MAX wifi packets.
	//if (!usb_bus_congested || txq_pkt_exceed_out_bundle) {
	if (!usb_bus_congested
	    // or the USB Bus is busy and the pkt of txq->sk_list exceeds the THRESHOLD_USB_OUT_BUNDLE_MAX
	    || (usb_bus_congested && txq_pkt_exceed_out_bundle)) {
		res = rwnx_txq_get_skb_to_push(rwnx_hw, hwq, txq, user,
					       sk_list_push);
	}

	return res;
}

static bool rwnx_txq_get_skb_to_push_sdio(struct rwnx_hw *rwnx_hw,
					  struct rwnx_hwq *hwq,
					  struct rwnx_txq *txq, int user,
					  struct sk_buff_head *sk_list_push)
{
	struct wq_sdio *wq_sdio =
		container_of(rwnx_hw->core, struct wq_sdio, core);
	struct sk_buff *skb;
	int credits = 0;
	bool res = false;
	int tx_pkt_free_num = wq_sdio_get_free_pkt_num(wq_sdio);

	__skb_queue_head_init(sk_list_push);

	while ((skb = skb_peek(&txq->sk_list)) != NULL) {
		if (tx_pkt_free_num > 0) {
			__skb_unlink(skb, &txq->sk_list);
			__skb_queue_tail(sk_list_push, skb);
			credits++;
			tx_pkt_free_num--;
		} else {
			break;
		}
	}

	if (skb_queue_len(&txq->sk_list) == 0) {
		res = true;
	}

	return res;
}

static inline int rwnx_txq_bundle(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq,
				  u8 tx_bundle_max, u16 max_amsdu_len)
{
	u16 amsdu_len = txq->bundle.amsdu_len;
	u16 tcpack_len = txq->bundle.tcpack_len;
	struct sk_buff *last = skb_peek_tail(&txq->bundle.list);
	struct sk_buff *skb;

	/* for TCP ACK */
	rwnx_txq_acklist_to_txlist(txq);

	while ((skb = skb_peek(&txq->sk_list))) {
		int in_host = WQ_SKB_TXCB(skb)->msdu_in_host;

		/* FIXME: for now, only bundle MSDUs in host memory */
		if (in_host) {
			u16 len = skb->len - HEADROOM_TXDESC + AMSDU_MAX_PAD +
				  sizeof(rfc1042_header) + 2;

			amsdu_len += len;
			if (last) {
				if ((WQ_SKB_TXCB(last)->pkt_cls &
				     BIT(WQ_PKT_CLS_TCP_ACK)) ||
				    amsdu_len >= max_amsdu_len) {
					return 0; /* don't bundle more msdu */
				}
				WQ_SKB_TXCB(last)->more_msdu = 1;
			}
			rwnx_hw->ll_pkt_cnt++;
		} else if (WQ_SKB_TXCB(skb)->pkt_cls &
			   BIT(WQ_PKT_CLS_TCP_ACK)) {
			u16 len = skb->len - HEADROOM_TXDESC + AMSDU_MAX_PAD +
				  sizeof(rfc1042_header) + 2;

			amsdu_len += len;
			tcpack_len += ALIGN(skb->len, sizeof(u32)) - HEADROOM_TXDESC + BUNDLE_HDR_LEN;

			if (last) {
				if (WQ_SKB_TXCB(last)->msdu_in_host ||
					amsdu_len >= max_amsdu_len ||
					(tcpack_len + HEADROOM_TXDESC) >= rwnx_hw->amsdu_param.max_len)
					return 0; /* don't bundle more msdu */
				WQ_SKB_TXCB(last)->more_msdu = 1;
			}
		} else if (!last) {
			++rwnx_hw->bundle_stats[0]; /* without msdu_in_host flag */
		} else {
			/* immediately send MSDUs already bundled */
			return 0;
		}

		if (in_host && TXQ_RING_FUNCTION_ENABLE) {
			rwnx_hwq_ring_push(rwnx_hw, txq->hwq->id, skb);
		}

		__skb_unlink(skb, &txq->sk_list);
		rwnx_tx_push_prepare(rwnx_hw, (struct txdesc_host *)skb->data,
				     txq);
		__skb_queue_tail(&txq->bundle.list, skb);

		if ((!in_host &&
			!(WQ_SKB_TXCB(skb)->pkt_cls & BIT(WQ_PKT_CLS_TCP_ACK))) ||
			!max_amsdu_len ||
			(in_host && skb_queue_len(&txq->bundle.list) >= tx_bundle_max) ||
			((WQ_SKB_TXCB(skb)->pkt_cls & BIT(WQ_PKT_CLS_TCP_ACK)) &&
			((tcpack_len + HEADROOM_TXDESC) >= rwnx_hw->amsdu_param.max_len ||
			skb_queue_len(&txq->bundle.list) >= rwnx_hw->amsdu_param.max_packets_num))) {
			return 0;
		}

		last = skb;
	}

	if (last)
		return 0;

	txq->bundle.amsdu_len = amsdu_len;
	txq->bundle.tcpack_len = tcpack_len;
	return -EAGAIN;
}

static inline int rwnx_txq_process_locked(struct rwnx_hw *rwnx_hw,
					  struct rwnx_hwq *hwq,
					  struct rwnx_txq *txq,
					  u8 tx_bundle_max,
					  u8 *stop_datapath)
{
	u16 max_amsdu_len = txq->sta ? txq->sta->max_amsdu_len : 0;
	int credits = 0;
	struct sk_buff *skb;
	struct wq_skb_txcb *txcb;

	/* use flow control instead of tx credit */
	while (rwnx_txq_bundle(rwnx_hw, txq, tx_bundle_max, max_amsdu_len) ==
	       0) {
		int qlen;
		int ret;
		u8 hw_txq = hwq->id;

		txq->bundle.amsdu_len = 0;
		txq->bundle.tcpack_len = 0;

		qlen = skb_queue_len(&txq->bundle.list);
		if (qlen < MAX_MSDU_BUNDLE_NUM)
			++rwnx_hw->bundle_stats[qlen];

		/* NOTE: solve the problem of out-of-order caused by multi-channel(copy engine channel) transmission
         * single pkt use WQ_QID_AC_VO.
         */
		BUG_ON(!(skb = skb_peek(&txq->bundle.list)));
		txcb = WQ_SKB_TXCB(skb);
		if ((txcb->pkt_cls & (BIT(WQ_PKT_CLS_EAPOL) | BIT(WQ_PKT_CLS_DHCP) | BIT(WQ_PKT_CLS_ARP))) ||
			txcb->is_small_pkt) {
			hw_txq = WQ_QID_AC_VO;
		}
		/* TCP ACK bundle */
		if (txcb->pkt_cls & BIT(WQ_PKT_CLS_TCP_ACK)) {
			rwnx_tx_refill_hostdesc(&txq->bundle.list);
			WQ_DBG(DM_TX, DL_VRB, "%s: bundlecnt (%d).\n", __func__, qlen);
		}
		ret = wq_ipc_tx_pkt_bundle(rwnx_hw->core, hw_txq,
					   &txq->bundle.list);
		if (ret == 0 || ret == -ENOBUFS) {
			rwnx_hw->ipc_tx_pkt_cnt += qlen;
			if (ret == -ENOBUFS) {
				*stop_datapath = TXQ_STOP_REASON_CE_WATERMARK;
			}
			credits += qlen;
		} else if (ret == -EIO) {
			*stop_datapath = TXQ_STOP_REASON_SUSPEND;
			skb_queue_splice_init(&txq->bundle.list, &txq->sk_list);
		}
		BUG_ON(skb_peek(&txq->bundle.list));

		if ((txq->push_limit && (credits >= txq->push_limit)) || *stop_datapath) {
			WQ_DBG(DM_TX, DL_WRN, "%s: push_limit (%d), stop_datapath:%d.\n",
			       __func__, txq->push_limit, *stop_datapath);
			break;
		}
	}

	/* FIXME: rwnx_mu_set_active_sta(rwnx_hw, rwnx_txq_2_sta(txq), credits); */

	return !rwnx_txq_skb_ready(txq);
}

/**
 * rwnx_txq_select_user - Select User queue for a txq
 *
 * @rwnx_hw: main driver data
 * @mu_lock: true is MU lock is taken
 * @txq: TXQ to select MU group for
 * @hwq: HWQ for the TXQ
 * @user: Updated with user position selected
 *
 * @return false if it is no possible to process this txq.
 *         true otherwise
 *
 * This function selects the MU group to use for a TXQ.
 * The selection is done as follow:
 *
 * - return immediately for STA that don't belongs to any group and select
 *   group 0 / user 0
 *
 * - If MU tx is disabled (by user mutx_on, or because mu group are being
 *   updated !mu_lock), select group 0 / user 0
 *
 * - Use the best group selected by @rwnx_mu_group_sta_select.
 *
 *   Each time a group is selected (except for the first case where sta
 *   doesn't belongs to a MU group), the function checks that no buffer is
 *   pending for this txq on another user position. If this is the case stop
 *   the txq (RWNX_TXQ_STOP_MU_POS) and return false.
 *
 */
__maybe_unused static bool rwnx_txq_select_user(struct rwnx_hw *rwnx_hw,
						bool mu_lock,
						struct rwnx_txq *txq,
						struct rwnx_hwq *hwq, int *user)
{
	int pos = 0;
#ifdef CONFIG_RWNX_MUMIMO_TX
	int id, group_id = 0;
	struct rwnx_sta *sta = rwnx_txq_2_sta(txq);

	/* for sta that belong to no group return immediately */
	if (!sta || !sta->group_info.cnt)
		goto end;

	/* If MU is disabled, need to check user */
	if (!rwnx_hw->mod_params.mutx_on || !mu_lock)
		goto check_user;

	/* Use the "best" group selected */
	group_id = sta->group_info.group;

	if (group_id > 0)
		pos = rwnx_mu_group_sta_get_pos(rwnx_hw, sta, group_id);

check_user:
	/* check that we can push on this user position */
#if CONFIG_USER_MAX == 2
	id = (pos + 1) & 0x1;
	if (txq->pkt_pushed[id]) {
		rwnx_txq_stop(txq, RWNX_TXQ_STOP_MU_POS);
		return false;
	}

#else
	for (id = 0; id < CONFIG_USER_MAX; id++) {
		if (id != pos && txq->pkt_pushed[id]) {
			rwnx_txq_stop(txq, RWNX_TXQ_STOP_MU_POS);
			return false;
		}
	}
#endif

end:
	rwnx_txq_set_mu_info(rwnx_hw, txq, group_id, pos);
#endif /* CONFIG_RWNX_MUMIMO_TX */

	*user = pos;
	return true;
}

static void rwnx_txq_net_stop(struct rwnx_txq *txq)
{
	txq->status |= RWNX_TXQ_NDEV_FLOW_CTRL;
	netif_stop_subqueue(txq->ndev, txq->ndev_idx);
	trace_txq_flowctrl_stop(txq);
}

static void rwnx_txq_ap_vif_stop(struct rwnx_vif *vif)
{
	struct rwnx_sta *sta;
	list_for_each_entry (sta, &vif->ap.sta_list, list) {
		struct rwnx_txq *txq;
		int tid;
		if (sta && sta->valid) {
			foreach_sta_txq(sta, txq, tid, vif->rwnx_hw) {
				/* not care manager txq */
				if (tid == 8) {
					continue;
				}
				if (txq->ndev_idx != NDEV_NO_TXQ) {
					rwnx_txq_net_stop(txq);
				}
				WQ_DBG(DM_TX, DL_WRN, "%s: sta idx:%d, macaddr:%pM, all txq:%d, tid:%d is stop\n",
					__func__, sta->sta_idx, sta->mac_addr,  txq->idx, tid);
			}
		}
	}
}

static void rwnx_txq_sta_vif_stop(struct rwnx_vif *vif)
{
	struct rwnx_txq *txq;
	int tid;

	if (vif->tdls_status == TDLS_LINK_ACTIVE) {
		foreach_sta_txq(vif->sta.tdls_sta, txq, tid, vif->rwnx_hw) {
			/* not care manager txq */
			if (tid == 8) {
				continue;
			}
			if (txq->ndev_idx != NDEV_NO_TXQ) {
				rwnx_txq_net_stop(txq);
			}
		}
		WQ_DBG(DM_TX, DL_WRN, "%s: tdls sta idx:%d, macaddr:%pM, all txq is stop\n",
			__func__, vif->sta.tdls_sta->sta_idx, vif->sta.tdls_sta->mac_addr);
	}

	if (vif->sta.ap) {
		foreach_sta_txq(vif->sta.ap, txq, tid, vif->rwnx_hw) {
			/* not care manager txq */
			if (tid == 8) {
				continue;
			}
			if (txq->ndev_idx != NDEV_NO_TXQ) {
				rwnx_txq_net_stop(txq);
			}
		}
		WQ_DBG(DM_TX, DL_WRN, "%s: ap sta idx:%d, macaddr:%pM, all txq is stop\n",
			__func__, vif->sta.ap->sta_idx, vif->sta.ap->mac_addr);
	}
}

static void rwnx_txq_stop_all(struct rwnx_hw *rwnx_hw, u8 reason)
{
	struct rwnx_vif *vif;
	if (reason == TXQ_STOP_REASON_CE_WATERMARK) {
		rwnx_hw->ce_sw_watermark_in++;
		rwnx_hw->feature.is_over_high_watermark = 1;
	} else {
		rwnx_hw->suspend_in++;
		rwnx_hw->feature.is_suspend = 1;
	}
	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		switch (RWNX_VIF_TYPE(vif)) {
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_P2P_GO:
		case NL80211_IFTYPE_MESH_POINT:
			rwnx_txq_ap_vif_stop(vif);
			break;
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_P2P_CLIENT:
			rwnx_txq_sta_vif_stop(vif);
			break;
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
		default:
			continue;
		}
	}
}

static void rwnx_hwq_stop_all_schedule(struct rwnx_hw *rwnx_hw)
{
	int id;

	for (id = ARRAY_SIZE(rwnx_hw->hwq) - 1; id >= 0 ; id--) {
		if (rwnx_hw->hwq[id].need_processing) {
			struct rwnx_txq *txq, *next;
			list_for_each_entry_safe(txq, next, &rwnx_hw->hwq[id].list, sched_list) {
				WQ_DBG(DM_TX, DL_WRN, "%s: txq:%p\n", __func__, txq);
				if (txq->idx != TXQ_INACTIVE) {
					rwnx_txq_del_from_hw_list(txq);
				}
			}
		}
	}
}

static void rwnx_txq_start_schedule(struct rwnx_txq *txq)
{
	/* start schedule */
	if (!rwnx_txq_is_scheduled(txq) &&
		(skb_queue_len(&txq->sk_list) || skb_queue_len(&txq->sk_ack_list))) {
		WQ_DBG(DM_TX, DL_WRN, "%s: txqid:%d, sklen:%d, acklilstlen:%d\n",
			__func__, txq->idx, skb_queue_len(&txq->sk_list),
			skb_queue_len(&txq->sk_ack_list));
		if (!rwnx_txq_is_stopped(txq)) {
			rwnx_txq_add_to_hw_list(txq);
			tasklet_schedule(&txq->hwq->tasklet);
		}
	}
}

static void rwnx_txq_ap_vif_start(struct rwnx_vif *vif)
{
	struct rwnx_sta *sta;
	list_for_each_entry (sta, &vif->ap.sta_list, list) {
		struct rwnx_txq *txq;
		int tid;
		if (sta && sta->valid) {
			foreach_sta_txq(sta, txq, tid, vif->rwnx_hw) {
				/* not care manager txq */
				if (tid == 8) {
					continue;
				}
				if (txq->ndev_idx != NDEV_NO_TXQ) {
					rwnx_txq_start_schedule(txq);
					rwnx_txq_subqueue_try_to_wake(vif->rwnx_hw, txq);
				}
				WQ_DBG(DM_TX, DL_WRN, "%s: sta idx:%d, macaddr:%pM, all txq:%d, tid:%d is start\n",
					__func__, sta->sta_idx, sta->mac_addr,  txq->idx, tid);
			}
		}
	}
}

static void rwnx_txq_sta_vif_start(struct rwnx_vif *vif)
{
	struct rwnx_txq *txq;
	int tid;

	if (vif->tdls_status == TDLS_LINK_ACTIVE) {
		foreach_sta_txq(vif->sta.tdls_sta, txq, tid, vif->rwnx_hw) {
			/* not care manager txq */
			if (tid == 8) {
				continue;
			}
			if (txq->ndev_idx != NDEV_NO_TXQ) {
				rwnx_txq_start_schedule(txq);
				rwnx_txq_subqueue_try_to_wake(vif->rwnx_hw, txq);
			}
		}
		WQ_DBG(DM_TX, DL_WRN, "%s: tdls sta idx:%d, macaddr:%pM, all txq is start\n",
			__func__, vif->sta.tdls_sta->sta_idx, vif->sta.tdls_sta->mac_addr);
	}

	if (vif->sta.ap) {
		foreach_sta_txq(vif->sta.ap, txq, tid, vif->rwnx_hw) {
			/* not care manager txq */
			if (tid == 8) {
				continue;
			}
			if (txq->ndev_idx != NDEV_NO_TXQ) {
				rwnx_txq_start_schedule(txq);
				rwnx_txq_subqueue_try_to_wake(vif->rwnx_hw, txq);
			}
		}
		WQ_DBG(DM_TX, DL_WRN,"%s: ap sta idx:%d, macaddr:%pM, all txq is start\n",
			__func__, vif->sta.ap->sta_idx, vif->sta.ap->mac_addr);
	}
}

void rwnx_txq_start_all(struct rwnx_hw *rwnx_hw, u8 reason)
{
	struct rwnx_vif *vif;
	if (reason == TXQ_STOP_REASON_CE_WATERMARK) {
		rwnx_hw->ce_sw_watermark_out++;
		rwnx_hw->feature.is_over_high_watermark = 0;
	} else {
		/* suspend handle */
		if (!rwnx_hw->feature.is_suspend) {
			return;
		}
		rwnx_hw->suspend_out++;
		rwnx_hw->feature.is_suspend = 0;
	}
	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		switch (RWNX_VIF_TYPE(vif)) {
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_P2P_GO:
		case NL80211_IFTYPE_MESH_POINT:
			rwnx_txq_ap_vif_start(vif);
			break;
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_P2P_CLIENT:
			rwnx_txq_sta_vif_start(vif);
			break;
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
		default:
			continue;
		}
	}
}

/**
 * rwnx_hwq_process - Process one HW queue list
 *
 * @rwnx_hw: Driver main data
 * @hw_queue: HW queue index to process
 *
 * The function will iterate over all the TX queues linked in this HW queue
 * list. For each TX queue, push as many buffers as possible in the HW queue.
 * (NB: TX queue have at least 1 buffer, otherwise it wouldn't be in the list)
 * - If TX queue no longer have buffer, remove it from the list and check next
 *   TX queue
 * - If TX queue no longer have credits or has a push_limit (PS mode) and it
 *   is reached , remove it from the list and check next TX queue
 * - If HW queue is full, update list head to start with the next TX queue on
 *   next call if current TX queue already pushed "too many" pkt in a row, and
 *   return
 *
 * To be called when HW queue list is modified:
 * - when a buffer is pushed on a TX queue
 * - when new credits are received
 * - when a STA returns from Power Save mode or receives traffic request.
 * - when Channel context change
 *
 * To be called with tx_lock hold
 */
void rwnx_hwq_process(struct rwnx_hw *rwnx_hw, struct rwnx_hwq *hwq)
{
	struct rwnx_txq *txq, *next;
	u8 hw_txq = hwq->id;
	u8 tx_bundle_max = rwnx_hw->core->config.tx_bundle_max;
	int ret = 0;
	struct sk_buff_head sk_list_push;
	struct sk_buff *skb;

	list_for_each_entry_safe (txq, next, &hwq->list, sched_list) {
		int user = 0;
		bool txq_empty;

		/* sanity check for debug */
		BUG_ON(!(txq->status & RWNX_TXQ_IN_HWQ_LIST));
		BUG_ON(txq->idx == TXQ_INACTIVE);
		BUG_ON(txq->credits <= 0);

		if (rwnx_hw->core->hif_ops->hif == WQ_HIF_USB) {
			txq_empty = rwnx_txq_get_skb_to_push_usb(
				rwnx_hw, hwq, txq, user, &sk_list_push);
			if (skb_queue_len(&sk_list_push) > 0) {
				skb_queue_walk(&sk_list_push, skb)
				{
					rwnx_tx_push_prepare(
						rwnx_hw,
						(struct txdesc_host *)skb->data,
						txq);
				}
				ret = wq_ipc_tx_pkt_bundle_usb(
					rwnx_hw->core, hw_txq, &sk_list_push);
			}

		} else if (rwnx_hw->core->hif_ops->hif == WQ_HIF_SDIO) {
			int sk_list_len;
			txq_empty = rwnx_txq_get_skb_to_push_sdio(
				rwnx_hw, hwq, txq, user, &sk_list_push);
			sk_list_len = skb_queue_len(&sk_list_push);
			if (sk_list_len > 0) {
				skb_queue_walk(&sk_list_push, skb)
				{
					rwnx_tx_push_prepare(
						rwnx_hw,
						(struct txdesc_host *)skb->data,
						txq);
				}
				ret = wq_ipc_tx_pkt_bundle(
					rwnx_hw->core, hw_txq, &sk_list_push);
				if (ret == 0) {
					rwnx_hw->ipc_tx_pkt_cnt += sk_list_len;
				}
			}

		} else {
			if (tx_bundle_max) {
				u8 stop_datapath = 0;
				txq_empty = rwnx_txq_process_locked(
					rwnx_hw, hwq, txq, tx_bundle_max, &stop_datapath);
				if (stop_datapath) {
					rwnx_hwq_stop_all_schedule(rwnx_hw);
					rwnx_txq_stop_all(rwnx_hw, stop_datapath);
					WQ_DBG(DM_TX, DL_VRB, "%s: ll triger watermark break the loop!\n",
						__func__);
					break;
				}
			} else if (!rwnx_txq_skb_ready(txq)) {
				txq_empty = true;
			} else {
				txq_empty = rwnx_txq_get_skb_to_push(
					rwnx_hw, hwq, txq, user, &sk_list_push);
				while ((skb = __skb_dequeue(&sk_list_push)) !=
				       NULL) {
					rwnx_tx_push_prepare(
						rwnx_hw,
						(struct txdesc_host *)skb->data,
						txq);
					ret = wq_ipc_tx_pkt(rwnx_hw->core,
							    hw_txq, skb);
					if (ret == 0 || ret == -ENOBUFS) {
						rwnx_hw->ipc_tx_pkt_cnt++;
						if (ret == -ENOBUFS) {
							rwnx_hwq_stop_all_schedule(rwnx_hw);
							rwnx_txq_stop_all(rwnx_hw, TXQ_STOP_REASON_CE_WATERMARK);
							WQ_DBG(DM_TX, DL_VRB, "%s: triger watermark break the loop!\n",
								__func__);
							break;
						}
					} else if (ret == -EIO) {
						rwnx_hwq_stop_all_schedule(rwnx_hw);
						rwnx_txq_stop_all(rwnx_hw, TXQ_STOP_REASON_SUSPEND);
						WQ_DBG(DM_TX, DL_VRB, "%s: triger suspend break the loop!\n",
							__func__);
						break;
					}
				}
			}
		}

		if (txq_empty) {
			if (skb_queue_empty(&txq->sk_ack_list)) {
				rwnx_txq_del_from_hw_list(txq);
				txq->pkt_sent = 0;
			}
		} else if ((hwq->credits[user] == 0) &&
			   rwnx_txq_is_scheduled(txq)) {
			/* txq not empty,
               - To avoid starving need to process other txq in the list
               - For better aggregation, need to send "as many consecutive
               pkt as possible" for he same txq
               ==> Add counter to trigger txq switch
            */
			if (txq->pkt_sent > hwq->size) {
				txq->pkt_sent = 0;
				list_rotate_left(&hwq->list);
			}
		}

		/* Unable to complete PS traffic request because of hwq credit */
		if (txq->push_limit && txq->sta) {
			if (txq->ps_id == LEGACY_PS_ID) {
				/* for legacy PS abort SP and wait next ps-poll */
				txq->sta->ps.sp_cnt[txq->ps_id] -=
					txq->push_limit;
				txq->push_limit = 0;
			}
			/* for u-apsd need to complete the SP to send EOSP frame */
		}

		if (ret) {
			WQ_DBG(DM_TX, DL_ERR, "%s: break the loop (ret=%d)!\n",
			       __func__, ret);
			break;
		}
	}
}

static void rwnx_hwq_process_task(unsigned long data)
{
	struct rwnx_hwq *hwq = (struct rwnx_hwq *)data;
	struct rwnx_hw *rwnx_hw =
		container_of(hwq, struct rwnx_hw, hwq[hwq->id]);

	spin_lock_bh(&rwnx_hw->tx_lock);
	PROFILING_SET(SW_PROF_TX_PROCESS);
	rwnx_hwq_process(rwnx_hw, hwq);
	PROFILING_CLR(SW_PROF_TX_PROCESS);
	spin_unlock_bh(&rwnx_hw->tx_lock);
}

static enum hrtimer_restart rwnx_txq_trampoline(struct hrtimer *timer)
{
	struct rwnx_txq *txq =
		container_of(timer, struct rwnx_txq, bundle.timer);

	tasklet_schedule(&txq->hwq->tasklet);

	return HRTIMER_NORESTART;
}

static enum hrtimer_restart rwnx_txq_ack_trampoline(struct hrtimer *timer)
{
	struct rwnx_txq *txq =
		container_of(timer, struct rwnx_txq, bundle.ack_timer);

	tasklet_schedule(&txq->hwq->tasklet);

	return HRTIMER_NORESTART;
}

void rwnx_txq_tx_done_pre(struct rwnx_hw *rwnx_hw, uint16_t txq_idx)
{
	struct rwnx_txq *txq;

	BUG_ON(!rwnx_hw);
	BUG_ON(txq_idx >= NX_NB_TXQ);

	txq = &rwnx_hw->txq[txq_idx];
	q_stats_txdone(&txq->stats, 1);
	q_stats_txdone(&rwnx_hw->tx_stats, 1);
	atomic_dec(&rwnx_hw->sending);
	atomic_dec(&txq->sending);

	if (txq->idx != TXQ_INACTIVE &&
		!rwnx_hw->feature.is_over_high_watermark &&
		!rwnx_hw->feature.is_suspend)
		rwnx_txq_subqueue_try_to_wake(rwnx_hw, txq);
}

void rwnx_txq_stats_dump(struct rwnx_hw *rwnx_hw)
{
	int i;
#ifdef WQ_STATS
	int txq_idx;

	for (txq_idx = 0; txq_idx < NX_NB_TXQ; txq_idx++) {
		struct rwnx_txq *txq = &rwnx_hw->txq[txq_idx];
		struct rwnx_sta *sta = txq->sta;

		if (txq->idx == TXQ_INACTIVE || !txq->stats.in)
			continue;

		if (sta && q_stats_n(&txq->stats)) {
			WQ_DBG(DM_TX, DL_WRN,
			       "TXQ[%2u] txqisstop:%d, is full:%d, isschedule:%d, "
			       "sklistqlen:%d, skacklistqlen:%d\n",
			       txq->idx, rwnx_txq_is_stopped(txq),
			       rwnx_txq_is_full(txq),
			       rwnx_txq_is_scheduled(txq),
			       skb_queue_len(&txq->sk_list),
			       skb_queue_len(&txq->sk_ack_list));
		}

		WQ_DBG(DM_TX, DL_WRN,
		       "TXQ[%2u]: %4d, in - out: %8d = %8u - %8u (%pM tid:%u hif %u)\n",
		       txq->idx, txq->stats.max, q_stats_n(&txq->stats),
		       txq->stats.in, txq->stats.out,
		       sta ? sta->mac_addr : NULL, txq->tid,
		       atomic_read(&txq->sending));
		q_stats_reset(&txq->stats);
	}
	WQ_DBG(DM_TX, DL_WRN,
	       "TXQ[**]: %4d, in - out: %8d = %8u - %8u (hif: %d)\n",
	       rwnx_hw->tx_stats.max, q_stats_n(&rwnx_hw->tx_stats),
	       rwnx_hw->tx_stats.in, rwnx_hw->tx_stats.out,
	       atomic_read(&rwnx_hw->sending));
	q_stats_reset(&rwnx_hw->tx_stats);
#endif
	WQ_DBG(DM_TX, DL_WRN,
	       "bundle: %4u, %4u, %4u, %4u, %4u, %4u, %4u, %4u, %4u, %4u, "
	       "%4u, %4u, %4u, %4u, %4u, %4u, %4u, %4u, %4u, %4u, "
	       "%4u, %4u\n",
	       rwnx_hw->bundle_stats[0],
	       rwnx_hw->bundle_stats[1] - rwnx_hw->bundle_stats[0],
	       rwnx_hw->bundle_stats[2], rwnx_hw->bundle_stats[3],
	       rwnx_hw->bundle_stats[4], rwnx_hw->bundle_stats[5],
	       rwnx_hw->bundle_stats[6], rwnx_hw->bundle_stats[7],
	       rwnx_hw->bundle_stats[8], rwnx_hw->bundle_stats[9],
	       rwnx_hw->bundle_stats[10], rwnx_hw->bundle_stats[11],
	       rwnx_hw->bundle_stats[12], rwnx_hw->bundle_stats[13],
	       rwnx_hw->bundle_stats[14], rwnx_hw->bundle_stats[15],
	       rwnx_hw->bundle_stats[16], rwnx_hw->bundle_stats[17],
	       rwnx_hw->bundle_stats[18], rwnx_hw->bundle_stats[19],
	       rwnx_hw->bundle_stats[20], rwnx_hw->bundle_stats[21]);

	for (i = 0; i < ARRAY_SIZE(rwnx_hw->bundle_stats); i++)
		rwnx_hw->bundle_stats[i] = 0;

	extap_tbl_dump();
}

/**
 * rwnx_hwq_init - Initialize all hwq structures
 *
 * @rwnx_hw: Driver main data
 *
 */
void rwnx_hwq_init(struct rwnx_hw *rwnx_hw)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(rwnx_hw->hwq); i++) {
		struct rwnx_hwq *hwq = &rwnx_hw->hwq[i];

		for (j = 0; j < CONFIG_USER_MAX; j++)
			hwq->credits[j] = nx_txdesc_cnt[i];
		hwq->id = i;
		hwq->size = nx_txdesc_cnt[i];
		INIT_LIST_HEAD(&hwq->list);
		tasklet_init(&hwq->tasklet, rwnx_hwq_process_task,
			     (unsigned long)hwq);
	}
}

void rwnx_hwq_deinit(struct rwnx_hw *rwnx_hw)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rwnx_hw->hwq); i++)
		tasklet_kill(&rwnx_hw->hwq[i].tasklet);
}

void rwnx_set_txq_flow_ctrl_threshlod(struct rwnx_hw *rwnx_hw, u32 stop_thr,
				      u32 restart_thr)
{
	rwnx_hw->txq_stop_threshlod = stop_thr;
	rwnx_hw->txq_restart_threshlod = restart_thr;
}

void rwnx_mgmt_tx_cb_list_deinit(struct rwnx_hw *rwnx_hw)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&rwnx_hw->core->ipc.mgmt_txdone))) {
		WQ_DBG(DM_TX, DL_WRN, "%s clean mgmt %p!!!\n", __func__, skb);
		wq_tx_skb_free_any(rwnx_hw->core, skb);
	}
}
