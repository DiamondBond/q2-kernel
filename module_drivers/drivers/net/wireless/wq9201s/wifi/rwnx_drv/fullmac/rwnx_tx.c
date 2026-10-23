/**
 ******************************************************************************
 *
 * @file rwnx_tx.c
 *
 * Copyright (C) RivieraWaves 2012-2020
 *
 ******************************************************************************
 */
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <net/sock.h>
#include <linux/kernel.h>

#include "rwnx_defs.h"
#include "rwnx_tx.h"
#include "rwnx_msg_tx.h"
#include "rwnx_mesh.h"
#include "rwnx_events.h"
#include "rwnx_compat.h"

#include "core.h"
#include "wq_profiling.h"
#include "wq_log.h"
#include "wq_ipc.h"
#include "wq_pkt_classify.h"
#include "hif_api.h"
#include "ieee80211_extap.h"

#define MSDU_IN_HOST_THRESHOLD (sizeof(struct txdesc_host) + 20)
#define AP_MGT_TXDONE_MAX_WAIT (100)
/******************************************************************************
 * Power Save functions
 *****************************************************************************/
/**
 * rwnx_set_traffic_status - Inform FW if traffic is available for STA in PS
 *
 * @rwnx_hw: Driver main data
 * @sta: Sta in PS mode
 * @available: whether traffic is buffered for the STA
 * @ps_id: type of PS data requested (@LEGACY_PS_ID or @UAPSD_ID)
  */
void rwnx_set_traffic_status(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
			     bool available, u8 ps_id)
{
	if (sta->tdls.active) {
		rwnx_send_tdls_peer_traffic_ind_req(
			rwnx_hw, rwnx_hw->vif_table[sta->vif_idx]);
	} else {
		bool uapsd = (ps_id != LEGACY_PS_ID);
		rwnx_send_me_traffic_ind(rwnx_hw, sta->sta_idx, uapsd,
					 available);
		trace_ps_traffic_update(sta->sta_idx, available, uapsd);
	}
}

/**
 * rwnx_ps_bh_enable - Enable/disable PS mode for one STA
 *
 * @rwnx_hw: Driver main data
 * @sta: Sta which enters/leaves PS mode
 * @enable: PS mode status
 *
 * This function will enable/disable PS mode for one STA.
 * When enabling PS mode:
 *  - Stop all STA's txq for RWNX_TXQ_STOP_STA_PS reason
 *  - Count how many buffers are already ready for this STA
 *  - For BC/MC sta, update all queued SKB to use hw_queue BCMC
 *  - Update TIM if some packet are ready
 *
 * When disabling PS mode:
 *  - Start all STA's txq for RWNX_TXQ_STOP_STA_PS reason
 *  - For BC/MC sta, update all queued SKB to use hw_queue AC_BE
 *  - Update TIM if some packet are ready (otherwise fw will not update TIM
 *    in beacon for this STA)
 *
 * All counter/skb updates are protected from TX path by taking tx_lock
 *
 * NOTE: _bh_ in function name indicates that this function is called
 * from a bottom_half tasklet.
 */
void rwnx_ps_bh_enable(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
		       bool enable)
{
	struct rwnx_txq *txq;

	if (enable) {
		trace_ps_enable(sta);

		spin_lock(&rwnx_hw->tx_lock);
		sta->ps.sp_cnt[LEGACY_PS_ID] = 0;
		sta->ps.sp_cnt[UAPSD_ID] = 0;

		if (is_multicast_sta(sta->sta_idx)) {
			txq = rwnx_txq_sta_get(sta, 0, rwnx_hw);
			sta->ps.pkt_ready[LEGACY_PS_ID] =
				(skb_queue_len(&txq->sk_list) +
				 skb_queue_len(&txq->sk_ack_list));
			sta->ps.pkt_ready[UAPSD_ID] = 0;
			txq->hwq = &rwnx_hw->hwq[RWNX_HWQ_BCMC];
		} else {
			int i;
			sta->ps.active = true;
			sta->ps.pkt_ready[LEGACY_PS_ID] = 0;
			sta->ps.pkt_ready[UAPSD_ID] = 0;
			foreach_sta_txq(sta, txq, i, rwnx_hw)
			{
				sta->ps.pkt_ready[txq->ps_id] +=
					(skb_queue_len(&txq->sk_list) +
					 skb_queue_len(&txq->sk_ack_list));
			}
			rwnx_txq_sta_stop(sta, RWNX_TXQ_STOP_STA_PS, rwnx_hw);
		}

		spin_unlock(&rwnx_hw->tx_lock);

		if (sta->ps.pkt_ready[LEGACY_PS_ID])
			rwnx_set_traffic_status(rwnx_hw, sta, true,
						LEGACY_PS_ID);

		if (sta->ps.pkt_ready[UAPSD_ID])
			rwnx_set_traffic_status(rwnx_hw, sta, true, UAPSD_ID);
	} else {
		trace_ps_disable(sta->sta_idx);

		spin_lock(&rwnx_hw->tx_lock);
		sta->ps.active = false;

		if (is_multicast_sta(sta->sta_idx)) {
			txq = rwnx_txq_sta_get(sta, 0, rwnx_hw);
			txq->hwq = &rwnx_hw->hwq[RWNX_HWQ_BE];
			txq->push_limit = 0;
		} else {
			int i;
			foreach_sta_txq(sta, txq, i, rwnx_hw)
			{
				txq->push_limit = 0;
			}
		}

		rwnx_txq_sta_start(sta, RWNX_TXQ_STOP_STA_PS, rwnx_hw);
		spin_unlock(&rwnx_hw->tx_lock);

		if (sta->ps.pkt_ready[LEGACY_PS_ID])
			rwnx_set_traffic_status(rwnx_hw, sta, false,
						LEGACY_PS_ID);

		if (sta->ps.pkt_ready[UAPSD_ID])
			rwnx_set_traffic_status(rwnx_hw, sta, false, UAPSD_ID);
	}
}

/**
 * rwnx_ps_bh_traffic_req - Handle traffic request for STA in PS mode
 *
 * @rwnx_hw: Driver main data
 * @sta: Sta which enters/leaves PS mode
 * @pkt_req: number of pkt to push
 * @ps_id: type of PS data requested (@LEGACY_PS_ID or @UAPSD_ID)
 *
 * This function will make sure that @pkt_req are pushed to fw
 * whereas the STA is in PS mode.
 * If request is 0, send all traffic
 * If request is greater than available pkt, reduce request
 * Note: request will also be reduce if txq credits are not available
 *
 * All counter updates are protected from TX path by taking tx_lock
 *
 * NOTE: _bh_ in function name indicates that this function is called
 * from the bottom_half tasklet.
 */
void rwnx_ps_bh_traffic_req(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
			    u16 pkt_req, u8 ps_id)
{
	int pkt_ready_all;
	struct rwnx_txq *txq;

    //Todo: if add OppPS func, delete the first if
    if((pkt_req != PS_SP_INTERRUPTED) || (ps_id != UAPSD_ID)) {
    	if (WARN(!sta->ps.active, "sta %pM is not in Power Save mode",
    		 sta->mac_addr))
    		return;
    }
	trace_ps_traffic_req(sta, pkt_req, ps_id);

	spin_lock(&rwnx_hw->tx_lock);

	/* Fw may ask to stop a service period with PS_SP_INTERRUPTED. This only
       happens for p2p-go interface if NOA starts during a service period */
	if ((pkt_req == PS_SP_INTERRUPTED) && (ps_id == UAPSD_ID)) {
		int tid;
		sta->ps.sp_cnt[ps_id] = 0;
		foreach_sta_txq(sta, txq, tid, rwnx_hw)
		{
			txq->push_limit = 0;
		}
		goto done;
	}

	pkt_ready_all = (sta->ps.pkt_ready[ps_id] - sta->ps.sp_cnt[ps_id]);

	/* Don't start SP until previous one is finished or we don't have
       packet ready (which must not happen for U-APSD) */
	if (sta->ps.sp_cnt[ps_id] || pkt_ready_all <= 0) {
		goto done;
	}

	/* Adapt request to what is available. */
	if (pkt_req == 0 || pkt_req > pkt_ready_all) {
		pkt_req = pkt_ready_all;
	}

	/* Reset the SP counter */
	sta->ps.sp_cnt[ps_id] = 0;

	/* "dispatch" the request between txq */
	if (is_multicast_sta(sta->sta_idx)) {
		txq = rwnx_txq_sta_get(sta, 0, rwnx_hw);
		if (txq->credits <= 0)
			goto done;
		if (pkt_req > txq->credits)
			pkt_req = txq->credits;
		txq->push_limit = pkt_req;
		sta->ps.sp_cnt[ps_id] = pkt_req;
		rwnx_txq_add_to_hw_list(txq);
	} else {
		int i, tid;

		foreach_sta_txq_prio(sta, txq, tid, i, rwnx_hw)
		{
			u16 txq_len;

			tid = nx_tid_prio[i];
			txq = rwnx_txq_sta_get(sta, tid, rwnx_hw);
			txq_len = skb_queue_len(&txq->sk_list) +
				  skb_queue_len(&txq->sk_ack_list);
			if (txq->ps_id != ps_id)
				continue;

			if (txq_len > txq->credits)
				txq_len = txq->credits;

			if (txq_len == 0)
				continue;

			if (txq_len < pkt_req) {
				/* Not enough pkt queued in this txq, add this
                   txq to hwq list and process next txq */
				pkt_req -= txq_len;
				txq->push_limit = txq_len;
				sta->ps.sp_cnt[ps_id] += txq_len;
				rwnx_txq_add_to_hw_list(txq);
			} else {
				/* Enough pkt in this txq to comlete the request
                   add this txq to hwq list and stop processing txq */
				txq->push_limit = pkt_req;
				sta->ps.sp_cnt[ps_id] += pkt_req;
				rwnx_txq_add_to_hw_list(txq);
				break;
			}
		}
	}

done:
	spin_unlock(&rwnx_hw->tx_lock);
}

/******************************************************************************
 * TX functions
 *****************************************************************************/
#define PRIO_STA_NULL 0xAA

static const int rwnx_down_hwq2tid[3] = {
	[RWNX_HWQ_BK] = 2,
	[RWNX_HWQ_BE] = 3,
	[RWNX_HWQ_VI] = 5,
};

