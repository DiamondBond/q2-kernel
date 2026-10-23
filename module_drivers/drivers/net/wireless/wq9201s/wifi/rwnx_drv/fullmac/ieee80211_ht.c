

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2008 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * IEEE 802.11n protocol support.
 */

//#include <linux/dma-mapping.h>
//#include <linux/ieee80211.h>
//#include <linux/etherdevice.h>
//#include <net/ieee80211_radiotap.h>

#include "fw_api/wifi/htc/e2a_event.h"

#include "rwnx_defs.h"
#include "rwnx_rx.h"
#include "rwnx_tx.h"
#include "rwnx_events.h"
#include "rwnx_compat.h"
#include "ieee80211_ht.h"
#include "wq_profiling.h"
#include "wq_log.h"

/* define here, used throughout file */
#define MS(_v, _f) (((_v)&_f) >> _f##_S)
#define SM(_v, _f) (((_v) << _f##_S) & _f)

#define IEEE80211_SEQ_FRAG_MASK 0x000f
#define IEEE80211_SEQ_FRAG_SHIFT 0
#define IEEE80211_SEQ_SEQ_MASK 0xfff0
#define IEEE80211_SEQ_SEQ_SHIFT 4
#define IEEE80211_SEQ_RANGE 4096

#define IEEE80211_SEQ_ADD(seq, incr)                                           \
	(((seq) + (incr)) & (IEEE80211_SEQ_RANGE - 1))

#define IEEE80211_SEQ_INC(seq) IEEE80211_SEQ_ADD(seq, 1)
#define IEEE80211_SEQ_SUB(a, b)                                                \
	(((a) + IEEE80211_SEQ_RANGE - (b)) & (IEEE80211_SEQ_RANGE - 1))
#define IEEE80211_SEQ_BA_RANGE 2048 /* 2^11 */
#define IEEE80211_SEQ_BA_BEFORE(a, b)                                          \
	(IEEE80211_SEQ_SUB(b, a + 1) < IEEE80211_SEQ_BA_RANGE - 1)

#define ticks (jiffies)

#define AMPDU_AGE_INIT_DEFAULT 100
/* FIXME: different network need own ampdu_age ,should place this param into private data */
static int ieee80211_ampdu_age = -1; /* threshold for ampdu reorder q (ms) */
//SYSCTL_PROC(_net_wlan, OID_AUTO, ampdu_age, CTLTYPE_INT | CTLFLAG_RW,
//	&ieee80211_ampdu_age, 0, ieee80211_sysctl_msecs_ticks, "I",
//	"AMPDU max reorder age (ms)");

//static	int ieee80211_recv_bar_ena = 1;
//SYSCTL_INT(_net_wlan, OID_AUTO, recv_bar, CTLFLAG_RW, &ieee80211_recv_bar_ena,
//	    0, "BAR frame processing (ena/dis)");

#define RX_AMPDU_SUPPORT_NUM                                                   \
	4 //HAWK-USB HW MAC support 4, HAWK HW HW MAC support 8
//struct ieee80211_rx_ampdu gv_rx_ampdu[RX_AMPDU_SUPPORT_NUM];
//uint16_t gv_rx_ampdu_alloc_bitmap;
//struct work_struct rx_ampdu_flush_task;

#define MODE_AMSDU_ONGOING_FREE 0 //free ongoing amsdu frame
#define MODE_AMSDU_ONGOING_RESERVE 1 //reserve ongoing amsdu frame
#define MODE_AMSDU_ONGOING_FLUSH 2 //flush ongoing amsdu frame
#define AMSDU_SEQ_END 0x80000000

void ieee80211_ht_init(void)
{
	/*
    * Setup HT parameters that depends on the clock frequency.
   */
	ieee80211_ampdu_age_msecs_set(AMPDU_AGE_INIT_DEFAULT);
}

void ieee80211_rx_reorder_timeout(struct timer_list *callback_timer);
void ampdu_reorder_timeout_task(struct work_struct *work);

struct ieee80211_rx_ampdu *alloc_rx_ampdu(struct rwnx_hw *rwnx_hw,
					  uint8_t sta_idx, uint8_t tid)
{
	struct rwnx_sta *sta = &rwnx_hw->sta_table[sta_idx];
	struct ieee80211_rx_ampdu *rx_ampdu = &sta->rx_ampdu[tid];

	WQ_DBG_MAC(DM_IEEE80211, DL_INF, sta,
		   "alloc_rx_ampdu, sta_idx:%d, tid:%d\n", sta_idx, tid);

	WARN(((sta_idx >= (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)) || (tid >= ARRAY_SIZE(sta->rx_ampdu))),
				 "Invalid param sta %d, tid %d!\n", sta_idx, tid);

	//check if rx_ampdu has been allocated
	//TODO:
	/*    if ((rx_ampdu->tid == tid))
    {
        printk("%s:%d ampdu existed in sta:%d,tid:%d\n", __func__,__LINE__,sta_idx,tid);
        return rx_ampdu;
    }
*/

	/* Avoid of Timer expiring while clearing ieee80211_rx_ampdu. */
	if (NULL != rx_ampdu->rx_reorder_timer.function) {
		del_timer_sync(&rx_ampdu->rx_reorder_timer);
	}

	memset(rx_ampdu, 0, sizeof(struct ieee80211_rx_ampdu));
	rx_ampdu->rwnx_hw = rwnx_hw;
	rx_ampdu->ni = sta;
	rx_ampdu->tid = tid;
	timer_setup(&rx_ampdu->rx_reorder_timer, ieee80211_rx_reorder_timeout,
		    0);
	//INIT_WORK(&rx_ampdu->flush_task_work, ampdu_reorder_timeout_task);

	return rx_ampdu;
}

void free_rx_ampdu(struct ieee80211_rx_ampdu *rap)
{
	struct rwnx_sta *sta = rap->ni;

	WQ_DBG_MAC(DM_IEEE80211, DL_INF, sta,
		   "free_rx_ampdu, sta_idx:%d, tid:%d\n", sta->sta_idx,
		   rap->tid);
	del_timer(&rap->rx_reorder_timer);
}

//SYSINIT(wlan_ht, SI_SUB_DRIVERS, SI_ORDER_FIRST, ieee80211_ht_init, NULL);

static int ampdu_rx_start(uint8_t sta_idx, uint8_t tid,
			  struct ieee80211_rx_ampdu *rap, uint16_t bufsiz,
			  uint16_t baseqctl);

static void ampdu_rx_stop(uint8_t sta_idx, struct ieee80211_rx_ampdu *rap);

/*
 * Add the given frame to the current RX reorder slot.
 *
 * For future offloaded A-MSDU handling where multiple frames with
 * the same sequence number show up here, this routine will append
 * those frames as long as they're appropriately tagged.
 */
