#define WQ_LOG_DM DM_IPC

/****************************
 * Include
 ****************************/
#include <linux/types.h>
#include <net/cfg80211.h>
#include <linux/skbuff.h>
#include <linux/time.h>
#include <linux/dma-mapping.h>
#include <linux/ieee80211.h>
#include <linux/etherdevice.h>
#include <net/ieee80211_radiotap.h>

#include "fw_api/wifi/htc/e2a_event.h"
#include "fw_api/wifi/mac/wq_pktdump.h"

#include "core.h"
#include "hif_api.h"
#include "wq_log.h"
#include "rwnx_msg_rx.h"
#include "rwnx_msg_tx.h"
#include "rwnx_rx.h"
#include "wq_wifi_dbg.h"
#include "ieee80211_ht.h"
#include "wq_tx_credit.h"
#include "rwnx_events.h"

#include "rwnx_main.h"
#include "wq_pktlog.h"

#include "wq_profiling.h"

/* linux/units.h */
#ifndef HZ_PER_MHZ
#define HZ_PER_MHZ 1000000UL
#endif

/*
 * FIXME: this definition is actually useless
 * IPC command type
 */
enum wq_ipc_cmd_type {
	WQ_IPC_CMD, /* obsoleted HTC commands */
	WQ_IPC_RWNX_CMD, /* for non-PM command */
	WQ_IPC_PWR_CMD, /* for PM (suspend/resume) command */

	WQ_IPC_CMD_MAX,
};

/*****************************
 * Function
 *****************************/

int wq_tx_skb_dma_map(struct wq_core *core, struct sk_buff *skb,
		      struct wq_skb_txcb *txcb)
{
	u8 *data = skb->data - HEADROOM_HIF_HTC;
	uint16_t len =
		HEADROOM_HIF_HTC + ALIGN(skb->len, sizeof(u32)) + TAILROOM_HIF;
	dma_addr_t phys_addr =
		dma_map_single(core->dev, data, len, DMA_TO_DEVICE);
	int err = dma_mapping_error(core->dev, phys_addr);

	WQ_DBG(DM_TX, DL_VRB,
	       "%s: data=%p(%d+%d+4=%d), phys_addr=%llx, err=%d\n", __func__,
	       data, skb->len, HEADROOM_HIF_HTC, len, (u64)phys_addr, err);
	if (err) {
		dev_kfree_skb_any(skb);
		return err;
	}

	txcb->phyaddr = phys_addr;
	txcb->total_dma_len = len;

	return 0;
}

void wq_tx_skb_dma_unmap(struct wq_core *core, struct sk_buff *skb)
{
	struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);

	if (txcb->total_dma_len) {
		BUG_ON(!txcb->phyaddr);
		dma_unmap_single(core->dev, txcb->phyaddr, txcb->total_dma_len,
				 DMA_TO_DEVICE);
		txcb->phyaddr = 0;
		txcb->total_dma_len = 0;
	}
}

void wq_tx_skb_free_any(struct wq_core *core, struct sk_buff *skb)
{
	wq_tx_skb_dma_unmap(core, skb);
	dev_kfree_skb_any(skb);
}

static inline int wq_ipc_can_handle_FWREADY(struct wq_core *core)
{
	/* get MAC_E2A_FWREADY earlier than rwnx_hw is ready */
	return wq_core_state_get(core) >= WQ_CORE_STATE_FW_DL;
}

static inline int wq_ipc_is_not_ready(struct wq_core *core)
{
	/* at least rwnx_hw should be ready */
	return wq_core_state_get(core) < WQ_CORE_STATE_WIF_NREADY;
}

#define WQ_IPC_PRECHECK(core, skb)                                             \
	BUG_ON(!skb);                                                          \
	if (wq_ipc_is_not_ready(core)) {                                       \
		WQ_DBG(DM_IPC, DL_VRB, "%s: ipc drop (%s, skb %p)!\n",         \
		       __func__, wq_core_state_name(core), skb);               \
		goto fail;                                                     \
	}

