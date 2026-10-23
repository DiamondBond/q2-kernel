/**
 ******************************************************************************
 *
 * @file rwnx_main.h
 *
 * Copyright (C) RivieraWaves 2012-2020
 *
 ******************************************************************************
 */

#ifndef _RWNX_MAIN_H_
#define _RWNX_MAIN_H_

#include "rwnx_defs.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0)
#define INCLUDE_WQ_IWPRIVE
#endif

int rwnx_cfg80211_init(struct wq_core *core);
void rwnx_cfg80211_deinit(struct rwnx_hw *rwnx_hw);

int rwnx_cfg80211_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wowl);
int rwnx_cfg80211_resume(struct wiphy *wiphy);
void rwnx_cfg80211_timer_shutdown(struct rwnx_hw *rwnx_hw);
void rwnx_cfg80211_timer_setup(struct rwnx_hw *rwnx_hw);
void rwnx_eid_update_nss_param(struct rwnx_hw *rwnx_hw, u8 *buf, u16 buf_len);

void rwnx_wdev_unregister(struct rwnx_hw *rwnx_hw);
u8 *rwnx_bcn_chan_change(struct rwnx_bcn *bcn, u32 chan, u8 band, bool need_csa);
int rwnx_send_ch_switch(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
				struct cfg80211_chan_def chandef);
u8 *rwnx_bcn_nss_update(struct rwnx_hw *rwnx_hw, struct rwnx_bcn *bcn);
void rwnx_reset_sta_stats(struct rwnx_sta *sta);

#endif /* _RWNX_MAIN_H_ */
