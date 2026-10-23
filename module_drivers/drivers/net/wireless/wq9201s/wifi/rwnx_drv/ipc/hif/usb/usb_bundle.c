/**
 ******************************************************************************
 *
 * @file usb_bundle.c
 *
 * Copyright (C) WUQi-Tech 2016-2024
 *
 ******************************************************************************
 */
#include "usb_bundle.h"
#include "fw_api/wifi/htc/htc_v0.h"
#include "htc.h"
#include "wq_tx_credit.h"
#include "wq_log.h"
#include "fw_api/non_wifi/hif/usb/api.h"
#include "hif_api.h"

extern enum wq_hif_ver hif_htc_decap(struct sk_buff *skb, enum wq_hif_qid qid);

int wq_usb_rx_get_pktnum(struct sk_buff *skb, u32 utf_len)
{
	struct wq_htc_v0 *htc_v0;
	struct wq_hif_hdr *hif_hdr_ptr;
	struct wq_hif_hdr hif_hdr;
	u32 flags, i, pkt_num = 0, subpkt_len = 0, pkt_accu_len = 0;
	u8 *data = skb->data;

	// sanity check
	do {
		hif_hdr_ptr = (struct wq_hif_hdr *)(data);
		*(u32 *)&hif_hdr = le32_to_cpu(*(u32 *)hif_hdr_ptr);

		if (hif_hdr.ver == WQ_HIF_HDR_VER_0) {
			subpkt_len = hif_hdr.dw_len << 2;
			pkt_accu_len += subpkt_len;
			data += subpkt_len;
			pkt_num++;
		} else {
			WQ_DBG(DM_TRBUS, DL_WRN,
			       "[auto]msg:%s: skb=0x%p, skb len=%d, invalid hif/htc header %*ph...\n",
			       __func__, skb, skb->len, HEADROOM_HIF_HTC,
			       skb->data);
			return -1;
		}

		if (pkt_num > WQ_USB_MAX_BUNDLE_I) {
			data = skb->data;

			WQ_DBG(DM_TRBUS, DL_WRN,
			       "[auto]msg:%s:fatal error, skb=0x%p, utf_len:%d, pkt_alen:%d, pkt_num=%u\n",
			       __func__, skb, utf_len, pkt_accu_len, pkt_num);

			for (i = 0; i < WQ_USB_MAX_BUNDLE_I; i++) {
				hif_hdr_ptr = (struct wq_hif_hdr *)(data);
				*(u32 *)&hif_hdr =
					le32_to_cpu(*(u32 *)hif_hdr_ptr);
				subpkt_len = hif_hdr.dw_len << 2;
				htc_v0 = (struct wq_htc_v0 *)(hif_hdr_ptr + 1);
				flags = le32_to_cpu(htc_v0->flags);
				WQ_DBG(DM_TRBUS, DL_WRN,
				       "| data=0x%p, i=%d, seq=%u, pkt_len=%d\n",
				       data, i, WQ_IPC_SEQ(flags), subpkt_len);

				data += subpkt_len;
			}
			return -1;
		}
	} while (pkt_accu_len < utf_len);

	if ((pkt_accu_len != utf_len)) {
		data = skb->data;

		WQ_DBG(DM_TRBUS, DL_WRN,
		       "[auto]msg:%s, rx pkt check failed! | skb=0x%p, utf_len=%u, pkt_alen=%u, pkt_num=%u\n",
		       __func__, skb, utf_len, pkt_accu_len, pkt_num);

		for (i = 0; i < pkt_num; i++) {
			hif_hdr_ptr = (struct wq_hif_hdr *)(data);
			*(u32 *)&hif_hdr = le32_to_cpu(*(u32 *)hif_hdr_ptr);
			subpkt_len = hif_hdr.dw_len << 2;
			htc_v0 = (struct wq_htc_v0 *)(hif_hdr_ptr + 1);
			flags = le32_to_cpu(htc_v0->flags);
			WQ_DBG(DM_TRBUS, DL_WRN,
			       "| data=0x%p, i=%d, seq=%u, pkt_len=%d\n", data,
			       i, WQ_IPC_SEQ(flags), subpkt_len);

			data += subpkt_len;
		}
		return -1;
	}
	return pkt_num;
}

void wq_usb_rx_debundle(struct wq_core *core, enum wq_hif_qid qid,
			struct sk_buff *skb, u8 pkt_num)
{
	struct wq_htc_v0 *htc_v0;
	struct htc_q *rxq = &core->htc.rxq.pkt;
	struct wq_hif_hdr *hif_hdr_ptr;
	struct wq_hif_hdr hif_hdr;
	u32 flags, i, subpkt_len = 0;
	u16 seq;
	u32 last_seq;
	u8 *data = skb->data;
	struct sk_buff *skb_rxpkt = NULL;

	BUG_ON(qid != WQ_QID_AC_BK);

	// de-bundle rx pkt
	for (i = 0; i < pkt_num; i++) {
		hif_hdr_ptr = (struct wq_hif_hdr *)(data);
		*(u32 *)&hif_hdr = le32_to_cpu(*(u32 *)hif_hdr_ptr);
		subpkt_len = hif_hdr.dw_len << 2;

		skb_rxpkt = dev_alloc_skb(subpkt_len);
		if (!skb_rxpkt) {
			htc_v0 = (struct wq_htc_v0 *)(hif_hdr_ptr + 1);
			flags = le32_to_cpu(htc_v0->flags);

			WQ_DBG(DM_IPC, DL_WRN,
			       "[auto]msg:%s: skb allocate failed | skb=0x%p, i=%d, pkt_num=%u, seq=%u\n",
			       __func__, skb, i, pkt_num, WQ_IPC_SEQ(flags));
			break;
		}

		//memory copy
		memcpy(skb_rxpkt->data, data, subpkt_len);
		skb_put(skb_rxpkt, subpkt_len);

		if (hif_htc_decap(skb_rxpkt, qid) != WQ_HIF_HDR_VER_0) {
			dev_kfree_skb_any(skb_rxpkt);
			goto NEXT;
		}

		htc_v0 = ((struct wq_htc_v0 *)skb_rxpkt->data) -
			 1; /* data points to htc end */

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
			       "[auto]msg:%s: qid %d, skb=0x%p + %4d, seq %x, correct seq=%x\n",
			       __func__, qid, skb_rxpkt, skb_rxpkt->len, seq,
			       last_seq);
		}

		BUG_ON(!core->hw);
		if (htc_v0->credit_grp[0].all || htc_v0->credit_grp[1].all) {
			rwnx_return_dev_credit(core->hw,
					       htc_v0->credit_grp[0].txq,
					       htc_v0->credit_grp[1].txq);
			wq_reschedule_hwq(core);
		}

		/* drop zero length packet carried with it the tx credit info */
		if (!skb_rxpkt->len) {
			dev_kfree_skb_any(skb_rxpkt);
			goto NEXT;
		}

		skb_queue_tail(&rxq->head, skb_rxpkt);

	NEXT:
		data += subpkt_len;
	}

	htc_q_kickoff(rxq);
}