int wq_ipc_tx_msg(struct wq_core *core, bool is_pwr_cmd, u8 *cmd_buf,
		  u32 cmd_len)
{
	enum wq_ipc_cmd_type cmd_type =
		is_pwr_cmd ? WQ_IPC_PWR_CMD : WQ_IPC_RWNX_CMD;
	struct sk_buff_head skbq;
	struct sk_buff *skb;
	struct wq_skb_txcb *txcb;
	int ret;

	if (core->flags.suspend && !is_pwr_cmd) {
		WQ_DBG(DM_IPC, DL_ERR, "%s: drop the command due to suspend.\n",
		       __func__);
		return -EIO;
	}

	skb = dev_alloc_skb(HEADROOM_HIF_HTC + ALIGN(cmd_len, sizeof(u32)) +
			    TAILROOM_HIF);
	if (!skb)
		return -ENOMEM;

	txcb = WQ_SKB_TXCB(skb);
	*txcb = (struct wq_skb_txcb){
		.jiffies = jiffies,
	};

	skb_reserve(skb, HEADROOM_HIF_HTC);
	if (!WARN_ON(!cmd_buf || !cmd_len))
		memcpy(skb_put(skb, cmd_len), cmd_buf, cmd_len);

	if (core->config.dma_map) {
		int err = wq_tx_skb_dma_map(core, skb, txcb);
		if (err)
			return err;
	}

	__skb_queue_head_init(&skbq);
	__skb_queue_tail(&skbq, skb);
	ret = htc_tx(core, WQ_QID_MSG, &skbq, cmd_type);
	if (ret) {
		WQ_DBG(DM_IPC, DL_ERR,
		       "%s: failed, state=%s, command id=%d, ret=%d\n",
		       __func__, wq_core_state_name(core), cmd_type, ret);
		wq_tx_skb_free_any(core, skb);
	}
	return ret;
}

void wq_ipc_mgmt_txdone_clean(struct wq_core *core)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&core->ipc.mgmt_txdone))) {
		WQ_DBG(DM_TX, DL_INF, "%s clean mgmt %p!!!\n", __func__, skb);
		wq_tx_skb_free_any(core, skb);
	}
}

static inline void wq_ipc_mgmt_txdone(struct wq_core *core,
				      struct rwnx_mgmt_frame_status *status)
{
	struct sk_buff *mgmt_skb, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&core->ipc.mgmt_txdone.lock, flags);
	skb_queue_walk_safe(&core->ipc.mgmt_txdone, mgmt_skb, tmp)
	{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
		struct timeval now = { .tv_sec = 0, .tv_usec = 0 };
#else
		struct timespec64 now = { .tv_sec = 0, .tv_nsec = 0 };
#endif
		struct txdesc_host *txdesc =
			(struct txdesc_host *)(mgmt_skb->data);
		struct ieee80211_mgmt *mgmt =
			(struct ieee80211_mgmt *)(txdesc + 1);

		spin_lock(&core->hw->mgmt_hist_lock);
		if (mgmt_idx < HIST_CNT &&
		    (mgmt->frame_control & IEEE80211_FCTL_STYPE) ==
			    IEEE80211_FTYPE_MGMT) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
			do_gettimeofday(&now);
#else
			ktime_get_real_ts64(&now);
#endif
			mgmt_hist[mgmt_idx].ts = now.tv_sec;
			mgmt_hist[mgmt_idx].dir = WIFI_DBG_PKT_TX;
			mgmt_hist[mgmt_idx].ack = status->ack;
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
		spin_unlock(&core->hw->mgmt_hist_lock);

		WQ_DBG(DM_IPC, DL_WRN,
		       "%s(%d) MAC_E2A_MGMT_CFM ack=%d frame_nb=%d,nb %d\n",
		       __func__, __LINE__, status->ack, status->frame_nb,
		       txdesc->api.host.mgmt_frame_nb);
		if (txdesc->api.host.mgmt_frame_nb == status->frame_nb) {
			struct rwnx_vif *rwnx_vif = netdev_priv(mgmt_skb->dev);

			if (((RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP) ||
				 (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO)) &&
				(ieee80211_is_deauth(mgmt->frame_control)) &&
				core->hw->ap_mgt_tx_ongoing) {
					del_timer_sync(&core->hw->ap_mgt_txdone_timer);
					core->hw->ap_mgt_tx_ongoing = 0;
			}

			__skb_unlink(mgmt_skb, &core->ipc.mgmt_txdone);
			if (rwnx_vif->ndev) {
				cfg80211_mgmt_tx_status(
					&rwnx_vif->wdev,
					(unsigned long)mgmt_skb,
					(mgmt_skb->data +
					 sizeof(struct txdesc_host)),
					mgmt_skb->len -
						sizeof(struct txdesc_host),
					status->ack, GFP_ATOMIC);
			}
			wq_tx_skb_free_any(core, mgmt_skb);
			break;
		}
	}
	spin_unlock_irqrestore(&core->ipc.mgmt_txdone.lock, flags);
}

void wq_ipc_msdu_txdone_clean(struct wq_core *core)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&core->ipc.msdu.hifdone))) {
		WQ_DBG(DM_TX, DL_ERR, "%s clean msdu %p!!!\n", __func__, skb);
		wq_tx_skb_free_any(core, skb);
	}
}