static void rwnx_downgrade_ac(struct rwnx_sta *sta, struct sk_buff *skb)
{
	int8_t ac = rwnx_tid2hwq[skb->priority];

	if (WARN((ac > RWNX_HWQ_VO),
		 "Unexepcted ac %d for skb before downgrade", ac))
		ac = RWNX_HWQ_VO;

	while (sta->acm & BIT(ac)) {
		if (ac == RWNX_HWQ_BK) {
			skb->priority = 1;
			return;
		}
		ac--;
		skb->priority = rwnx_down_hwq2tid[ac];
	}
}

__maybe_unused static void rwnx_tx_statistic(struct rwnx_hw *rwnx_hw,
					     struct rwnx_txq *txq,
					     union rwnx_hw_txstatus rwnx_txst,
					     unsigned int data_len)
{
	struct rwnx_sta *sta = txq->sta;
	if (!sta || !rwnx_txst.acknowledged)
		return;

	sta->stats.tx_pkts++;
	sta->stats.tx_bytes += data_len;
	sta->stats.last_act = rwnx_hw->stats.last_tx;
}

u16 rwnx_select_txq(struct rwnx_vif *rwnx_vif, struct sk_buff *skb)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct wireless_dev *wdev = &rwnx_vif->wdev;
	struct rwnx_sta *sta = NULL;
	struct rwnx_txq *txq;
	u16 netdev_queue;
	bool tdls_mgmgt_frame = false;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT: {
		struct ethhdr *eth;
		eth = (struct ethhdr *)skb->data;
		if (eth->h_proto == cpu_to_be16(ETH_P_TDLS)) {
			tdls_mgmgt_frame = true;
		}
		if ((rwnx_vif->tdls_status == TDLS_LINK_ACTIVE) &&
		    (rwnx_vif->sta.tdls_sta != NULL) &&
		    (memcmp(eth->h_dest, rwnx_vif->sta.tdls_sta->mac_addr,
			    ETH_ALEN) == 0))
			sta = rwnx_vif->sta.tdls_sta;
		else
			sta = rwnx_vif->sta.ap;
		break;
	}
	case NL80211_IFTYPE_AP_VLAN:
		if (rwnx_vif->ap_vlan.sta_4a) {
			sta = rwnx_vif->ap_vlan.sta_4a;
			break;
		}

		/* AP_VLAN interface is not used for a 4A STA,
           fallback searching sta amongs all AP's clients */
		rwnx_vif = rwnx_vif->ap_vlan.master;
		fallthrough;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO: {
		struct rwnx_sta *cur;
		struct ethhdr *eth = (struct ethhdr *)skb->data;

		if (is_multicast_ether_addr(eth->h_dest)) {
			sta = &rwnx_hw->sta_table[rwnx_vif->ap.bcmc_index];
		} else {
			list_for_each_entry (cur, &rwnx_vif->ap.sta_list,
					     list) {
				if (!memcmp(cur->mac_addr, eth->h_dest,
					    ETH_ALEN)) {
					sta = cur;
					break;
				}
			}
		}

		break;
	}
	case NL80211_IFTYPE_MESH_POINT: {
		struct ethhdr *eth = (struct ethhdr *)skb->data;

		if (!rwnx_vif->is_resending) {
			/*
             * If ethernet source address is not the address of a mesh wireless interface, we are proxy for
             * this address and have to inform the HW
             */
			if (memcmp(&eth->h_source[0],
				   &rwnx_vif->ndev->perm_addr[0], ETH_ALEN)) {
				/* Check if LMAC is already informed */
				if (!rwnx_get_mesh_proxy_info(
					    rwnx_vif, (u8 *)&eth->h_source,
					    true)) {
					rwnx_send_mesh_proxy_add_req(
						rwnx_hw, rwnx_vif,
						(u8 *)&eth->h_source);
				}
			}
		}

		if (is_multicast_ether_addr(eth->h_dest)) {
			sta = &rwnx_hw->sta_table[rwnx_vif->ap.bcmc_index];
		} else {
			/* Path to be used */
			struct rwnx_mesh_path *p_mesh_path = NULL;
			struct rwnx_mesh_path *p_cur_path;
			/* Check if destination is proxied by a peer Mesh STA */
			struct rwnx_mesh_proxy *p_mesh_proxy =
				rwnx_get_mesh_proxy_info(
					rwnx_vif, (u8 *)&eth->h_dest, false);
			/* Mesh Target address */
			struct mac_addr *p_tgt_mac_addr;

			if (p_mesh_proxy) {
				p_tgt_mac_addr = &p_mesh_proxy->proxy_addr;
			} else {
				p_tgt_mac_addr =
					(struct mac_addr *)&eth->h_dest;
			}

			/* Look for path with provided target address */
			list_for_each_entry (p_cur_path,
					     &rwnx_vif->ap.mpath_list, list) {
				if (!memcmp(&p_cur_path->tgt_mac_addr,
					    p_tgt_mac_addr, ETH_ALEN)) {
					p_mesh_path = p_cur_path;
					break;
				}
			}

			if (p_mesh_path) {
				sta = p_mesh_path->nhop_sta;
			} else {
				rwnx_send_mesh_path_create_req(
					rwnx_hw, rwnx_vif,
					(u8 *)p_tgt_mac_addr);
			}
		}

		break;
	}
	default:
		break;
	}

	if (sta && sta->qos) {
		if (tdls_mgmgt_frame) {
			skb_set_queue_mapping(skb,
					      NX_STA_NDEV_IDX(skb->priority,
							      sta->sta_idx));
		} else {
			/* use the data classifier to determine what 802.1d tag the
             * data frame has */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
			skb->priority = cfg80211_classify8021d(skb, NULL) &
					IEEE80211_QOS_CTL_TAG1D_MASK;
#else
			skb->priority = cfg80211_classify8021d(skb) &
					IEEE80211_QOS_CTL_TAG1D_MASK;
#endif
		}
		if (sta->acm)
			rwnx_downgrade_ac(sta, skb);

		txq = rwnx_txq_sta_get(sta, skb->priority, rwnx_hw);
		netdev_queue = txq->ndev_idx;
	} else if (sta) {
		skb->priority = 0xFF;
		txq = rwnx_txq_sta_get(sta, 0, rwnx_hw);
		netdev_queue = txq->ndev_idx;
	} else {
		/* This packet will be dropped in xmit function, still need to select
           an active queue for xmit to be called. As it most likely to happen
           for AP interface, select BCMC queue
           (TODO: select another queue if BCMC queue is stopped) */
		skb->priority = PRIO_STA_NULL;
		netdev_queue = NX_BCMC_TXQ_NDEV_IDX;
	}

	BUG_ON(netdev_queue >= NX_NB_NDEV_TXQ);

	return netdev_queue;
}

/**
 * rwnx_get_tx_info - Get STA and tid for one skb
 *
 * @rwnx_vif: vif ptr
 * @skb: skb
 * @tid: pointer updated with the tid to use for this skb
 *
 * @return: pointer on the destination STA (may be NULL)
 *
 * skb has already been parsed in rwnx_select_queue function
 * simply re-read information form skb.
 */
static struct rwnx_sta *rwnx_get_tx_info(struct rwnx_vif *rwnx_vif,
					 struct sk_buff *skb, u8 *tid)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_sta *sta;
	int sta_idx;

	*tid = skb->priority;
	if (unlikely(skb->priority == PRIO_STA_NULL)) {
		return NULL;
	} else {
		int ndev_idx = skb_get_queue_mapping(skb);

		if (ndev_idx == NX_BCMC_TXQ_NDEV_IDX) {
			sta_idx = NX_REMOTE_STA_MAX + master_vif_idx(rwnx_vif);
			*tid = 0xFF;
		} else
			sta_idx = ndev_idx / NX_NB_TID_PER_STA;

		if (sta_idx >= NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX) {
			WQ_DBG(DM_TX, DL_ERR,
			       "%s: ndev_idx:%d, staidx:%d, stamax:%d, devmax:%d ",
			       __func__, ndev_idx, sta_idx, NX_REMOTE_STA_MAX,
			       NX_VIRT_DEV_MAX);
			BUG_ON(1);
		}

		sta = &rwnx_hw->sta_table[sta_idx];
	}

	return sta;
}

/**
 *  rwnx_tx_push - Push one packet to fw
 *
 * @rwnx_hw: Driver main data
 * @txhdr: tx desc of the buffer to push
 * @flags: push flags (see @rwnx_push_flags)
 *
 * Push one packet to fw. Sw desc of the packet has already been updated.
 * Only MORE_DATA flag will be set if needed.
 */
void rwnx_tx_push_prepare(struct rwnx_hw *rwnx_hw,
			  struct txdesc_host *txdesc_host, struct rwnx_txq *txq)
{
	struct rwnx_sta *sta = &rwnx_hw->sta_table[txdesc_host->api.host.staid];
	struct rwnx_vif *vif =
		rwnx_hw->vif_table[txdesc_host->api.host.vif_idx];

	if (unlikely(sta->ps.active)) {
		sta->ps.pkt_ready[txq->ps_id]--;
		sta->ps.sp_cnt[txq->ps_id]--;

		trace_ps_push(sta);

		if (((txq->ps_id == UAPSD_ID) ||
		     (vif->wdev.iftype == NL80211_IFTYPE_MESH_POINT) ||
		     (sta->tdls.active)) &&
		    !sta->ps.sp_cnt[txq->ps_id]) {
			txdesc_host->api.host.flags |= TXU_CNTRL_EOSP;
		}

		if (sta->ps.pkt_ready[txq->ps_id]) {
			txdesc_host->api.host.flags |= TXU_CNTRL_MORE_DATA;
		} else {
			rwnx_set_traffic_status(rwnx_hw, sta, false,
						txq->ps_id);
		}
		txdesc_host->api.host.ext_flags |= TXU_EXT_CNTRL_PS_TX;
	} else {
		if (sta->ps.pkt_ready[txq->ps_id])
			sta->ps.pkt_ready[txq->ps_id]--;
	}

	if (txq->push_limit)
		txq->push_limit--;
}

