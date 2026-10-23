/**
 ******************************************************************************
 *
 * @file rwnx_rx.c
 *
 * Copyright (C) WUQi-Tech 2012-2021
 *
 ******************************************************************************
 */
#include <linux/dma-mapping.h>
#include <linux/ieee80211.h>
#include <linux/etherdevice.h>
#include <net/ieee80211_radiotap.h>
#include <linux/time.h>

#include "rwnx_defs.h"
#include "rwnx_rx.h"
#include "rwnx_tx.h"
#include "rwnx_events.h"
#include "rwnx_compat.h"
#include "ieee80211_ht.h"
#include "wq_profiling.h"
#include "wq_log.h"
#include "wq_wifi_dbg.h"
#include "wq_pkt_classify.h"
#include "wq_ipc.h"
#include "ieee80211_ht.h"
// TODO: remove later, temp including
#include "hif_api.h"
#include "ieee80211_extap.h"

struct vendor_radiotap_hdr {
	u8 oui[3];
	u8 subns;
	u16 len;
	u8 data[];
};

bool check_roc_ignore_nego_req(uint8_t *frame_ctrl, int len, int set_roc,
			       unsigned int roc_duration)
{
	static unsigned long roc_end_jiffies = 0;
	u8 roc_ms_remain_for_nego_cfm = 30;

	if (set_roc) {
		if (roc_duration > roc_ms_remain_for_nego_cfm)
			roc_end_jiffies =
				jiffies +
				msecs_to_jiffies(roc_duration -
						 roc_ms_remain_for_nego_cfm);
		else
			roc_end_jiffies = 0;
	} else {
		if ((len > 31) && ((frame_ctrl[0] & 0xc) == 0) &&
		    ((frame_ctrl[0] >> 4) == 0xD))
			if ((frame_ctrl[24] == 0x4) &&
			    (frame_ctrl[29] == 0x9) && (frame_ctrl[30] == 0))
				if (time_after(jiffies, roc_end_jiffies)) {
					WQ_DBG(DM_RX, DL_WRN,
					       "%s roc_end_jiffies: %lu, jiffies: %lu\n",
					       __func__, roc_end_jiffies,
					       jiffies);
					return true;
				}
	}

	return false;
}

/**
 * rwnx_rx_get_vif - Return pointer to the destination vif
 *
 * @rwnx_hw: main driver data
 * @vif_idx: vif index present in rx descriptor
 *
 * Select the vif that should receive this frame. Returns NULL if the destination
 * vif is not active or vif is not specified in the descriptor.
 */
inline struct rwnx_vif *rwnx_rx_get_vif(struct rwnx_hw *rwnx_hw, int vif_idx)
{
	struct rwnx_vif *rwnx_vif = NULL;

	if (vif_idx < NX_VIRT_DEV_MAX) {
		rwnx_vif = rwnx_hw->vif_table[vif_idx];
		if (!rwnx_vif || !rwnx_vif->up)
			return NULL;
	}

	return rwnx_vif;
}

/**
 * rwnx_rx_statistic - save some statistics about received frames
 *
 * @rwnx_hw: main driver data.
 * @hw_rxhdr: Rx Hardware descriptor of the received frame.
 * @sta: STA that sent the frame.
 */
void rwnx_rx_statistic(struct rwnx_hw *rwnx_hw, struct hw_rxhdr *hw_rxhdr,
		       struct rwnx_sta *sta)
{
#ifdef CONFIG_RWNX_DEBUGFS
	struct rwnx_stats *stats = &rwnx_hw->stats;
	struct rwnx_rx_rate_stats *rate_stats = &sta->stats.rx_rate;
	struct rx_vec_detail_1 *rxvect = &hw_rxhdr->hwvect.rx_vec_1;
	int mpdu, ampdu, mpdu_prev, rate_idx;

	/* update ampdu rx stats */
	mpdu = hw_rxhdr->hwvect.mpdu_cnt;
	ampdu = hw_rxhdr->hwvect.ampdu_cnt;
	mpdu_prev = stats->ampdus_rx_map[ampdu];

	/* work-around, for MACHW that incorrectly return 63 for last MPDU of A-MPDU or S-MPDU */
	if (mpdu == 63) {
		if (ampdu == stats->ampdus_rx_last)
			mpdu = mpdu_prev + 1;
		else
			mpdu = 0;
	}

	if (ampdu != stats->ampdus_rx_last) {
		stats->ampdus_rx[mpdu_prev]++;
		stats->ampdus_rx_miss += mpdu;
	} else {
		if (mpdu <= mpdu_prev) {
			/* lost 4 (or a multiple of 4) complete A-MPDU/S-MPDU */
			stats->ampdus_rx_miss += mpdu;
		} else {
			stats->ampdus_rx_miss += mpdu - mpdu_prev - 1;
		}
	}

	stats->ampdus_rx_map[ampdu] = mpdu;
	stats->ampdus_rx_last = ampdu;

	/* update rx rate statistic */
	if (!rate_stats->size)
		return;

	if (rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM) {
		int mcs;
		int bw = rxvect->ch_bw;
		int sgi;
		int nss;
		switch (rxvect->format_mod) {
		case FORMATMOD_HT_MF:
		case FORMATMOD_HT_GF:
			mcs = rxvect->ht.mcs % 8;
			nss = rxvect->ht.mcs / 8;
			sgi = rxvect->ht.short_gi;
			rate_idx = N_CCK + N_OFDM + nss * 32 + mcs * 4 +
				   bw * 2 + sgi;
			break;
		case FORMATMOD_VHT:
			mcs = rxvect->vht.mcs;
			nss = rxvect->vht.nss;
			sgi = rxvect->vht.short_gi;
			rate_idx = N_CCK + N_OFDM + N_HT + nss * 80 + mcs * 8 +
				   bw * 2 + sgi;
			break;
		case FORMATMOD_HE_SU:
			mcs = rxvect->he.mcs;
			nss = rxvect->he.nss;
			sgi = rxvect->he.gi_type;
			rate_idx = N_CCK + N_OFDM + N_HT + N_VHT + nss * 144 +
				   mcs * 12 + bw * 3 + sgi;
			break;
		default:
			mcs = rxvect->he.mcs;
			nss = rxvect->he.nss;
			sgi = rxvect->he.gi_type;
			rate_idx = N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU +
				   nss * 216 + mcs * 18 +
				   rxvect->he.ru_size * 3 + sgi;
			break;
		}
	} else {
		int idx = legrates_lut[rxvect->leg_rate].idx;
		if (idx < 4) {
			rate_idx = idx * 2 + rxvect->pre_type;
		} else {
			rate_idx = N_CCK + idx - 4;
		}
	}
	if (rate_idx < rate_stats->size) {
		// sta->stats.rx_rate.table could be free in rwnx_dbgfs_unregister_sta
		if (rate_stats->table)
			if (!rate_stats->table[rate_idx])
				rate_stats->rate_cnt++;
		rate_stats->table[rate_idx]++;
		rate_stats->cpt++;
	} else {
		wiphy_err(rwnx_hw->wiphy,
			  "RX: Invalid index conversion => %d/%d\n", rate_idx,
			  rate_stats->size);
	}
#endif

	/* Always save complete hwvect */
	sta->stats.last_rx = hw_rxhdr->hwvect;

	sta->stats.rx_pkts++;
	sta->stats.rx_bytes += hw_rxhdr->hwvect.frmlen;
	sta->stats.last_act = jiffies;
}

/**
 * rwnx_rx_defer_skb - Defer processing of a SKB
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: vif that received the buffer
 * @skb: buffer to defer
 */
void rwnx_rx_defer_skb(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
		       struct sk_buff *skb)
{
	struct rwnx_defer_rx_cb *rx_cb = (struct rwnx_defer_rx_cb *)skb->cb;

	// for now don't support deferring the same buffer on several interfaces
	if (skb_shared(skb))
		return;

	// Increase ref count to avoid freeing the buffer until it is processed
	skb_get(skb);

	rx_cb->vif = rwnx_vif;
	skb_queue_tail(&rwnx_hw->defer_rx.sk_list, skb);
	schedule_work(&rwnx_hw->defer_rx.work);
}

#ifdef AMSDU_DEBUG

static void __frame_add_frag(struct sk_buff *skb, struct page *page, void *ptr,
			     int len, int size)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	int page_offset;

	get_page(page);
	page_offset = ptr - page_address(page);
	skb_add_rx_frag(skb, sh->nr_frags, page, page_offset, len, size);
}

