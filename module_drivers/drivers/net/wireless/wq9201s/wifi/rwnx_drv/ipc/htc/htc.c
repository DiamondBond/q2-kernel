#include "fw_api/wifi/htc/htc_v0.h"
#include "fw_api/wifi/mac/cp_api.h"

#include "hif_api.h"
#include "htc.h"
#include "wq_tx_credit.h"
#include "wq_ipc.h"
#include "wq_log.h"
#include "wq_profiling.h"
#include "utils.h"

#define HTC_TASK_BUDGET 0
#define NAPI_RX_WEIGHT 64

static struct htc_wait_q htc_tx_waitq;

void hif_htc_encap_v0(struct wq_core *core, struct sk_buff *skb,
		      enum wq_hif_qid qid, u16 seq, u32 u)
{
	struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);
	u16 len = skb->len;
	u16 padding;
	struct wq_htc_v0 *htc_v0 = (void *)skb_push(skb, sizeof(*htc_v0));
	struct wq_hif_hdr *hif_hdr = (void *)skb_push(skb, sizeof(*hif_hdr));
	__le32 *hif_hdr_ptr = (void *)hif_hdr;
	u8 type = qid == WQ_QID_MSG ? WQ_IPC_TPE_CMD : WQ_IPC_TPE_PKT;

	txcb->qid = qid;
	txcb->has_hif_htc = 1;

	*htc_v0 = (struct wq_htc_v0){
		.flags = cpu_to_le32(
			WQ_IPC_FLAGS_MAKE(type, WQ_IPC_RCV_DEV, qid, seq)),
		.u.cmd_type = cpu_to_le32(u),
		/* keep credit_grp all zero */
		.buf_len = cpu_to_le32(len),
	};

	if (qid == WQ_QID_MSG) {
		struct ipc_a2e_msg *msg = (struct ipc_a2e_msg *)(htc_v0 + 1);
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: msg id=%d, seq=%d\n", __func__,
		       msg->id, WQ_IPC_SEQ(htc_v0->flags));
	}

	len += sizeof(*htc_v0);
	if ((padding = len & (sizeof(u32) - 1))) {
		padding = sizeof(u32) - padding;
		if (skb_tailroom(skb) >= padding)
			skb_put(skb, padding);
		len += padding; /* add padding even no enough tail room */
	}
	if (core->hif_ops->hif != WQ_HIF_USB &&
	    skb_tailroom(skb) >= TAILROOM_HIF) {
		skb_put(skb, TAILROOM_HIF);
		len += TAILROOM_HIF;
	}

	hif_hdr->ptn = WQ_HIF_HDR_MAGIC;
	hif_hdr->ver = WQ_HIF_HDR_VER_0;
	hif_hdr->qid = qid;
	hif_hdr->dw_len = len >> 2; /* length in u32 */
	hif_hdr->crc = 0; /* reserved */

	*hif_hdr_ptr = cpu_to_le32(*hif_hdr_ptr);
}