static int ampdu_rx_add_slot(struct ieee80211_rx_ampdu *rap, int off, int tid,
			     ieee80211_seq rxseq, uint8_t sta_idx,
			     uint8_t msdu_seq, uint8_t msdu_seq_end,
			     struct sk_buff *m)
{
	struct rwnx_hw *rwnx_hw = rap->rwnx_hw;
	struct rwnx_sta *sta = &rwnx_hw->sta_table[sta_idx];
	struct sk_buff *m_ptr = NULL;
	uint8_t msdu_seq_check = 0;
	uint8_t old_off = rap->rxa_off;
	struct sk_buff *m_drop_ptr = NULL, *m_drop_next_ptr = NULL;

	if (rap->rxa_off == RXA_OFF_INVALID) {
		//case1: non-ongoing, new frame
	} else if (rap->rxa_off == off) {
		//case2: ongoing, same frame
	} else {
		//case3: ongoing, diff frame
		WQ_DBG_MAC(
			DM_IEEE80211, DL_WRN, sta,
			"%s: off=%u->%u, nfrm=%d, seq=0x%x, qfrm=%u, m=0x%p\n",
			__func__, rap->rxa_off, off, rap->rxa_nframes,
			rap->rxa_m_msdu_seq[rap->rxa_off],
			rap->rxa_msdu_qframe[rap->rxa_off],
			rap->rxa_m[rap->rxa_off]);

		rap->amsdu_f_diff_cnt++;

		m_drop_ptr = rap->rxa_m[rap->rxa_off];
		while (m_drop_ptr != NULL) {
			m_drop_next_ptr = m_drop_ptr->next;
			m_drop_ptr->next = NULL;

			rap->rxa_qbytes -= m_drop_ptr->len;
			rap->rxa_msdu_qframe[rap->rxa_off]--;
			rap->rxa_nframes--;
			dev_kfree_skb(m_drop_ptr);
			m_drop_ptr = m_drop_next_ptr;
		}
		rap->rxa_m[rap->rxa_off] = NULL;
		WQ_ASSERT((rap->rxa_m_msdu_seq[rap->rxa_off] & AMSDU_SEQ_END) ==
				  0,
			  "%s: seq=0x%x", __func__,
			  rap->rxa_m_msdu_seq[rap->rxa_off]);
		rap->rxa_m_msdu_seq[rap->rxa_off] = 0;
		old_off = 0xFF;
	}
	if (rap->rxa_off != off) {
		rap->rxa_off = off;
	}

	if (rap->rxa_m[off] == NULL) {
		rap->rxa_m[off] = m;
		rap->rxa_qbytes += m->len;
		rap->rxa_m_msdu_seq[off] |= 1 << msdu_seq;
		rap->rxa_msdu_qframe[off]++;
		rap->rxa_nframes++;

		//vap->iv_stats.is_ampdu_rx_reorder++; //TODO:
		WQ_DBG_MAC(
			DM_IEEE80211, DL_VRB, sta,
			"ampdu_rx_add_slot:BA win <%u:%u> (%u frames) (%u a-msdu subframes) tid %u msdu_seq %u msdu_seq_end %u\n",
			rap->rxa_start,
			IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
			rap->rxa_qframes, rap->rxa_msdu_qframe[off], rap->tid,
			msdu_seq, msdu_seq_end);
	} else if (!(rap->rxa_m_msdu_seq[off] & (1 << msdu_seq))) {
		m_ptr = rap->rxa_m[off];
		while (m_ptr->next) {
			m_ptr = m_ptr->next;
		}
		m_ptr->next = m;

		rap->rxa_qbytes += m->len;
		rap->rxa_m_msdu_seq[off] |= 1 << msdu_seq;
		rap->rxa_msdu_qframe[off]++;
		rap->rxa_nframes++;

		WQ_DBG_MAC(
			DM_IEEE80211, DL_VRB, sta,
			"ampdu_rx_add_slot:BA win <%u:%u> (%u frames) (%u a-msdu subframes others) tid %u msdu_seq %u msdu_seq_end %u\n",
			rap->rxa_start,
			IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
			rap->rxa_qframes, rap->rxa_msdu_qframe[off], rap->tid,
			msdu_seq, msdu_seq_end);
	} else {
		WQ_DBG_MAC(
			DM_IEEE80211, DL_INF, sta,
			"discard due to a-mpdu (%u a-msdu subframes) duplicate"
			"seqno %u tid %u BA win <%u:%u> msdu_seq %u msdu_seq_end %u\n",
			rap->rxa_msdu_qframe[off], rxseq, tid, rap->rxa_start,
			IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
			msdu_seq, msdu_seq_end);
		//vap->iv_stats.is_rx_dup++;
		//  IEEE80211_NODE_STAT(ni, rx_dup); //TODO:where to store this?

		dev_kfree_skb(m);
		rap->rxa_off = old_off;

		return (-1);
	}

	if (msdu_seq_end) {
		rap->rxa_off = RXA_OFF_INVALID;

		if (msdu_seq == 0) {
			msdu_seq_check = (1 << msdu_seq);
		} else {
			msdu_seq_check =
				(1 << msdu_seq) + ((1 << msdu_seq) - 1);
		}

		if (rap->rxa_m_msdu_seq[off] != msdu_seq_check) {
			WQ_DBG_MAC(
				DM_IEEE80211, DL_WRN, sta,
				"%s: BA win <%u:%u> (%u frames), off=%u, m=0x%p, qfrm=%u, tid=%u, msdu_seq=0x%x-0x%x-0x%x\n",
				__func__, rap->rxa_start,
				IEEE80211_SEQ_ADD(rap->rxa_start,
						  rap->rxa_wnd - 1),
				rap->rxa_qframes, off, rap->rxa_m[off],
				rap->rxa_msdu_qframe[off], rap->tid,
				rap->rxa_m_msdu_seq[off], msdu_seq,
				msdu_seq_check);

			rap->amsdu_f_end_cnt++;

			m_drop_ptr = rap->rxa_m[off];
			while (m_drop_ptr != NULL) {
				m_drop_next_ptr = m_drop_ptr->next;
				m_drop_ptr->next = NULL;

				rap->rxa_qbytes -= m_drop_ptr->len;
				rap->rxa_msdu_qframe[off]--;
				rap->rxa_nframes--;

				dev_kfree_skb(m_drop_ptr);
				m_drop_ptr = m_drop_next_ptr;
			}
			rap->rxa_m[off] = NULL;
			rap->rxa_m_msdu_seq[off] = 0;

			return (-1);
		} else {
			rap->rxa_qframes++;
			rap->rxa_m_msdu_seq[off] |=
				AMSDU_SEQ_END; //keep msdu subframe end

			return (0);
		}
	} else {
		return (0);
	}
}