static void __ieee80211_amsdu_copy_frag(struct sk_buff *skb,
					struct sk_buff *frame, int offset,
					int len)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	const skb_frag_t *frag = &sh->frags[0];
	struct page *frag_page;
	void *frag_ptr;
	int frag_len, frag_size;
	int head_size = skb->len - skb->data_len;
	int cur_len;

	frag_page = virt_to_head_page(skb->head);
	frag_ptr = skb->data;
	frag_size = head_size;

	while (offset >= frag_size) {
		offset -= frag_size;
		frag_page = skb_frag_page(frag);
		frag_ptr = skb_frag_address(frag);
		frag_size = skb_frag_size(frag);
		frag++;
	}

	frag_ptr += offset;
	frag_len = frag_size - offset;

	cur_len = min(len, frag_len);

	__frame_add_frag(frame, frag_page, frag_ptr, cur_len, frag_size);
	len -= cur_len;

	while (len > 0) {
		frag_len = skb_frag_size(frag);
		cur_len = min(len, frag_len);
		__frame_add_frag(frame, skb_frag_page(frag),
				 skb_frag_address(frag), cur_len, frag_len);
		len -= cur_len;
		frag++;
	}
}

static struct sk_buff *__ieee80211_amsdu_copy(struct sk_buff *skb,
					      unsigned int hlen, int offset,
					      int len, bool reuse_frag)
{
	struct sk_buff *frame;
	int cur_len = len;

	if (skb->len - offset < len)
		return NULL;

	/*
	 * When reusing framents, copy some data to the head to simplify
	 * ethernet header handling and speed up protocol header processing
	 * in the stack later.
	 */
	if (reuse_frag)
		cur_len = min_t(int, len, 32);

	/*
	 * Allocate and reserve two bytes more for payload
	 * alignment since sizeof(struct ethhdr) is 14.
	 */
	frame = dev_alloc_skb(hlen + sizeof(struct ethhdr) + 2 + cur_len);
	if (!frame)
		return NULL;

	skb_reserve(frame, hlen + sizeof(struct ethhdr) + 2);
	skb_copy_bits(skb, offset, skb_put(frame, cur_len), cur_len);

	len -= cur_len;
	if (!len)
		return frame;

	offset += cur_len;
	__ieee80211_amsdu_copy_frag(skb, frame, offset, len);

	return frame;
}

static void
check_ieee80211_amsdu_to_8023s(struct sk_buff *skb, struct sk_buff_head *list,
			       const u8 *addr, enum nl80211_iftype iftype,
			       const unsigned int extra_headroom,
			       const u8 *check_da, const u8 *check_sa)
{
	unsigned int hlen = ALIGN(extra_headroom, 4);
	struct sk_buff *frame = NULL;
	u16 ethertype;
	u8 *payload;
	int offset = 0, remaining;
	struct ethhdr eth;
	bool reuse_frag = skb->head_frag && !skb_has_frag_list(skb);
	bool reuse_skb = false;
	bool last = false;
	uint8_t dbg = 0;
	unsigned int subframe_len;
	int len;
	u8 padding;

	while (!last) {
		skb_copy_bits(skb, offset, &eth, sizeof(eth));
		len = ntohs(eth.h_proto);
		subframe_len = sizeof(struct ethhdr) + len;
		padding = (4 - subframe_len) & 0x3;

		/* the last MSDU has no padding */
		remaining = skb->len - offset;
		if (subframe_len > remaining) {
			dbg = 1;
			goto purge;
		}
		/* mitigate A-MSDU aggregation injection attacks */
		if (ether_addr_equal(eth.h_dest, rfc1042_header)) {
			dbg = 2;
			goto purge;
		}

		offset += sizeof(struct ethhdr);
		last = remaining <= subframe_len + padding;

		/* FIXME: should we really accept multicast DA? */
		if ((check_da && !is_multicast_ether_addr(eth.h_dest) &&
		     !ether_addr_equal(check_da, eth.h_dest)) ||
		    (check_sa && !ether_addr_equal(check_sa, eth.h_source))) {
			offset += len + padding;
			dbg = 3;
			continue;
		}

		/* reuse skb for the last subframe */
		if (!skb_is_nonlinear(skb) && !reuse_frag && last) {
			skb_pull(skb, offset);
			frame = skb;
			reuse_skb = true;
		} else {
			frame = __ieee80211_amsdu_copy(skb, hlen, offset, len,
						       reuse_frag);
			if (!frame) {
				dbg = 4;
				goto purge;
			}

			offset += len + padding;
		}

		skb_reset_network_header(frame);
		frame->dev = skb->dev;
		frame->priority = skb->priority;

		payload = frame->data;
		ethertype = (payload[6] << 8) | payload[7];
		if (likely((ether_addr_equal(payload, rfc1042_header) &&
			    ethertype != ETH_P_AARP &&
			    ethertype != ETH_P_IPX) ||
			   ether_addr_equal(payload, bridge_tunnel_header))) {
			eth.h_proto = htons(ethertype);
			skb_pull(frame, ETH_ALEN + 2);
		}

		memcpy(skb_push(frame, sizeof(eth)), &eth, sizeof(eth));
		__skb_queue_tail(list, frame);
		WQ_DBG(DM_RX, DL_WRN,
		       "%s: dbg=%d, subframe_len=%d, remaining=%d, offset=%d, len=%d, padding=%d",
		       __func__, dbg, subframe_len, remaining, offset, len,
		       padding);
	}

	if (!reuse_skb)
		dev_kfree_skb(skb);

	return;

purge:
	WQ_DBG(DM_RX, DL_WRN,
	       "%s: dbg=%d, subframe_len=%d, remaining=%d, offset=%d, len=%d, padding=%d",
	       __func__, dbg, subframe_len, remaining, offset, len, padding);
	__skb_queue_purge(list);
	dev_kfree_skb(skb);
}
#endif

/**
 * rwnx_rx_data_skb - Process one data frame
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: vif that received the buffer
 * @skb: skb received
 * @rxhdr: HW rx descriptor
 * @return: true if buffer has been forwarded to upper layer
 *
 * If buffer is amsdu , it is first split into a list of skb.
 * Then each skb may be:
 * - forwarded to upper layer
 * - resent on wireless interface
 *
 * When vif is a STA interface, every skb is only forwarded to upper layer.
 * When vif is an AP interface, multicast skb are forwarded and resent, whereas
 * skb for other BSS's STA are only resent.
 */
bool rwnx_rx_data_skb(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
		      struct sk_buff *skb, struct hw_rxhdr *rxhdr)
{
	struct sk_buff_head list;
	struct sk_buff *rx_skb;
	bool amsdu = rxhdr->flags_is_amsdu;
	bool resend = false, forward = true;
	bool renew_path = false;
	int skip_after_eth_hdr = 0;

	WQ_DBG(DM_RX, DL_INF,
	       "%s: vif(id=%u, tp=%u), skb=0x%p(len=%u, amsdu=%u), dst_idx=%u\n",
	       __func__, rwnx_vif->vif_index, RWNX_VIF_TYPE(rwnx_vif), skb,
	       skb->len, amsdu, rxhdr->flags_dst_idx);

	skb->dev = rwnx_vif->ndev;

	__skb_queue_head_init(&list);

	if (amsdu) {
		int count;

		if (gv_cksum_offload) {
			ieee80211_amsdu_to_8023s_ll(skb, &list,
						    rwnx_vif->ndev->dev_addr,
						    RWNX_VIF_TYPE(rwnx_vif), 0,
						    NULL, NULL);
		} else {
#ifdef AMSDU_DBG
			check_ieee80211_amsdu_to_8023s(skb, &list,
						       rwnx_vif->ndev->dev_addr,
						       RWNX_VIF_TYPE(rwnx_vif),
						       0, NULL, NULL);
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
			ieee80211_amsdu_to_8023s(skb, &list,
						 rwnx_vif->ndev->dev_addr,
						 RWNX_VIF_TYPE(rwnx_vif), 0,
						 NULL, NULL, 0);
#else
			ieee80211_amsdu_to_8023s(skb, &list,
						 rwnx_vif->ndev->dev_addr,
						 RWNX_VIF_TYPE(rwnx_vif), 0,
						 NULL, NULL);
#endif

#endif
		}

		count = skb_queue_len(&list);
		if (count == 0) {
			WQ_DBG(DM_GENERIC, DL_WRN, "de-amsdu failed, drop!\n");
			return true;
		}
		if (count > ARRAY_SIZE(rwnx_hw->stats.amsdus_rx))
			count = ARRAY_SIZE(rwnx_hw->stats.amsdus_rx);
		rwnx_hw->stats.amsdus_rx[count - 1]++;
	} else {
		rwnx_hw->stats.non_amsdu_rx++;
		__skb_queue_head(&list, skb);
	}

