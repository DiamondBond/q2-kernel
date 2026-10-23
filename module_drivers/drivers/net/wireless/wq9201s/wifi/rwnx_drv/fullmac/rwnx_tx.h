/**
 ******************************************************************************
 *
 * @file rwnx_tx.h
 *
 * Copyright (C) RivieraWaves 2012-2020
 *
 ******************************************************************************
 */
#ifndef _RWNX_TX_H_
#define _RWNX_TX_H_

#include "fw_api/wifi/mac/dp_tx.h"
#include "fw_api/wifi/mac/cp_api.h"

#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <linux/netdevice.h>
#include "rwnx_txq.h"
#include "hal_desc.h"

#define IPC_TX_MAX_HEADROOM (u32)(HEADROOM_HIF_HTC_TXDESC + RWNX_TX_ALIGN_SIZE)

#define RWNX_HWQ_BK 0
#define RWNX_HWQ_BE 1
#define RWNX_HWQ_VI 2
#define RWNX_HWQ_VO 3
#define RWNX_HWQ_BCMC 4
#define RWNX_HWQ_NB NX_TXQ_CNT
#define RWNX_HWQ_ALL_ACS (RWNX_HWQ_BK | RWNX_HWQ_BE | RWNX_HWQ_VI | RWNX_HWQ_VO)
#define RWNX_HWQ_ALL_ACS_BIT                                                   \
	(BIT(RWNX_HWQ_BK) | BIT(RWNX_HWQ_BE) | BIT(RWNX_HWQ_VI) |              \
	 BIT(RWNX_HWQ_VO))

#define RWNX_TX_LIFETIME_MS 100
#define RWNX_TX_MAX_RATES NX_TX_MAX_RATES

#define AMSDU_PADDING(x) ((4 - ((x)&0x3)) & 0x3)

extern const int rwnx_tid2hwq[IEEE80211_NUM_TIDS];

/*
 * define the threshold value of tx tput, unit Mbps
 * if > this value, PCIE would wait for 1000us to bundle
 * else 200us instead
 */
#define PCIE_MAX_BUNDLE_WAIT_TIME_H_TPUT_THRD  550

/* define the time to wait for incoming pkt aggregation on low tput mode, unit us */
#define PCIE_MAX_BUNDLE_WAIT_TIME_L_US 200
/* define the time to wait for incoming pkt aggregation on high tput mode, unit us */
#define PCIE_MAX_BUNDLE_WAIT_TIME_H_US 1000

/* define the cnt to tx pkt, unit us */
#define PCIE_MAX_BUNDLE_MPDU_PER_PPDU_CNT 144

/**
 * struct rwnx_amsdu - Structure to manage creation of an A-MSDU, updated
 * only In the first subframe of an A-MSDU
 *
 * @hdrs List of subframe of rwnx_amsdu_txhdr
 * @len  Current size for this A-MDSU (doesn't take padding into account)
 *       0 means that no amsdu is in progress
 * @nb   Number of subframe in the amsdu
 * @pad  Padding to add before adding a new subframe
 */
struct rwnx_amsdu {
	struct list_head hdrs;
	u16 len;
	u8 nb;
	u8 pad;
};

struct rwnx_amsdu_param {
	u16 max_len;
	u16 max_packets_num;
	unsigned long timeout;
	bool enable;
};

struct rwnx_ampdu_param {
	u8 max_aggr_num;
};

struct rwnx_dbg_debug_flag_set {
	u32 debug_type;
	u32 value1;
};

enum { TXQ_RING_READ,
       TXQ_RING_WRITE,
};

struct rwnx_msdu_txdone_rcd {
	/* record txqring cnt */
	u32 push_txqring_cnt[TXQ_RING_ID_MAX];
	u32 pop_txqring_cnt[TXQ_RING_ID_MAX];
	struct sk_buff *ring0_skb_tbl[CONFIG_RING0_SZ];
	struct sk_buff *ring1_skb_tbl[CONFIG_RING1_SZ];
	struct sk_buff *ring2_skb_tbl[CONFIG_RING2_SZ];
	struct sk_buff *ring3_skb_tbl[CONFIG_RING3_SZ];
};

struct rwnx_msdu_txdone_status {
	struct rwnx_msdu_txdone_rcd txq_ring_rcd[TXQ_RING_RCD_SOFTAP_MAX];
};

#define RWNX_TX_ALIGN_SIZE 4
#define RWNX_TX_ALIGN_MASK (RWNX_TX_ALIGN_SIZE - 1)

u16 rwnx_select_txq(struct rwnx_vif *rwnx_vif, struct sk_buff *skb);
netdev_tx_t rwnx_start_xmit(struct sk_buff *skb, struct net_device *dev);
int rwnx_start_mgmt_xmit(struct rwnx_vif *vif, struct rwnx_sta *sta,
			 struct cfg80211_mgmt_tx_params *params, bool offchan,
			 u64 *cookie);
int rwnx_txdatacfm(struct rwnx_hw *rwnx_hw, struct sk_buff *skb);

struct rwnx_hw;
struct rwnx_sta;
void rwnx_set_traffic_status(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
			     bool available, u8 ps_id);
void rwnx_ps_bh_enable(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
		       bool enable);
void rwnx_ps_bh_traffic_req(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
			    u16 pkt_req, u8 ps_id);

void rwnx_switch_vif_sta_txq(struct rwnx_sta *sta, struct rwnx_vif *old_vif,
			     struct rwnx_vif *new_vif);

int rwnx_dbgfs_print_sta(char *buf, size_t size, struct rwnx_sta *sta,
			 struct rwnx_hw *rwnx_hw);
void rwnx_txq_credit_update(struct rwnx_hw *rwnx_hw, int sta_idx, u8 tid,
			    s8 update);
void rwnx_tx_push_prepare(struct rwnx_hw *rwnx_hw, struct txdesc_host *txhdr,
			  struct rwnx_txq *txq);
#if defined(CONFIG_HIGH_RES_TIMERS) && defined(USE_HRT)
enum hrtimer_restart rwnx_tx_amsdu_timeout_cb(struct hrtimer *t);
void amsdu_task(unsigned long data);
#else
void rwnx_tx_amsdu_timeout_cb(struct timer_list *t);
#endif
void rwnx_amsdu_tx_drain(struct rwnx_txq *txq);

int rwnx_hwq_ll_data_free(struct rwnx_hw *rwnx_hw);
void rwnx_hwq_ring_push(struct rwnx_hw *rwnx_hw, int hwqid,
			struct sk_buff *skb);

int rwnx_hwq_ll_data_idx_check(struct rwnx_hw *rwnx_hw);

void rwnx_hwq_process_lock(struct rwnx_hw *rwnx_hw);
void rwnx_tx_refill_hostdesc(struct sk_buff_head *skbq);

#endif /* _RWNX_TX_H_ */