static void ampdu_rx_purge_slot(struct ieee80211_rx_ampdu *rap, int i)
{
	struct sk_buff *m, *m_next = NULL;

	m = rap->rxa_m[i];
	if (m == NULL)
		return;

	WQ_DBG(DM_RX, DL_WRN,
	       "%s: BA win <%u:%u> (%u frames), off=%u, m=0x%p, qfrm=%u, tid=%u, msdu_seq=0x%x",
	       __func__, rap->rxa_start,
	       IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
	       rap->rxa_qframes, i, rap->rxa_m[i], rap->rxa_msdu_qframe[i],
	       rap->tid, rap->rxa_m_msdu_seq[i]);

	rap->rxa_m[i] = NULL;
	while (m != NULL) {
		m_next = m->next;
		m->next = NULL;
		rap->rxa_qbytes -= m->len;
		rap->rxa_msdu_qframe[i]--;
		rap->rxa_nframes--;
		dev_kfree_skb(m);
		m = m_next;
	}

	if (rap->rxa_m_msdu_seq[i] & AMSDU_SEQ_END) { //check msdu subframe end
		rap->rxa_qframes--;
	}
	rap->rxa_m_msdu_seq[i] = 0;
}

/*
 * Purge all frames in the A-MPDU re-order queue.
 */
static void ampdu_rx_purge(struct ieee80211_rx_ampdu *rap)
{
	int i;

	for (i = 0; i < rap->rxa_wnd; i++) {
		ampdu_rx_purge_slot(rap, i);
	}
	WQ_ASSERT(rap->rxa_qbytes == 0 && rap->rxa_qframes == 0,
		  "lost %u data, %u frames on ampdu rx q", rap->rxa_qbytes,
		  rap->rxa_qframes);
}

/*
 * Start A-MPDU rx/re-order processing for the specified TID.
 */
static int ampdu_rx_start(uint8_t sta_idx, uint8_t tid,
			  struct ieee80211_rx_ampdu *rap, uint16_t bufsiz,
			  uint16_t baseqctl)
{
	struct rwnx_hw *rwnx_hw = NULL;
	struct rwnx_sta *sta = NULL;

	rwnx_hw = rap->rwnx_hw;
	sta = &rwnx_hw->sta_table[sta_idx];

	if (rap->rxa_flags & IEEE80211_AGGR_RUNNING) {
		/*
		 * AMPDU previously setup and not terminated with a DELBA,
		 * flush the reorder q's in case anything remains.
		 */
		ampdu_rx_purge(rap);
	}

	rap->rxa_wnd = bufsiz;
	rap->rxa_start = baseqctl;
	rap->rxa_flags |= IEEE80211_AGGR_RUNNING | IEEE80211_AGGR_XCHGPEND;
	rap->rxa_off = RXA_OFF_INVALID;

	WQ_DBG_MAC(DM_IEEE80211, DL_INF, sta,
		   "ampdu_rx_start sta_idx:%d, tid=%d, seq=%d, win=%d\n",
		   sta_idx, tid, rap->rxa_start, rap->rxa_wnd);

	return 0;
}

/*
 * Stop A-MPDU rx processing for the specified TID.
 */
static void ampdu_rx_stop(uint8_t sta_idx, struct ieee80211_rx_ampdu *rap)
{
	struct rwnx_hw *rwnx_hw = rap->rwnx_hw;
	struct rwnx_sta *sta = &rwnx_hw->sta_table[sta_idx];
	ampdu_rx_purge(rap);
	rap->rxa_flags &= ~(IEEE80211_AGGR_RUNNING | IEEE80211_AGGR_XCHGPEND);
	//| IEEE80211_AGGR_WAITRX);
	WQ_DBG_MAC(DM_IEEE80211, DL_INF, sta,
		   "ampdu_rx_stop sta_idx:%d tid=%d\n", sta_idx, rap->tid);
}

/*
 * Dispatch a frame from the A-MPDU reorder queue.  The
 * frame is fed back into ieee80211_input marked with an
 * M_AMPDU_MPDU flag so it doesn't come back to us (it also
 * permits ieee80211_input to optimize re-processing).
 */
static __inline void ampdu_dispatch(struct rwnx_hw *rwnx_hw,
				    struct rwnx_sta *ni, struct sk_buff *skb)
{
	struct rwnx_vif *rwnx_vif;
	struct hw_rxhdr *hw_rxhdr;

	WQ_DBG(DM_RX, DL_INF, "%s: skb=0x%p\n", __func__, skb);

	hw_rxhdr = (struct hw_rxhdr *)(skb->data - sizeof(struct hw_rxhdr));

	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ni->vif_idx);

	if (rwnx_vif == NULL)
		dev_kfree_skb_any(skb);
	else {
		rwnx_rx_data_skb(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
#ifdef NAPI_SUPPORT
		if (rwnx_hw->core->config.napi_enable) {
			napi_schedule(&rwnx_hw->napi_rx);
		}
#endif
	}
}

static int ampdu_dispatch_slot(struct ieee80211_rx_ampdu *rap,
			       struct rwnx_sta *ni, int i, int mode)
{
	struct sk_buff *m, *m_next = NULL;

	if (rap->rxa_m[i] == NULL) {
		if (mode == MODE_AMSDU_ONGOING_FREE) {
			rap->seq_f_null_cnt++;
		} else if (mode == MODE_AMSDU_ONGOING_FLUSH) {
			rap->seq_f_null_flush_cnt++;
		}
		return (0);
	}

	if ((mode == MODE_AMSDU_ONGOING_RESERVE) &&
	    ((rap->rxa_m_msdu_seq[i] & AMSDU_SEQ_END) == 0)) {
		return 2;
	}

	m = rap->rxa_m[i];
	rap->rxa_m[i] = NULL;

	if (rap->rxa_m_msdu_seq[i] & AMSDU_SEQ_END) { //check msdu subframe end
		if (rap->rxa_msdu_qframe[i] > 1) {
			rap->amsdu_s_cnt++;
		}
		while (m != NULL) {
			m_next = m->next;
			m->next = NULL;
			rap->rxa_qbytes -= m->len;
			rap->rxa_msdu_qframe[i]--;
			rap->rxa_nframes--;
			WQ_DBG(DM_RX, DL_INF, "%s: m=0x%p\n", __func__, m);
			ampdu_dispatch(rap->rwnx_hw, ni, m);
			m = m_next;
		}
		rap->rxa_qframes--;
		rap->seq_s_cnt++;
	} else {
		WQ_DBG(DM_RX, DL_WRN,
		       "%s: BA win <%u:%u> (%u frames), off=%u, m=0x%p, qfrm=%u, tid=%u, msdu_seq=0x%x",
		       __func__, rap->rxa_start,
		       IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
		       rap->rxa_qframes, i, rap->rxa_m[i],
		       rap->rxa_msdu_qframe[i], rap->tid,
		       rap->rxa_m_msdu_seq[i]);

		if (mode == MODE_AMSDU_ONGOING_FLUSH) {
			rap->amsdu_f_noend_flush_cnt++;
			rap->seq_f_noend_flush_cnt++;
		} else {
			rap->amsdu_f_noend_cnt++;
			rap->seq_f_noend_cnt++;
		}

		while (m != NULL) {
			m_next = m->next;
			m->next = NULL;
			rap->rxa_qbytes -= m->len;
			rap->rxa_msdu_qframe[i]--;
			rap->rxa_nframes--;
			WQ_DBG(DM_RX, DL_WRN, "%s: drop m=0x%p\n", __func__, m);
			dev_kfree_skb(m);
			m = m_next;
		}
	}
	rap->rxa_m_msdu_seq[i] = 0;