	if (((RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP) ||
	     (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP_VLAN) ||
	     (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO)) &&
	    !(rwnx_vif->ap.flags & RWNX_AP_ISOLATE)) {
		if (amsdu) {
			renew_path = true;
		} else {
			const struct ethhdr *eth;
			rx_skb = skb_peek(&list);

			skb_reset_mac_header(rx_skb);
			eth = eth_hdr(rx_skb);

			if (unlikely(is_multicast_ether_addr(eth->h_dest))) {
				/* broadcast pkt need to be forwared to upper layer and resent
                   on wireless interface */
				resend = true;
			} else {
				/* unicast pkt for STA inside the BSS, no need to forward to upper
                   layer simply resend on wireless interface */
				if (rxhdr->flags_dst_idx != RWNX_INVALID_STA) {
					struct rwnx_sta *sta =
						&rwnx_hw->sta_table
							 [rxhdr->flags_dst_idx];
					if (sta->valid &&
					    (sta->vlan_idx ==
					     rwnx_vif->vif_index)) {
						forward = false;
						resend = true;
					} else {
						WQ_DBG(DM_RX, DL_INF,
						       "%s: valid=%u, vlan_idx=%u\n",
						       __func__, sta->valid,
						       sta->vlan_idx);
					}
				}
			}

			WQ_DBG(DM_RX, DL_INF,
			       "%s: resend=%u, forward=%u, mac=%pM (mc=%u)\n",
			       __func__, resend, forward, eth->h_dest,
			       is_multicast_ether_addr(eth->h_dest));
		}
	} else if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MESH_POINT) {
		const struct ethhdr *eth;
		rx_skb = skb_peek(&list);
		skb_reset_mac_header(rx_skb);
		eth = eth_hdr(rx_skb);

		if (rxhdr->flags_dst_idx != RWNX_INVALID_STA) {
			resend = true;

			if (is_multicast_ether_addr(eth->h_dest)) {
				// MC/BC frames are uploaded with mesh control and LLC/snap
				// (so they can be mesh forwarded) that need to be removed.
				uint8_t *mesh_ctrl = (uint8_t *)(eth + 1);
				skip_after_eth_hdr = 8 + 6;

				if ((*mesh_ctrl & MESH_FLAGS_AE) ==
				    MESH_FLAGS_AE_A4)
					skip_after_eth_hdr += ETH_ALEN;
				else if ((*mesh_ctrl & MESH_FLAGS_AE) ==
					 MESH_FLAGS_AE_A5_A6)
					skip_after_eth_hdr += 2 * ETH_ALEN;
			} else {
				forward = false;
			}
		}
	}

	while (!skb_queue_empty(&list)) {
		rx_skb = __skb_dequeue(&list);

		/* AMSDU only: update resend/forward per DA */
		if (renew_path == true) {
			const struct ethhdr *eth;

			//reset to default value
			resend = false;
			forward = true;

			skb_reset_mac_header(rx_skb);
			eth = eth_hdr(rx_skb);

			if (unlikely(is_multicast_ether_addr(eth->h_dest))) {
				/* broadcast pkt need to be forwared to upper layer and resent
                   on wireless interface */
				resend = true;
			} else {
				/* unicast pkt for STA inside the BSS, no need to forward to upper
                   layer simply resend on wireless interface */
				struct rwnx_sta *sta_iter;
				list_for_each_entry (sta_iter,
						     &rwnx_vif->ap.sta_list,
						     list) {
					if (sta_iter->valid &&
					    ether_addr_equal(sta_iter->mac_addr,
							     eth->h_dest)) {
						if (sta_iter->vlan_idx ==
						    rwnx_vif->vif_index) {
							forward = false;
							resend = true;
							break;
						} else {
							WQ_DBG(DM_RX, DL_INF,
							       "%s: valid=%u, vlan_idx=%u\n",
							       __func__,
							       sta_iter->valid,
							       sta_iter->vlan_idx);
						}
					}
				}
			}

			WQ_DBG(DM_RX, DL_INF,
			       "%s: rx_skb=0x%p(len=%u), resend=%u, forward=%u, mac=%pM (mc=%u)\n",
			       __func__, rx_skb, rx_skb->len, resend, forward,
			       eth->h_dest,
			       is_multicast_ether_addr(eth->h_dest));
		}

		/* resend pkt on wireless interface */
		if (resend) {
			struct sk_buff *skb_copy;
			/* always need to copy buffer even when forward=0 to get enough headrom for tsdesc */
			skb_copy = skb_copy_expand(rx_skb, IPC_TX_MAX_HEADROOM,
						   (RWNX_TX_ALIGN_SIZE +
						    WQ_HIF_TRAILER_SPACE_RSVD),
						   GFP_ATOMIC);
			if (skb_copy) {
				int res;
				skb_copy->protocol = htons(ETH_P_802_3);
				skb_reset_network_header(skb_copy);
				skb_reset_mac_header(skb_copy);

				rwnx_vif->is_resending = true;
				res = dev_queue_xmit(skb_copy);
				rwnx_vif->is_resending = false;
				/* note: buffer is always consummed by dev_queue_xmit */
				if (res == NET_XMIT_DROP) {
					WQ_DBG(DM_RX, DL_INF,
					       "%s: resend drop !\n", __func__);

					rwnx_vif->net_stats.rx_dropped++;
					rwnx_vif->net_stats.tx_dropped++;
				} else if (res != NET_XMIT_SUCCESS) {
					netdev_err(
						rwnx_vif->ndev,
						"Failed to re-send buffer to driver (res=%d)",
						res);
					rwnx_vif->net_stats.tx_errors++;
				}
			} else {
				netdev_err(rwnx_vif->ndev,
					   "Failed to copy skb");
			}
		}

		/* forward pkt to upper layer */
		if (forward) {
			rx_skb->protocol =
				eth_type_trans(rx_skb, rwnx_vif->ndev);

			// Special case for MESH when BC/MC is uploaded and resend
			if (unlikely(skip_after_eth_hdr)) {
				memmove(skb_mac_header(rx_skb) +
						skip_after_eth_hdr,
					skb_mac_header(rx_skb),
					sizeof(struct ethhdr));
				__skb_pull(rx_skb, skip_after_eth_hdr);
				skb_reset_mac_header(rx_skb);
				skip_after_eth_hdr = 0;
			}

			PROFILING_SET(SW_PROF_IEEE80211RX);

#if MEM_RECORED_CHECK
			/* pkt passed to upper layer */
			del_mem_record(rx_skb, __func__, __LINE__);
#endif

			atomic_inc(&rwnx_hw->rx_packets);
			atomic_add(rx_skb->len, &rwnx_hw->rx_bytes);

			/* Update statistics */
			rwnx_vif->net_stats.rx_packets++;
			rwnx_vif->net_stats.rx_bytes += rx_skb->len;

			if(rwnx_vif->extAP_supp)
			{
				struct ethhdr *eth;
				ieee80211_extap_input((struct ethhdr *)(rx_skb->data - sizeof(struct ethhdr)), rwnx_vif->ndev->dev_addr);

				eth = (struct ethhdr *)rx_skb->data;
				--eth;

				if (!memcmp(eth->h_source, rwnx_vif->ndev->dev_addr, ETH_ALEN)) {
					if (rx_skb->len > 64)
						dump_bytes(DL_WRN, "", (char *)eth, 64);
					else {
						dump_bytes(DL_WRN, "", (char *)eth, skb->len+sizeof(struct ethhdr));
					}

					dev_kfree_skb(rx_skb);
					continue;
				}
			}

			WQ_DBG(DM_RX, DL_VRB, "%s %4d bytes: %*ph\n", __func__,
				rx_skb->len, rx_skb->len > 32 ? 32 : rx_skb->len,
				rx_skb->data);
			wq_pkt_classify(rx_skb, 0, false);

#ifdef NAPI_SUPPORT
			if (rwnx_hw->napi_param.napi_enable) {
				skb_queue_tail(&rwnx_hw->napi_rx_pkt_list, rx_skb);
			} else {
				if (irq_count())
					netif_receive_skb(rx_skb);
				else
					netif_rx(rx_skb);
			}
#else
			/* FIXME: use NAPI here */
			if (irq_count())
				netif_receive_skb(rx_skb);
			else
				netif_rx(rx_skb);
#endif

			PROFILING_CLR(SW_PROF_IEEE80211RX);
		} else {
			dev_kfree_skb(rx_skb);
		}
	}

	return forward;
}

/**
 * rwnx_rx_mgmt - Process one 802.11 management frame
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: vif to upload the buffer to
 * @skb: skb received
 * @rxhdr: HW rx descriptor
 *
 * Forward the management frame to a given interface.
 */