static inline struct sk_buff *__wq_ipc_msdu_macdone(struct wq_core *core,
						    addr32 msdu_addr)
{
	struct msdu_txdone *msdu = &core->ipc.msdu;
	struct sk_buff *skb;
	struct sk_buff *match = NULL;
	unsigned long flags;

	spin_lock_irqsave(&msdu->hifdone.lock, flags);
	skb_queue_walk(&msdu->hifdone, skb)
	{
		struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);
		addr32 this_addr = txcb->phyaddr + HEADROOM_HIF_HTC_TXDESC;

		BUG_ON(!txcb->phyaddr);
		if (msdu_addr == this_addr) {
			__skb_unlink(skb, &msdu->hifdone);
			match = skb;
			WQ_DBG(DM_IPC, DL_VRB,
			       "%s: msdu_addr %x phyaddr %llx + %d \n",
			       __func__, msdu_addr, (u64)txcb->phyaddr,
			       txcb->total_dma_len);
			break;
		}
	}

	if (!match) {
		struct macdone_entry *macdone;
		struct list_head *pos;
		u32 freecnt = 0, headcnt = 0;

		macdone = list_first_entry_or_null(&msdu->macdone.free,
						   struct macdone_entry, entry);
		if (!macdone) {
			if (!list_empty(&msdu->macdone.free)) {
				list_for_each (pos, &msdu->macdone.free) {
					freecnt++;
				}
			}
			if (!list_empty(&msdu->macdone.head)) {
				list_for_each (pos, &msdu->macdone.head) {
					headcnt++;
				}
			}
			WQ_DBG(DM_IPC, DL_ERR,
			       "%s: msdu_addr %x, freeisempty:%u, headisempty:%u, freecnt:%u, headcnt:%u, "
			       "fwdone:%u - %u, hidone:%u - %u, lltx:%u - %u, hifdonelist:%u\n",
			       __func__, msdu_addr,
			       list_empty(&msdu->macdone.free),
			       list_empty(&msdu->macdone.head), freecnt,
			       headcnt, core->hw->ll_fwdone_cnt, core->hw->ll_fwdone_free_cnt,
			       core->hw->ll_hifdone_cnt, core->hw->ll_hifdone_free_cnt,
			       core->hw->ll_pkt_cnt, core->hw->free_ll_pkt_cnt,
			       skb_queue_len(&msdu->hifdone));
			BUG_ON(!macdone);
		}
		if (macdone) {
			macdone->addr = msdu_addr;
			list_move(&macdone->entry, &msdu->macdone.head);
		}
	}
	spin_unlock_irqrestore(&msdu->hifdone.lock, flags);

	return match;
}

static void wq_ipc_msdu_macdone(struct wq_core *core, addr32 *msdu_addrs,
				int cnt)
{
	u16 i;

	for (i = 0; i < cnt; i++) {
		addr32 msdu_addr = __le32_to_cpu(msdu_addrs[i]);
		struct sk_buff *skb;

		if (!msdu_addr)
			break;
		core->hw->ll_fwdone_cnt++;
		skb = __wq_ipc_msdu_macdone(core, msdu_addr);
		if (skb) {
			core->hw->ll_fwdone_free_cnt++;
			rwnx_txdatacfm(core->hw, skb);
		}
	}
}

static int wq_ipc_msdu_hifdone(struct wq_core *core, struct sk_buff *skb)
{
	struct msdu_txdone *msdu = &core->ipc.msdu;
	addr32 this_addr = WQ_SKB_TXCB(skb)->phyaddr + HEADROOM_HIF_HTC_TXDESC;
	struct macdone_entry *macdone;
	unsigned long flags;
	bool done = false;

	spin_lock_irqsave(&msdu->hifdone.lock, flags);
	list_for_each_entry (macdone, &msdu->macdone.head, entry) {
		if (macdone->addr == this_addr) {
			list_move(&macdone->entry, &msdu->macdone.free);
			done = true;
			break;
		}
	}
	if (!done)
		__skb_queue_tail(&msdu->hifdone, skb);
	spin_unlock_irqrestore(&msdu->hifdone.lock, flags);

	core->hw->ll_hifdone_cnt++;

	if (done) {
		core->hw->ll_hifdone_free_cnt++;
		rwnx_txdatacfm(core->hw, skb);
	}
	return 0;
}

int wq_ipc_msdu_tx_is_all_done(struct wq_core *core)
{
	struct msdu_txdone *msdu = &core->ipc.msdu;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&msdu->hifdone.lock, flags);
	if (list_empty(&msdu->macdone.head) && (0 == skb_queue_len(&msdu->hifdone))) {
		ret = 1;
	}
	spin_unlock_irqrestore(&msdu->hifdone.lock, flags);

	return ret;
}

static inline void wq_ipc_msdu_txdone_init(struct msdu_txdone *msdu)
{
	int i;

	skb_queue_head_init(&msdu->hifdone);

	INIT_LIST_HEAD(&msdu->macdone.head);
	INIT_LIST_HEAD(&msdu->macdone.free);
	for (i = 0; i < ARRAY_SIZE(msdu->macdone.pool); i++)
		list_add_tail(&msdu->macdone.pool[i].entry,
			      &msdu->macdone.free);
}