	WQ_DBG_MAC(DM_IEEE80211, DL_VRB, ni,
		   "ampdu_dispatch_slot: i:%d, remain qframes:%d\n", i,
		   rap->rxa_qframes);

	return (1);
}

static void ampdu_rx_moveup(struct ieee80211_rx_ampdu *rap, struct rwnx_sta *ni,
			    int i, int winstart)
{
	if ((rap->rxa_nframes != 0) && (i != 0)) {
		int n = rap->rxa_qframes, j, k;

		if (winstart != -1) {
			/*
             * NB: in window-sliding mode, loop assumes i > 0
             * and/or rxa_m[0] is NULL
             */
			WQ_ASSERT(rap->rxa_m[0] == NULL,
				  "%s: BA window slot 0 occupied", __func__);
		}
		for (j = i; j < rap->rxa_wnd; j++) {
			if (rap->rxa_m[j] != NULL) {
				rap->rxa_m[j - i] = rap->rxa_m[j];
				rap->rxa_m[j] = NULL;
				rap->rxa_m_msdu_seq[j - i] =
					rap->rxa_m_msdu_seq[j];
				rap->rxa_m_msdu_seq[j] = 0;
				rap->rxa_msdu_qframe[j - i] =
					rap->rxa_msdu_qframe[j];
				rap->rxa_msdu_qframe[j] = 0;
				if (rap->rxa_m_msdu_seq[j - i] &
				    AMSDU_SEQ_END) {
					n--;
				}
			}
		}

		//check n != 0 situation
		if(n != 0) {
			for (k = 0; k < rap->rxa_wnd; k++) {
				if (rap->rxa_m[k] != NULL && 
					(rap->rxa_m_msdu_seq[k] & AMSDU_SEQ_END)) {
					WQ_DBG(DM_RX, DL_WRN, "%s: rap->rxa_m[%d]=0x%p\n", __func__, k, rap->rxa_m[k]);
				}
			}
		}

		WQ_ASSERT(n == 0,
			  "%s: lost %d frames, qframes %d off %d "
			  "BA win <%d:%d> winstart %d",
			  __func__, n, rap->rxa_qframes, i, rap->rxa_start,
			  IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
			  winstart);
		//vap->iv_stats.is_ampdu_rx_copy += rap->rxa_qframes;
	}
}

/*
 * Dispatch as many frames as possible from the re-order queue.
 * Frames will always be "at the front"; we process all frames
 * up to the first empty slot in the window.  On completion we
 * cleanup state if there are still pending frames in the current
 * BA window.  We assume the frame at slot 0 is already handled
 * by the caller; we always start at slot 1.
 */
static void ampdu_rx_dispatch(struct ieee80211_rx_ampdu *rap,
			      struct rwnx_sta *ni)
{
	int i;

	/* flush run of frames */
	for (i = 1; i < rap->rxa_wnd; i++) {
		if (ampdu_dispatch_slot(rap, ni, i,
					MODE_AMSDU_ONGOING_RESERVE) != 1)
			break;
	}

	/*
     * If frames remain, copy the sk_buff pointers down so
     * they correspond to the offsets in the new window.
     */
	ampdu_rx_moveup(rap, ni, i, -1);

	/*
     * Adjust the start of the BA window to
     * reflect the frames just dispatched.
     */
	rap->rxa_start = IEEE80211_SEQ_ADD(rap->rxa_start, i);
	if (rap->rxa_off != RXA_OFF_INVALID) {
		rap->rxa_off = (rap->rxa_off < i) ? RXA_OFF_INVALID : (rap->rxa_off - i);
	}

	WQ_DBG_MAC(
		DM_IEEE80211, DL_VRB, ni,
		"ampdu_rx_dispatch new BA win <%u:%u> (%u frames) (%d nframes) tid %u\n",
		rap->rxa_start,
		IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
		rap->rxa_qframes, rap->rxa_nframes, rap->tid);
	//vap->iv_stats.is_ampdu_rx_oor += i;
}

/*
 * Dispatch all frames in the A-MPDU re-order queue.
 */
static int ampdu_rx_flush(struct rwnx_sta *ni, struct ieee80211_rx_ampdu *rap)
{
	int i;

	WQ_DBG_MAC(DM_IEEE80211, DL_VRB, ni,
		   "ampdu_rx_flush: sta_idx:%d, tid:%d\n", ni->sta_idx,
		   rap->tid);
	for (i = 0; i < rap->rxa_wnd; i++) {
		ampdu_dispatch_slot(rap, ni, i, MODE_AMSDU_ONGOING_FLUSH);
		if (rap->rxa_nframes == 0) {
			break;
		}
	}
	rap->rxa_off = RXA_OFF_INVALID;
	return i;
}

/*
 * Dispatch all frames in the A-MPDU re-order queue
 * preceding the specified sequence number.  This logic
 * handles window moves due to a received MSDU or BAR.
 */
static void ampdu_rx_flush_upto(struct rwnx_sta *ni,
				struct ieee80211_rx_ampdu *rap,
				ieee80211_seq winstart)
{
	ieee80211_seq seqno;
	int i, r;

	WQ_DBG_MAC(
		DM_IEEE80211, DL_VRB, ni,
		"ampdu_rx_flush_upto: sta_idx:%d, tid:%d, BA win <%u:%u> (%u frames) (%d nframes), winstart:%d\n",
		ni->sta_idx, rap->tid, rap->rxa_start,
		IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
		rap->rxa_qframes, rap->rxa_nframes, winstart);

	/*
     * Flush any complete MSDU's with a sequence number lower
     * than winstart.  Gaps may exist.  Note that we may actually
     * dispatch frames past winstart if a run continues; this is
     * an optimization that avoids having to do a separate pass
     * to dispatch frames after moving the BA window start.
     */
	seqno = rap->rxa_start;
	for (i = 0; i < rap->rxa_wnd; i++) {
		r = ampdu_dispatch_slot(rap, ni, i,
					IEEE80211_SEQ_BA_BEFORE(seqno,
								winstart) ?
						MODE_AMSDU_ONGOING_FREE :
						MODE_AMSDU_ONGOING_RESERVE);
		if (r != 1) {
			if (!IEEE80211_SEQ_BA_BEFORE(seqno, winstart))
				break;
		}
		//vap->iv_stats.is_ampdu_rx_oor += r;
		seqno = IEEE80211_SEQ_INC(seqno);
	}
	/*
     * If frames remain, copy the sk_buff pointers down so
     * they correspond to the offsets in the new window.
     */
	ampdu_rx_moveup(rap, ni, i, winstart);

	/*
     * Move the start of the BA window; we use the
     * sequence number of the last MSDU that was
     * passed up the stack+1 or winstart if stopped on
     * a gap in the reorder buffer.
     */
	rap->rxa_start = seqno;
	if (rap->rxa_off != RXA_OFF_INVALID) {
		rap->rxa_off = (rap->rxa_off < i) ? RXA_OFF_INVALID : (rap->rxa_off - i);
	}
}