static void rwnx_rx_mgmt(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			 struct sk_buff *skb, struct hw_rxhdr *hw_rxhdr)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct rx_vec_detail_1 *rxvect = &hw_rxhdr->hwvect.rx_vec_1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
	struct timeval now = { .tv_sec = 0, .tv_usec = 0 };
#else
	struct timespec64 now = { .tv_sec = 0, .tv_nsec = 0 };
#endif

	WQ_DBG(DM_RX, DL_INF, "%s, vif_type: %d\n", __func__,
	       RWNX_VIF_TYPE(rwnx_vif));
	dump_bytes(DL_VRB, "rwnx_rx_mgmt()", skb->data,
		   sizeof(struct ieee80211_hdr));

	spin_lock(&rwnx_hw->mgmt_hist_lock);
	if (mgmt_idx < HIST_CNT &&
	    (mgmt->frame_control & IEEE80211_FCTL_STYPE) &&
	    (!ieee80211_is_beacon(mgmt->frame_control))) { //exclude beacon
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
		do_gettimeofday(&now);
#else
		ktime_get_real_ts64(&now);
#endif
		mgmt_hist[mgmt_idx].ts = now.tv_sec;
		mgmt_hist[mgmt_idx].dir = WIFI_DBG_PKT_RX;
		mgmt_hist[mgmt_idx].ack = 0;
		mgmt_hist[mgmt_idx].frame_ctrl = mgmt->frame_control;
		mgmt_hist[mgmt_idx].category = mgmt->u.action.category;
		mgmt_hist[mgmt_idx].action_type =
			mgmt->u.action.u.wme_action.action_code;
		mgmt_hist[mgmt_idx].p2p =
			*((uint8_t *)&mgmt->u.action.category +
			  MGMT_ACTION_OUI_SUBTYPE_OFFSET);
		memcpy(mgmt_hist[mgmt_idx].da, mgmt->da, ETH_ALEN);
		memcpy(mgmt_hist[mgmt_idx].sa, mgmt->sa, ETH_ALEN);
		mgmt_idx++;
		if (mgmt_idx == HIST_CNT)
			mgmt_idx = 0;
	}
	spin_unlock(&rwnx_hw->mgmt_hist_lock);

	if (ieee80211_is_beacon(mgmt->frame_control)) {
		if ((RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MESH_POINT) &&
		    hw_rxhdr->flags_new_peer) {
			cfg80211_notify_new_peer_candidate(
				rwnx_vif->ndev, mgmt->sa,
				mgmt->u.beacon.variable,
				skb->len - offsetof(struct ieee80211_mgmt,
						    u.beacon.variable),
				rxvect->rssi_leg, GFP_ATOMIC);
		} else {
			cfg80211_report_obss_beacon(
				rwnx_hw->wiphy, skb->data, skb->len,
				hw_rxhdr->phy_info.phy_prim20_freq,
				rxvect->rssi_leg);
		}
	} else if ((ieee80211_is_deauth(mgmt->frame_control) ||
		    ieee80211_is_disassoc(mgmt->frame_control)) &&
		   (mgmt->u.deauth.reason_code ==
			    WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA ||
		    mgmt->u.deauth.reason_code ==
			    WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
		cfg80211_rx_unprot_mlme_mgmt(rwnx_vif->ndev, skb->data,
					     skb->len);
#else
		if (ieee80211_is_deauth(mgmt->frame_control))
			cfg80211_send_unprot_deauth(rwnx_vif->ndev,
							skb->data,
							skb->len);
		else if (ieee80211_is_disassoc(mgmt->frame_control))
			cfg80211_send_unprot_disassoc(rwnx_vif->ndev,
							skb->data,
							skb->len);
#endif
	} else if ((RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION) &&
		   (ieee80211_is_action(mgmt->frame_control) &&
		    (mgmt->u.action.category == 6))) {
		// Wpa_supplicant will ignore the FT action frame if reported via cfg80211_rx_mgmt
		// and cannot call cfg80211_ft_event from atomic context so defer message processing
		rwnx_rx_defer_skb(rwnx_hw, rwnx_vif, skb);
	} else {
		//dump_bytes("cfg80211_rx_mgmt()", skb->data, skb->len);
		if (!check_roc_ignore_nego_req(skb->data, skb->len, 0, 0))
			cfg80211_rx_mgmt(&rwnx_vif->wdev,
					 hw_rxhdr->phy_info.phy_prim20_freq,
					 rxvect->rssi_leg, skb->data, skb->len,
					 0);
	}
}

int wq_get_vif_band(struct rwnx_vif *vif) {
	int band = -1;
	if(vif->up) {
		if (vif->sta.ap) //STA or P2P_GC
		{
			band = vif->sta.ap->band;
		}
		else if (vif->ap.chandef.chan) //AP or P2P_GO
		{
			band = vif->ap.chandef.chan->band;
		}
	}
	return band;
}

/**
 * rwnx_rx_mgmt_any - Process one 802.11 management frame
 *
 * @rwnx_hw: main driver data
 * @skb: skb received
 * @rxhdr: HW rx descriptor
 *
 * Process the management frame and free the corresponding skb.
 * If vif is not specified in the rx descriptor, the the frame is uploaded
 * on all active vifs.
 */
void rwnx_rx_mgmt_any(struct rwnx_hw *rwnx_hw, struct sk_buff *skb,
		      struct hw_rxhdr *hw_rxhdr)
{
	struct rwnx_vif *rwnx_vif;
	int vif_idx = hw_rxhdr->flags_vif_idx;
	int vif_band;

	trace_mgmt_rx(hw_rxhdr->phy_info.phy_prim20_freq, vif_idx,
		      hw_rxhdr->flags_sta_idx,
		      (struct ieee80211_mgmt *)skb->data);