void hif_htc_bundle_encap_v0(struct sk_buff_head *skbq, enum wq_hif_qid qid,
			     u16 seq, u32 u)
{
	struct sk_buff *skb = skb_peek(skbq);
	struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);
	u16 len = skb->len;
	u16 padding;
	struct wq_htc_v0 *htc_v0 = (void *)skb_push(skb, sizeof(*htc_v0));
	struct wq_hif_hdr *hif_hdr = (void *)skb_push(skb, sizeof(*hif_hdr));
	__le32 *hif_hdr_ptr = (void *)hif_hdr;
	u8 type = qid == WQ_QID_MSG ? WQ_IPC_TPE_CMD : WQ_IPC_TPE_PKT;

	txcb->qid = qid;
	txcb->has_hif_htc = 1;
	if (txcb->msdu_in_host) {
		len = skb_queue_len(skbq) * sizeof(struct txdesc_host);
	} else if (skb_queue_len(skbq) > 1) {
		struct sk_buff *skb_walk;
		u8 skbidx = 0;
		skb_queue_walk(skbq, skb_walk)
		{
			skbidx++;
			if (skbidx == 1) {
				len = ALIGN(len, sizeof(u32));
				continue;
			}
			len += ALIGN(skb_walk->len, sizeof(u32));
		}
	}

	*htc_v0 = (struct wq_htc_v0){
		.flags = cpu_to_le32(
			WQ_IPC_FLAGS_MAKE(type, WQ_IPC_RCV_DEV, qid, seq)),
		.u.cmd_type = cpu_to_le32(u),
		/* keep credit_grp all zero */
		.buf_len = cpu_to_le32(len),
	};

	len += sizeof(*htc_v0);
	if ((padding = len & (sizeof(u32) - 1))) {
		padding = sizeof(u32) - padding;
		if (skb_tailroom(skb) >= padding)
			skb_put(skb, padding);
		len += padding; /* add padding even no enough tail room */
	}
	if (skb_tailroom(skb) >= TAILROOM_HIF) {
		skb_put(skb, TAILROOM_HIF);
		len += TAILROOM_HIF;
	}

	hif_hdr->ptn = WQ_HIF_HDR_MAGIC;
	hif_hdr->ver = WQ_HIF_HDR_VER_0;
	hif_hdr->qid = qid;
	hif_hdr->dw_len = len >> 2; /* length in u32 */
	hif_hdr->crc = 0; /* reserved */

	*hif_hdr_ptr = cpu_to_le32(*hif_hdr_ptr);
}

enum wq_hif_ver hif_htc_decap(struct sk_buff *skb, enum wq_hif_qid qid)
{
	struct wq_hif_hdr *hif_hdr_ptr = (struct wq_hif_hdr *)(skb->data);
	struct wq_htc_v0 *htc_v0 = (struct wq_htc_v0 *)(hif_hdr_ptr + 1);
	struct wq_hif_hdr hif_hdr;
	u32 buf_len;

	*(u32 *)&hif_hdr = le32_to_cpu(*(u32 *)hif_hdr_ptr);

	if (hif_hdr.ver == WQ_HIF_HDR_VER_0) {
		buf_len = HEADROOM_HIF_HTC + le32_to_cpu(htc_v0->buf_len);
		// align(hif hdr + htc hdr + buf_len + crc tailer) == skb->len is expected
		// align(hif hdr + htc hdr + buf_len + crc tailer) < skb->len is acceptted
		// align(hif hdr + htc hdr + buf_len + crc tailer) > skb->len is invalid
		if (ALIGN(buf_len + TAILROOM_HIF, sizeof(u32)) > skb->len) {
			WQ_DBG(DM_IPC, DL_ERR, "%s:buf_len:%d, %d-%d-%d, skb_len:%d\n",
				__func__, buf_len, HEADROOM_HIF_HTC, TAILROOM_HIF,
				htc_v0->buf_len, skb->len);
			WARN_ON(1);
			goto invalid;
		}

		/* check qid in hif header */
		if (hif_hdr.qid != qid) {
			WQ_DBG(DM_IPC, DL_ERR,
			       "%s: invalid qid %d (expect %d)\n", __func__,
			       hif_hdr.qid, qid);
			goto invalid;
		}

		if (qid == WQ_QID_MSG) {
			struct ipc_e2a_msg *e2a =
				(struct ipc_e2a_msg *)(htc_v0 + 1);
			WQ_DBG(DM_TRBUS, DL_INF, "%s: e2a msg id=%d, seq=%d\n",
			       __func__, e2a->id, WQ_IPC_SEQ(htc_v0->flags));
		}

		// remove hif/htc header, padding and trailer
		skb_trim(skb, buf_len);
		skb_pull(skb, HEADROOM_HIF_HTC);

		return WQ_HIF_HDR_VER_0;
	}

invalid:
	WQ_DBG(DM_IPC, DL_WRN,
	       "%s, skb len %d, invalid hif/htc header %*ph...\n", __func__,
	       skb->len, HEADROOM_HIF_HTC, skb->data);
	BUG_ON(1);
	return WQ_HIF_HDR_VER_RESERVED;
}
EXPORT_SYMBOL(hif_htc_decap);