void ampdu_reorder_timeout_task(struct work_struct *work)
{
	int ret;

	struct ieee80211_rx_ampdu *rap =
		(struct ieee80211_rx_ampdu *)container_of(
			work, struct ieee80211_rx_ampdu, flush_task_work);
	struct rwnx_sta *sta = rap->ni;

	if (sta == NULL) {
		return;
	}

	WQ_DBG_MAC(DM_IEEE80211, DL_VRB, sta,
		   "ampdu_reorder_timeout_task, sta_idx:%d, tid:%d\n",
		   sta->sta_idx, rap->tid);

	/* flush rx reorder queue if frame is queued for a long time */
	if (time_after(ticks, (rap->rxa_age + ieee80211_ampdu_age))) {
		ampdu_rx_flush_upto(rap->ni, rap, rap->rx_reorder_pending_seq);

		/* try to send the consequent queued frames if any */
		spin_lock_bh(&rap->rxa_qframes_lock);
		ret = ampdu_dispatch_slot(rap, rap->ni, 0,
					  MODE_AMSDU_ONGOING_RESERVE);
		spin_unlock_bh(&rap->rxa_qframes_lock);

		if (ret) {
			ampdu_rx_dispatch(rap, rap->ni);
		}
	}

	return;
}

void ieee80211_rx_reorder_timeout(struct timer_list *callback_timer)
{
	struct ieee80211_rx_ampdu *rap =
		(struct ieee80211_rx_ampdu *)container_of(
			callback_timer, struct ieee80211_rx_ampdu,
			rx_reorder_timer);
	//struct work_struct *work = &rap->flush_task_work;
	struct rwnx_sta *sta = rap->ni;

	if (rap->ni == NULL) {
		printk("%s:%d rap->ni is NULL\n", __func__, __LINE__);
		return;
	}

	WQ_DBG_MAC(DM_IEEE80211, DL_VRB, sta,
		   "ieee80211_rx_reorder_timeout: sta_idx:%d, tid:%d\n",
		   sta->sta_idx, rap->tid);

	spin_lock_bh(&rap->rxa_qframes_lock);

	if ((rap->rxa_flags & IEEE80211_AGGR_RUNNING) == 0) {
		printk("%s:%d rx aggre is not RUNNING\n", __func__, __LINE__);
		spin_unlock_bh(&rap->rxa_qframes_lock);
		return;
	}

	//schedule_work(work);

	/* flush rx reorder queue if frame is queued for a long time */
	if (time_after(ticks, (rap->rxa_age + ieee80211_ampdu_age))) {
		if ((rap->rxa_qframes == 0) ||
		    ((rap->rx_reorder_pending_seq == rap->rxa_start) ||
		     (IEEE80211_SEQ_SUB(rap->rx_reorder_pending_seq,
					rap->rxa_start) >=
		      IEEE80211_SEQ_BA_RANGE))) {
			WQ_DBG_MAC(
				DM_IEEE80211, DL_VRB, sta,
				"ieee80211_rx_reorder_timeout: rap->rxa_qframes:%d, rap->rx_reorder_pending_seq:%u, rap->rxa_start:%u \n",
				rap->rxa_qframes, rap->rx_reorder_pending_seq,
				rap->rxa_start);
			spin_unlock_bh(&rap->rxa_qframes_lock);
			return;
		}

		ampdu_rx_flush_upto(rap->ni, rap, rap->rx_reorder_pending_seq);
		rap->timeout_cnt++;
	}

	spin_unlock_bh(&rap->rxa_qframes_lock);

	return;
}

void ieee80211_ampdu_reorder_dump_info(struct rwnx_hw *rwnx_hw,
				       struct ieee80211_rx_ampdu *rap,
				       bool need_lock_flag, bool reset_flag,
				       uint16_t sta_idx, uint8_t tid)
{
	struct rwnx_sta *sta = NULL;
	struct ieee80211_rx_ampdu *rap_tmp = NULL;
	int i = 0, j = 0;

	if (rwnx_hw) {
		if (rap) {
			sta = &rwnx_hw->sta_table[sta_idx];
			rap_tmp = rap;
			WQ_DBG(DM_RX, DL_WRN,
			       "%s reset: sta_idx:%d, tid:%d, MAC:%pM\n",
			       __func__, sta_idx, tid, sta->mac_addr);
			WQ_DBG(DM_RX, DL_WRN,
			       "sc/nc/nfc/nec/nefc/asc/adc/aec/anec/anefc:%u/%u/%u/%u/%u/%u/%u/%u/%u/%u\n",
			       rap_tmp->seq_s_cnt, rap_tmp->seq_f_null_cnt,
			       rap_tmp->seq_f_null_flush_cnt,
			       rap_tmp->seq_f_noend_cnt,
			       rap_tmp->seq_f_noend_flush_cnt,
			       rap_tmp->amsdu_s_cnt, rap_tmp->amsdu_f_diff_cnt,
			       rap_tmp->amsdu_f_end_cnt,
			       rap_tmp->amsdu_f_noend_cnt,
			       rap_tmp->amsdu_f_noend_flush_cnt);

			if (reset_flag) {
				rap_tmp->seq_s_cnt = 0;
				rap_tmp->seq_f_null_cnt = 0;
				rap_tmp->seq_f_null_flush_cnt = 0;
				rap_tmp->seq_f_noend_cnt = 0;
				rap_tmp->seq_f_noend_flush_cnt = 0;
				rap_tmp->amsdu_s_cnt = 0;
				rap_tmp->amsdu_f_diff_cnt = 0;
				rap_tmp->amsdu_f_end_cnt = 0;
				rap_tmp->amsdu_f_noend_cnt = 0;
				rap_tmp->amsdu_f_noend_flush_cnt = 0;
			}
		} else {
			for (i = 0; i < NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX;
			     i++) {
				sta = &rwnx_hw->sta_table[i];
				if (sta->valid) {
					for (j = 0; j < 8; j++) {
						rap_tmp = &sta->rx_ampdu[j];
						if ((rap_tmp->rxa_flags &
						     IEEE80211_AGGR_XCHGPEND) !=
						    0) {
							if (need_lock_flag) {
								spin_lock_bh(
									&rap_tmp->rxa_qframes_lock);
							}

							WQ_DBG(DM_RX, DL_WRN,
							       "%s: sta_idx:%d, tid:%d, MAC:%pM\n",
							       __func__, i, j,
							       sta->mac_addr);
							WQ_DBG(DM_RX, DL_WRN,
							       "sc/nc/nfc/nec/nefc/asc/adc/aec/anec/anefc:%u/%u/%u/%u/%u/%u/%u/%u/%u/%u\n",
							       rap_tmp->seq_s_cnt,
							       rap_tmp->seq_f_null_cnt,
							       rap_tmp->seq_f_null_flush_cnt,
							       rap_tmp->seq_f_noend_cnt,
							       rap_tmp->seq_f_noend_flush_cnt,
							       rap_tmp->amsdu_s_cnt,
							       rap_tmp->amsdu_f_diff_cnt,
							       rap_tmp->amsdu_f_end_cnt,
							       rap_tmp->amsdu_f_noend_cnt,
							       rap_tmp->amsdu_f_noend_flush_cnt);
							WQ_DBG(DM_RX, DL_WRN,
							       "mtc/hbc/bc/obc/tc/bbc/bbrc:%u/%u/%u/%u/%u/%u/%u\n",
							       rap_tmp->timer_cnt,
							       rap_tmp->timeout_flush_cnt,
							       rap_tmp->in_range_cnt,
							       rap_tmp->out_range_cnt,
							       rap_tmp->timeout_cnt,
							       rap_tmp->bar_cnt,
							       rap_tmp->bar_seq_cnt);

							if (reset_flag) {
								rap_tmp->seq_s_cnt =
									0;
								rap_tmp->seq_f_null_cnt =
									0;
								rap_tmp->seq_f_null_flush_cnt =
									0;
								rap_tmp->seq_f_noend_cnt =
									0;
								rap_tmp->seq_f_noend_flush_cnt =
									0;
								rap_tmp->amsdu_s_cnt =
									0;
								rap_tmp->amsdu_f_diff_cnt =
									0;
								rap_tmp->amsdu_f_end_cnt =
									0;
								rap_tmp->amsdu_f_noend_cnt =
									0;
								rap_tmp->amsdu_f_noend_flush_cnt =
									0;
							}

							if (need_lock_flag) {
								spin_unlock_bh(
									&rap_tmp->rxa_qframes_lock);
							}
						}
					}
				}
			}
		}
	}

	return;
}