	if (vif_idx == RWNX_INVALID_VIF) {
		list_for_each_entry (rwnx_vif, &rwnx_hw->vifs, list) {
			if (!rwnx_vif->up)
				continue;

			vif_band = wq_get_vif_band(rwnx_vif);
			if(vif_band == -1 ||
				vif_band == hw_rxhdr->phy_info.phy_band) {
				rwnx_rx_mgmt(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
			}
		}
	} else {
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (rwnx_vif)
			rwnx_rx_mgmt(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
	}

	WQ_DBG(DM_RX, DL_INF, "%s, vif_idx: %d, rwnx_vif=0x%p \n", __func__,
	       vif_idx, rwnx_vif);
	dev_kfree_skb(skb);
}


static void rwnx_rx_cntrl(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                                    struct sk_buff *skb,  struct hw_rxhdr *hw_rxhdr)
{
    struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

    if (ieee80211_is_back_req(hdr->frame_control)) {  //BAR
        struct bam_evt_bar_parm   *bar = (struct bam_evt_bar_parm *)(skb->data + 4); //addr + offset 4 is bam_evt_bar_parm
        WQ_DBG(DM_RX, DL_INF, "%s, bar->sta_idx=%d, bar->tid=%d, bar->ssn=%d, bar->fctrl_retry=%d \n", __func__, bar->sta_idx, bar->tid, bar->ssn, bar->fctrl_retry);
        ieee80211_recv_bar(rwnx_hw, bar->sta_idx, bar->tid, bar->ssn, bar->fctrl_retry);
    } else {
        WQ_DBG(DM_RX, DL_ERR, "%s, receive a unknown cntrl pkt\n", __func__);
    }
}

void rwnx_rx_cntrl_any(struct rwnx_hw *rwnx_hw, struct sk_buff *skb,
                                    struct hw_rxhdr *hw_rxhdr)
{
    struct rwnx_vif *rwnx_vif;
    int vif_idx = hw_rxhdr->flags_vif_idx;

    trace_mgmt_rx(hw_rxhdr->phy_info.phy_prim20_freq, vif_idx,
            hw_rxhdr->flags_sta_idx,
            (struct ieee80211_mgmt *)skb->data);

    if (vif_idx == RWNX_INVALID_VIF) {
        list_for_each_entry (rwnx_vif, &rwnx_hw->vifs, list) {
            if (!rwnx_vif->up)
                continue;
            rwnx_rx_cntrl(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
        }
    } else {
        rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
        if (rwnx_vif)
            rwnx_rx_cntrl(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
    }

    WQ_DBG(DM_RX, DL_INF, "%s, vif_idx: %d, rwnx_vif=0x%p \n", __func__,
         vif_idx, rwnx_vif);
    dev_kfree_skb(skb);
}


/**
 * rwnx_rx_rtap_hdrlen - Return radiotap header length
 *
 * @rxvect: Rx vector used to fill the radiotap header
 * @has_vend_rtap: boolean indicating if vendor specific data is present
 *
 * Compute the length of the radiotap header based on @rxvect and vendor
 * specific data (if any).
 */
u8 rwnx_rx_rtap_hdrlen(struct rx_vec_detail_1 *rxvect, bool has_vend_rtap)
{
	u8 rtap_len;

	/* Compute radiotap header length */
	rtap_len = sizeof(struct ieee80211_radiotap_header) + 8;

	// Check for multiple antennas
	if (hweight32(rxvect->antenna_set) > 1)
		// antenna and antenna signal fields
		rtap_len += 4 * hweight8(rxvect->antenna_set);

	// TSFT
	if (!has_vend_rtap) {
		rtap_len = ALIGN(rtap_len, 8);
		rtap_len += 8;
	}

	// IEEE80211_HW_SIGNAL_DBM
	rtap_len++;

	// Check if single antenna
	if (hweight32(rxvect->antenna_set) == 1)
		rtap_len++; //Single antenna

	// padding for RX FLAGS
	rtap_len = ALIGN(rtap_len, 2);

	// Check for HT frames
	if ((rxvect->format_mod == FORMATMOD_HT_MF) ||
	    (rxvect->format_mod == FORMATMOD_HT_GF))
		rtap_len += 3;

	// Check for AMPDU
	if (!(has_vend_rtap) &&
	    ((rxvect->format_mod >= FORMATMOD_VHT) ||
	     ((rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM) &&
	      (rxvect->ht.aggregation)))) {
		rtap_len = ALIGN(rtap_len, 4);
		rtap_len += 8;
	}

	// Check for VHT frames
	if (rxvect->format_mod == FORMATMOD_VHT) {
		rtap_len = ALIGN(rtap_len, 2);
		rtap_len += 12;
	}

	// Check for HE frames
	if (rxvect->format_mod == FORMATMOD_HE_SU) {
		rtap_len = ALIGN(rtap_len, 2);
		rtap_len += sizeof(struct ieee80211_radiotap_he);
	}

	// Check for multiple antennas
	if (hweight32(rxvect->antenna_set) > 1) {
		// antenna and antenna signal fields
		rtap_len += 2 * hweight8(rxvect->antenna_set);
	}

	// Check for vendor specific data
	if (has_vend_rtap) {
		/* vendor presence bitmap */
		rtap_len += 4;
		/* alignment for fixed 6-byte vendor data header */
		rtap_len = ALIGN(rtap_len, 2);
	}

	return rtap_len;
}

/**
 * rwnx_rx_add_rtap_hdr - Add radiotap header to sk_buff
 *
 * @rwnx_hw: main driver data
 * @skb: skb received (will include the radiotap header)
 * @rxvect: Rx vector
 * @phy_info: Information regarding the phy
 * @hwvect: HW Info (NULL if vendor specific data is available)
 * @rtap_len: Length of the radiotap header
 * @vend_rtap_len: radiotap vendor length (0 if not present)
 * @vend_it_present: radiotap vendor present
 *
 * Builds a radiotap header and add it to @skb.
 */
static void rwnx_rx_add_rtap_hdr(struct rwnx_hw *rwnx_hw, struct sk_buff *skb,
				 struct rx_vec_detail_1 *rxvect,
				 struct phy_channel_info_desc *phy_info,
				 struct hw_vect *hwvect, int rtap_len,
				 u8 vend_rtap_len, u32 vend_it_present)
{
	struct ieee80211_radiotap_header *rtap;
	u8 *pos, rate_idx;
	__le32 *it_present;
	u32 it_present_val = 0;
	bool fec_coding = false;
	bool short_gi = false;
	bool stbc = false;
	bool aggregation = false;

	if (rwnx_hw->monitor_vif != RWNX_INVALID_VIF) {
		rwnx_hw->monitor_param.rx_rssi = rxvect->rssi_leg;
	}

	rtap = (struct ieee80211_radiotap_header *)skb_push(skb, rtap_len);
	memset((u8 *)rtap, 0, rtap_len);

	rtap->it_version = 0;
	rtap->it_pad = 0;
	rtap->it_len = cpu_to_le16(rtap_len + vend_rtap_len);

	it_present = &rtap->it_present;

	// Check for multiple antennas
	if (hweight32(rxvect->antenna_set) > 1) {
		int chain;
		unsigned long chains = rxvect->antenna_set;

		for_each_set_bit (chain, &chains, IEEE80211_MAX_CHAINS) {
			it_present_val |=
				BIT(IEEE80211_RADIOTAP_EXT) |
				BIT(IEEE80211_RADIOTAP_RADIOTAP_NAMESPACE);
			put_unaligned_le32(it_present_val, it_present);
			it_present++;
			it_present_val = BIT(IEEE80211_RADIOTAP_ANTENNA) |
					 BIT(IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
		}
	}

	// Check if vendor specific data is present
	if (vend_rtap_len) {
		it_present_val |= BIT(IEEE80211_RADIOTAP_VENDOR_NAMESPACE) |
				  BIT(IEEE80211_RADIOTAP_EXT);
		put_unaligned_le32(it_present_val, it_present);
		it_present++;
		it_present_val = vend_it_present;
	}

	put_unaligned_le32(it_present_val, it_present);
	pos = (void *)(it_present + 1);

	// IEEE80211_RADIOTAP_TSFT
	if (hwvect) {
		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_TSFT);
		// padding
		while ((pos - (u8 *)rtap) & 7)
			*pos++ = 0;
		put_unaligned_le64((((u64)le32_to_cpu(hwvect->tsfhi) << 32) +
				    (u64)le32_to_cpu(hwvect->tsflo)),
				   pos);
		pos += 8;
	}

	// IEEE80211_RADIOTAP_FLAGS
	rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_FLAGS);
	if (hwvect && (!hwvect->statinfo.frm_successful_rx))
		*pos |= IEEE80211_RADIOTAP_F_BADFCS;
	if (!rxvect->pre_type &&
	    (rxvect->format_mod <= FORMATMOD_NON_HT_DUP_OFDM))
		*pos |= IEEE80211_RADIOTAP_F_SHORTPRE;
	pos++;

	// IEEE80211_RADIOTAP_RATE
	// check for HT, VHT or HE frames
	if (rxvect->format_mod >= FORMATMOD_HE_SU) {
		rate_idx = rxvect->he.mcs;
		fec_coding = rxvect->he.fec;
		stbc = rxvect->he.stbc;
		aggregation = true;
		*pos = 0;
	} else if (rxvect->format_mod == FORMATMOD_VHT) {
		rate_idx = rxvect->vht.mcs;
		fec_coding = rxvect->vht.fec;
		short_gi = rxvect->vht.short_gi;
		stbc = rxvect->vht.stbc;
		aggregation = true;
		*pos = 0;
	} else if (rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM) {
		rate_idx = rxvect->ht.mcs;
		fec_coding = rxvect->ht.fec;
		short_gi = rxvect->ht.short_gi;
		stbc = rxvect->ht.stbc;
		aggregation = rxvect->ht.aggregation;
		*pos = 0;
	} else {
		struct ieee80211_supported_band *band =
			rwnx_hw->wiphy->bands[phy_info->phy_band];
		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_RATE);
		BUG_ON((rate_idx = legrates_lut[rxvect->leg_rate].idx) == -1);
		if (phy_info->phy_band == NL80211_BAND_5GHZ)
			rate_idx -=
				4; /* rwnx_ratetable_5ghz[0].hw_value == 4 */
		*pos = DIV_ROUND_UP(band->bitrates[rate_idx].bitrate, 5);
	}
	pos++;

	// IEEE80211_RADIOTAP_CHANNEL
	rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_CHANNEL);
	put_unaligned_le16(phy_info->phy_prim20_freq, pos);
	pos += 2;

	if (phy_info->phy_band == NL80211_BAND_5GHZ)
		put_unaligned_le16(IEEE80211_CHAN_OFDM | IEEE80211_CHAN_5GHZ,
				   pos);
	else if (rxvect->format_mod > FORMATMOD_NON_HT_DUP_OFDM)
		put_unaligned_le16(IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ,
				   pos);
	else
		put_unaligned_le16(IEEE80211_CHAN_CCK | IEEE80211_CHAN_2GHZ,
				   pos);
	pos += 2;

	if (hweight32(rxvect->antenna_set) == 1) {
		// IEEE80211_RADIOTAP_DBM_ANTSIGNAL
		rtap->it_present |=
			cpu_to_le32(1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
		*pos++ = rxvect->rssi_leg;

		// IEEE80211_RADIOTAP_ANTENNA
		rtap->it_present |=
			cpu_to_le32(1 << IEEE80211_RADIOTAP_ANTENNA);
		*pos++ = rxvect->antenna_set;
	}

	// IEEE80211_RADIOTAP_LOCK_QUALITY is missing
	// IEEE80211_RADIOTAP_DB_ANTNOISE is missing

	// IEEE80211_RADIOTAP_RX_FLAGS
	rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_RX_FLAGS);
	// 2 byte alignment
	if ((pos - (u8 *)rtap) & 1)
		*pos++ = 0;
	put_unaligned_le16(0, pos);
	//Right now, we only support fcs error (no RX_FLAG_FAILED_PLCP_CRC)
	pos += 2;

	// Check if HT
	if ((rxvect->format_mod == FORMATMOD_HT_MF) ||
	    (rxvect->format_mod == FORMATMOD_HT_GF)) {
		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_MCS);
		*pos++ = (IEEE80211_RADIOTAP_MCS_HAVE_MCS |
			  IEEE80211_RADIOTAP_MCS_HAVE_GI |
			  IEEE80211_RADIOTAP_MCS_HAVE_BW |
			  IEEE80211_RADIOTAP_MCS_HAVE_FMT |
			  IEEE80211_RADIOTAP_MCS_HAVE_FEC |
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
			  IEEE80211_RADIOTAP_MCS_HAVE_STBC);
#else
			  0);
#endif