static int htc_enqueue_tx_waitq(struct sk_buff_head *skbq, enum wq_hif_qid qid)
{
	struct htc_tx_req *tx_req;
	struct sk_buff *skb;
	WQ_DBG(DM_TRBUS, DL_WRN, "htc_enqueue_tx_waitq enter");
	tx_req = kzalloc(sizeof(struct htc_tx_req),
			 in_softirq() ? GFP_ATOMIC : GFP_KERNEL);
	if (!tx_req) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: no memory for htc wait queue!\n",
		       __func__);
		return -ENOMEM;
	}
	__skb_queue_head_init(&tx_req->skbq);
	while ((skb = __skb_dequeue(skbq))) {
		__skb_queue_tail(&tx_req->skbq, skb);
	}
	tx_req->qid = qid;
	spin_lock_bh(&htc_tx_waitq.lock);
	list_add_tail(&tx_req->list, &htc_tx_waitq.tx_req_list);
	htc_tx_waitq.tx_wait_cnt++;
	spin_unlock_bh(&htc_tx_waitq.lock);
	return 0;
}

static void __htc_insert_seq(struct wq_core *core, enum wq_hif_qid qid,
			     uint32_t seq, struct sk_buff_head *skbq)
{
	struct sk_buff *skb = skb_peek(skbq);
	struct wq_htc_v0 *htc_v0;
	uint32_t new_flags;

	htc_v0 = (struct wq_htc_v0 *)(skb->data + hif_get_hdr_sz(core));

	if (qid == WQ_QID_MSG) {
		struct ipc_a2e_msg *msg = (struct ipc_a2e_msg *)(htc_v0 + 1);
		if (msg->id != ME_FREE_HOST_DATA_RING_REQ) {
			WQ_DBG(DM_TRBUS, DL_WRN, "%s: e2a msg id=%d, seq=%d\n",
			       __func__, msg->id, seq);
		}
	}

	new_flags = le32_to_cpu(htc_v0->flags);
	new_flags = ((new_flags) & (~WQ_IPC_SEQ_MASK)) |
		    (((seq) << WQ_IPC_SEQ_SHIFT) & WQ_IPC_SEQ_MASK);
	htc_v0->flags = cpu_to_le32(new_flags);

	return;
}

static int __htc_hif_tx(struct wq_core *core, enum wq_hif_qid qid,
			struct sk_buff_head *skbq)
{
	int ret;
	uint32_t seq;
	struct htc_txq *txq = &core->htc.txq[qid];

	spin_lock_bh(&txq->up.lock);
	seq = ++txq->fc.seq;
	__htc_insert_seq(core, qid, seq, skbq);
	ret = core->hif_ops->hif_tx(core, qid, skbq);
	spin_unlock_bh(&txq->up.lock);

	WQ_DBG(DM_IPC, DL_VRB, "%s: qid=%d, seq=%x(%x), qlen=%d, ret %d\n",
	       __func__, qid, seq, (u16)seq, skb_queue_len(skbq), ret);

	return ret;
}