static int wq_e2a_msg_param_len(enum e2a_event_id id, struct ipc_e2a_msg *e2a,
				u32 len)
{
	u16 param_len = 0;

	if (len < IPC_E2A_MSG_HDR_LEN ||
	    len != (IPC_E2A_MSG_HDR_LEN +
		    (param_len = __le16_to_cpu(e2a->param_len)))) {
		WQ_DBG(DM_IPC, DL_ERR, "e2a_msg(%d) length %d != %d! [%*ph]\n",
		       id, len, IPC_E2A_MSG_HDR_LEN + param_len, len, e2a);
		return -1;
	}
	return param_len;
}

void wq_calculate_per(struct rwnx_hw *rwnx_hw,
	struct peer_tx_stats *old, struct peer_tx_stats *now)
{
	int delta_xmit_success;
	int delta_retry_cnt;
	int pkt_err_rate = 0;

	delta_xmit_success =
		(now->single_xmit_success - old->single_xmit_success) +
		(now->mpdu_xmit_success - old->mpdu_xmit_success);
	delta_retry_cnt =
		8 * (now->single_fail_cnt - old->single_fail_cnt) +
		(now->mpdu_retry_cnt - old->mpdu_retry_cnt);

	if ((delta_retry_cnt + delta_xmit_success))
		pkt_err_rate = ((100 * delta_retry_cnt) / (delta_retry_cnt + delta_xmit_success));

	if (rwnx_hw->enable_show_tx_info) {
		WQ_DBG(DM_TX, DL_WRN,
			"single_xmit_success: (%u/%u), mpdu_xmit_success: (%u/%u), single_fail_cnt: (%u/%u), mpdu_retry_cnt: (%u/%u), mac_total_tx_cnt: (%u/%u), PER: %d%%\n",
			old->single_xmit_success, now->single_xmit_success,
			old->mpdu_xmit_success, now->mpdu_xmit_success,
			old->single_fail_cnt, now->single_fail_cnt,
			old->mpdu_retry_cnt, now->mpdu_retry_cnt,
			old->mac_total_tx_cnt, now->mac_total_tx_cnt,
			pkt_err_rate);
	}
}

static void wq_peer_tx_info_event(struct rwnx_hw *rwnx_hw,
	struct target_peer_info *peer_info)
{
	int i;
	struct peer_tx_stats *peer_tx_stats;

	//printk(KERN_ERR "peer_num: %d, index: %d, flags: 0x%04x\n",
	//      peer_info->peer_num, peer_info->index, peer_info->flags);
	peer_tx_stats = &peer_info->stats[0];
	for (i = 0; i < peer_info->peer_num; i++) {
		int j;
		struct rwnx_sta *rwnx_sta;

		if (rwnx_hw->enable_show_tx_info) {
			WQ_DBG(DM_TX, DL_WRN,
				"STA[%d]: sw_retry_step: %u, retry_step[0]: %u, retry_step[1]: %u, retry_step[2]: %u, retry_step[3]: %u\n",
				peer_tx_stats->sta_idx,
				peer_tx_stats->sw_retry_step,
				peer_tx_stats->retry_step_idx[0],
				peer_tx_stats->retry_step_idx[1],
				peer_tx_stats->retry_step_idx[2],
				peer_tx_stats->retry_step_idx[3]);
			for (j = 0; j <= peer_tx_stats->sw_retry_step; j++) {
				char buf[256];
				int r_idx = 0;
				int len;

				len = print_rate_from_cfg(buf, sizeof(buf),
					peer_tx_stats->rate_config[j],
					&r_idx, 0);

				if (len)
					WQ_DBG(DM_TX, DL_WRN, "%s\n", buf);
			}
		}

		rwnx_sta = &rwnx_hw->sta_table[peer_tx_stats->sta_idx];
		wq_calculate_per(rwnx_hw,
			&rwnx_sta->stats.tx_info.tx_stats,
			peer_tx_stats);

		memcpy(&rwnx_sta->stats.tx_info.tx_stats, peer_tx_stats,
			sizeof(struct peer_tx_stats));
		rwnx_sta->stats.tx_info.timestamp = jiffies;
		peer_tx_stats++;
	}
}