		pos++;
		*pos = 0;
		if (short_gi)
			*pos |= IEEE80211_RADIOTAP_MCS_SGI;
		if (rxvect->ch_bw == PHY_CHNL_BW_40)
			*pos |= IEEE80211_RADIOTAP_MCS_BW_40;
		if (rxvect->format_mod == FORMATMOD_HT_GF)
			*pos |= IEEE80211_RADIOTAP_MCS_FMT_GF;
		if (fec_coding)
			*pos |= IEEE80211_RADIOTAP_MCS_FEC_LDPC;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
		*pos++ |= stbc << IEEE80211_RADIOTAP_MCS_STBC_SHIFT;
#endif
		*pos++ = rate_idx;
	}

	// check for HT or VHT frames
	if (aggregation && hwvect) {
		// 4 byte alignment
		while ((pos - (u8 *)rtap) & 3)
			pos++;
		rtap->it_present |=
			cpu_to_le32(1 << IEEE80211_RADIOTAP_AMPDU_STATUS);
		put_unaligned_le32(hwvect->ampdu_cnt, pos);
		pos += 4;
		put_unaligned_le32(0, pos);
		pos += 4;
	}

	// Check for VHT frames
	if (rxvect->format_mod == FORMATMOD_VHT) {
		u16 vht_details = IEEE80211_RADIOTAP_VHT_KNOWN_GI |
				  IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH;
		u8 vht_nss = rxvect->vht.nss + 1;

		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_VHT);

		if ((rxvect->ch_bw == PHY_CHNL_BW_160) &&
		    phy_info->phy_center2_freq)
			vht_details &= ~IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH;
		put_unaligned_le16(vht_details, pos);
		pos += 2;

		// flags
		if (short_gi)
			*pos |= IEEE80211_RADIOTAP_VHT_FLAG_SGI;
		if (stbc)
			*pos |= IEEE80211_RADIOTAP_VHT_FLAG_STBC;
		pos++;

		// bandwidth
		if (rxvect->ch_bw == PHY_CHNL_BW_40)
			*pos++ = 1;
		if (rxvect->ch_bw == PHY_CHNL_BW_80)
			*pos++ = 4;
		else if ((rxvect->ch_bw == PHY_CHNL_BW_160) &&
			 phy_info->phy_center2_freq)
			*pos++ = 0; //80P80
		else if (rxvect->ch_bw == PHY_CHNL_BW_160)
			*pos++ = 11;
		else // 20 MHz
			*pos++ = 0;

		// MCS/NSS
		*pos++ = (rate_idx << 4) | vht_nss;
		*pos++ = 0;
		*pos++ = 0;
		*pos++ = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
		if (fec_coding)
			*pos |= IEEE80211_RADIOTAP_CODING_LDPC_USER0;
#endif
		pos++;
		// group ID
		pos++;
		// partial_aid
		pos += 2;
	}

	// Check for HE frames
	if (rxvect->format_mod >= FORMATMOD_HE_SU) {
		struct ieee80211_radiotap_he he;
#define HE_PREP(f, val) cpu_to_le16(FIELD_PREP(IEEE80211_RADIOTAP_HE_##f, val))
#define D1_KNOWN(f) cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_##f##_KNOWN)
#define D2_KNOWN(f) cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_##f##_KNOWN)

		he.data1 = D1_KNOWN(BSS_COLOR) | D1_KNOWN(BEAM_CHANGE) |
			   D1_KNOWN(UL_DL) | D1_KNOWN(STBC) |
			   D1_KNOWN(DOPPLER) | D1_KNOWN(DATA_DCM);
		he.data2 = D2_KNOWN(GI) | D2_KNOWN(TXBF) | D2_KNOWN(TXOP);

		he.data3 |= HE_PREP(DATA3_BSS_COLOR, rxvect->he.bss_color);
		he.data3 |= HE_PREP(DATA3_BEAM_CHANGE, rxvect->he.beam_change);
		he.data3 |= HE_PREP(DATA3_UL_DL, rxvect->he.uplink_flag);
		he.data3 |= HE_PREP(DATA3_BSS_COLOR, rxvect->he.bss_color);
		he.data3 |= HE_PREP(DATA3_DATA_DCM, rxvect->he.dcm);

		he.data5 |= HE_PREP(DATA5_GI, rxvect->he.gi_type);
		he.data5 |= HE_PREP(DATA5_TXBF, rxvect->he.beamformed);
		he.data5 |= HE_PREP(DATA5_LTF_SIZE, rxvect->he.he_ltf_type + 1);

		he.data6 |= HE_PREP(DATA6_DOPPLER, rxvect->he.doppler);
		he.data6 |= HE_PREP(DATA6_TXOP, rxvect->he.txop_duration);

		if (rxvect->format_mod != FORMATMOD_HE_TB) {
			he.data1 |=
				(D1_KNOWN(DATA_MCS) | D1_KNOWN(CODING) |
				 D1_KNOWN(SPTL_REUSE) | D1_KNOWN(BW_RU_ALLOC));

			if (stbc) {
				he.data6 |= HE_PREP(DATA6_NSTS, 2);
				he.data3 |= HE_PREP(DATA3_STBC, 1);
			} else {
				he.data6 |= HE_PREP(DATA6_NSTS, rxvect->he.nss + 1);
			}

			he.data3 |= HE_PREP(DATA3_DATA_MCS, rxvect->he.mcs);
			he.data3 |= HE_PREP(DATA3_CODING, rxvect->he.fec);

			he.data4 = HE_PREP(DATA4_SU_MU_SPTL_REUSE,
					   rxvect->he.spatial_reuse);

			if (rxvect->format_mod == FORMATMOD_HE_MU) {
				he.data1 |=
					IEEE80211_RADIOTAP_HE_DATA1_FORMAT_MU;
				he.data5 |= HE_PREP(
					DATA5_DATA_BW_RU_ALLOC,
					rxvect->he.ru_size +
						IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_26T);
			} else {
				if (rxvect->format_mod == FORMATMOD_HE_SU)
					he.data1 |=
						IEEE80211_RADIOTAP_HE_DATA1_FORMAT_SU;
				else
					he.data1 |=
						IEEE80211_RADIOTAP_HE_DATA1_FORMAT_EXT_SU;

				switch (rxvect->ch_bw) {
				case PHY_CHNL_BW_20:
					he.data5 |= HE_PREP(
						DATA5_DATA_BW_RU_ALLOC,
						IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_20MHZ);
					break;
				case PHY_CHNL_BW_40:
					he.data5 |= HE_PREP(
						DATA5_DATA_BW_RU_ALLOC,
						IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_40MHZ);
					break;
				case PHY_CHNL_BW_80:
					he.data5 |= HE_PREP(
						DATA5_DATA_BW_RU_ALLOC,
						IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_80MHZ);
					break;
				case PHY_CHNL_BW_160:
					he.data5 |= HE_PREP(
						DATA5_DATA_BW_RU_ALLOC,
						IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_160MHZ);
					break;
				default:
					WARN_ONCE(1, "Invalid SU BW %d\n",
						  rxvect->ch_bw);
				}
			}
		} else {
			he.data1 |= IEEE80211_RADIOTAP_HE_DATA1_FORMAT_TRIG;
		}

		/* ensure 2 bytes alignment */
		while ((pos - (u8 *)rtap) & 1)
			pos++;
		rtap->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_HE);
		*(struct ieee80211_radiotap_he *)pos = he;
		pos += sizeof(he);
	}

	// Rx Chains
	if (hweight32(rxvect->antenna_set) > 1) {
		int chain;
		unsigned long chains = rxvect->antenna_set;
		u8 rssis[4] = { rxvect->rssi_leg, rxvect->rssi_leg,
				rxvect->rssi_leg, rxvect->rssi_leg };

		for_each_set_bit (chain, &chains, IEEE80211_MAX_CHAINS) {
			*pos++ = rssis[chain];
			*pos++ = chain;
		}
	}
}