int htc_tx(struct wq_core *core, enum wq_hif_qid qid, struct sk_buff_head *skbq,
	   uint32_t u)
{
	struct htc_txq *txq = &core->htc.txq[qid];
	struct sk_buff *skb;
	int ret;
	u8 type;
	uint32_t seq;

	ret = hif_autopm_get(core);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: auto pm, ret=%d\n", __func__,
		       ret);
		return ret;
	}

	type = qid == WQ_QID_MSG ? WQ_IPC_TPE_CMD : WQ_IPC_TPE_PKT;

	if (core->hif_ops->hif == WQ_HIF_USB) {
		spin_lock_bh(&txq->up.lock);
		skb_queue_walk(skbq, skb)
		{
			struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);
			if (type == WQ_IPC_TPE_PKT) {
				u = WQ_IPC_FLAGS_TX_NORMAL_QUEUE;
				if (txcb->pkt_cls & BIT(WQ_PKT_CLS_EAPOL))
					u = WQ_IPC_FLAGS_TX_HIGH_QUEUE;
			}
			seq = ++txq->fc.seq;
			hif_htc_encap_v0(core, skb, qid, seq, u);
			txcb->qid = qid;

			WQ_DBG(DM_IPC, DL_VRB,
			       "%s: qid=%d, seq=%x(%x), qlen=%d, u=0x%x\n",
			       __func__, qid, seq, (u16)seq,
			       skb_queue_len(skbq), u);
		}
		ret = core->hif_ops->hif_tx(core, qid, skbq);
		spin_unlock_bh(&txq->up.lock);
	} else if (core->hif_ops->hif == WQ_HIF_SDIO) {
		if (atomic_fetch_add(skb_queue_len(skbq), &htc_tx_waitq.pending_req) == 0) {
			hif_autopm_get_async(core);
		}
		spin_lock_bh(&txq->up.lock);
		skb_queue_walk(skbq, skb)
		{
			struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);
			seq = ++txq->fc.seq;
			hif_htc_encap_v0(core, skb, qid, seq, u);
			txcb->qid = qid;

			WQ_DBG(DM_IPC, DL_VRB,
			       "%s: qid=%d, seq=%x(%x), qlen=%d, u=0x%x\n",
			       __func__, qid, seq, (u16)seq,
			       skb_queue_len(skbq), u);
		}
		ret = core->hif_ops->hif_tx(core, qid, skbq);
		spin_unlock_bh(&txq->up.lock);
	} else {
		hif_htc_bundle_encap_v0(skbq, qid, 0, u);
		skb_queue_walk(skbq, skb)
		{
			struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);

			txcb->qid = qid;
			if (atomic_fetch_inc(&htc_tx_waitq.pending_req) == 0) {
				hif_autopm_get_async(core);
			}
		}
		if (!hif_autopm_is_bus_active(core)) {
			ret = htc_enqueue_tx_waitq(skbq, qid);
			return ret;
		}
		ret = __htc_hif_tx(core, qid, skbq);
	}

	if (ret && ret != -ENOBUFS && ret!= -ENXIO) {
		skb_queue_walk(skbq, skb)
		{
			WQ_DBG(DM_IPC, DL_ERR,
			       "%s: qid=%d, skb=%p, qlen=%d, u=0x%x ret=%d\n",
			       __func__, qid, skb, skb_queue_len(skbq), u, ret);
		}
		if (core->hif_ops->hif == WQ_HIF_PCIE)
			BUG_ON(1);
	}

	hif_autopm_put(core);
	return ret;
}

/* NB: may be under ISR context, it should be done quickly as possible. */
static void __htc_txq_done(struct wq_core *core, struct sk_buff_head *skbq,
			   int status)
{
	struct sk_buff *skb;
	enum wq_hif_qid qid;
	struct htc_txq *txq;

	if (atomic_sub_return(skb_queue_len(skbq),&htc_tx_waitq.pending_req) == 0) {
		hif_autopm_put_async(core);
	}

	while ((skb = __skb_dequeue(skbq))) {
		struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);
		int seq = -1;

		qid = txcb->qid;
		BUG_ON(qid >= WQ_QID_MAX);

		txq = &core->htc.txq[qid];

		if (txcb->has_hif_htc) {
			struct wq_hif_hdr *hif_hdr =
				(struct wq_hif_hdr *)skb->data;
			struct wq_htc_v0 *htc_v0 =
				(struct wq_htc_v0 *)(hif_hdr + 1);
			u32 buf_len = le32_to_cpu(htc_v0->buf_len);

			BUG_ON(hif_hdr->ptn != WQ_HIF_HDR_MAGIC);
			BUG_ON(hif_hdr->ver != WQ_HIF_HDR_VER_0);
			BUG_ON(qid != hif_hdr->qid);

			if (txcb->usb_out_bundle_num) {
				txq->fc.deq += txcb->usb_out_bundle_num;
			} else {
				++txq->fc.deq;
			}
			seq = WQ_IPC_SEQ(le32_to_cpu(htc_v0->flags));

			skb_pull(skb, HEADROOM_HIF_HTC);

			// Remove the 4-byte align and TAILROOM_HIF since DVR should send cb event to up layer
			if (skb->len > buf_len)
				skb_trim(skb, buf_len);
		}
		WQ_DBG(DM_IPC, status ? DL_ERR : DL_INF,
		       "%s: qid=%d, seq=%d, status=%d\n", __func__, qid, seq,
		       status);

		if (txcb->qid != WQ_QID_MSG &&
		    wq_ipc_tx_pkt_done_pre(core, skb, status) < 0)
			continue;

		if (core->hif_ops->hif == WQ_HIF_SDIO) {
			if (qid == WQ_QID_MSG) {
				wq_ipc_tx_msg_done(&txq->up, skb);
			} else {
				wq_ipc_tx_pkt_done(&txq->up, skb);
			}
		} else {
			skb_queue_tail(&txq->up.head, skb);
		}
	}

	if (core->hif_ops->hif != WQ_HIF_SDIO) {
		for (qid = 0; qid < ARRAY_SIZE(core->htc.txq); qid++) {
			txq = &core->htc.txq[qid];
			if (!skb_queue_empty(&txq->up.head)) {
				htc_q_kickoff(&txq->up);
			}
		}
	}
}