static void wq_ipc_event_handler(struct wq_core *core, enum e2a_event_id id,
				 void *payload, u32 len)
{
	struct rwnx_hw *rwnx_hw = core->hw;
	struct ipc_e2a_msg *e2a = payload; /* most of them is e2a_msg */
	int param_len = -1;
	static u32 tracer_dump_event_num;

	if (id == MAC_E2A_FWREADY && wq_ipc_can_handle_FWREADY(core) &&
	    (param_len = wq_e2a_msg_param_len(id, e2a, len)) >= 0) {
		WQ_DBG(DM_IPC, DL_INF, "%s: wlan firmware is ready\n",
		       dev_name(core->dev));
		wq_core_state_set(core, WQ_CORE_STATE_WLAN_FW_READY);
		complete_all(&core->fw_ready);
		/* later HIF will create and register wlan interface(s) */
		return;
	}

	if (id >= MAC_E2A_LAST || wq_ipc_is_not_ready(core)) {
		WQ_DBG(DM_IPC, DL_WRN, "%s: drop, id=%d, state=%s\n", __func__,
		       id, wq_core_state_name(core));
		return;
	}

	BUG_ON(!rwnx_hw);

#undef WQ_EVENT_LEN_ASSERT
#define WQ_EVENT_LEN_ASSERT(_len, _struct)                                     \
	if ((_len) != sizeof(_struct))                                     \
		WQ_DBG(DM_IPC, DL_INF,                                         \
		       "%s: event len %d != %d(size of %s)! [%*ph...]\n",      \
		       __func__, _len, (u32)sizeof(_struct), #_struct, 16,     \
		       payload);

	/* 2. event process */
	switch (id) {
	case MAC_E2A_UNSUP_RX_VEC:
	case MAC_E2A_RADAR:
	case MAC_E2A_TBTT_SEC:
	case MAC_E2A_TBTT_PRIM:
	case MAC_E2A_RXDESC:
		break;
	case MAC_E2A_MSG_ACK:
		param_len = wq_e2a_msg_param_len(id, e2a, len);
		if (param_len == 0)
			rwnx_msgackind(rwnx_hw, e2a);
		break;
	case MAC_E2A_MSG:
		param_len = wq_e2a_msg_param_len(id, e2a, len);
		if (param_len >= 0)
			rwnx_rx_handle_msg(rwnx_hw, e2a);
		break;
	case MAC_E2A_DBG:
		break;
	case MAC_E2A_FWREADY:
		/* already handled */
		break;
	case MAC_E2A_TRIGGER_PATTERN:
		WQ_EVENT_LEN_ASSERT(len, u16);
		hif_send_trigger(core, WQ_USB_TRI_EVENT, *((u16 *)payload));
		break;
	case MAC_E2A_PACKET_DUMP:
#ifndef PHY_ADC_DUMP
		WQ_EVENT_LEN_ASSERT(len / PKTDUMP_COUNT, WIFI_DBG_PKTDUMP);
		wq_pktlog_save(&rwnx_hw->pktlog, payload, sizeof(WIFI_DBG_PKTDUMP) * PKTDUMP_COUNT);
#else
		WQ_DBG(DM_IPC, DL_WRN, "PHY ADC DUMP START\n");
		wq_pktlog_save(&rwnx_hw->pktlog, payload, 1024);//PHY_ADC_DUMP_LEN);
#endif
		break;
	case MAC_E2A_BAM_ADDBA:
	case MAC_E2A_BAM_DELBA:
	case MAC_E2A_BAM_BAR:
		/* FIXME: check len */
		ht_handle_bam_event(rwnx_hw, id, payload);
		break;
	case MAC_E2A_MGMT_CFM:
		WQ_EVENT_LEN_ASSERT(len, struct rwnx_mgmt_frame_status);
		wq_ipc_mgmt_txdone(core,
				   (struct rwnx_mgmt_frame_status *)payload);
		break;
	case MAC_E2A_HML_RX_MGMT: {
		struct hml_mgmt_to_host *pkt =
			(struct hml_mgmt_to_host *)payload;
		struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[pkt->vif_index];

		WQ_EVENT_LEN_ASSERT(len, struct hml_mgmt_to_host);
		WQ_DBG(DM_IPC, DL_WRN,
		       "%s::vif_idx=%d,req=%d,rssi=%d,buf_len=%d\n", __func__,
		       pkt->vif_index, pkt->freq, pkt->rssi, pkt->buf_len);
		cfg80211_rx_mgmt(&rwnx_vif->wdev, pkt->freq, pkt->rssi,
				 pkt->frame_buf, pkt->buf_len, 0);
		break;
	}
	case MAC_E2A_EAPOL_M4_TX_STATUS: {
		struct eapol_m4_tx_status *eapol_m4_tx_status =
			(struct eapol_m4_tx_status *)payload;
		WQ_DBG(DM_IPC, DL_WRN, "%s(%d) MAC_E2A_EP4_TX_STATUS ack=%d\n",
		       __func__, __LINE__, eapol_m4_tx_status->ack);

		WQ_EVENT_LEN_ASSERT(len, struct eapol_m4_tx_status);
		if (eapol_m4_tx_status->ack) {
			spin_lock_bh(&rwnx_hw->delayed_key_lock);
			rwnx_hw->key_add_params.m4_ack_done = 1;
			spin_unlock_bh(&rwnx_hw->delayed_key_lock);
			//add ptk
			WQ_DBG(DM_IPC, DL_WRN, "schedule key add work\n");
			schedule_work(&rwnx_hw->add_key_task);
			del_timer(&rwnx_hw->key_add_timer);
		}
		break;
	}
	case MAC_E2A_TX_FLOW_CTRL: {
		u8 *throttling = (u8 *)payload;

		WQ_EVENT_LEN_ASSERT(len, u8);
		WQ_DBG(DM_IPC, DL_WRN,
		       "FIXME: should handle throttling: %d by firmware\n",
		       *throttling);
		BUG();
		break;
	}
	case MAC_E2A_TXDONE_REPORT:
		param_len = wq_e2a_msg_param_len(id, e2a, len);
		if (param_len > 0)
			wq_ipc_msdu_macdone(core, e2a->param,
					    param_len / sizeof(addr32));
		break;
	case MAC_E2A_PEER_INFO: {
		struct target_peer_info *peer_info;

		peer_info = (struct target_peer_info *)e2a->param;
		wq_peer_tx_info_event(rwnx_hw, peer_info);
	}
		break;
	case MAC_E2A_TRACER_DUMP: {
		memcpy(rwnx_hw->tracer.payload[tracer_dump_event_num], payload, 1024);
		tracer_dump_event_num++;
		if (tracer_dump_event_num == NUM_EVENT_OF_TRACER_DUMP) {
			tracer_dump_event_num = 0;
			schedule_work(&rwnx_hw->tracer_dump_task);
		}
	}
		break;
	default:
		BUG();
		break;
	}
#undef WQ_EVENT_LEN_ASSERT
}