/**
 * rwnx_rx_monitor - Build radiotap header for skb an send it to netdev
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: vif that received the buffer
 * @skb: sk_buff received
 * @hw_rxhdr_ptr: Pointer to HW RX header
 * @rtap_len: Radiotap Header length
 *
 * Add radiotap header to the receved skb and send it to netdev
 */
int rwnx_rx_monitor(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
		    struct sk_buff *skb, struct hw_rxhdr *hw_rxhdr_ptr,
		    u8 rtap_len)
{
	skb->dev = rwnx_vif->ndev;

	if (rwnx_vif->wdev.iftype != NL80211_IFTYPE_MONITOR) {
		netdev_err(rwnx_vif->ndev, "not a monitor vif\n");
		return -1;
	}

	/* Add RadioTap Header */
	rwnx_rx_add_rtap_hdr(rwnx_hw, skb, &hw_rxhdr_ptr->hwvect.rx_vec_1,
			     &hw_rxhdr_ptr->phy_info, &hw_rxhdr_ptr->hwvect,
			     rtap_len, 0, 0);

	skb_reset_mac_header(skb);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);

	netif_receive_skb(skb);
#if MEM_RECORED_CHECK
	/* pkt passed to upper layer */
	del_mem_record(skb, __func__, __LINE__);
#endif

	return 0;
}

/**
 * rwnx_rxdataind - Process rx buffer
 *
 * @rwnx_hw: Pointer to the object attached to the IPC structure
 *         (points to struct rwnx_hw is this case)
 * @hostid: Address of the RX descriptor
 *
 * This function is called for each buffer received by the fw
 *
 * +-----------+
 * |sw_rxhdr   |
 * +-----------+
 * |hw_rxhdr  |
 * +-----------+
 * +payload   |
 * +-----------+
 */
u8 rwnx_rxdataind(struct rwnx_hw *rwnx_hw, struct sk_buff *skb)
{
	struct wq_rx_hdr *sw_rxhdr;
	struct hw_rxhdr *hw_rxhdr;
	struct rxdesc_tag_wq *rxdesc;
	struct rwnx_vif *rwnx_vif;
	int msdu_offset = sizeof(struct hw_rxhdr);
	u16 status;
	u32 frame_len;
	u8 monitor_skb_need_free = 0;

	PROFILING_SET(SW_PROF_RX_DATA_IND);

	atomic_inc(&rwnx_hw->ipc_rx_pkt_cnt);
	if (!skb->len) {
		/* free zero length rx packet used for tx credit report */
		dev_kfree_skb(skb);
		goto end;
	}

	rxdesc = (struct rxdesc_tag_wq *)skb->data;
	sw_rxhdr = &rxdesc->sw_rxhdr;
	status = sw_rxhdr->status;

	skb_pull(skb, sizeof(struct wq_rx_hdr));
	hw_rxhdr = (struct hw_rxhdr *)skb->data;
	frame_len = hw_rxhdr->hwvect.frmlen;

	WQ_DBG(DM_RX, DL_INF,
	       "%s: skb=0x%p, status: 0x%x, is_ampdu: %u, tid: %u, sn:%u, sta_idx: %u, is_80211_mpdu: %u\n",
	       __func__, skb, sw_rxhdr->status, sw_rxhdr->is_ampdu,
	       sw_rxhdr->tid, sw_rxhdr->sn, hw_rxhdr->flags_sta_idx,
	       hw_rxhdr->flags_is_80211_mpdu);

	/* Check if we need to delete the buffer */
	if (status & RX_STAT_DELETE) {
		WQ_DBG(DM_RX, DL_INF, "%s, RX_STAT_DELETE !!\n", __func__);
		/* Free the buffer */
		dev_kfree_skb(skb);
		goto end;
	}

	/* Check if we need to forward the buffer coming from a monitor interface */
	if (status & RX_STAT_MONITOR) {
		struct sk_buff *skb_monitor;
		struct hw_rxhdr hw_rxhdr_copy;
		u8 rtap_len;
		u16 frm_len;

		WQ_DBG(DM_RX, DL_INF, "%s, RX_STAT_MONITOR !!\n", __func__);

		//Check if monitor interface exists and is open
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, rwnx_hw->monitor_vif);
		if (!rwnx_vif) {
			monitor_skb_need_free = 1;
			dev_err_ratelimited(rwnx_hw->dev,
				"Received monitor frame but there is no monitor interface open\n");
			goto check_len_update;
		}

		hw_rxhdr = (struct hw_rxhdr *)skb->data;
		rwnx_rx_vector_convert(rwnx_hw->machw_type,
				       &hw_rxhdr->hwvect.rx_vec_1,
				       &hw_rxhdr->hwvect.rx_vec_2);
		rtap_len =
			rwnx_rx_rtap_hdrlen(&hw_rxhdr->hwvect.rx_vec_1, false);

		// Move skb->data pointer to MAC Header or Ethernet header
		skb->data += msdu_offset;

		//Save frame length
		frm_len = le32_to_cpu(hw_rxhdr->hwvect.frmlen);

		// Reserve space for frame
		skb->len = frm_len;

		if (status == RX_STAT_MONITOR) {
			atomic_inc(&rwnx_hw->rx_packets);
			atomic_add(skb->len, &rwnx_hw->rx_bytes);

			/* Update statistics */
			rwnx_vif->net_stats.rx_packets++;
			rwnx_vif->net_stats.rx_bytes += skb->len;

			//Check if there is enough space to add the radiotap header
			if (skb_headroom(skb) > rtap_len) {
				skb_monitor = skb;

				//Duplicate the HW Rx Header to override with the radiotap header
				memcpy(&hw_rxhdr_copy, hw_rxhdr,
				       sizeof(hw_rxhdr_copy));

				hw_rxhdr = &hw_rxhdr_copy;
			} else {
				//Duplicate the skb and extend the headroom
				skb_monitor = skb_copy_expand(skb, rtap_len, 0,
							      GFP_ATOMIC);

				//Reset original skb->data pointer
				skb->data = (void *)hw_rxhdr;
			}
		} else {
#ifdef CONFIG_RWNX_MON_DATA
			// Check if MSDU
			if (!hw_rxhdr->flags_is_80211_mpdu) {
				// MSDU
				//Extract MAC header
				u16 machdr_len =
					hw_rxhdr->mac_hdr_backup.buf_len;
				u8 *machdr_ptr =
					hw_rxhdr->mac_hdr_backup.buffer;

				//Pull Ethernet header from skb
				skb_pull(skb, sizeof(struct ethhdr));

				// Copy skb and extend for adding the radiotap header and the MAC header
				skb_monitor =
					skb_copy_expand(skb,
							rtap_len + machdr_len,
							0, GFP_ATOMIC);

				//Reserve space for the MAC Header
				skb_push(skb_monitor, machdr_len);

				//Copy MAC Header
				memcpy(skb_monitor->data, machdr_ptr,
				       machdr_len);

				//Update frame length
				frm_len += machdr_len - sizeof(struct ethhdr);
			} else {
				// MPDU
				skb_monitor = skb_copy_expand(skb, rtap_len, 0,
							      GFP_ATOMIC);
			}

			//Reset original skb->data pointer
			skb->data = (void *)hw_rxhdr;
#else
			//Reset original skb->data pointer
			skb->data = (void *)hw_rxhdr;

			wiphy_err(
				rwnx_hw->wiphy,
				"RX status %d is invalid when MON_DATA is disabled\n",
				status);
			goto check_len_update;
#endif
		}

		skb_reset_tail_pointer(skb);
		skb->len = 0;
		skb_reset_tail_pointer(skb_monitor);
		skb_monitor->len = 0;

		skb_put(skb_monitor, frm_len);
		if (rwnx_rx_monitor(rwnx_hw, rwnx_vif, skb_monitor, hw_rxhdr,
				    rtap_len))
			dev_kfree_skb(skb_monitor);

		if (status == RX_STAT_MONITOR) {
			status |= RX_STAT_ALLOC;
			if (skb_monitor != skb) {
				dev_kfree_skb(skb);
			}
		}
	}