static void __htc_tx_done(struct wq_core *core, struct sk_buff *skb, int status)
{
	struct sk_buff_head skbq;

	__skb_queue_head_init(&skbq);
	__skb_queue_tail(&skbq, skb);

	__htc_txq_done(core, &skbq, status);
}

/* NB: may be under ISR context, it should be done quickly as possible. */
void __htc_ll_msdu_tx_done(struct wq_core *core, struct sk_buff *skb,
			   int status)
{
#if TXQ_RING_FUNCTION_ENABLE
	//struct htc *htc = &core->htc;
	//u8 txq_idx = (u8)status;
	(void)skb;
	(void)status;

	if (atomic_dec_return(&htc_tx_waitq.pending_req) == 0) {
		hif_autopm_put_async(core);
	}

	//extern void rwnx_txq_tx_done_pre(struct rwnx_hw *rwnx_hw, uint8_t txq_idx);
	//rwnx_txq_tx_done_pre(core->hw, txq_idx);
	//htc_txq_ring_2task();

#else
	__htc_tx_done(core, skb, status);
#endif
}

#ifdef NAPI_SUPPORT
static void htc_napi_schedule(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw->napi_param.param_enable) {
		if (skb_queue_len(&rwnx_hw->napi_rx_pkt_list) > rwnx_hw->napi_param.packets_num) {
			hrtimer_try_to_cancel(&rwnx_hw->napi_rx_defer_timer);
			napi_schedule(&rwnx_hw->napi_rx);
		} else {
			hrtimer_start(
				&rwnx_hw->napi_rx_defer_timer,
				ns_to_ktime(rwnx_hw->napi_param.timeout),
				HRTIMER_MODE_REL);
		}
	} else {
		napi_schedule(&rwnx_hw->napi_rx);
	}
}
#endif