static int wq_ipc_rx_msg(struct htc_q *q, struct sk_buff *skb)
{
	struct wq_core *core = container_of(q, struct wq_core, htc.rxq.msg);
	struct wq_htc_v0 *htc_v0 = ((struct wq_htc_v0 *)(skb->data)) - 1;
	struct rwnx_hw *rwnx_hw = core->hw;
	u64 time_start_us = 0, time_end_us = 0;

	u32 flags = le32_to_cpu(htc_v0->flags);
	enum wq_ipc_types type = WQ_IPC_TPE(flags);
	u32 seq = WQ_IPC_SEQ(flags);
	u32 status = WQ_IPC_STS(flags);
	u32 u = le32_to_cpu(htc_v0->u.evt_id);

	if (!wq_ipc_can_handle_FWREADY(core)) {
		WQ_DBG(DM_IPC, DL_ERR, "%s: ipc drop (%s, skb %p)!\n", __func__,
		       wq_core_state_name(core), skb);
		return -1;
	}

	time_start_us = (u64)ktime_to_us(ktime_get());

	WQ_DBG(DM_IPC, DL_VRB,
	       "%s(%d) type=%d, status=%d, seq=%d, u=%d, len=%d, skb len=%d\n",
	       __func__, __LINE__, type, status, seq, u, skb->len, skb->len);

	switch (type) {
	case WQ_IPC_TPE_EVT:
		wq_ipc_event_handler(core, u, skb->data, skb->len);
		break;
	default:
		dump_bytes(DL_WRN, (char *)__func__, skb->data, skb->len);
		WQ_DBG(DM_IPC, DL_OOPS, "%s: wrong type, flags = 0x%x\n",
		       __func__, flags);
		BUG();
		break;
	}

	WARN_ON(skb->next);
	dev_kfree_skb_any(skb);

	time_end_us = (u64)ktime_to_us(ktime_get());

	if (rwnx_hw) {
		atomic_add((u32)(time_end_us - time_start_us), &rwnx_hw->ipc_rx_msg_time);
	}

	return 0;
}

/* rwnx_hw->tx_lock is locked */
int wq_ipc_tx_pkt_bundle_usb(struct wq_core *core, u8 hw_txq,
			     struct sk_buff_head *skbq)
{
	static const enum wq_hif_qid pkt_qid_map[] = {
		WQ_QID_AC_BK, WQ_QID_AC_BE,
		WQ_QID_AC_VI, WQ_QID_AC_VO, /* alias of MGMT */
		WQ_QID_AC_BK, /* BC/MC */
	};

	struct sk_buff *skb;
	enum wq_hif_qid qid;
	int ret;

	PROFILING_SET(SW_PROF_IPC_TX_PKT);

	BUG_ON(hw_txq >= ARRAY_SIZE(pkt_qid_map));
	qid = pkt_qid_map[hw_txq];

	BUG_ON(!(skb = skb_peek(skbq)));
	if (wq_core_is_in_deep_suspend(core) || core->flags.suspend) {
		WQ_DBG(DM_IPC, DL_ERR, "%s: device is suspend.\n", __func__);
		ret = -EIO;
	} else {
		ret = htc_tx(core, qid, skbq, 0);
	}