void rwnx_tx_refill_hostdesc(struct sk_buff_head *skbq)
{
	struct sk_buff *skb;
	struct wq_skb_txcb *txcb;
	int qlen;
	struct txdesc_host *txdesc_host;
	//struct txdesc_host *txdesc_host_tmp;
	struct sk_buff *skb_walk;
	u8 skb_idx = 0;
	u32 valid_len = 0;
	struct rwnx_tx_bundle_head *bundle_head;
	struct txdesc_host txdesc_host1 = { 0 };

	BUG_ON(skbq == NULL);
	qlen = skb_queue_len(skbq);

	skb = skb_peek(skbq);
	BUG_ON(skb == NULL);

	txcb = WQ_SKB_TXCB(skb);
	txdesc_host = (struct txdesc_host *)skb->data;

	txdesc_host->api.host.packet_len[1] = qlen;
	if (qlen > 1) {
		txdesc_host->api.host.ext_flags |= TXU_EXT_CNTRL_HOST_BUNDLE;
	}

	skb_queue_walk(skbq, skb_walk) {
		if (skb_idx == 0) {
			skb_idx++;
			valid_len += skb_walk->len - HEADROOM_TXDESC;
			continue;
		}
		memcpy((u8 *)&txdesc_host1, skb_walk->data, HEADROOM_TXDESC);
		skb_push(skb_walk, BUNDLE_HDR_LEN);
		bundle_head = (struct rwnx_tx_bundle_head *)skb_pull(skb_walk, HEADROOM_TXDESC);
		bundle_head->pattern = 0xA5A5;
		bundle_head->index = skb_idx;
		bundle_head->skb_len = skb_walk->len - sizeof(struct rwnx_tx_bundle_head);
		bundle_head->total_bundle_len = ALIGN(bundle_head->skb_len, sizeof(u32));
		skb_idx++;
		valid_len += bundle_head->skb_len;
		memcpy((u8 *)skb_walk->data - HEADROOM_TXDESC, (u8 *)&txdesc_host1, HEADROOM_TXDESC);
	}
	txdesc_host->api.host.packet_len[2] = valid_len;
}

#ifdef CONFIG_RWNX_AMSDUS_TX
static inline bool rwnx_amsdu_is_aggregable(struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *)skb->data;

	/* only TCP ACK frame is allowed to form AMSDU */
	if (ntohs(eth->h_proto) == ETH_P_IP) {
		struct iphdr *ip_hdr = (struct iphdr *)(eth + 1);

		if (ip_hdr->protocol == IPPROTO_TCP &&
		    skb->len < AMSDU_MAX_MSDU_LEN &&
		    !is_multicast_ether_addr(eth->h_dest))
			return true;
	}

	return false;
}

netdev_tx_t rwnx_prepare_xmit(struct sk_buff *skb, struct rwnx_vif *rwnx_vif,
			      struct rwnx_txq *txq, struct rwnx_sta *sta,
			      u8 tid, bool amsdu)
{
	struct txdesc_host *txdesc_host;
	struct hostdesc *host;
	struct ethhdr *eth;
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);
	u8 bundle_num = 0;
	u16 bundle_len = 0;

	if (rwnx_hw->core->hif_ops->hif != WQ_HIF_PCIE && amsdu) {
		bundle_num = txcb->bundle_num;
		bundle_len = txcb->bundle_len;
	}

	/* Save pointer to the Ethernet header */
	eth = (struct ethhdr *)skb->data;

	if (rwnx_vif->extAP_supp) {
		ieee80211_extap_output(eth, rwnx_vif->ndev->dev_addr);
	}

	*txcb = (struct wq_skb_txcb){
		.jiffies = jiffies,
		.pkt_cls = wq_pkt_classify(skb, 1, true),
		.txq_idx = txq - rwnx_hw->txq,
		.is_small_pkt = 0,
	};

	/* Use headroom to store struct txdesc_host */
	txdesc_host = (void *)skb_push(skb, sizeof(*txdesc_host));
	memset(txdesc_host, 0, sizeof(*txdesc_host));

	/* Fill-in the API descriptor for the MACSW */
	host = &txdesc_host->api.host;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	ether_addr_copy(host->ethhdr.h_dest, eth->h_dest);
	ether_addr_copy(host->ethhdr.h_source, eth->h_source);
#else
	(void)memcpy(host->ethhdr.h_dest, eth->h_dest, ETH_ALEN);
	(void)memcpy(host->ethhdr.h_source, eth->h_source, ETH_ALEN);
#endif
	host->ethhdr.h_proto = eth->h_proto;
	host->staid = sta->sta_idx;
	host->tid = tid;
	if (txcb->pkt_cls & (BIT(WQ_PKT_CLS_DHCP) | BIT(WQ_PKT_CLS_EAPOL) |
		BIT(WQ_PKT_CLS_ARP) | BIT(WQ_PKT_CLS_IPERF_UDP_SETUP))) {
		host->dhcp_flag = 1;
		if (rwnx_hw->core->hif_ops->hif == WQ_HIF_PCIE) {
			if (host->tid != 0xff) {
				host->tid = TID_6;
			}
		}
	}
	if (rwnx_hw->core->config.tx_bundle_max) {
		if (!(txcb->pkt_cls & (BIT(WQ_PKT_CLS_EAPOL) | BIT(WQ_PKT_CLS_DHCP) |
			BIT(WQ_PKT_CLS_ARP))) && skb->len <= MSDU_IN_HOST_THRESHOLD) {
			txcb->is_small_pkt = 1;
			host->tid = TID_6;
		}
	}

	if (unlikely(rwnx_vif->wdev.iftype == NL80211_IFTYPE_MONITOR)) {
		host->encap_type = MSDU_ENCAP_RAW;
	} else {
		host->encap_type = MSDU_ENCAP_ETH_V2;
	}

	/* host->ext_flags = 0; */
	if (txcb->pkt_cls & BIT(WQ_PKT_CLS_EAPOL_M4))
		host->ext_flags |= TXU_EXT_CNTRL_EAPOL_M4;

	if (unlikely(rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP_VLAN))
		host->vif_idx = rwnx_vif->ap_vlan.master->vif_index;
	else
		host->vif_idx = rwnx_vif->vif_index;

	/* host->flags = 0; */
	if (rwnx_vif->use_4addr && (sta->sta_idx < NX_REMOTE_STA_MAX))
		host->flags |= TXU_CNTRL_USE_4ADDR;

	if ((rwnx_vif->tdls_status == TDLS_LINK_ACTIVE) &&
	    rwnx_vif->sta.tdls_sta &&
	    ether_addr_equal(host->ethhdr.h_dest,
			     rwnx_vif->sta.tdls_sta->mac_addr)) {
		host->flags |= TXU_CNTRL_TDLS;
		rwnx_vif->sta.tdls_sta->tdls.last_tid = host->tid;
		rwnx_vif->sta.tdls_sta->tdls.last_sn = host->sn;
	}

	if ((rwnx_vif->wdev.iftype == NL80211_IFTYPE_MESH_POINT) &&
	    (rwnx_vif->is_resending))
		host->flags |= TXU_CNTRL_MESH_FWD;

	if (rwnx_hw->core->hif_ops->hif == WQ_HIF_PCIE) {
		if (amsdu)
			host->flags |= TXU_CNTRL_AMSDU_PRESENT;
	} else {
		// for usb/sdio
		if (bundle_num > 1) {
			host->ext_flags |= TXU_EXT_CNTRL_HOST_BUNDLE;
			host->packet_len[1] = bundle_num;
			host->packet_len[2] = bundle_len;
			//WQ_DBG(DM_TX, DL_ERR, "%s:: bundle_num=%d,bundle_len=%d, len=%d, headroom=%d, max_packets_num=%d\n",
			//    __func__, bundle_num, bundle_data_len, (u16)skb->len, headroom,
			//    rwnx_hw->amsdu_param.max_packets_num);
			//dump_bytes(DL_ERR, "rwnx_prepare_xmit::", (skb->data + headroom), ((u16)skb->len - headroom));
		}
	}

	host->packet_len[0] = (u16)skb->len - sizeof(*txdesc_host);
	host->packet_cnt = 1;

#ifdef CONFIG_HML
	host->is_hml = rwnx_vif->is_hml;
#endif

	if (rwnx_hw->core->config.dma_map) {
		int err = wq_tx_skb_dma_map(rwnx_hw->core, skb, txcb);

		if (err)
			return NETDEV_TX_OK;

		if (rwnx_hw->core->config.tx_bundle_max && /* for PCIe only */
			skb->len > MSDU_IN_HOST_THRESHOLD && !host->flags &&
			!host->ext_flags && /* only apply normal packet for now */
			!(txcb->pkt_cls & (BIT(WQ_PKT_CLS_EAPOL) | BIT(WQ_PKT_CLS_DHCP) |
			BIT(WQ_PKT_CLS_ARP))) && /* single pkt tx */
			/* tcp ACK use HL mode */
			!(txcb->pkt_cls & (BIT(WQ_PKT_CLS_TCP_ACK)))) {
			txcb->msdu_in_host = 1;
			host->packet_addr[0] =
				(addr32)txcb->phyaddr + HEADROOM_HIF_HTC_TXDESC;
			host->ext_flags |= TXU_EXT_CNTRL_HOST_MEM;
		}
		/* just pcie TCP ACK */
		if (rwnx_hw->core->config.tx_bundle_max &&
			txcb->pkt_cls & (BIT(WQ_PKT_CLS_TCP_ACK))) {
			host->packet_len[1] =
				ALIGN(host->packet_len[0], sizeof(u32));
		}
	}

	/* for PCIe, use flow control instead of tx credit */
	host->via_grp_id = TX_CREDIT_GROUP_DISABLED;

	host->end_marker = HOST_DESC_END_MARKER;

	/* queue the buffer */
	spin_lock_bh(&rwnx_hw->tx_lock);
	if (txq->idx != TXQ_INACTIVE && rwnx_txq_queue_skb(rwnx_hw, txq, skb)) {
		PROFILING_SET(SW_PROF_TX_PROCESS);
		rwnx_hwq_process(rwnx_hw, txq->hwq);
		PROFILING_CLR(SW_PROF_TX_PROCESS);
	} else {
		WQ_DBG(DM_TX, DL_VRB,
		       "%s: txq stopped: idx: %d, status: 0x%04x, "
		       "da: %pM\n",
		       __func__, txq->idx, txq->status, host->ethhdr.h_dest);
	}
	spin_unlock_bh(&rwnx_hw->tx_lock);

	return NETDEV_TX_OK;
}