check_len_update:
	if (status == RX_STAT_MONITOR) {
		if (monitor_skb_need_free) {
			dev_kfree_skb(skb);
			goto end;
		}
	}

	/* Check if we need to update the length */
	if (status & RX_STAT_LEN_UPDATE) {
		WQ_DBG(DM_RX, DL_INF, "%s, RX_STAT_LEN_UPDATE !!\n", __func__);
		hw_rxhdr = (struct hw_rxhdr *)skb->data;
		hw_rxhdr->hwvect.frmlen = frame_len;

		if (status & RX_STAT_ETH_LEN_UPDATE) {
			/* Update Length Field inside the Ethernet Header */
			struct ethhdr *hdr =
				(struct ethhdr *)((u8 *)hw_rxhdr + msdu_offset);
			hdr->h_proto = htons(frame_len - sizeof(struct ethhdr));
		}

		dev_kfree_skb(skb);
		goto end;
	}

	/* Check if it must be discarded after informing upper layer */
	if (status & RX_STAT_SPURIOUS) {
		struct ieee80211_hdr *hdr;
		WQ_DBG(DM_RX, DL_INF, "%s, RX_STAT_SPURIOUS !!\n", __func__);
		hw_rxhdr = (struct hw_rxhdr *)skb->data;
		hdr = (struct ieee80211_hdr *)(skb->data + msdu_offset);
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, hw_rxhdr->flags_vif_idx);
		if (rwnx_vif) {
			cfg80211_rx_spurious_frame(rwnx_vif->ndev, hdr->addr2,
						   GFP_ATOMIC);
		}

		dev_kfree_skb(skb);
		goto end;
	}

	/* Check if we need to forward the buffer */
	if (status & RX_STAT_FORWARD) {
		struct rwnx_sta *sta = NULL;
		WQ_DBG(DM_RX, DL_INF, "%s, RX_STAT_FORWARD !! %d\n", __func__,
		       __LINE__);
		hw_rxhdr = (struct hw_rxhdr *)skb->data;
		rwnx_rx_vector_convert(rwnx_hw->machw_type,
				       &hw_rxhdr->hwvect.rx_vec_1,
				       &hw_rxhdr->hwvect.rx_vec_2);

		skb_pull(skb, msdu_offset);

		WQ_DBG(DM_RX, DL_INF,
		       "%s: flags_sta_idx: %d, flags_is_80211_mpdu: %d, sw_rxhdr->is_ampdu=%d, skb=0x%p\n",
		       __func__, hw_rxhdr->flags_sta_idx,
		       hw_rxhdr->flags_is_80211_mpdu, sw_rxhdr->is_ampdu, skb);

		if (hw_rxhdr->flags_sta_idx != RWNX_INVALID_STA) {
			sta = &rwnx_hw->sta_table[hw_rxhdr->flags_sta_idx];
			rwnx_rx_statistic(rwnx_hw, hw_rxhdr, sta);
		}

		if (hw_rxhdr->flags_is_80211_mpdu) {
			// LL: mgmt frame will come to here
			struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;

			if(ieee80211_is_ctl(mgmt->frame_control)) {
				rwnx_rx_cntrl_any(rwnx_hw, skb, hw_rxhdr);
			} else {
				rwnx_rx_mgmt_any(rwnx_hw, skb, hw_rxhdr);
			}
		} else {
			int ret;
			u64 time_start_us = 0, time_end_us = 0;

			rwnx_vif = rwnx_rx_get_vif(rwnx_hw,
						   hw_rxhdr->flags_vif_idx);

			if (!rwnx_vif) {
				dev_err(rwnx_hw->dev,
					"Frame received but no active vif (%d)",
					hw_rxhdr->flags_vif_idx);
				dev_kfree_skb(skb);
				goto check_alloc;
			}

			if (sta) {
				if (sta->vlan_idx != rwnx_vif->vif_index) {
					rwnx_vif =
						rwnx_hw->vif_table[sta->vlan_idx];
					if (!rwnx_vif) {
						dev_kfree_skb(skb);
						goto check_alloc;
					}
				}

				if (hw_rxhdr->flags_is_4addr &&
				    !rwnx_vif->use_4addr) {
					cfg80211_rx_unexpected_4addr_frame(
						rwnx_vif->ndev, sta->mac_addr,
						GFP_ATOMIC);
				}
			}

			skb->priority = 256 + hw_rxhdr->flags_user_prio;

			time_start_us = (u64)ktime_to_us(ktime_get());

			ret = sw_rxhdr->is_ampdu && ieee80211_ampdu_reorder(
						rwnx_hw, hw_rxhdr->flags_sta_idx,
						sw_rxhdr->tid, sw_rxhdr->sn,
						sw_rxhdr->msdu_seq, sw_rxhdr->msdu_seq_end,
						skb);

			time_end_us = (u64)ktime_to_us(ktime_get());
			atomic_add((u32)(time_end_us - time_start_us), &rwnx_hw->rx_reorder_time);

			if (ret) {
				goto check_alloc;
			}
#ifdef NAPI_SUPPORT
			else {
				/* It looks like that in the current design,
				   napi_schedule will be done when receiving
				   AMPDU packet. When operating at legacy mode
				   or do 4 way handshake, the packet might
				   not be the AMPDU. In order to avoid the
				    problem, we add napi_schedule */
				if (rwnx_hw->core->hif_ops->hif == WQ_HIF_USB &&
				    rwnx_hw->napi_param.napi_enable)
					napi_schedule(&rwnx_hw->napi_rx);
			}
#endif

			rwnx_rx_data_skb(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
		}
	}

check_alloc:
#if 0
    /* Check if we need to allocate a new buffer */
    if (status & RX_STAT_ALLOC) {
        printk("%s, RX_STAT_ALLOC !! %d\n", __func__, __LINE__);
    }
#endif

	if (!(status & (RX_STAT_FORWARD | RX_STAT_SPURIOUS | RX_STAT_LEN_UPDATE | RX_STAT_MONITOR))) {
			WQ_DBG(DM_RX, DL_ERR, "[auto]ERROR: %s Invalid data frame, status 0x%x !\n", __func__, status);
			dev_kfree_skb(skb);
	}

end:
	PROFILING_CLR(SW_PROF_RX_DATA_IND);

	return 0;
}

/**
 * rwnx_rx_deferred - Work function to defer processing of buffer that cannot be
 * done in rwnx_rxdataind (that is called in atomic context)
 *
 * @ws: work field within struct rwnx_defer_rx
 */
void rwnx_rx_deferred(struct work_struct *ws)
{
	struct rwnx_defer_rx *rx = container_of(ws, struct rwnx_defer_rx, work);
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&rx->sk_list)) != NULL) {
		// Currently only management frame can be deferred
		struct ieee80211_mgmt *mgmt =
			(struct ieee80211_mgmt *)skb->data;
		struct rwnx_defer_rx_cb *rx_cb =
			(struct rwnx_defer_rx_cb *)skb->cb;

		if (ieee80211_is_action(mgmt->frame_control) &&
		    (mgmt->u.action.category == 6)) {
			struct cfg80211_ft_event_params ft_event;
			struct rwnx_vif *vif = rx_cb->vif;
			u8 *action_frame = (u8 *)&mgmt->u.action;
			u8 action_code = action_frame[1];
			u16 status_code =
				*((u16 *)&action_frame[2 + 2 * ETH_ALEN]);

			if ((action_code == 2) && (status_code == 0)) {
				ft_event.target_ap =
					action_frame + 2 + ETH_ALEN;
				ft_event.ies =
					action_frame + 2 + 2 * ETH_ALEN + 2;
				ft_event.ies_len =
					skb->len - (ft_event.ies - (u8 *)mgmt);
				ft_event.ric_ies = NULL;
				ft_event.ric_ies_len = 0;
				cfg80211_ft_event(rx_cb->vif->ndev, &ft_event);
				vif->sta.flags |= RWNX_STA_FT_OVER_DS;
				memcpy(vif->sta.ft_target_ap,
				       ft_event.target_ap, ETH_ALEN);
			}
		} else if (ieee80211_is_auth(mgmt->frame_control)) {
			struct cfg80211_ft_event_params ft_event;
			struct rwnx_vif *vif = rx_cb->vif;
			ft_event.target_ap = vif->sta.ft_target_ap;
			ft_event.ies = mgmt->u.auth.variable;
			ft_event.ies_len =
				(skb->len - offsetof(struct ieee80211_mgmt,
						     u.auth.variable));
			ft_event.ric_ies = NULL;
			ft_event.ric_ies_len = 0;
			cfg80211_ft_event(rx_cb->vif->ndev, &ft_event);
			vif->sta.flags |= RWNX_STA_FT_OVER_AIR;
		} else {
			netdev_warn(rx_cb->vif->ndev,
				    "Unexpected deferred frame fctl=0x%04x",
				    mgmt->frame_control);
		}

		dev_kfree_skb(skb);
	}
}
