/**
 ****************************************************************************************
 *
 * @file rwnx_msg_rx.h
 *
 * @brief RX function declarations
 *
 * Copyright (C) RivieraWaves 2012-2020
 *
 ****************************************************************************************
 */

#ifndef _RWNX_MSG_RX_H_
#define _RWNX_MSG_RX_H_

#include "fw_api/wifi/mac/cp_api.h"
#include "rwnx_defs.h"

void rwnx_disconnect_task(struct work_struct *w);
void rwnx_rx_handle_msg(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg);
void rwnx_msgackind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *ack);
void rwnx_set_sta_amsdu_len_from_htcap(struct rwnx_sta *sta, u16 htcap_info);
void rwnx_set_sta_amsdu_len_from_vhtcap(struct rwnx_sta *sta, u16 vhtcap_info);
void rwnx_roc_timeout(struct timer_list *roc_timer);
void rwnx_bcn_change_task(struct work_struct *w);
u8 vht_mcs_map_to_mcs_val(u8 vht_mcs_map);
u8 he_mcs_map_to_mcs_max(u8 he_mcs_map);
void rwnx_tracer_dump_task(struct work_struct *w);

#endif /* _RWNX_MSG_RX_H_ */