static struct sk_buff *rwnx_form_amsdu(struct rwnx_hw *rwnx_hw,
				       struct rwnx_txq *txq)
{
	struct sk_buff *amsdu_skb;
	int headroom = txq->ndev->needed_headroom;
	int frame_len = txq->amsdu_len + headroom + sizeof(struct ethhdr);

	amsdu_skb = dev_alloc_skb(frame_len);
	if (amsdu_skb) {
		struct sk_buff *skb;
		uint8_t *p;

		amsdu_skb->dev = txq->ndev;
		/* reserve header room */
		skb_reserve(amsdu_skb, headroom);

		skb = skb_peek(&txq->amsdu_list);
		p = (uint8_t *)skb_put(amsdu_skb, sizeof(struct ethhdr));
		memcpy(p, skb->data, sizeof(struct ethhdr));
		amsdu_skb->priority = skb->priority;
		skb_copy_queue_mapping(amsdu_skb, skb);

		while ((skb = __skb_dequeue(&txq->amsdu_list)) != NULL) {
			int pad_len, msdu_len;
			struct ethhdr *eth;

			eth = (struct ethhdr *)skb_put(amsdu_skb,
						       sizeof(struct ethhdr));

			/* add 802.3 header */
			memcpy(eth, skb->data, sizeof(struct ethhdr));
			skb_pull(skb, sizeof(*eth));

			/* add LLC header */
			p = (uint8_t *)skb_put(amsdu_skb, 8);
			memcpy(p, rfc1042_header, sizeof(rfc1042_header));
			p += sizeof(rfc1042_header);
			memcpy(p, &eth->h_proto, sizeof(eth->h_proto));

			/* add payload */
			p = (uint8_t *)skb_put(amsdu_skb, skb->len);
			memcpy(p, skb->data, skb->len);

			/* calculate pad length */
			msdu_len = sizeof(struct ethhdr) +
				   sizeof(rfc1042_header) + 2 + skb->len;
			pad_len = AMSDU_PADDING(msdu_len);
			if (pad_len)
				skb_put(amsdu_skb, pad_len);

			msdu_len =
				sizeof(rfc1042_header) + 2 + skb->len + pad_len;
			eth->h_proto = htons(msdu_len);

			dev_kfree_skb_any(skb);
		}

		txq->amsdu_len = 0;
	}

	return amsdu_skb;
}

static struct sk_buff *rwnx_tx_small_skb_bundle(struct rwnx_hw *rwnx_hw,
						struct rwnx_txq *txq)
{
	struct sk_buff *bundle_skb = NULL;
	struct sk_buff *skb;
	uint8_t *p;
	int headroom = txq->ndev->needed_headroom;
	u16 skb_index = 0;
	struct rwnx_tx_bundle_head *bundle_head;
	unsigned int pad_len = 0;
	u16 avail_data_len = 0;
	struct wq_skb_txcb *txcb;

	if (skb_queue_len(&txq->amsdu_list) == 1) { //only one to
		skb = __skb_dequeue(&txq->amsdu_list);
		txq->amsdu_len = 0;
		txcb = WQ_SKB_TXCB(skb);
		txcb->bundle_num = 1;
		return skb;
	}

	bundle_skb = dev_alloc_skb(txq->amsdu_len + headroom);
	if (bundle_skb) {
		bundle_skb->dev = txq->ndev;
		/* reserve header room */
		skb_reserve(bundle_skb, headroom);

		skb = skb_peek(&txq->amsdu_list);
		p = (uint8_t *)skb_put(
			bundle_skb,
			sizeof(struct ethhdr) +
				AMSDU_PADDING(sizeof(struct ethhdr)));
		memcpy(p, skb->data, sizeof(struct ethhdr));

		bundle_skb->priority = skb->priority;
		skb_copy_queue_mapping(bundle_skb, skb);

		while ((skb = __skb_dequeue(&txq->amsdu_list)) != NULL) {
			/* add bundle header */
			bundle_head = (struct rwnx_tx_bundle_head *)skb_put(
				bundle_skb, sizeof(struct rwnx_tx_bundle_head));
			bundle_head->pattern = 0xA5A5;
			bundle_head->index = skb_index;
			bundle_head->skb_len = skb->len;

			/* add payload */
			p = (uint8_t *)skb_put(bundle_skb, skb->len);
			memcpy(p, skb->data, skb->len);

			pad_len = AMSDU_PADDING(skb->len);
			//WQ_DBG(DM_TX, DL_ERR, "%s::index=%d, amsdu_len=%d,pad_len=%d\n",
			//	__func__, skb_index, skb->len, pad_len);
			if (pad_len)
				skb_put(bundle_skb, pad_len);

			bundle_head->total_bundle_len = skb->len + pad_len;
			avail_data_len += skb->len;
			dev_kfree_skb_any(skb);
			skb_index++;
		}

		if (pad_len) {
			bundle_skb->len -= pad_len;
			bundle_skb->tail -= pad_len;
		}

		if (bundle_head && pad_len) {
			bundle_head->total_bundle_len -= pad_len;
		}
		txq->amsdu_len = 0;

		txcb = WQ_SKB_TXCB(bundle_skb);
		txcb->bundle_num = skb_index;
		txcb->bundle_len = avail_data_len;
	}

	return bundle_skb;
}

#if defined(CONFIG_HIGH_RES_TIMERS) && defined(USE_HRT)
void amsdu_task(unsigned long data)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)data;
	struct rwnx_txq *txq, *next;

	list_for_each_entry_safe(txq, next, &rwnx_hw->amsdu_list_head, amsdu_sched_list) {
		struct net_device *ndev = txq->ndev;
		struct rwnx_vif *rwnx_vif = netdev_priv(ndev);
		struct sk_buff *bundle_skb = NULL;

		spin_lock_bh(&rwnx_hw->tx_lock);
		if (skb_queue_len(&txq->amsdu_list)) {
			if (rwnx_hw->core->hif_ops->hif == WQ_HIF_USB ||
			    rwnx_hw->core->hif_ops->hif == WQ_HIF_SDIO) {
				bundle_skb = rwnx_tx_small_skb_bundle(rwnx_hw, txq);
			} else
				bundle_skb = rwnx_form_amsdu(rwnx_hw, txq);
		}
		spin_unlock_bh(&rwnx_hw->tx_lock);

		if (bundle_skb) {
			u8 tid;
			/* Get the STA id and TID information */
			struct rwnx_sta *sta = rwnx_get_tx_info(rwnx_vif,
							bundle_skb, &tid);

			if (sta)
				rwnx_prepare_xmit(bundle_skb, rwnx_vif, txq,
					sta, tid, true);
			else {
				netdev_err(ndev, "%s: can't find sta", __func__);
				dev_kfree_skb_any(bundle_skb);
			}
		}

		list_del(&txq->amsdu_sched_list);
		txq->txq_in_amsdu_list = false;
	}
}