void hif_htc_rxq_decap(struct wq_core *core, struct sk_buff_head *skbq)
{
	struct wq_htc_v0 *htc_v0;
	struct htc_q *rxq;
	struct sk_buff *skb;
	u32 flags;
	u16 seq;
	u32 last_seq;
	enum wq_hif_qid qid;

	while ((skb = __skb_dequeue(skbq))) {
		struct wq_hif_hdr *hif_hdr = (struct wq_hif_hdr *)(skb->data);
		qid = hif_hdr->qid;

		if (qid == WQ_QID_MSG) {
			rxq = &core->htc.rxq.msg;
		} else {
			rxq = &core->htc.rxq.pkt;
		}

		if (hif_htc_decap(skb, qid) != WQ_HIF_HDR_VER_0) {
			dev_kfree_skb_any(skb);
			continue;
		}

		/* data points to htc end */
		htc_v0 = ((struct wq_htc_v0 *)skb->data) - 1;

		flags = le32_to_cpu(htc_v0->flags);
		seq = WQ_IPC_SEQ(flags);
		WQ_DBG(DM_IPC, DL_VRB, "%s: qid %d, seq=%x/%x, len=%d\n",
		       __func__, qid, seq, rxq->last_seq,
		       le32_to_cpu(htc_v0->buf_len));

		spin_lock(&rxq->lock);
		// check sequence
		last_seq = ++rxq->last_seq;
		if (seq != (typeof(seq))last_seq) {
			rxq->last_seq &= ~((u32)(typeof(seq))(-1));
			rxq->last_seq |= seq;
		}
		spin_unlock(&rxq->lock);

		if (seq != (typeof(seq))last_seq) {
			WQ_DBG(DM_IPC, DL_WRN,
			       "%s: qid %d, skb=0x%p + %4d, seq %x, correct seq=%x\n",
			       __func__, qid, skb, skb->len, seq, last_seq);
		}

		/* NB: tx credit is never returned by RX message */
		if (qid == WQ_QID_AC_BK) {
			BUG_ON(!core->hw);
			if (htc_v0->credit_grp[0].all ||
			    htc_v0->credit_grp[1].all) {
				rwnx_return_dev_credit(
					core->hw, htc_v0->credit_grp[0].txq,
					htc_v0->credit_grp[1].txq);
				wq_reschedule_hwq(core);
			}
		}

		/* drop zero length packet carried with it the tx credit info */
		if (!skb->len) {
			dev_kfree_skb_any(skb);
			continue;
		}

		skb_queue_tail(&rxq->head, skb);
	}
}

/* NB: may be under ISR context, it should be done quickly as possible. */
static void __htc_rxq(struct wq_core *core, struct sk_buff_head *skbq)
{
	struct htc_q *rxq;
	struct rwnx_hw *rwnx_hw = core->hw;
	struct sk_buff *skb;
	u64 time_start_us = 0, time_end_us = 0;

	if (rwnx_hw) {
		time_start_us = (u64)ktime_to_us(ktime_get());
	}

	hif_htc_rxq_decap(core, skbq);

	if (rwnx_hw) {
		time_end_us = (u64)ktime_to_us(ktime_get());
		atomic_add((u32)(time_end_us - time_start_us), &rwnx_hw->htc_rxq_decap_time);
	}

	rxq = &core->htc.rxq.msg;
	if (!skb_queue_empty(&rxq->head)) {
		htc_q_kickoff(rxq);
	}

	rxq = &core->htc.rxq.pkt;
#ifdef NAPI_SUPPORT
	if (rwnx_hw) {
		if (core->config.ipc_rx_pkt_use_wq) {
			while ((skb = skb_dequeue(&rxq->head))) {
				local_bh_disable();
				__wq_ipc_rx_pkt(rwnx_hw, skb);
				local_bh_enable();
			}

			if (rwnx_hw->napi_param.napi_enable
					&& !skb_queue_empty(&rwnx_hw->napi_rx_pkt_list)) {
				htc_napi_schedule(rwnx_hw);
			}
		} else {
			if (!skb_queue_empty(&rxq->head)) {
				if (rwnx_hw->napi_param.napi_enable) {
					htc_napi_schedule(rwnx_hw);
				} else {
					htc_q_kickoff(rxq);
				}
			}
		}
	}
#else
	if (!skb_queue_empty(&rxq->head)) {
		htc_q_kickoff(rxq);
	}
#endif

	if (rwnx_hw) {
		time_end_us = (u64)ktime_to_us(ktime_get());
		atomic_add((u32)(time_end_us - time_start_us), &rwnx_hw->htc_rxq_time);
	}
}

/* htc call netif_receive_skb directly in workqueue or kthread env */
static void __htc_rxq_internal(struct wq_core *core, struct sk_buff_head *skbq)
{
	struct htc_q *rxq;
	struct rwnx_hw *rwnx_hw = core->hw;
	struct sk_buff *skb;

	hif_htc_rxq_decap(core, skbq);

	rxq = &core->htc.rxq.msg;
	if (!skb_queue_empty(&rxq->head)) {
		htc_q_kickoff(rxq);
	}

	if (rwnx_hw) {
		rxq = &core->htc.rxq.pkt;
		while ((skb = skb_dequeue(&rxq->head))) {
			local_bh_disable();
			__wq_ipc_rx_pkt(rwnx_hw, skb);
			local_bh_enable();
		}

		while ((skb = skb_dequeue(&rwnx_hw->napi_rx_pkt_list))) {
			local_bh_disable();
			netif_receive_skb(skb);
			local_bh_enable();
		}
	}
}