void ieee80211_ampdu_age_msecs_set(unsigned int ampdu_age_msecs)
{
	ieee80211_ampdu_age = msecs_to_jiffies(ampdu_age_msecs);
}

unsigned int ieee80211_ampdu_age_msecs_get(void)
{
	return jiffies_to_msecs(ieee80211_ampdu_age);
}

/*
 * Process a received QoS data frame for an HT station.  Handle
 * A-MPDU reordering: if this frame is received out of order
 * and falls within the BA window hold onto it.  Otherwise if
 * this frame completes a run, flush any pending frames.  We
 * return 1 if the frame is consumed.  A 0 is returned if
 * the frame should be processed normally by the caller.
 */
#if 0
FAST_ATTR int
ieee80211_ampdu_reorder(struct rwnx_sta *ni, struct sk_buff *m,
    const struct ieee80211_rx_stats *rxs)
#else
int ieee80211_ampdu_reorder(struct rwnx_hw *rwnx_hw, uint16_t sta_idx,
			    uint8_t tid, uint16_t rxseq, uint8_t msdu_seq,
			    uint8_t msdu_seq_end, struct sk_buff *m)
#endif
{
#define PROCESS 0 /* caller should process frame */
#define CONSUMED 1 /* frame consumed, caller does nothing */
	int off;
	int i = 0;
#if 0
    WQ_ASSERT((m->m_flags & (M_AMPDU | M_AMPDU_MPDU)) == M_AMPDU,
        ("!a-mpdu or already re-ordered, flags 0x%x", m->m_flags));
    WQ_ASSERT(ni->ni_flags & IEEE80211_NODE_HT, ("not an HT sta"));

    /* NB: m_len known to be sufficient */
    wh = mtod(m, struct ieee80211_qosframe *);
    if (wh->i_fc[0] != IEEE80211_FC0_QOSDATA) {
        /*
         * Not QoS data, shouldn't get here but just
         * return it to the caller for processing.
         */
        return PROCESS;
    }

    /*
     * 802.11-2012 9.3.2.10 - Duplicate detection and recovery.
     *
     * Multicast QoS data frames are checked against a different
     * counter, not the per-TID counter.
     */
    if (IEEE80211_IS_MULTICAST(wh->i_addr1))
        return PROCESS;
#endif
	struct rwnx_sta *sta = &rwnx_hw->sta_table[sta_idx];
	struct ieee80211_rx_ampdu *rap = &sta->rx_ampdu[tid];

	spin_lock_bh(&rap->rxa_qframes_lock);

	if (((rap->rxa_flags & IEEE80211_AGGR_XCHGPEND) == 0) ||
	    (rap->rxa_wnd == 0)) {
		/*
         * No ADDBA request yet, don't touch.
         */
		spin_unlock_bh(&rap->rxa_qframes_lock);
		return PROCESS;
	}

	WQ_DBG_MAC(
		DM_IEEE80211, DL_INF, sta,
		"%s: BA win <%u:%u> (%u frames), rxseq=%u, tid=%u, msdu_seq=%u, msdu_seq_end=%u\n",
		__func__, rap->rxa_start,
		IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
		rap->rxa_qframes, rxseq, tid, msdu_seq, msdu_seq_end);

	//for first rx ampdu, check its sequence num, if seq# does not match rxa_start, skip rx reorder
	//this is to workaround a specific mercury AP(MTK chipset), which sometimes send a data frame with abnormal/random seq# during addba negotiation
	if ((rap->rxa_flags & IEEE80211_AGGR_FIRST_RXED) == 0) {
		rap->rxa_flags |= IEEE80211_AGGR_FIRST_RXED;

		if (rxseq != rap->rxa_start) {
			WQ_DBG_MAC(
				DM_IEEE80211, DL_WRN, sta,
				"warn : rxseq:%d, first rx ampdu seq:%d not expected\n",
				rxseq, rap->rxa_start);
			spin_unlock_bh(&rap->rxa_qframes_lock);
			return PROCESS;
		}
	}
again:
	if (rxseq == rap->rxa_start) {
		/*
         * First frame in window.
         */

		ampdu_rx_add_slot(rap, 0, tid, rxseq, sta_idx, msdu_seq,
				  msdu_seq_end, m);
		if (msdu_seq_end == 1) {
			/*
             * Dispatch as many packets as we can.
             */
			WQ_DBG_MAC(DM_IEEE80211, DL_VRB, sta,
				   "%s: 1st frame: m=0x%p, rxa_qframes=%d\n",
				   __func__, m, rap->rxa_qframes);
			if (ampdu_dispatch_slot(rap, sta, 0,
						MODE_AMSDU_ONGOING_FREE) == 0) {
				WQ_DBG_MAC(
					DM_IEEE80211, DL_WRN, sta,
					"%s_%d: m=0x%p, qfrm=%d, tid=%d, seq=%d, sta=%d, msdu_seq=%d, msdu_end=%d\n",
					__func__, __LINE__, m, rap->rxa_qframes,
					tid, rxseq, sta_idx, msdu_seq,
					msdu_seq_end);
			}
			ampdu_rx_dispatch(rap, sta);

			del_timer(&rap->rx_reorder_timer);
		}

		spin_unlock_bh(&rap->rxa_qframes_lock);
		return CONSUMED;
	}

	/*
     * Frame is out of order; store if in the BA window.
     */
	/* calculate offset in BA window */
	off = IEEE80211_SEQ_SUB(rxseq, rap->rxa_start);
	if (off < rap->rxa_wnd) {
		short old_qframes = rap->rxa_qframes;

		/*
         * Common case (hopefully): in the BA window.
         * Sec 9.10.7.6.2 a) (p.137)
         */

		/*
         * Check for frames sitting too long in the reorder queue.
         * This should only ever happen if frames are not delivered
         * without the sender otherwise notifying us (e.g. with a
         * BAR to move the window).  Typically this happens because
         * of vendor bugs that cause the sequence number to jump.
         * When this happens we get a gap in the reorder queue that
         * leaves frame sitting on the queue until they get pushed
         * out due to window moves.  When the vendor does not send
         * BAR this move only happens due to explicit packet sends
         *
         * NB: we only track the time of the oldest frame in the
         * reorder q; this means that if we flush we might push
         * frames that still "new"; if this happens then subsequent
         * frames will result in BA window moves which cost something
         * but is still better than a big throughput dip.
         */

		/* save packet - this consumes, no matter what */
		ampdu_rx_add_slot(rap, off, tid, rxseq, sta_idx, msdu_seq,
				  msdu_seq_end, m);
		if (msdu_seq_end == 1) {
			if (rap->rxa_qframes == old_qframes) {
				WQ_DBG_MAC(
					DM_IEEE80211, DL_WRN, sta,
					"%s_%d: m=0x%p, qfrm=%d, tid=%d, seq=%d, sta=%d, msdu_seq=%d, msdu_end=%d\n",
					__func__, __LINE__, m, rap->rxa_qframes,
					tid, rxseq, sta_idx, msdu_seq,
					msdu_seq_end);
			}

			if (old_qframes == 0 && rap->rxa_qframes == 1) {
				/*
                 * First frame, start aging timer.
                 */
				WQ_DBG_MAC(DM_IEEE80211, DL_VRB, sta,
					   "%s: mod timer (age=%d)\n", __func__,
					   ieee80211_ampdu_age);

				rap->rxa_age = ticks;
				rap->rx_reorder_pending_seq = rxseq;
				mod_timer(&rap->rx_reorder_timer,
					  ticks + ieee80211_ampdu_age);
				rap->timer_cnt++;
			} else {
				if (rap->rxa_qframes > 0) {
					/* XXX honor batimeout? */
					if (time_after(ticks,
						       (rap->rxa_age +
							ieee80211_ampdu_age))) {
						/*
                        * Too long since we received the first
                        * frame; flush the reorder buffer.
                        */
						WQ_DBG_MAC(
							DM_IEEE80211, DL_INF,
							sta,
							"Do rx flush (ticks=%lu, rxa_age=%lu)!\n",
							(unsigned long)ticks,
							rap->rxa_age);

						i = ampdu_rx_flush(sta, rap);
						/*
						 * ampdu_rx_flush() is push all received frame to tcp/ip protocol stack.
						 * so, the next start seq should be the last seq of ampdu_rx_flush() + 1.
						 */
						//rap->rxa_start =IEEE80211_SEQ_INC(rxseq);
						rap->rxa_start = IEEE80211_SEQ_ADD(rap->rxa_start, i+1);
						del_timer(
							&rap->rx_reorder_timer);
						rap->timeout_flush_cnt++;
						ieee80211_ampdu_reorder_dump_info(
							rwnx_hw, rap, false,
							true, sta_idx, tid);
					}
				}
			}
		}

		spin_unlock_bh(&rap->rxa_qframes_lock);
		return CONSUMED;
	}

	if (off < IEEE80211_SEQ_BA_RANGE) {
		/*
         * Outside the BA window, but within range;
         * flush the reorder q and move the window.
         * Sec 9.10.7.6.2 b) (p.138)
         */
		WQ_DBG_MAC(
			DM_IEEE80211, DL_WRN, sta,
			"%s: move BA win <%u:%u> (%u frames), rxseq=%u, tid=%u, msdu_seq=%u, msdu_seq_end=%u\n",
			__func__, rap->rxa_start,
			IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
			rap->rxa_qframes, rxseq, tid, msdu_seq, msdu_seq_end);
		//vap->iv_stats.is_ampdu_rx_move++;

		/*
         * The spec says to flush frames up to but not including:
         *  WinStart_B = rxseq - rap->rxa_wnd + 1
         * Then insert the frame or notify the caller to process
         * it immediately.  We can safely do this by just starting
         * over again because we know the frame will now be within
         * the BA window.
         */
		/* NB: rxa_wnd known to be >0 */
		ampdu_rx_flush_upto(sta, rap,
				    IEEE80211_SEQ_SUB(rxseq, rap->rxa_wnd - 1));
		del_timer(&rap->rx_reorder_timer);
		rap->in_range_cnt++;

		goto again;
	} else {
		/*
         * Outside the BA window and out of range; toss.
         * Sec 9.10.7.6.2 c) (p.138)
         */
		WQ_DBG_MAC(
			DM_IEEE80211, DL_INF, sta,
			"%s: Outside BA win <%u:%u> (%u frames), rxseq=%u, tid=%u, msdu_seq=%u, msdu_seq_end=%u\n",
			__func__, rap->rxa_start,
			IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
			rap->rxa_qframes, rxseq, tid, msdu_seq, msdu_seq_end);

		dev_kfree_skb(m);
		rap->out_range_cnt++;
		spin_unlock_bh(&rap->rxa_qframes_lock);
		return CONSUMED;
	}
#undef CONSUMED
#undef PROCESS
}