enum hrtimer_restart rwnx_tx_amsdu_timeout_cb(struct hrtimer *t)
{
	struct rwnx_txq *txq =
		container_of(t, struct rwnx_txq, amsdu_timer);
	struct net_device *ndev = txq->ndev;
	struct rwnx_vif *rwnx_vif = netdev_priv(ndev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;

	if (txq->txq_in_amsdu_list == false) {
		list_add_tail(&txq->amsdu_sched_list, &rwnx_hw->amsdu_list_head);
		txq->txq_in_amsdu_list = true;
		tasklet_schedule(&rwnx_hw->amsdu_task);
	}

	return HRTIMER_NORESTART;
}
#else
void rwnx_tx_amsdu_timeout_cb(struct timer_list *t)
{
	struct sk_buff *bundle_skb = NULL;
	struct rwnx_txq *txq = from_timer(txq, t, amsdu_timer);
	struct net_device *ndev = txq->ndev;
	struct rwnx_vif *rwnx_vif = netdev_priv(ndev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;

	spin_lock_bh(&rwnx_hw->tx_lock);
	if (skb_queue_len(&txq->amsdu_list)) {
		if (rwnx_hw->core->hif_ops->hif == WQ_HIF_USB ||
		    rwnx_hw->core->hif_ops->hif == WQ_HIF_SDIO) {
			bundle_skb = rwnx_tx_small_skb_bundle(rwnx_hw, txq);
		} else
			bundle_skb = rwnx_form_amsdu(rwnx_hw, txq);
	}
	spin_unlock_bh(&rwnx_hw->tx_lock);

	if (bundle_skb) {
		u8 tid;
		/* Get the STA id and TID information */
		struct rwnx_sta *sta =
			rwnx_get_tx_info(rwnx_vif, bundle_skb, &tid);

		if (sta)
			rwnx_prepare_xmit(bundle_skb, rwnx_vif, txq, sta, tid,
					  true);
		else {
			netdev_err(ndev, "%s: can't find sta", __func__);
			dev_kfree_skb_any(bundle_skb);
		}
	}
}
#endif

/**
 * rwnx_amsdu_add_subframe - Add this buffer as an A-MSDU subframe if possible
 *
 * @rwnx_hw Driver main data
 * @skb Buffer to aggregate if possible
 * @sta Destination STA
 * @txq sta's txq used for this buffer
 *
 * Try to aggregate the buffer in an A-MSDU. If it succeed then the
 * buffer is added as a new A-MSDU subframe with AMSDU and LLC/SNAP
 * headers added (so FW won't have to modify this subframe).
 *
 * To be added as subframe :
 * - sta must allow amsdu
 * - buffer must be aggregable (to be defined)
 * - at least one other aggregable buffer is pending in the queue
 *  or an a-msdu (with enough free space) is currently in progress
 *
 * returns true if buffer has been added as A-MDSP subframe, false otherwise
 *
 */
static struct sk_buff *rwnx_amsdu_add_subframe(struct rwnx_hw *rwnx_hw,
					       struct sk_buff *skb,
					       struct rwnx_sta *sta,
					       struct rwnx_txq *txq)
{
	struct sk_buff *new_skb = NULL;

	/* Adjust the maximum number of MSDU allowed in A-MSDU */
	rwnx_adjust_amsdu_maxnb(rwnx_hw);

	/* immediately return if amsdu are not allowed for this sta */
	if (rwnx_hw->amsdu_param.max_packets_num <= 0 || !txq->amsdu_allow ||
	    !rwnx_amsdu_is_aggregable(skb))
		return skb;

	spin_lock_bh(&rwnx_hw->tx_lock);
	if (skb_queue_len(&txq->amsdu_list)) {
		/* queue packet into amsdu_list */
		skb_queue_tail(&txq->amsdu_list, skb);
		txq->amsdu_len +=
			skb->len + AMSDU_MAX_PAD + sizeof(rfc1042_header) + 2;

		if (txq->amsdu_len >= rwnx_hw->amsdu_param.max_len ||
		    skb_queue_len(&txq->amsdu_list) >=
			    rwnx_hw->amsdu_param.max_packets_num) {
			new_skb = rwnx_form_amsdu(rwnx_hw, txq);
			if (new_skb) {
#if defined(CONFIG_HIGH_RES_TIMERS) && defined(USE_HRT)
				hrtimer_try_to_cancel(&txq->amsdu_timer);
#else
				del_timer(&txq->amsdu_timer);
#endif
			}

			goto done;
		}
	} else {
		/* initialize skb list for AMSDU */
		skb_queue_head_init(&txq->amsdu_list);
		skb_queue_tail(&txq->amsdu_list, skb);
		txq->amsdu_len =
			skb->len + AMSDU_MAX_PAD + sizeof(rfc1042_header) + 2;
	}

#if defined(CONFIG_HIGH_RES_TIMERS) && defined(USE_HRT)
	hrtimer_start(&txq->amsdu_timer,
		ns_to_ktime(rwnx_hw->amsdu_param.timeout), HRTIMER_MODE_REL);
#else
	mod_timer(&txq->amsdu_timer,
		  jiffies + msecs_to_jiffies(rwnx_hw->amsdu_param.timeout));
#endif
done:
	spin_unlock_bh(&rwnx_hw->tx_lock);
	return new_skb;
}

static struct sk_buff *rwnx_bundle_skb_add_subframe(struct rwnx_hw *rwnx_hw,
						    struct sk_buff *skb,
						    struct rwnx_txq *txq)
{
	struct sk_buff *new_skb = NULL;

	/* Adjust the maximum number of MSDU allowed in A-MSDU */
	rwnx_adjust_amsdu_maxnb(rwnx_hw);

	/* immediately return if amsdu are not allowed for this sta */
	if (rwnx_hw->amsdu_param.max_packets_num <= 0 ||
	    rwnx_hw->amsdu_param.enable == false ||
	    !rwnx_amsdu_is_aggregable(skb))
		return skb;

	spin_lock_bh(&rwnx_hw->tx_lock);
	if (skb_queue_len(&txq->amsdu_list)) {
		/* queue packet into amsdu_list */
		skb_queue_tail(&txq->amsdu_list, skb);
		txq->amsdu_len += skb->len +
				  sizeof(struct rwnx_tx_bundle_head) +
				  AMSDU_MAX_PAD;

		if (txq->amsdu_len >= rwnx_hw->amsdu_param.max_len ||
		    (skb_queue_len(&txq->amsdu_list) >=
		     rwnx_hw->amsdu_param.max_packets_num)) {
			new_skb = rwnx_tx_small_skb_bundle(rwnx_hw, txq);
			if (new_skb) {
#if defined(CONFIG_HIGH_RES_TIMERS) && defined(USE_HRT)
				hrtimer_try_to_cancel(&txq->amsdu_timer);
#else
				del_timer(&txq->amsdu_timer);
#endif
			}
			goto done;
		}
	} else {
		/* initialize skb list for AMSDU */
		skb_queue_head_init(&txq->amsdu_list);
		skb_queue_tail(&txq->amsdu_list, skb);
		txq->amsdu_len = skb->len + sizeof(struct rwnx_tx_bundle_head) +
				 AMSDU_MAX_PAD + sizeof(struct ethhdr) + 2;
	}

#if defined(CONFIG_HIGH_RES_TIMERS) && defined(USE_HRT)
	hrtimer_start(&txq->amsdu_timer,
		ns_to_ktime(rwnx_hw->amsdu_param.timeout), HRTIMER_MODE_REL);
#else
	mod_timer(&txq->amsdu_timer,
		  jiffies + msecs_to_jiffies(rwnx_hw->amsdu_param.timeout));
#endif
done:
	spin_unlock_bh(&rwnx_hw->tx_lock);
	return new_skb;
}

void rwnx_amsdu_tx_drain(struct rwnx_txq *txq)
{
	struct sk_buff *skb;

#if defined(CONFIG_HIGH_RES_TIMERS) && defined(USE_HRT)
	hrtimer_try_to_cancel(&txq->amsdu_timer);
#else
	del_timer(&txq->amsdu_timer);
#endif
	while ((skb = __skb_dequeue(&txq->amsdu_list)) != NULL)
		dev_kfree_skb_any(skb);
}
#endif /* CONFIG_RWNX_AMSDUS_TX */

#ifdef CONFIG_HML
u8 rwnx_xmit_multicast_to_unicast(struct sk_buff *skb, struct net_device *dev)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_sta *sta;
	struct rwnx_txq *txq;
	struct sk_buff *new_skb;
	struct sk_buff *skb_copy;
	uint8_t sta_num_max = NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX;
	uint8_t i;
	u8 tid = skb->priority;
	bool multicast_to_unicast_flag = false;

	//for all sta
	for (i = 0; i < sta_num_max; i++) {
		sta = &rwnx_hw->sta_table[i];
		if (sta->valid && (sta->vif_idx == rwnx_vif->vif_index) &&
		    (sta->sta_idx != rwnx_vif->ap.bcmc_index)) {
			skb_copy = skb_copy_expand(skb, IPC_TX_MAX_HEADROOM,
						   (RWNX_TX_ALIGN_SIZE +
						    WQ_HIF_TRAILER_SPACE_RSVD),
						   GFP_ATOMIC);
			if (skb_copy) {
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "%s:MULTICAST:sta_idx=%d [%pM]\n",
				       __func__, sta->sta_idx, sta->mac_addr);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
				ether_addr_copy(
					((struct ethhdr *)skb_copy->data)
						->h_dest,
					sta->mac_addr);
#else
				(void)memcpy(
					((struct ethhdr *)skb_copy->data)
						->h_dest,
					sta->mac_addr, ETH_ALEN);
#endif

				txq = rwnx_txq_sta_get(sta, tid, rwnx_hw);
				if (txq->idx == TXQ_INACTIVE) {
					WQ_DBG(DM_TX, DL_ERR,
					       "%s, txq inactive\n", __func__);
					dev_kfree_skb_any(skb_copy);
					continue;
				}
				multicast_to_unicast_flag = true;
#ifdef CONFIG_RWNX_AMSDUS_TX
				if (!(new_skb = rwnx_amsdu_add_subframe(
					      rwnx_hw, skb_copy, sta, txq))) {
					continue;
				}
#endif

				rwnx_prepare_xmit(new_skb, rwnx_vif, txq, sta,
						  tid, (new_skb != skb_copy));
			}
		}
	}

	if (multicast_to_unicast_flag == true) {
		dev_kfree_skb_any(skb);
		return CO_OK;
	}

	return CO_FAIL;
}
#endif

netdev_tx_t rwnx_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_sta *sta;
	struct rwnx_txq *txq;
	struct sk_buff *new_skb;
	u8 tid;
#ifdef CONFIG_HML
	struct ethhdr *eth;
#endif
	netdev_tx_t ret;

	PROFILING_SET(SW_PROF_START_XMIT);

#if MEM_RECORED_CHECK
	add_mem_record(skb->truesize, __func__, __LINE__, skb);
#endif

	sk_pacing_shift_update(skb->sk, rwnx_hw->tcp_pacing_shift);

	// If buffer is shared (or may be used by another interface) need to make a
	// copy as TX infomration is stored inside buffer's headroom
	if (skb_shared(skb) || (skb_headroom(skb) < IPC_TX_MAX_HEADROOM) ||
	    (skb_tailroom(skb) <
	     (RWNX_TX_ALIGN_SIZE + WQ_HIF_TRAILER_SPACE_RSVD)) ||
	    (skb_cloned(skb) && (dev->priv_flags & IFF_BRIDGE_PORT))) {
		struct sk_buff *newskb = skb_copy_expand(
			skb, IPC_TX_MAX_HEADROOM,
			(RWNX_TX_ALIGN_SIZE + WQ_HIF_TRAILER_SPACE_RSVD),
			GFP_ATOMIC);
		if (unlikely(newskb == NULL)) {
			WQ_DBG(DM_TX, DL_ERR, "%s: null newskb\n", __func__);
			goto free;
		}

		dev_kfree_skb_any(skb);
		skb = newskb;
	}
#ifdef CONFIG_HML
	eth = (struct ethhdr *)skb->data;
	if (rwnx_vif->is_hml && is_multicast_ether_addr(eth->h_dest)) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "rwnx_start_xmit:MULTICAST:vif_id=%d dest_mac=[%pM]\n",
		       rwnx_vif->vif_index, eth->h_dest);
		if (rwnx_xmit_multicast_to_unicast(skb, dev) == CO_OK) {
			return NETDEV_TX_OK;
		}
	}
#endif

	/* Get the STA id and TID information */
	sta = rwnx_get_tx_info(rwnx_vif, skb, &tid);
	if (!sta || !sta->valid) {
		WQ_DBG(DM_TX, DL_ERR, "%s: null sta, isvalid:%d\n", __func__, sta != NULL ? sta->valid : 2);
		goto free;
	}

	txq = rwnx_txq_sta_get(sta, tid, rwnx_hw);
	if (txq->idx == TXQ_INACTIVE) {
		WQ_DBG(DM_TX, DL_ERR, "%s: txq inactive\n", __func__);
		goto free;
	}

#ifdef CONFIG_RWNX_AMSDUS_TX
	if (0) { //default is TAE_PPDU_MODE, don't need to do amsdu
		if (!(new_skb =
			      rwnx_amsdu_add_subframe(rwnx_hw, skb, sta, txq)))
			return NETDEV_TX_OK;
	} else {
		new_skb = skb;
	}
#endif

	rwnx_hw->hard_start_xmit_cnt++;

	if (rwnx_hw->core->hif_ops->hif == WQ_HIF_USB ||
	    rwnx_hw->core->hif_ops->hif == WQ_HIF_SDIO) {
		if (!(new_skb =
			      rwnx_bundle_skb_add_subframe(rwnx_hw, skb, txq)))
			return NETDEV_TX_OK;
	}

	ret = rwnx_prepare_xmit(new_skb, rwnx_vif, txq, sta, tid,
				(new_skb != skb));
	PROFILING_CLR(SW_PROF_START_XMIT);

	return ret;