static void __htc_rx(struct wq_core *core, enum wq_hif_qid qid,
		     struct sk_buff *skb)
{
	struct sk_buff_head skbq;

	(void)qid;

	__skb_queue_head_init(&skbq);
	__skb_queue_tail(&skbq, skb);

	__htc_rxq(core, &skbq);
}

static bool __htc_pending_req_empty(struct wq_core *core)
{
	return !atomic_read(&htc_tx_waitq.pending_req);
}

static void __htc_retrigger_tx_task(struct wq_core *core)
{
	spin_lock_bh(&htc_tx_waitq.lock);
	if (htc_tx_waitq.tx_wait_cnt) {
		WQ_DBG(DM_IPC, DL_WRN, "%s: schedule tx", __func__);
		tasklet_schedule(&htc_tx_waitq.hif_tx_task);
	}
	spin_unlock_bh(&htc_tx_waitq.lock);
}

static void __htc_txq_ring_txdone(struct wq_core *core)
{
	wq_ipc_txq_ring_free(core);
}

static void __htc_tx_skb_dma_unmap(struct wq_core *core, struct sk_buff *skb)
{
	wq_tx_skb_dma_unmap(core, skb);
}

void htc_txq_ring_2task(struct wq_core *core)
{
	if (core->hif_ops->hif_txq_ring_2task)
		core->hif_ops->hif_txq_ring_2task(core);
}

void htc_txq_ring_start_timer(struct wq_core *core)
{
	if (core->hif_ops->hif_txq_ring_timerstart)
		core->hif_ops->hif_txq_ring_timerstart(core);
}

void wq_hif_ops_setup(struct wq_hif_ops *ops)
{
	ops->htc_rx = __htc_rx;
	ops->htc_rxq = __htc_rxq;
	ops->htc_rxq_internal = __htc_rxq_internal;
	ops->htc_tx_done = __htc_tx_done;
	ops->htc_txq_done = __htc_txq_done;
	ops->htc_ll_msdu_tx_done = __htc_ll_msdu_tx_done;
	ops->htc_pending_req_empty = __htc_pending_req_empty;
	ops->htc_retrigger_tx_task = __htc_retrigger_tx_task;
	ops->htc_txq_ring_txdone = __htc_txq_ring_txdone;
	ops->htc_tx_skb_dma_unmap = __htc_tx_skb_dma_unmap;
}

static inline void __htc_tx_ll_msdu_task(struct htc_q *q)
{
	if (!q->fn) {
		return;
	}

	q->fn(q, 0);

	return;
}

static inline void __htc_task(struct htc_q *q)
{
#if HTC_TASK_BUDGET > 0
	int budget = HTC_TASK_BUDGET;
#endif
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&q->head))) {
		if (!q->fn || q->fn(q, skb)) {
			dev_kfree_skb_any(skb);
		}
#if HTC_TASK_BUDGET > 0
		if (--budget < 0)
			break;
#endif
	}
}

static void htc_task(unsigned long data)
{
	struct htc_q *q = (struct htc_q *)data;
	if (q->qid <= WQ_QID_MSG) {
		__htc_task(q);
		if (!skb_queue_empty(&q->head)) {
			WQ_DBG(DM_IPC, DL_WRN,
			       "%s: qid %d, %d pending, reschedule it.\n",
			       __func__, q->qid, skb_queue_len(&q->head));
			tasklet_schedule(&q->task);
		}
	} else {
		__htc_tx_ll_msdu_task(q);
	}
}

int htc_q_init(struct wq_core *core, struct htc_q *q, enum wq_hif_qid qid,
	       htc_fn_t fn)
{
	q->qid = qid;
	q->fn = fn;

	spin_lock_init(&q->lock);
	skb_queue_head_init(&q->head);
	tasklet_init(&q->task, htc_task, (unsigned long)q);
	return 0;
}