/*
 * Process a BAR ctl frame.  Dispatch all frames up to
 * the sequence number of the frame.  If this frame is
 * out of range it's discarded.
 */
void ieee80211_recv_bar(struct rwnx_hw *rwnx_hw, uint8_t sta_idx, uint8_t tid,
			uint16_t rxseq, uint8_t fctrl_retry)
{
	struct rwnx_sta *sta = &rwnx_hw->sta_table[sta_idx];
	struct ieee80211_rx_ampdu *rap = &sta->rx_ampdu[tid];
	uint16_t off;

	spin_lock_bh(&rap->rxa_qframes_lock);

	if ((rap->rxa_flags & IEEE80211_AGGR_XCHGPEND) == 0) {
		/*
         * No ADDBA request yet, don't touch.
         */
		WQ_DBG_MAC(DM_IEEE80211, DL_INF, sta,
			   "BAR: no BA stream, tid %u\n", tid);
		//vap->iv_stats.is_ampdu_bar_bad++;
		spin_unlock_bh(&rap->rxa_qframes_lock);
		return;
	}

	if (rxseq == rap->rxa_start) {
		WQ_DBG_MAC(DM_IEEE80211, DL_INF, sta,
			   "BAR: no Change, rxseq %u\n", rxseq);
		spin_unlock_bh(&rap->rxa_qframes_lock);
		return;
	}

	/* calculate offset in BA window */
	off = IEEE80211_SEQ_SUB(rxseq, rap->rxa_start);
	if (off < IEEE80211_SEQ_BA_RANGE) {
		/*
         * Flush the reorder q up to rxseq and move the window.
         * Sec 9.10.7.6.3 a) (p.138)
        */
		WQ_DBG_MAC(
			DM_IEEE80211, DL_INF, sta,
			"BAR: moves BA win <%u:%u> (%u frames) rxseq %u tid %u\n",
			rap->rxa_start,
			IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
			rap->rxa_qframes, rxseq, tid);
		//vap->iv_stats.is_ampdu_bar_move++;
		ampdu_rx_flush_upto(sta, rap, rxseq);
		rap->bar_cnt++;
		if (off >= rap->rxa_wnd) {
			/*
             * BAR specifies a window start to the right of BA
             * window; we must move it explicitly since
             * ampdu_rx_flush_upto will not.
             */
			rap->rxa_start = rxseq;
			rap->bar_seq_cnt++;
		}
	} else {
		/*
         * Out of range; toss.
         * Sec 9.10.7.6.3 b) (p.138)
         */
		WQ_DBG_MAC(
			DM_IEEE80211, DL_INF, sta,
			"BAR: BA win <%u:%u> (%u frames) rxseq %u tid %u%s\n",
			rap->rxa_start,
			IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd - 1),
			rap->rxa_qframes, rxseq, tid,
			(fctrl_retry == 1) ? " (retransmit)" : "");
		//vap->iv_stats.is_ampdu_bar_oow++;
		//IEEE80211_NODE_STAT(ni, rx_drop); //TODO:where to store this?
	}
	spin_unlock_bh(&rap->rxa_qframes_lock);
}