free:
	WQ_DBG(DM_TX, DL_ERR, "%s: drop packet, len: %d\n", __func__, skb->len);
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

/**
 * rwnx_start_mgmt_xmit - Transmit a management frame
 *
 * @vif: Vif that send the frame
 * @sta: Destination of the frame. May be NULL if the destiantion is unknown
 *       to the AP.
 * @params: Mgmt frame parameters
 * @offchan: Indicate whether the frame must be send via the offchan TXQ.
 *           (is is redundant with params->offchan ?)
 * @cookie: updated with a unique value to identify the frame with upper layer
 *
 */
int rwnx_start_mgmt_xmit(struct rwnx_vif *vif, struct rwnx_sta *sta,
			 struct cfg80211_mgmt_tx_params *params, bool offchan,
			 u64 *cookie)
{
	struct rwnx_hw *rwnx_hw = vif->rwnx_hw;
	struct txdesc_host *txdesc_host;
	struct hostdesc *host;
	struct sk_buff *skb;
	struct wq_skb_txcb *txcb;
	u16 frame_len, headroom, tailroom;
	u8 *data;
	struct rwnx_txq *txq;
	bool robust;
	int ret = 0;
	struct ieee80211_mgmt *mgmt = (void *)params->buf;

	headroom = IPC_TX_MAX_HEADROOM;
	tailroom = RWNX_TX_ALIGN_SIZE + WQ_HIF_TRAILER_SPACE_RSVD;

	frame_len = params->len;
	WQ_DBG(DM_TX, DL_INF, "%s mgmt_seq %d,txdesc %d,data %d\n", __func__,
	       rwnx_hw->mgmt_seq, headroom, frame_len);

	/* Set TID and Queues indexes */
	if (sta) {
		txq = rwnx_txq_sta_get(sta, 8, rwnx_hw);
	} else {
		if (offchan)
			txq = &rwnx_hw->txq[NX_OFF_CHAN_TXQ_IDX];
		else
			txq = rwnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE);
	}

	/* Ensure that TXQ is active */
	if (txq->idx == TXQ_INACTIVE) {
		netdev_dbg(vif->ndev, "TXQ inactive\n");
		return -EBUSY;
	}

	/* set sequence number */
	mgmt->seq_ctrl = __cpu_to_le16((vif->ap.seqno++ << 4) & IEEE80211_SCTL_SEQ);

	/* Create a SK Buff object that will contain the provided data */
	skb = dev_alloc_skb(headroom + frame_len + tailroom);
	if (!skb)
		return -ENOMEM;
	*cookie = (unsigned long)skb;

	txcb = WQ_SKB_TXCB(skb);
	*txcb = (struct wq_skb_txcb){
		.jiffies = jiffies,
		.pkt_cls = BIT(WQ_PKT_CLS_80211),
		.txq_idx = txq - rwnx_hw->txq,
	};

	/* Reserve headroom in skb. Do this so that we can easily re-use ieee80211
       functions that take skb with 802.11 frame as parameter */
	skb_reserve(skb, headroom);

	/* Copy data in skb buffer */
	data = skb_put(skb, frame_len);
	memcpy(data, params->buf, frame_len);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	robust = ieee80211_is_robust_mgmt_frame(skb);
#else
	if (skb->len < 25)
		return false;
	return ieee80211_is_robust_mgmt_frame((void *)skb->data);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	/* Update CSA counter if present */
	if (unlikely(params->n_csa_offsets) &&
	    vif->wdev.iftype == NL80211_IFTYPE_AP && vif->ap.csa) {
		int i;

		data = skb->data;
		for (i = 0; i < params->n_csa_offsets; i++) {
			data[params->csa_offsets[i]] = vif->ap.csa->count;
		}
	}
#endif

	/* Use headroom to store struct txdesc_host */
	txdesc_host = (void *)skb_push(skb, sizeof(struct txdesc_host));
	memset(txdesc_host, 0, sizeof(*txdesc_host));

	/* Fill-in the API Descriptor for the MACSW */
	host = &txdesc_host->api.host;
	host->staid = (sta) ? sta->sta_idx : 0xFF;
	host->vif_idx = vif->vif_index;
	host->tid = 0xFF;
	host->flags = TXU_CNTRL_MGMT;
	/* host->ext_flags = 0; */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	ether_addr_copy(host->ethhdr.h_dest, mgmt->da);
	ether_addr_copy(host->ethhdr.h_source, mgmt->sa);
#else
	(void)memcpy(host->ethhdr.h_dest, mgmt->da, ETH_ALEN);
	(void)memcpy(host->ethhdr.h_source, mgmt->sa, ETH_ALEN);
#endif
#ifdef CONFIG_HML
	host->is_hml = vif->is_hml;
#endif
	if (robust)
		host->flags |= TXU_CNTRL_MGMT_ROBUST;

	if (params->no_cck)
		host->flags |= TXU_CNTRL_MGMT_NO_CCK;

	host->packet_len[0] = frame_len;
	host->packet_cnt = 1;
	host->mgmt_frame_nb = ++rwnx_hw->mgmt_seq;

	if (rwnx_hw->core->config.dma_map) {
		int err = wq_tx_skb_dma_map(rwnx_hw->core, skb, txcb);

		if (err)
			return err;
	}
	host->end_marker = HOST_DESC_END_MARKER;

	skb->dev = vif->ndev;

#if 0
    /* queue the buffer */
    spin_lock_bh(&rwnx_hw->tx_lock);
    if (rwnx_txq_queue_skb(rwnx_hw, txq, skb))
        rwnx_hwq_process(rwnx_hw, txq->hwq);
#else
	if (!rwnx_get_dev_credit(rwnx_hw, RWNX_HWQ_BCMC, host->vif_idx, 1,
				 &host->via_grp_id, &host->via_type_id)) {
		dump_bytes(DL_INF, "no credit", data, frame_len);
		wq_tx_skb_free_any(rwnx_hw->core, skb);
		return -EBUSY;
	}
	spin_lock_bh(&rwnx_hw->tx_lock);
	ret = wq_ipc_tx_pkt(rwnx_hw->core, RWNX_HWQ_VO, skb);
	if (ret && ret != -ENOBUFS)
		ret = -EBUSY;

	// Set delay timer for bcmc deauth frame
	if (((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP) ||
		 (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO)) &&
		(is_broadcast_ether_addr(mgmt->da)) &&
		(ieee80211_is_deauth(mgmt->frame_control))) {
		mod_timer(&rwnx_hw->ap_mgt_txdone_timer, jiffies + AP_MGT_TXDONE_MAX_WAIT);
		rwnx_hw->ap_mgt_tx_ongoing = 1;
		WQ_DBG(DM_TX, DL_WRN, "tx_mgmt: ap tx mgt going\n");
	}
#endif
	spin_unlock_bh(&rwnx_hw->tx_lock);

	return ret;
}

/**
 * rwnx_txdatacfm - FW callback for TX confirmation
 *
 * called with tx_lock hold
 */
int rwnx_txdatacfm(struct rwnx_hw *rwnx_hw, struct sk_buff *skb)
{
	struct txdesc_host *txdesc_host;
	struct hostdesc *host;
	struct rwnx_vif *rwnx_vif;
	struct rwnx_sta *sta;
	uint32_t pkt_len;
	struct wq_skb_txcb *txcb;
	// the number of skb aggregated by this bundle-skb
	uint16_t skb_count;
	struct ethhdr *ethhdr;

	PROFILING_SET(SW_PROF_TX_CFM_HDL);

	txdesc_host = (struct txdesc_host *)(skb->data);
	host = &txdesc_host->api.host;

	if (skb->dev)
		rwnx_vif = netdev_priv(skb->dev);
	else
		rwnx_vif = rwnx_hw->vif_table[host->vif_idx];

	//mgmt frame via IPC_EVENT handle
	if (host->flags & TXU_CNTRL_MGMT) {
		WQ_DBG(DM_TX, DL_ERR,
		       "%s: qid:%d, inhost:%d, pktlen:%d, "
		       "txqidx:%d, pktcls:0x%x, flags:0x%x\n",
		       __func__, WQ_SKB_TXCB(skb)->qid,
		       WQ_SKB_TXCB(skb)->msdu_in_host,
		       WQ_SKB_TXCB(skb)->pkt_len,
		       WQ_SKB_TXCB(skb)->txq_idx,
		       WQ_SKB_TXCB(skb)->pkt_cls,
		       host->flags);
		dump_bytes(DL_WRN, "dropped skb:", skb->data, skb->len);
		BUG_ON(host->flags & TXU_CNTRL_MGMT);
	}

	txcb = WQ_SKB_TXCB(skb);
	if (txcb->usb_out_bundle_num) {
		//a bundle-skb consists of multiple skbs under USB interface
		skb_count = txcb->usb_out_bundle_num;
		pkt_len = txcb->pkt_len;
	} else {
		skb_count = 1;
		pkt_len = host->packet_len[0];
	}

	if (txcb->pkt_cls & (BIT(WQ_PKT_CLS_TCP_ACK))) {
		u32 xmit_hifdone_ms = jiffies_to_msecs(jiffies) - jiffies_to_msecs(txcb->jiffies);
		u32 xmit_hif_ms = txcb->tx_ms - jiffies_to_msecs(txcb->jiffies);
		u32 old_hidone = atomic_read(&rwnx_hw->xmit_to_hifdone_ms_max);
		u32 old_hif = atomic_read(&rwnx_hw->xmit_to_hif_ms_max);
		if (old_hidone < xmit_hifdone_ms) {
			atomic_set(&rwnx_hw->xmit_to_hifdone_ms_max, xmit_hifdone_ms);
		}
		if (old_hif < xmit_hif_ms) {
			atomic_set(&rwnx_hw->xmit_to_hif_ms_max, xmit_hif_ms);
		}
	}
	if (txcb->msdu_in_host) {
		rwnx_hw->free_ll_pkt_cnt++;
	}

	/* Update statistics */
	rwnx_vif->net_stats.tx_packets += skb_count;
	rwnx_vif->net_stats.tx_bytes += pkt_len;

	atomic_add(skb_count, &rwnx_hw->tx_packets);
	atomic_add(pkt_len, &rwnx_hw->tx_bytes);

	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||
	    RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
		sta = rwnx_vif->sta.ap;
	}
	else {
		/* When use txdesc compression, the ethernet header might be
		   wrong, use the ethernet header reserved for TAE to correct
		   the DA */
		if (rwnx_hw->mod_params.compress_txdesc)
			ethhdr = (struct ethhdr *)(txdesc_host+1);
		else
			ethhdr = &host->ethhdr;

		sta = rwnx_get_sta(rwnx_hw, ethhdr->h_dest);
	}

	if (sta) {
		sta->stats.tx_pkts += skb_count;
		sta->stats.tx_bytes += pkt_len;
		sta->stats.last_act = jiffies;
	}

	wq_tx_skb_free_any(rwnx_hw->core, skb);

	PROFILING_CLR(SW_PROF_TX_CFM_HDL);

	return 0;
}