#ifdef CONFIG_RX_THREAD
static int htc_thread(void *data)
{
	struct wq_kthread *thread = (struct wq_kthread *)data;
	struct htc_q *q = container_of(thread, struct htc_q, thread);
	u32 start_ms;

	WQ_DBG(DM_TRBUS, DL_WRN, "%s: enter\n", __func__);

	while (true) {
		wait_for_completion_interruptible(&thread->comp);

		if (kthread_should_stop()) {
			WQ_DBG(DM_TRBUS, DL_ERR, "%s: kthread should stop!\n",
			       __func__);
			goto exit;
		}
		/* polling */
		start_ms = jiffies_to_msecs(jiffies);
		while (jiffies_to_msecs(jiffies) - start_ms <= HTC_POLLING_RX_TASK_TIME_MS) {
			__htc_task(q);
		}
	}

exit:
	WQ_DBG(DM_TRBUS, DL_WRN, "%s: leave\n", __func__);
	return 0;
}

static int htc_q_name(char *name, struct wq_core *core, struct htc_q *q)
{
	struct htc *htc = &core->htc;

	if (q >= &htc->txq[0].up) {
		if (q->qid == WQ_QID_MSG)
			return sprintf(name, "tx.msg");
		return sprintf(name, "tx[%d]", q->qid);
	} else if (q == &htc->rxq.msg) {
		return sprintf(name, "rx.msg");
	} else if (q == &htc->rxq.pkt) {
		return sprintf(name, "rx.pkt");
	} else {
		BUG_ON(1);
		return 0;
	}
}

int htc_q_init_thread(struct wq_core *core, struct htc_q *q,
		      enum wq_hif_qid qid, htc_fn_t fn)
{
	int ret;
	char name[64];

	htc_q_init(core, q, qid, fn);

	q->use_thread = true;
	htc_q_name(name, core, q);
	ret = wq_thread_init(&q->thread, htc_thread, "wq.htc.%s", name);
	if (ret) {
		wq_thread_deinit(&q->thread);
	}

	return 0;
}
#endif

void htc_q_deinit(struct htc_q *q)
{
	struct sk_buff *skb;

#ifdef CONFIG_RX_THREAD
	if (q->use_thread)
		wq_thread_deinit(&q->thread);
#endif
	tasklet_kill(&q->task);

	while ((skb = skb_dequeue(&q->head)))
		dev_kfree_skb_any(skb);
}

// only for sys suspend/resume and rpm suspend/resume
void htc_tx_flush_waitq(struct wq_core *core)
{
	struct htc_tx_req *tx_req, *tmp;
	int ret = 0;
	struct htc_txq *txq;

	spin_lock_bh(&htc_tx_waitq.lock);

	if (list_empty(&htc_tx_waitq.tx_req_list)) {
		goto done;
	}

	list_for_each_entry_safe (tx_req, tmp, &htc_tx_waitq.tx_req_list,
				  list) {
		txq = &core->htc.txq[tx_req->qid];

		if (0 !=
		    (ret = __htc_hif_tx(core, tx_req->qid, &tx_req->skbq))) {
			WQ_DBG(DM_IPC, DL_ERR, "%s: Tx failed - %d.", __func__,
			       ret);
		}

		list_del(&tx_req->list);
		kfree(tx_req);
		htc_tx_waitq.tx_wait_cnt--;
	}

	BUG_ON(htc_tx_waitq.tx_wait_cnt);

done:
	spin_unlock_bh(&htc_tx_waitq.lock);
}

static void htc_tx_task(unsigned long data)
{
	struct wq_core *core = (struct wq_core *)data;

	WQ_DBG(DM_IPC, DL_WRN, "%s: enter", __func__);

	htc_tx_flush_waitq(core);

	WQ_DBG(DM_IPC, DL_WRN, "%s: end", __func__);
}

void htc_tx_waitq_init(struct wq_core *core)
{
	tasklet_init(&htc_tx_waitq.hif_tx_task, htc_tx_task,
		     (unsigned long)core);
	INIT_LIST_HEAD(&htc_tx_waitq.tx_req_list);
	spin_lock_init(&htc_tx_waitq.lock);
}