/*
 * Process a received action frame using the default aggregation
 * policy.  We intercept ADDBA-related frames and use them to
 * update our aggregation state.  All other frames are passed up
 * for processing by ieee80211_recv_action.
 */
int ht_recv_action_ba_addba_request(struct rwnx_hw *rwnx_hw, uint8_t sta_idx,
				    uint8_t tid, uint16_t bufsiz,
				    uint16_t baseqctl)
{
	struct rwnx_sta *sta = &rwnx_hw->sta_table[sta_idx];
	struct ieee80211_rx_ampdu *rap;

	WQ_DBG_MAC(DM_IEEE80211, DL_INF, sta,
		   "recv ADDBA request: (tid %d bufsiz %d) baseqctl %d\n", tid,
		   bufsiz, baseqctl);

	rap = alloc_rx_ampdu(
		rwnx_hw, sta_idx,
		tid); //alloc_rx_ampdu() reuses existed rap or allocates a new one
	if (rap != NULL) {
		spin_lock_init(&rap->rxa_qframes_lock);
	}

	if (rap) {
		spin_lock_bh(&rap->rxa_qframes_lock);
		ampdu_rx_start(sta_idx, tid, rap, bufsiz, baseqctl);
		spin_unlock_bh(&rap->rxa_qframes_lock);
	}
	return 0;
}

static int ht_recv_action_ba_delba(struct rwnx_hw *rwnx_hw, uint8_t sta_idx,
				   uint8_t tid)
{
	struct rwnx_sta *sta = &rwnx_hw->sta_table[sta_idx];
	struct ieee80211_rx_ampdu *rap;

	rap = &sta->rx_ampdu[tid];
	if (rap != NULL) {
		spin_lock_bh(&rap->rxa_qframes_lock);
		ieee80211_ampdu_reorder_dump_info(rwnx_hw, NULL, false, false,
						  0, 0);
		ampdu_rx_stop(sta_idx, rap);
		spin_unlock_bh(&rap->rxa_qframes_lock);
		//  ni->ni_rx_ampdu[tid] = 0;
		free_rx_ampdu(rap);
	}
	return 0;
}

static void ht_set_amsdu_allow(struct rwnx_hw *rwnx_hw, uint8_t sta_idx,
			       uint8_t tid, uint8_t amsdu_allow)
{
	struct rwnx_txq *txq;
	struct rwnx_sta *sta;

	if (sta_idx < NX_REMOTE_STA_MAX) {
		sta = &rwnx_hw->sta_table[sta_idx];
		txq = rwnx_txq_sta_get(sta, tid, rwnx_hw);
		txq->amsdu_allow = amsdu_allow;
	}
}

void ht_handle_bam_event(struct rwnx_hw *rwnx_hw, uint8_t evt_id,
			 void *bam_evt_parm)
{
	struct bam_evt_addba_parm *addba =
		(struct bam_evt_addba_parm *)bam_evt_parm;
	struct bam_evt_delba_parm *delba =
		(struct bam_evt_delba_parm *)bam_evt_parm;
	//struct bam_evt_bar_parm *bar = (struct bam_evt_bar_parm *)bam_evt_parm;

	switch (evt_id) {
	case MAC_E2A_BAM_ADDBA:
		WQ_DBG(DM_IEEE80211, DL_WRN, "%s(AddBA), dev_type: %d, sta_idx: %d, tid: %d, buffer_size: %d, ssn: %d, amsdu: %d\n",
		       __func__, addba->dev_type, addba->sta_idx, addba->tid,
		       addba->buffer_size, addba->ssn, addba->amsdu);

		if (addba->dev_type == BA_RESPONDER)
			ht_recv_action_ba_addba_request(rwnx_hw, addba->sta_idx,
							addba->tid,
							addba->buffer_size,
							addba->ssn);
		else if (addba->dev_type == BA_ORIGINATOR)
			ht_set_amsdu_allow(rwnx_hw, addba->sta_idx, addba->tid,
					   addba->amsdu);

		break;
	case MAC_E2A_BAM_DELBA:
		WQ_DBG(DM_IEEE80211, DL_WRN, "%s(DelBA), dev_type: %d, sta_idx: %d, tid: %d\n",
		       __func__, delba->dev_type, delba->sta_idx, delba->tid);

		if (delba->dev_type == BA_RESPONDER)
			ht_recv_action_ba_delba(rwnx_hw, delba->sta_idx,
						delba->tid);
		else if (delba->dev_type == BA_ORIGINATOR)
			ht_set_amsdu_allow(rwnx_hw, delba->sta_idx, delba->tid,
					   0);

		break;
	case MAC_E2A_BAM_BAR:
		//move ieee80211_recv_bar() to rwnx_rx_cntrl()
		//ieee80211_recv_bar(rwnx_hw, bar->sta_idx, bar->tid, bar->ssn, bar->fctrl_retry);
		break;
	default:
		printk("%s(%d) error!\n", __func__, evt_id);
		break;
	};

	return;
}

#define ADDSHORT(frm, v)                                                       \
	do {                                                                   \
		frm[0] = (v)&0xff;                                             \
		frm[1] = (v) >> 8;                                             \
		frm += 2;                                                      \
	} while (0)