/**
 * rwnx_txq_credit_update - Update credit for one txq
 *
 * @rwnx_hw: Driver main data
 * @sta_idx: STA idx
 * @tid: TID
 * @update: offset to apply in txq credits
 *
 * Called when fw send ME_TX_CREDITS_UPDATE_IND message.
 * Apply @update to txq credits, and stop/start the txq if needed
 */
void rwnx_txq_credit_update(struct rwnx_hw *rwnx_hw, int sta_idx, u8 tid,
			    s8 update)
{
}

void rwnx_txq_ring_set_rcd_skb(struct rwnx_hw *rwnx_hw, u16 sta_idx, u8 ring_id,
			       u8 r_w_idx_type, struct sk_buff *skb)
{
	struct _tx_buf_ring *txq_ring_vaddr;
	struct _tx_ring_cfg *txq_ring_cfg;
	struct rwnx_msdu_txdone_rcd *txdone_rcd;

	if (is_multicast_sta(sta_idx)) {
		sta_idx = NX_REMOTE_STA_MAX;
	}

	txq_ring_vaddr = rwnx_hw->txq_ring_vaddr + sta_idx;
	txq_ring_cfg = &txq_ring_vaddr->txq_ring[ring_id];
	txdone_rcd = &rwnx_hw->txq_ring_sts->txq_ring_rcd[sta_idx];

	if (ring_id >= TXQ_RING_ID_MAX) {
		WQ_DBG(DM_TX, DL_ERR, "%s: err ringid: %d\n", __func__,
		       ring_id);
		BUG_ON(ring_id >= TXQ_RING_ID_MAX);
	}

	if (r_w_idx_type == TXQ_RING_READ) {
		BUG_ON(txq_ring_cfg->ring_host_read_idx >=
		       txq_ring_cfg->ring_sz);

		switch (ring_id) {
		case TXQ_RING_ID_0:
			txdone_rcd->ring0_skb_tbl[txq_ring_cfg
							  ->ring_host_read_idx] =
				skb;
			break;
		case TXQ_RING_ID_1:
			txdone_rcd->ring1_skb_tbl[txq_ring_cfg
							  ->ring_host_read_idx] =
				skb;
			break;
		case TXQ_RING_ID_2:
			txdone_rcd->ring2_skb_tbl[txq_ring_cfg
							  ->ring_host_read_idx] =
				skb;
			break;
		case TXQ_RING_ID_3:
			txdone_rcd->ring3_skb_tbl[txq_ring_cfg
							  ->ring_host_read_idx] =
				skb;
			break;
		default:
			break;
		}

		if (++txq_ring_cfg->ring_host_read_idx >=
		    txq_ring_cfg->ring_sz) {
			txq_ring_cfg->ring_host_read_idx = 0;
		}
	} else {
		BUG_ON(txq_ring_cfg->ring_host_write_idx >=
		       txq_ring_cfg->ring_sz);

		switch (ring_id) {
		case TXQ_RING_ID_0:
			txdone_rcd->ring0_skb_tbl
				[txq_ring_cfg->ring_host_write_idx] = skb;
			break;
		case TXQ_RING_ID_1:
			txdone_rcd->ring1_skb_tbl
				[txq_ring_cfg->ring_host_write_idx] = skb;
			break;
		case TXQ_RING_ID_2:
			txdone_rcd->ring2_skb_tbl
				[txq_ring_cfg->ring_host_write_idx] = skb;
			break;
		case TXQ_RING_ID_3:
			txdone_rcd->ring3_skb_tbl
				[txq_ring_cfg->ring_host_write_idx] = skb;
			break;
		default:
			break;
		}

		if (++txq_ring_cfg->ring_host_write_idx >=
		    txq_ring_cfg->ring_sz) {
			txq_ring_cfg->ring_host_write_idx = 0;
		}
	}
}

struct sk_buff *rwnx_txq_ring_read_rcd_skb(struct rwnx_hw *rwnx_hw, u16 sta_idx, u8 ring_id,
					   u8 r_w_idx_type)
{
	struct _tx_buf_ring *txq_ring_vaddr;
	struct _tx_ring_cfg *txq_ring_cfg;
	struct rwnx_msdu_txdone_rcd *txdone_rcd;

	if (is_multicast_sta(sta_idx)) {
		sta_idx = NX_REMOTE_STA_MAX;
	}

	txq_ring_vaddr = rwnx_hw->txq_ring_vaddr + sta_idx;
	txq_ring_cfg = &txq_ring_vaddr->txq_ring[ring_id];
	txdone_rcd = &rwnx_hw->txq_ring_sts->txq_ring_rcd[sta_idx];

	switch (ring_id) {
	case TXQ_RING_ID_0:
		if (r_w_idx_type == TXQ_RING_READ) {
			BUG_ON(txq_ring_cfg->ring_host_read_idx >=
			       txq_ring_cfg->ring_sz);
			return txdone_rcd
				->ring0_skb_tbl[txq_ring_cfg->ring_host_read_idx];
		} else {
			BUG_ON(txq_ring_cfg->ring_host_write_idx >=
			       txq_ring_cfg->ring_sz);
			return txdone_rcd->ring0_skb_tbl
				[txq_ring_cfg->ring_host_write_idx];
		}
		break;
	case TXQ_RING_ID_1:
		if (r_w_idx_type == TXQ_RING_READ) {
			BUG_ON(txq_ring_cfg->ring_host_read_idx >=
			       txq_ring_cfg->ring_sz);
			return txdone_rcd
				->ring1_skb_tbl[txq_ring_cfg->ring_host_read_idx];
		} else {
			BUG_ON(txq_ring_cfg->ring_host_write_idx >=
			       txq_ring_cfg->ring_sz);
			return txdone_rcd->ring1_skb_tbl
				[txq_ring_cfg->ring_host_write_idx];
		}
		break;
	case TXQ_RING_ID_2:
		if (r_w_idx_type == TXQ_RING_READ) {
			BUG_ON(txq_ring_cfg->ring_host_read_idx >=
			       txq_ring_cfg->ring_sz);
			return txdone_rcd
				->ring2_skb_tbl[txq_ring_cfg->ring_host_read_idx];
		} else {
			BUG_ON(txq_ring_cfg->ring_host_write_idx >=
			       txq_ring_cfg->ring_sz);
			return txdone_rcd->ring2_skb_tbl
				[txq_ring_cfg->ring_host_write_idx];
		}
		break;
	case TXQ_RING_ID_3:
		if (r_w_idx_type == TXQ_RING_READ) {
			BUG_ON(txq_ring_cfg->ring_host_read_idx >=
			       txq_ring_cfg->ring_sz);
			return txdone_rcd
				->ring3_skb_tbl[txq_ring_cfg->ring_host_read_idx];
		} else {
			BUG_ON(txq_ring_cfg->ring_host_write_idx >=
			       txq_ring_cfg->ring_sz);
			return txdone_rcd->ring3_skb_tbl
				[txq_ring_cfg->ring_host_write_idx];
		}
		break;
	default:
		WQ_DBG(DM_TX, DL_ERR, "%s: err ringid: %d\n", __func__,
		       ring_id);
		BUG_ON(ring_id >= TXQ_RING_ID_MAX);
		break;
	}

	return 0;
}

struct sk_buff *rwnx_txq_ring_read_rcd_skb_skip(struct rwnx_hw *rwnx_hw, u16 sta_idx,
						u8 ring_id, u16 idx)
{
	struct _tx_buf_ring *txq_ring_vaddr;
	struct _tx_ring_cfg *txq_ring_cfg;
	struct rwnx_msdu_txdone_rcd *txdone_rcd;

	if (is_multicast_sta(sta_idx)) {
		sta_idx = NX_REMOTE_STA_MAX;
	}

	txq_ring_vaddr = rwnx_hw->txq_ring_vaddr + sta_idx;
	txq_ring_cfg = &txq_ring_vaddr->txq_ring[ring_id];
	txdone_rcd = &rwnx_hw->txq_ring_sts->txq_ring_rcd[sta_idx];

	switch (ring_id) {
	case TXQ_RING_ID_0:
		BUG_ON(idx >= txq_ring_cfg->ring_sz);
		return txdone_rcd->ring0_skb_tbl[idx];
	case TXQ_RING_ID_1:
		BUG_ON(idx >= txq_ring_cfg->ring_sz);
		return txdone_rcd->ring1_skb_tbl[idx];
	case TXQ_RING_ID_2:
		BUG_ON(idx >= txq_ring_cfg->ring_sz);
		return txdone_rcd->ring2_skb_tbl[idx];
	case TXQ_RING_ID_3:
		BUG_ON(idx >= txq_ring_cfg->ring_sz);
		return txdone_rcd->ring3_skb_tbl[idx];
	default:
		WQ_DBG(DM_TX, DL_ERR, "%s: err ringid: %d\n", __func__,
		       ring_id);
		BUG_ON(ring_id >= TXQ_RING_ID_MAX);
		break;
	}

	return 0;
}

