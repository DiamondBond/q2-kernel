/**
 ******************************************************************************
 *
 * @file wq_tx_credit.h
 *
 * Copyright (C) WUQi-Tech 2012-2021
 *
 ******************************************************************************
 */

#ifndef _RWNX_TX_CREDIT_H_
#define _RWNX_TX_CREDIT_H_

#include "fw_api/wifi/htc/tx_credit.h"

#define WQ_INVALID_CRDT_ID 0xFF

struct credit_grp {
	uint8_t size[WQ_CREDIT_TYPE_NUM]; //total credit size
	uint8_t credit[WQ_CREDIT_TYPE_NUM]; //available credit
	uint8_t lend[WQ_CREDIT_TYPE_NUM]; //lend credit
	unsigned long
		tick[WQ_CREDIT_TYPE_NUM]; //last time to get credit for self-use
	uint8_t type[NX_TXQ_CNT]; //credit type
	uint8_t is_used;
};

struct credit_mgmt {
	uint8_t enabled;
	uint8_t dev_credit_sz; //dev totoal credit size
	uint8_t active_group; //bit0: group0, bit1: group1
	uint8_t drv_crdt_num; //driver total available credit
	struct credit_grp credit_grp[WQ_CREDIT_GROUP_NUM];
	spinlock_t credit_mgmt_lock;
	unsigned long get_tick; //latest get credit tick
	unsigned long want_tick; //latest want credit tick
};

bool rwnx_credit_mgmt_init(struct credit_mgmt *crdt_mgmt, u8 crdt_sz);

bool rwnx_add_credit_grp(struct credit_mgmt *crdt_mgmt, u8 *grp_id);
bool rwnx_del_credit_grp(struct credit_mgmt *crdt_mgmt, u8 grp_id);

bool rwnx_get_dev_credit(struct rwnx_hw *rwnx_hw, u8 hwq_id, u8 vif_idx,
			 u8 is_mgmt, u8 *grp_id, u8 *type_id);
bool rwnx_return_dev_credit(struct rwnx_hw *rwnx_hw, u8 *grp0, u8 *grp1);
bool rwnx_return_dev_credit_ex(struct rwnx_hw *rwnx_hw, u8 grp, u8 type_id);
bool rwnx_renew_dev_credit_mapping(struct rwnx_hw *rwnx_hw, bool qos_flag,
				   u8 grp, u32 *ac_param);
void rwnx_credit_dump_info(struct rwnx_hw *rwnx_hw);

void rwnx_credit_enable(struct rwnx_hw *rwnx_hw, int enable);
void wq_reschedule_hwq(struct wq_core *core);

#endif // _RWNX_TX_CREDIT_H_