	if (ret) {
		while ((skb = __skb_dequeue(skbq))) {
			struct txdesc_host *txdesc_host = (struct txdesc_host*)((u8 *)skb->data);
			struct hostdesc *host = &txdesc_host->api.host;

			WQ_DBG(DM_IPC, DL_ERR, "%s: qid=%d return tx credit gid=%u, tid=%u\n",
				__func__, qid, host->via_grp_id, host->via_type_id);

			rwnx_return_dev_credit_ex(core->hw, host->via_grp_id, host->via_type_id);
			wq_tx_skb_free_any(core, skb);
		}
	}
	PROFILING_CLR(SW_PROF_IPC_TX_PKT);

	return ret;
}

/* rwnx_hw->tx_lock is locked */
int wq_ipc_tx_pkt_bundle(struct wq_core *core, u8 hw_txq,
			 struct sk_buff_head *skbq)
{
	static const enum wq_hif_qid pkt_qid_map[] = {
		WQ_QID_AC_BK, WQ_QID_AC_BE,
		WQ_QID_AC_VI, WQ_QID_AC_VO, /* alias of MGMT */
		WQ_QID_AC_BK, /* BC/MC */
	};

	struct sk_buff *skb;
	enum wq_hif_qid qid;
	int ret;

	PROFILING_SET(SW_PROF_IPC_TX_PKT);

	BUG_ON(hw_txq >= ARRAY_SIZE(pkt_qid_map));
	qid = pkt_qid_map[hw_txq];

	BUG_ON(!(skb = skb_peek(skbq)));
	if (wq_core_is_in_deep_suspend(core) || core->flags.suspend) {
		WQ_DBG(DM_IPC, DL_ERR, "%s: device is suspend.\n", __func__);
		ret = -EIO;
	} else {
		struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);
		u32 pkt_flags = WQ_IPC_FLAGS_TX_NORMAL_QUEUE;
		if (txcb->pkt_cls & BIT(WQ_PKT_CLS_EAPOL))
			pkt_flags = WQ_IPC_FLAGS_TX_HIGH_QUEUE;
		ret = htc_tx(core, qid, skbq, pkt_flags);
	}
	if (ret && ret != -ENOBUFS && ret != -EIO) {
		while ((skb = __skb_dequeue(skbq))) {
			struct txdesc_host *txdesc_host =
				(struct txdesc_host *)((u8 *)skb->data +
						       HEADROOM_HIF_HTC);
			struct hostdesc *host = &txdesc_host->api.host;

			WQ_DBG(DM_IPC, DL_ERR,
			       "%s: qid=%d return tx credit gid=%u, tid=%u\n",
			       __func__, qid, host->via_grp_id,
			       host->via_type_id);

			rwnx_return_dev_credit_ex(core->hw, host->via_grp_id,
						  host->via_type_id);
			wq_tx_skb_free_any(core, skb);
		}
	}
    PROFILING_CLR(SW_PROF_IPC_TX_PKT);
	return ret;
}

int wq_ipc_tx_msg_done(struct htc_q *q, struct sk_buff *skb)
{
	struct wq_core *core =
		container_of(q, struct wq_core, htc.txq[q->qid].up);
	WQ_DBG(DM_IPC, DL_VRB, "%s: skb=%p+%d\n", __func__, skb, skb->len);
	wq_tx_skb_free_any(core, skb);
	return 0;
}

int wq_ipc_tx_pkt_done_pre(struct wq_core *core, struct sk_buff *skb, int status)
{
	struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);
	int i = 0;

	/* FIXME: how to deal if MAC tx done is earlier? */
	if (txcb->pkt_cls & BIT(WQ_PKT_CLS_80211)) {
		struct txdesc_host *desc = (struct txdesc_host *)(skb->data);

		BUG_ON(desc->api.host.end_marker != HOST_DESC_END_MARKER);

		skb_queue_tail(&core->ipc.mgmt_txdone, skb);
		return -1;
	}

	// do rwnx_txq_tx_done_pre usb_out_bundle_num times for USB interface
	if (txcb->usb_out_bundle_num) {
		i = txcb->usb_out_bundle_num;
	}
	do {
		rwnx_txq_tx_done_pre(core->hw, txcb->txq_idx);
		if (status > 0 && !core->hw->feature.is_suspend) {
			rwnx_txq_start_all(core->hw, TXQ_STOP_REASON_CE_WATERMARK);
		}
		i--;
	} while (i > 0);

	return 0;
}