void rwnx_txq_ring_set_rcd_skb_skip(struct rwnx_hw *rwnx_hw, u16 sta_idx, u8 ring_id,
				    u16 idx, struct sk_buff *skb)
{
	struct _tx_buf_ring *txq_ring_vaddr;
	struct _tx_ring_cfg *txq_ring_cfg;
	struct rwnx_msdu_txdone_rcd *txdone_rcd;

	if (is_multicast_sta(sta_idx)) {
		sta_idx = NX_REMOTE_STA_MAX;
	}

	txq_ring_vaddr = rwnx_hw->txq_ring_vaddr + sta_idx;
	txq_ring_cfg = &txq_ring_vaddr->txq_ring[ring_id];
	txdone_rcd = &rwnx_hw->txq_ring_sts->txq_ring_rcd[sta_idx];

	if (ring_id >= TXQ_RING_ID_MAX) {
		WQ_DBG(DM_TX, DL_ERR, "%s: err ringid: %d\n", __func__,
		       ring_id);
		BUG_ON(ring_id >= TXQ_RING_ID_MAX);
	}

	BUG_ON(idx >= txq_ring_cfg->ring_sz);

	switch (ring_id) {
	case TXQ_RING_ID_0:
		txdone_rcd->ring0_skb_tbl[idx] = skb;
		break;
	case TXQ_RING_ID_1:
		txdone_rcd->ring1_skb_tbl[idx] = skb;
		break;
	case TXQ_RING_ID_2:
		txdone_rcd->ring2_skb_tbl[idx] = skb;
		break;
	case TXQ_RING_ID_3:
		txdone_rcd->ring3_skb_tbl[idx] = skb;
		break;
	default:
		break;
	}
}

void rwnx_hwq_ring_push(struct rwnx_hw *rwnx_hw, int hwqid, struct sk_buff *skb)
{
	u8 ring_id = (u8)hwqid;
	struct txdesc_host *host = (struct txdesc_host *)skb->data;
	u16 sta_idx;

	if (!rwnx_hw->txq_ring_vaddr) {
		WQ_DBG(DM_TX, DL_ERR, "%s: txqring is not init error\n", __func__);
		return;
	}

	BUG_ON(ring_id >= TXQ_RING_ID_MAX);

	sta_idx = host->api.host.staid;
	if (is_multicast_sta(sta_idx)) {
		sta_idx = NX_REMOTE_STA_MAX;
	}
	rwnx_hw->txq_ring_sts->txq_ring_rcd[sta_idx].push_txqring_cnt[ring_id]++;

	spin_lock_bh(&rwnx_hw->txq_ring_lock);

	if (rwnx_txq_ring_read_rcd_skb(rwnx_hw, host->api.host.staid, ring_id, TXQ_RING_WRITE)) {
		struct _tx_buf_ring *txq_ring_vaddr;
		struct _tx_ring_cfg *txq_ring_cfg;
		txq_ring_vaddr = rwnx_hw->txq_ring_vaddr + host->api.host.staid;
		txq_ring_cfg = &txq_ring_vaddr->txq_ring[ring_id];
		WQ_DBG(DM_TX, DL_ERR, "%s: staid:%d, ringid:%d, hostreadidx:%d, hostwriteidx:%d, fwdoneidx:%d\n",
			__func__,
			host->api.host.staid, ring_id, txq_ring_cfg->ring_host_read_idx,
			txq_ring_cfg->ring_host_write_idx, txq_ring_cfg->ring_fw_done_idx);
		BUG_ON(1);
	}
	rwnx_txq_ring_set_rcd_skb(rwnx_hw, host->api.host.staid, ring_id, TXQ_RING_WRITE, skb);

	spin_unlock_bh(&rwnx_hw->txq_ring_lock);
}

int rwnx_hwq_ll_data_free(struct rwnx_hw *rwnx_hw)
{
	u8 ring_idx;
	u16 fw_txdone_idx, host_read_idx, txq_ring_sz;
	u16 free_window, free_idx;
	u16 more_msdu_num = 0, pos = 0, sync_seq_start, sync_seq_end, shift;
	u16 msdu_idx;
	u32 cal_bitmap;
	u32 msdu_bitmap_tmp[MAC_TXQ_MSDU_SEQ_WINSZ_WD_MAX];
	u16 sta_idx;

	struct sk_buff *ll_skb = NULL;
	struct _tx_ring_cfg *txq_ring_cfg;
	struct _tx_ring_msdu_map *msdu_map;
	struct wq_skb_txcb *txcb;

	if (!rwnx_hw->txq_ring_vaddr) {
		return 0;
	}

	spin_lock_bh(&rwnx_hw->txq_ring_lock);
	for (sta_idx = 0; sta_idx <= NX_REMOTE_STA_MAX; sta_idx++) {
		struct _tx_buf_ring *txq_ring_vaddr = rwnx_hw->txq_ring_vaddr + sta_idx;
		for (ring_idx = 0; ring_idx < TXQ_RING_ID_MAX; ring_idx++) {
			txq_ring_cfg = &txq_ring_vaddr->txq_ring[ring_idx];
			msdu_map = &txq_ring_vaddr->msdu_map[ring_idx];

			for (free_idx = 0; free_idx < MAC_TXQ_MSDU_SEQ_WINSZ_WD_MAX;
				free_idx++) {
				msdu_bitmap_tmp[free_idx] =
					msdu_map->msdu_bitmap[free_idx];
			}

			fw_txdone_idx = txq_ring_cfg->ring_fw_done_idx;
			host_read_idx = txq_ring_cfg->ring_host_read_idx;
			txq_ring_sz = txq_ring_cfg->ring_sz;
			more_msdu_num = txq_ring_cfg->ring_fw_more_msdu_num;
			sync_seq_start = txq_ring_cfg->ring_sync_seq_start;
			sync_seq_end = txq_ring_cfg->ring_sync_seq_end;

			if (fw_txdone_idx >= host_read_idx) {
				free_window = fw_txdone_idx - host_read_idx;
			} else {
				free_window =
					fw_txdone_idx + txq_ring_sz - host_read_idx;
			}

			if (free_window) {
				for (free_idx = 0; free_idx < free_window; free_idx++) {
					ll_skb = rwnx_txq_ring_read_rcd_skb(
						rwnx_hw, sta_idx, ring_idx, TXQ_RING_READ);
					if (ll_skb) {
						txcb = WQ_SKB_TXCB(ll_skb);
						if (txcb->has_hif_htc) {
							skb_pull(ll_skb, HEADROOM_HIF_HTC);
						}
						rwnx_txq_tx_done_pre(rwnx_hw,
								txcb->txq_idx);
						rwnx_txdatacfm(rwnx_hw, ll_skb);
						rwnx_hw->txq_ring_sts->txq_ring_rcd[sta_idx].pop_txqring_cnt[ring_idx]++;
					}
					/* set skb tbl = 0 */
					rwnx_txq_ring_set_rcd_skb(rwnx_hw, sta_idx, ring_idx,
								TXQ_RING_READ, 0);
				}
			}

			if (sync_seq_start != sync_seq_end || !more_msdu_num) {
				if (sync_seq_start != sync_seq_end) {
					WQ_DBG(DM_GENERIC, DL_ERR,
						"ring:%d, fw:%d, host:%d, pos:%d, startsync: %d - %d "
						"bitmap:0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
						ring_idx, fw_txdone_idx, host_read_idx,
						more_msdu_num, sync_seq_start,
						sync_seq_end, msdu_bitmap_tmp[0],
						msdu_bitmap_tmp[1], msdu_bitmap_tmp[2],
						msdu_bitmap_tmp[3], msdu_bitmap_tmp[4],
						msdu_bitmap_tmp[5], msdu_bitmap_tmp[6],
						msdu_bitmap_tmp[7], msdu_bitmap_tmp[8],
						msdu_bitmap_tmp[9]);
				}
				continue;
			}

			for (msdu_idx = 0; msdu_idx < MAC_TXQ_MSDU_SEQ_WINSZ_WD_MAX;
				msdu_idx++) {
				cal_bitmap = msdu_bitmap_tmp[msdu_idx];
				shift = 0;
				while (cal_bitmap) {
					if (cal_bitmap & 1) {
						free_idx = fw_txdone_idx + pos + shift;
						if (free_idx >= txq_ring_sz) {
							free_idx -= txq_ring_sz;
						}
						ll_skb =
							rwnx_txq_ring_read_rcd_skb_skip(
								rwnx_hw, sta_idx, ring_idx,
								free_idx);
						if (ll_skb) {
							txcb = WQ_SKB_TXCB(ll_skb);
							if (txcb->has_hif_htc) {
								skb_pull(ll_skb, HEADROOM_HIF_HTC);
							}
							rwnx_txq_tx_done_pre(
								rwnx_hw, txcb->txq_idx);
							rwnx_txdatacfm(rwnx_hw, ll_skb);
							rwnx_txq_ring_set_rcd_skb_skip(
								rwnx_hw, sta_idx, ring_idx,
								free_idx, 0);
							rwnx_hw->txq_ring_sts->txq_ring_rcd[sta_idx].pop_txqring_cnt[ring_idx]++;
						}
					}
					cal_bitmap = cal_bitmap >> 1;
					shift++;
					if (pos + shift >= more_msdu_num) {
						break;
					}
				}
				pos = (msdu_idx + 1) * 32;
				if (pos >= more_msdu_num) {
					break;
				}
			}
		}
	}
	spin_unlock_bh(&rwnx_hw->txq_ring_lock);

	return 0;
}

int rwnx_hwq_ll_data_idx_check(struct rwnx_hw *rwnx_hw)
{
	u8 ring_idx;
	u16 fw_txdone_idx, host_read_idx, txq_ring_sz;
	u16 free_window, sync_seq_start, sync_seq_end;
	struct _tx_ring_cfg *txq_ring_cfg;
	u32 more_msdu_num;

	if (!rwnx_hw->txq_ring_vaddr) {
		return 0;
	}

	for (ring_idx = 0; ring_idx < TXQ_RING_ID_MAX; ring_idx++) {
		txq_ring_cfg = &rwnx_hw->txq_ring_vaddr->txq_ring[ring_idx];
		fw_txdone_idx = txq_ring_cfg->ring_fw_done_idx;
		host_read_idx = txq_ring_cfg->ring_host_read_idx;
		txq_ring_sz = txq_ring_cfg->ring_sz;
		more_msdu_num = txq_ring_cfg->ring_fw_more_msdu_num;
		sync_seq_start = txq_ring_cfg->ring_sync_seq_start;
		sync_seq_end = txq_ring_cfg->ring_sync_seq_end;

		if (fw_txdone_idx >= host_read_idx) {
			free_window = fw_txdone_idx - host_read_idx;
		} else {
			free_window =
				fw_txdone_idx + txq_ring_sz - host_read_idx;
		}

		if (free_window ||
		    ((sync_seq_start == sync_seq_end) && more_msdu_num)) {
			return 1;
		}
	}
	return 0;
}