int wq_ipc_tx_pkt_done(struct htc_q *q, struct sk_buff *skb)
{
	struct wq_core *core =
		container_of(q, struct wq_core, htc.txq[q->qid].up);
	struct rwnx_hw *hw = core->hw;
	struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);
	struct txdesc_host *desc = (struct txdesc_host *)(skb->data);
	if (txcb->pkt_cls & (BIT(WQ_PKT_CLS_TCP_ACK))) {
		struct rwnx_tx_bundle_head *bundle_head = (struct rwnx_tx_bundle_head *)skb->data;
		if (bundle_head->pattern == 0xA5A5 &&
			bundle_head->skb_len + sizeof(struct rwnx_tx_bundle_head) == skb->len) {
			skb_push(skb, HEADROOM_TXDESC);
			desc = (struct txdesc_host *)(skb->data);
		}
	}
	BUG_ON(!hw);
	BUG_ON(desc->api.host.end_marker != HOST_DESC_END_MARKER);
	BUG_ON(TXQ_RING_FUNCTION_ENABLE == 1 ? txcb->msdu_in_host : 0);

	WQ_DBG(DM_IPC, DL_VRB,
	       "%s: qid=%d msdu_in_host=%u skb data@%p+%d status=%x/%x\n",
	       __func__, txcb->qid, txcb->msdu_in_host, skb->data, skb->len,
	       desc->api.host.status_desc_addr,
	       (u32)(txcb->phyaddr + HEADROOM_TXDESC - 48));
	if (!TXQ_RING_FUNCTION_ENABLE && txcb->msdu_in_host) {
		return wq_ipc_msdu_hifdone(core, skb);
	}

	return rwnx_txdatacfm(hw, skb);
}

static int wq_ipc_rx_pkt(struct htc_q *q, struct sk_buff *skb)
{
	struct wq_core *core = container_of(q, struct wq_core, htc.rxq.pkt);
	struct rwnx_hw *hw = core->hw;

#ifdef CONFIG_RX_THREAD
	local_bh_disable();
#endif

	__wq_ipc_rx_pkt(hw, skb);

#ifdef CONFIG_RX_THREAD
	local_bh_enable();
#endif

	return 0;
}

int __wq_ipc_rx_pkt(struct rwnx_hw *hw, struct sk_buff *skb)
{
	u64 time_start_us = 0, time_end_us = 0;

	if (WARN_ON(!hw))
		return -1;

	PROFILING_SET(SW_PROF_IPC_RX_PKT);

	time_start_us = (u64)ktime_to_us(ktime_get());

	if (likely(hw->rx_ll.rx_ll_support)) {
		rwnx_rxdataind_ll(hw, skb);
	} else {
		rwnx_rxdataind(hw, skb);
	}

	time_end_us = (u64)ktime_to_us(ktime_get());
	atomic_add((u32)(time_end_us - time_start_us), &hw->ipc_rx_pkt_time);

	PROFILING_CLR(SW_PROF_IPC_RX_PKT);

	return 0;
}

void wq_ipc_txq_ring_free(struct wq_core *core)
{
	struct rwnx_hw *hw = core->hw;
	rwnx_hwq_ll_data_free(hw);
}

void wq_ipc_txq_ring_2task(struct wq_core *core)
{
	htc_txq_ring_2task(core);
}

void wq_ipc_txq_ring_start_timer(struct wq_core *core)
{
	htc_txq_ring_start_timer(core);
}

int wq_ipc_init(struct wq_core *core)
{
	struct wq_ipc *ipc = &core->ipc;
	struct htc *htc = &core->htc;
	enum wq_hif_qid qid;

	skb_queue_head_init(&ipc->mgmt_txdone);

	wq_ipc_msdu_txdone_init(&ipc->msdu);

	htc_q_init(core, &htc->rxq.msg, WQ_QID_MSG, wq_ipc_rx_msg);
#ifdef CONFIG_RX_THREAD
	if (core->config.force_rx_use_tasklet == false)
		htc_q_init_thread(core, &htc->rxq.pkt, WQ_QID_AC_BK,
				  wq_ipc_rx_pkt);
	else
#endif

	htc_q_init(core, &htc->rxq.pkt, WQ_QID_AC_BK, wq_ipc_rx_pkt);
	for (qid = 0; qid < ARRAY_SIZE(htc->txq); qid++) {
		htc_q_init(core, &htc->txq[qid].up, qid,
			   qid == WQ_QID_MSG ? wq_ipc_tx_msg_done :
					       wq_ipc_tx_pkt_done);
	}
	htc_tx_waitq_init(core);
	return 0;
}

void wq_ipc_deinit(struct wq_core *core)
{
	struct htc *htc = &core->htc;
	enum wq_hif_qid qid;

	htc_q_deinit(&htc->rxq.msg);
	htc_q_deinit(&htc->rxq.pkt);
	for (qid = 0; qid < ARRAY_SIZE(htc->txq); qid++)
		htc_q_deinit(&htc->txq[qid].up);

	wq_ipc_mgmt_txdone_clean(core);
	wq_ipc_msdu_txdone_clean(core);
}
