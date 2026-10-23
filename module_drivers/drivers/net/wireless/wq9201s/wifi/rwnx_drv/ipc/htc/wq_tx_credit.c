/**
 ******************************************************************************
 *
 * @file rwnx_tx_credit.c
 *
 * Copyright (C) WUQi-Tech 2012-2021
 *
 ******************************************************************************
 */

#include "rwnx_defs.h"
#include "wq_log.h"
#include "wq_tx_credit.h"

/****************************
 * CRDT debug
 ****************************/
#define TX_CREDIT_LOG_THR 10 //force credit log threshold

#define TX_CRDT_CGRP_ON_2 1 //support CGRP_ON_2

/*****************************
 * Function
 *****************************/
bool rwnx_credit_mgmt_init(struct credit_mgmt *crdt_mgmt, u8 crdt_sz)
{
	u8 i = 0;
	struct credit_grp *grp[2] = { 0 };
	u32 crdt_ratio[WQ_CREDIT_TYPE_NUM] = { 16, 24, 28, 32 };
	u8 type_crdt_sz[WQ_CREDIT_TYPE_NUM] = { 0 };
	u8 used_crdt = 0;
	u8 grp_id = 0;

	crdt_mgmt->enabled = 1;
	crdt_mgmt->dev_credit_sz = crdt_sz;
	crdt_mgmt->active_group = 0;
	crdt_mgmt->drv_crdt_num = crdt_mgmt->dev_credit_sz;

	spin_lock_init(&crdt_mgmt->credit_mgmt_lock);

	for (i = 0; i < WQ_CREDIT_TYPE_NUM; i++) {
		if (i == (WQ_CREDIT_TYPE_NUM - 1)) {
			type_crdt_sz[i] = crdt_sz - used_crdt;
		} else {
			type_crdt_sz[i] = (crdt_sz * crdt_ratio[i]) / 100;
			used_crdt += type_crdt_sz[i];
		}

		if (type_crdt_sz[i] % 2) {
			grp_id++;
			grp_id = grp_id % 2;
		}

		grp[0] = &(crdt_mgmt->credit_grp[grp_id]);
		grp[1] = &(crdt_mgmt->credit_grp[(grp_id + 1) % 2]);

		grp[1]->size[i] = grp[1]->credit[i] = type_crdt_sz[i] / 2;
		grp[1]->lend[i] = 0;
		grp[1]->tick[i] = jiffies;

		grp[0]->size[i] = grp[0]->credit[i] =
			type_crdt_sz[i] - grp[1]->size[i];
		grp[0]->lend[i] = 0;
		grp[0]->tick[i] = jiffies;
	}

	WQ_DBG(DM_CRDT, DL_WRN, "%s: crdt sz=%u, act grp=%u\n", __func__,
	       crdt_mgmt->dev_credit_sz, crdt_mgmt->active_group);

	WQ_DBG(DM_CRDT, DL_WRN, "grp0 - size=%u:%u:%u:%u\n",
	       crdt_mgmt->credit_grp[0].size[0],
	       crdt_mgmt->credit_grp[0].size[1],
	       crdt_mgmt->credit_grp[0].size[2],
	       crdt_mgmt->credit_grp[0].size[3]);
	WQ_DBG(DM_CRDT, DL_VRB, "     - crdt=%u:%u:%u:%u\n",
	       crdt_mgmt->credit_grp[0].credit[0],
	       crdt_mgmt->credit_grp[0].credit[1],
	       crdt_mgmt->credit_grp[0].credit[2],
	       crdt_mgmt->credit_grp[0].credit[3]);
	WQ_DBG(DM_CRDT, DL_VRB, "     - lend=%u:%u:%u:%u\n",
	       crdt_mgmt->credit_grp[0].lend[0],
	       crdt_mgmt->credit_grp[0].lend[1],
	       crdt_mgmt->credit_grp[0].lend[2],
	       crdt_mgmt->credit_grp[0].lend[3]);
	WQ_DBG(DM_CRDT, DL_VRB, "     - tick=%lu:%lu:%lu:%lu\n",
	       crdt_mgmt->credit_grp[0].tick[0],
	       crdt_mgmt->credit_grp[0].tick[1],
	       crdt_mgmt->credit_grp[0].tick[2],
	       crdt_mgmt->credit_grp[0].tick[3]);

	WQ_DBG(DM_CRDT, DL_WRN, "grp1 - size=%u:%u:%u:%u\n",
	       crdt_mgmt->credit_grp[1].size[0],
	       crdt_mgmt->credit_grp[1].size[1],
	       crdt_mgmt->credit_grp[1].size[2],
	       crdt_mgmt->credit_grp[1].size[3]);
	WQ_DBG(DM_CRDT, DL_VRB, "     - crdt=%u:%u:%u:%u\n",
	       crdt_mgmt->credit_grp[1].credit[0],
	       crdt_mgmt->credit_grp[1].credit[1],
	       crdt_mgmt->credit_grp[1].credit[2],
	       crdt_mgmt->credit_grp[1].credit[3]);
	WQ_DBG(DM_CRDT, DL_VRB, "     - lend=%u:%u:%u:%u\n",
	       crdt_mgmt->credit_grp[1].lend[0],
	       crdt_mgmt->credit_grp[1].lend[1],
	       crdt_mgmt->credit_grp[1].lend[2],
	       crdt_mgmt->credit_grp[1].lend[3]);
	WQ_DBG(DM_CRDT, DL_VRB, "     - tick=%lu:%lu:%lu:%lu\n",
	       crdt_mgmt->credit_grp[1].tick[0],
	       crdt_mgmt->credit_grp[1].tick[1],
	       crdt_mgmt->credit_grp[1].tick[2],
	       crdt_mgmt->credit_grp[1].tick[3]);

	return true;
}

bool rwnx_add_credit_grp(struct credit_mgmt *crdt_mgmt, u8 *grp_id)
{
	struct credit_grp *crdt_grp = &(crdt_mgmt->credit_grp[0]);
	u8 i = 0;
	bool ret = true;
	int j = 0;

	if (!crdt_mgmt->enabled)
		return true;

	spin_lock_bh(&crdt_mgmt->credit_mgmt_lock);

	for (i = 0; i < WQ_CREDIT_GROUP_NUM; i++) {
		if (!(crdt_mgmt->active_group & (0x1 << i))) {
			break;
		}
	}
	if (i < WQ_CREDIT_GROUP_NUM) {
		*grp_id = i;

		crdt_grp[i].is_used = 0;

		for (j = 0; j < RWNX_HWQ_NB; j++) {
			if (j == RWNX_HWQ_BCMC) {
				crdt_grp[i].type[j] = 0;
			} else {
				crdt_grp[i].type[j] = j;
			}
		}
	} else {
		*grp_id = WQ_INVALID_CRDT_ID;
	}

	switch (*grp_id) {
	case 0:
		crdt_mgmt->active_group |= 0x1;
		break;
	case 1:
		crdt_mgmt->active_group |= 0x2;
		break;
	default:
		WQ_DBG(DM_CRDT, DL_ERR,
		       "rwnx_add_credit_grp grp_id error :%u\n", *grp_id);
		ret = false;
		break;
	}

	WQ_DBG(DM_CRDT, DL_WRN,
	       "%s[%lu]: G0: %u/%u/%lu-%u/%u/%lu-%u/%u/%lu-%u/%u/%lu | G1: %u/%u/%lu-%u/%u/%lu-%u/%u/%lu-%u/%u/%lu | grp_id=%u, active_grp=0x%x, r=%u\n",
	       __func__, jiffies, crdt_grp[0].credit[0], crdt_grp[0].lend[0],
	       crdt_grp[0].tick[0], crdt_grp[0].credit[1], crdt_grp[0].lend[1],
	       crdt_grp[0].tick[1], crdt_grp[0].credit[2], crdt_grp[0].lend[2],
	       crdt_grp[0].tick[2], crdt_grp[0].credit[3], crdt_grp[0].lend[3],
	       crdt_grp[0].tick[3], crdt_grp[1].credit[0], crdt_grp[1].lend[0],
	       crdt_grp[1].tick[0], crdt_grp[1].credit[1], crdt_grp[1].lend[1],
	       crdt_grp[1].tick[1], crdt_grp[1].credit[2], crdt_grp[1].lend[2],
	       crdt_grp[1].tick[2], crdt_grp[1].credit[3], crdt_grp[1].lend[3],
	       crdt_grp[1].tick[3], *grp_id, crdt_mgmt->active_group, ret);

	spin_unlock_bh(&crdt_mgmt->credit_mgmt_lock);

	return ret;
}

bool rwnx_del_credit_grp(struct credit_mgmt *crdt_mgmt, u8 grp_id)
{
	struct credit_grp *crdt_grp = &(crdt_mgmt->credit_grp[0]);
	bool ret = true;

	if (!crdt_mgmt->enabled)
		return true;

	spin_lock_bh(&crdt_mgmt->credit_mgmt_lock);

	switch (grp_id) {
	case 0:
		crdt_mgmt->active_group &= 0xFE;
		crdt_grp[grp_id].is_used = 0;
		break;
	case 1:
		crdt_mgmt->active_group &= 0xFD;
		crdt_grp[grp_id].is_used = 0;
		break;
	default:
		WQ_DBG(DM_CRDT, DL_ERR,
		       "rwnx_del_credit_grp grp_id error :%u\n", grp_id);
		ret = false;
		break;
	}

	WQ_DBG(DM_CRDT, DL_WRN,
	       "%s[%lu]: G0: %u/%u/%lu-%u/%u/%lu-%u/%u/%lu-%u/%u/%lu | G1: %u/%u/%lu-%u/%u/%lu-%u/%u/%lu-%u/%u/%lu | grp_id=%u, active_grp=0x%x, r=%u\n",
	       __func__, jiffies, crdt_grp[0].credit[0], crdt_grp[0].lend[0],
	       crdt_grp[0].tick[0], crdt_grp[0].credit[1], crdt_grp[0].lend[1],
	       crdt_grp[0].tick[1], crdt_grp[0].credit[2], crdt_grp[0].lend[2],
	       crdt_grp[0].tick[2], crdt_grp[0].credit[3], crdt_grp[0].lend[3],
	       crdt_grp[0].tick[3], crdt_grp[1].credit[0], crdt_grp[1].lend[0],
	       crdt_grp[1].tick[0], crdt_grp[1].credit[1], crdt_grp[1].lend[1],
	       crdt_grp[1].tick[1], crdt_grp[1].credit[2], crdt_grp[1].lend[2],
	       crdt_grp[1].tick[2], crdt_grp[1].credit[3], crdt_grp[1].lend[3],
	       crdt_grp[1].tick[3], grp_id, crdt_mgmt->active_group, ret);

	spin_unlock_bh(&crdt_mgmt->credit_mgmt_lock);

	return ret;
}

bool rwnx_get_dev_credit(struct rwnx_hw *rwnx_hw, u8 hwq_id, u8 vif_idx,
			 u8 is_mgmt, u8 *grp_id, u8 *type_id)
{
#define WQ_CRDT_RESERVE_TIME (1 * HZ) // 1 secs

#define CGRP_OFF (0) //can use self-group credit only
#define CGRP_ON_1 (1) //can use all-group credit, operate as 1 group
#define CGRP_ON_2 (2) //can use all-group credit, operate as 2 group

	struct credit_mgmt *crdt_mgmt = &rwnx_hw->crdt_mgmt;
	struct credit_grp *crdt_grp = &(crdt_mgmt->credit_grp[0]);
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[vif_idx];
	unsigned long timeout = 0;
	u8 crdt_tid = 0;
	u8 crdt_gid_ex = 0;
	int crdt_used = 0;
	u8 cgrp_mode = CGRP_OFF; //cross group credit mode
	u8 i = 0, j = 0;
	bool ret = true;
	unsigned long flags;

	if (!crdt_mgmt->enabled) {
		*grp_id = 0xFF;
		*type_id = 0xFF;
		return true;
	}

	spin_lock_irqsave(&crdt_mgmt->credit_mgmt_lock, flags);

	crdt_mgmt->get_tick = jiffies;

	//1. sanity check
	if (vif_idx >= NX_VIRT_DEV_MAX) {
		WQ_DBG(DM_CRDT, DL_OOPS, "%s: wrong vif idx = %u\n", __func__,
		       vif_idx);
		goto done_failed;
	}

	if (hwq_id >= RWNX_HWQ_NB) {
		WQ_DBG(DM_CRDT, DL_OOPS, "%s: wrong hwq id = %u\n", __func__,
		       hwq_id);
		goto done_failed;
	}

	if (rwnx_vif->crdt_gid >= WQ_CREDIT_GROUP_NUM &&
	    !rwnx_hw->large_ap_mode) {
		WQ_DBG(DM_CRDT, DL_OOPS, "%s: wrong gid = %u\n", __func__,
		       rwnx_vif->crdt_gid);
		goto done_failed;
	}

	if ((crdt_mgmt->active_group & (1 << rwnx_vif->crdt_gid)) == 0 &&
	    !rwnx_hw->large_ap_mode) {
		WQ_DBG(DM_CRDT, DL_OOPS,
		       "%s: crdt grp is inactive, active_grp = 0x%x, crdt_gid=%u\n",
		       __func__, crdt_mgmt->active_group, rwnx_vif->crdt_gid);
		goto done_failed;
	}

	//2. data preparation
	//in large AP mode, we don't use the origianl group/AC for credit
	if (rwnx_hw->large_ap_mode == 1) {
		for (j = 0; j < WQ_CREDIT_GROUP_NUM; j++) {
			for (i = 0; i < WQ_CREDIT_TYPE_NUM; i++) {
				if (crdt_grp[j].credit[i]) {
					crdt_grp[j].credit[i]--;

					(*grp_id) = j;
					(*type_id) = i;
					goto done_succ;
				}
			}
		}

		goto done_failed;
	}

	//update latest want credit tick
	crdt_mgmt->want_tick = jiffies;

	//get the other group id
	crdt_gid_ex = (rwnx_vif->crdt_gid + 1) % 2;

	//bcmc use bk type
	crdt_tid = crdt_grp[rwnx_vif->crdt_gid].type[hwq_id];

	//update tick
	crdt_grp[rwnx_vif->crdt_gid].tick[crdt_tid] = jiffies;

	//update used flag
	crdt_grp[rwnx_vif->crdt_gid].is_used = 1;

	//cross group mode
	if (crdt_mgmt->active_group == 0x3) { //2 active group
		if (crdt_grp[crdt_gid_ex].is_used == 0) {
			cgrp_mode = CGRP_ON_1;
		} else {
#if TX_CRDT_CGRP_ON_2
			cgrp_mode = CGRP_ON_2;
#else
			cgrp_mode = CGRP_OFF;
#endif
		}
	} else { //1 active group
		cgrp_mode = CGRP_ON_1;
	}

	//3. get one credit
	//a. self type
	if (cgrp_mode == CGRP_ON_1) {
		if (crdt_grp[rwnx_vif->crdt_gid].credit[crdt_tid]) {
			crdt_grp[rwnx_vif->crdt_gid].credit[crdt_tid]--;

			(*grp_id) = rwnx_vif->crdt_gid;
			(*type_id) = crdt_tid;
			goto done_succ;
		}

		if ((crdt_grp[crdt_gid_ex].credit[crdt_tid])) {
			crdt_grp[crdt_gid_ex].credit[crdt_tid]--;

			(*grp_id) = crdt_gid_ex;
			(*type_id) = crdt_tid;
			goto done_succ;
		}
	} else {
		if (crdt_grp[rwnx_vif->crdt_gid].credit[crdt_tid]) {
			crdt_grp[rwnx_vif->crdt_gid].credit[crdt_tid]--;

			(*grp_id) = rwnx_vif->crdt_gid;
			(*type_id) = crdt_tid;
			goto done_succ;
		}
	}

	//b. barrow type
	//   - priority: low -> high
	for (i = 0; i < WQ_CREDIT_TYPE_NUM; i++) {
		//[reject case # 1] credit type
		if (cgrp_mode != CGRP_ON_2) {
			if (i == crdt_tid) {
				continue;
			}
		}

		if (cgrp_mode == CGRP_ON_1) {
			//[reject case # 2] reserve time
			timeout = crdt_grp[rwnx_vif->crdt_gid].tick[i] +
				  WQ_CRDT_RESERVE_TIME; //timeout calc
			if ((is_mgmt == 0) && (time_before(jiffies, timeout))) {
				continue;
			}

			//[reject case # 3] credit used
			//self used credit calc
			crdt_used = (crdt_grp[rwnx_vif->crdt_gid].size[i] -
				     crdt_grp[rwnx_vif->crdt_gid].credit[i] -
				     crdt_grp[rwnx_vif->crdt_gid].lend[i]) +
				    (crdt_grp[crdt_gid_ex].size[i] -
				     crdt_grp[crdt_gid_ex].credit[i] -
				     crdt_grp[crdt_gid_ex].lend[i]);
			if ((crdt_used == 0) || (is_mgmt != 0)) {
				if (crdt_grp[rwnx_vif->crdt_gid].credit[i]) {
					crdt_grp[rwnx_vif->crdt_gid].credit[i]--;
					crdt_grp[rwnx_vif->crdt_gid].lend[i]++;

					(*grp_id) = rwnx_vif->crdt_gid;
					(*type_id) = i;
					goto done_succ;
				}

				if (crdt_grp[crdt_gid_ex].credit[i]) {
					crdt_grp[crdt_gid_ex].credit[i]--;
					crdt_grp[crdt_gid_ex].lend[i]++;

					(*grp_id) = crdt_gid_ex;
					(*type_id) = i;
					goto done_succ;
				}
			}
		} else if (cgrp_mode == CGRP_ON_2) {
			//the same group
			//[reject case # 1] credit type
			if (i != crdt_tid) {
				//[reject case # 2] reserve time
				timeout = crdt_grp[rwnx_vif->crdt_gid].tick[i] +
					  WQ_CRDT_RESERVE_TIME; //timeout calc
				if ((is_mgmt != 0) ||
				    (!(time_before(jiffies, timeout)))) {
					//[reject case # 3] credit used
					//self used credit calc
					crdt_used = crdt_grp[rwnx_vif->crdt_gid]
							    .size[i] -
						    crdt_grp[rwnx_vif->crdt_gid]
							    .credit[i] -
						    crdt_grp[rwnx_vif->crdt_gid]
							    .lend[i];
					if ((crdt_used == 0) ||
					    (is_mgmt != 0)) {
						if (crdt_grp[rwnx_vif->crdt_gid]
							    .credit[i]) {
							crdt_grp[rwnx_vif->crdt_gid]
								.credit[i]--;
							crdt_grp[rwnx_vif->crdt_gid]
								.lend[i]++;

							(*grp_id) =
								rwnx_vif->crdt_gid;
							(*type_id) = i;
							goto done_succ;
						}
					}
				}
			}

			//another group
			//[reject case # 2] reserve time
			timeout = crdt_grp[crdt_gid_ex].tick[i] +
				  WQ_CRDT_RESERVE_TIME; //timeout calc
			if ((is_mgmt != 0) ||
			    (!(time_before(jiffies, timeout)))) {
				//[reject case # 3] credit used
				//self used credit calc
				crdt_used = crdt_grp[crdt_gid_ex].size[i] -
					    crdt_grp[crdt_gid_ex].credit[i] -
					    crdt_grp[crdt_gid_ex].lend[i];
				if ((crdt_used == 0) || (is_mgmt != 0)) {
					if (crdt_grp[crdt_gid_ex].credit[i]) {
						crdt_grp[crdt_gid_ex]
							.credit[i]--;
						crdt_grp[crdt_gid_ex].lend[i]++;

						(*grp_id) = crdt_gid_ex;
						(*type_id) = i;
						goto done_succ;
					}
				}
			}
		} else {
			//[reject case # 2] reserve time
			timeout = crdt_grp[rwnx_vif->crdt_gid].tick[i] +
				  WQ_CRDT_RESERVE_TIME; //timeout calc
			if ((is_mgmt == 0) && (time_before(jiffies, timeout))) {
				continue;
			}

			//[reject case # 3] credit used
			//self used credit calc
			crdt_used = crdt_grp[rwnx_vif->crdt_gid].size[i] -
				    crdt_grp[rwnx_vif->crdt_gid].credit[i] -
				    crdt_grp[rwnx_vif->crdt_gid].lend[i];
			if ((crdt_used == 0) || (is_mgmt != 0)) {
				if (crdt_grp[rwnx_vif->crdt_gid].credit[i]) {
					crdt_grp[rwnx_vif->crdt_gid].credit[i]--;
					crdt_grp[rwnx_vif->crdt_gid].lend[i]++;

					(*grp_id) = rwnx_vif->crdt_gid;
					(*type_id) = i;
					goto done_succ;
				}
			}
		}
	}

done_failed:
	// no credit available
	(*grp_id) = WQ_INVALID_CRDT_ID;
	(*type_id) = WQ_INVALID_CRDT_ID;
	ret = false;

	//4. return result
done_succ:
	if (ret == true) {
		crdt_mgmt->drv_crdt_num--;
		crdt_mgmt->get_tick = jiffies;
	}

	if (ret == false) {
		WQ_DBG(DM_CRDT, DL_VRB,
		       "Can't get credit [%lu]: vid=%u, gid=%u, qid=%u, tid=%u, mgmt=%u, crdt_num=%u | G0: used=%u, %u/%u/%u/%lu-%u/%u/%u/%lu-%u/%u/%u/%lu-%u/%u/%u/%lu | G1: used=%u, %u/%u/%u/%lu-%u/%u/%u/%lu-%u/%u/%u/%lu-%u/%u/%u/%lu\n",
		       jiffies, vif_idx, rwnx_vif->crdt_gid, hwq_id, crdt_tid,
		       is_mgmt, crdt_mgmt->drv_crdt_num, crdt_grp[0].is_used,
		       crdt_grp[0].size[0], crdt_grp[0].credit[0],
		       crdt_grp[0].lend[0], crdt_grp[0].tick[0],
		       crdt_grp[0].size[1], crdt_grp[0].credit[1],
		       crdt_grp[0].lend[1], crdt_grp[0].tick[1],
		       crdt_grp[0].size[2], crdt_grp[0].credit[2],
		       crdt_grp[0].lend[2], crdt_grp[0].tick[2],
		       crdt_grp[0].size[3], crdt_grp[0].credit[3],
		       crdt_grp[0].lend[3], crdt_grp[0].tick[3],
		       crdt_grp[1].is_used, crdt_grp[1].size[0],
		       crdt_grp[1].credit[0], crdt_grp[1].lend[0],
		       crdt_grp[1].tick[0], crdt_grp[1].size[1],
		       crdt_grp[1].credit[1], crdt_grp[1].lend[1],
		       crdt_grp[1].tick[1], crdt_grp[1].size[2],
		       crdt_grp[1].credit[2], crdt_grp[1].lend[2],
		       crdt_grp[1].tick[2], crdt_grp[1].size[3],
		       crdt_grp[1].credit[3], crdt_grp[1].lend[3],
		       crdt_grp[1].tick[3]);
	} else if (crdt_mgmt->drv_crdt_num <= TX_CREDIT_LOG_THR) {
		WQ_DBG(DM_CRDT, DL_VRB,
		       "%s[%lu]: vid=%u, gid=%u, qid=%u, tid=%u, mgmt=%u, crdt_num=%u | G0: used=%u, %u/%u/%lu-%u/%u/%lu-%u/%u/%lu-%u/%u/%lu | G1: used=%u, %u/%u/%lu-%u/%u/%lu-%u/%u/%lu-%u/%u/%lu | TXD: gid=%u, tid=%u, r=%u\n",
		       __func__, jiffies, vif_idx, rwnx_vif->crdt_gid, hwq_id,
		       crdt_tid, is_mgmt, crdt_mgmt->drv_crdt_num,
		       crdt_grp[0].is_used, crdt_grp[0].credit[0],
		       crdt_grp[0].lend[0], crdt_grp[0].tick[0],
		       crdt_grp[0].credit[1], crdt_grp[0].lend[1],
		       crdt_grp[0].tick[1], crdt_grp[0].credit[2],
		       crdt_grp[0].lend[2], crdt_grp[0].tick[2],
		       crdt_grp[0].credit[3], crdt_grp[0].lend[3],
		       crdt_grp[0].tick[3], crdt_grp[1].is_used,
		       crdt_grp[1].credit[0], crdt_grp[1].lend[0],
		       crdt_grp[1].tick[0], crdt_grp[1].credit[1],
		       crdt_grp[1].lend[1], crdt_grp[1].tick[1],
		       crdt_grp[1].credit[2], crdt_grp[1].lend[2],
		       crdt_grp[1].tick[2], crdt_grp[1].credit[3],
		       crdt_grp[1].lend[3], crdt_grp[1].tick[3], *grp_id,
		       *type_id, ret);
	} else {
		WQ_DBG(DM_CRDT, DL_VRB,
		       "%s[%lu]: vid=%u, gid=%u, qid=%u, tid=%u, mgmt=%u, crdt_num=%u | G0: used=%u, %u/%u/%lu-%u/%u/%lu-%u/%u/%lu-%u/%u/%lu | G1: used=%u, %u/%u/%lu-%u/%u/%lu-%u/%u/%lu-%u/%u/%lu | TXD: gid=%u, tid=%u, r=%u\n",
		       __func__, jiffies, vif_idx, rwnx_vif->crdt_gid, hwq_id,
		       crdt_tid, is_mgmt, crdt_mgmt->drv_crdt_num,
		       crdt_grp[0].is_used, crdt_grp[0].credit[0],
		       crdt_grp[0].lend[0], crdt_grp[0].tick[0],
		       crdt_grp[0].credit[1], crdt_grp[0].lend[1],
		       crdt_grp[0].tick[1], crdt_grp[0].credit[2],
		       crdt_grp[0].lend[2], crdt_grp[0].tick[2],
		       crdt_grp[0].credit[3], crdt_grp[0].lend[3],
		       crdt_grp[0].tick[3], crdt_grp[1].is_used,
		       crdt_grp[1].credit[0], crdt_grp[1].lend[0],
		       crdt_grp[1].tick[0], crdt_grp[1].credit[1],
		       crdt_grp[1].lend[1], crdt_grp[1].tick[1],
		       crdt_grp[1].credit[2], crdt_grp[1].lend[2],
		       crdt_grp[1].tick[2], crdt_grp[1].credit[3],
		       crdt_grp[1].lend[3], crdt_grp[1].tick[3], *grp_id,
		       *type_id, ret);
	}

	spin_unlock_irqrestore(&crdt_mgmt->credit_mgmt_lock, flags);

	return ret;

#undef WQ_CRDT_RESERVE_TIME
}

bool rwnx_return_dev_credit(struct rwnx_hw *rwnx_hw, u8 *grp0, u8 *grp1)
{
	struct credit_mgmt *crdt_mgmt = &rwnx_hw->crdt_mgmt;
	struct credit_grp *crdt_grp = &(crdt_mgmt->credit_grp[0]);
	uint8_t ori_crdt_num = 0;
	u8 i = 0;
	unsigned long delta_time;

	if (!crdt_mgmt->enabled)
		return true;

	if (in_irq())
		spin_lock(&crdt_mgmt->credit_mgmt_lock);
	else
		spin_lock_bh(&crdt_mgmt->credit_mgmt_lock);

	ori_crdt_num = crdt_mgmt->drv_crdt_num;

	for (i = 0; i < WQ_CREDIT_TYPE_NUM; i++) {
		crdt_grp[0].credit[i] += grp0[i];
		crdt_grp[1].credit[i] += grp1[i];
		crdt_mgmt->drv_crdt_num += (grp0[i] + grp1[i]);

		if (crdt_grp[0].lend[i] >= grp0[i])
			crdt_grp[0].lend[i] -= grp0[i];
		else
			crdt_grp[0].lend[i] = 0;

		if (crdt_grp[1].lend[i] >= grp1[i])
			crdt_grp[1].lend[i] -= grp1[i];
		else
			crdt_grp[1].lend[i] = 0;
	}

	delta_time = (jiffies - crdt_mgmt->get_tick);

	if ((wq_conf.wq_dbg_mod & DM_PKTDUMP) == 0 &&
	    jiffies_to_msecs(delta_time) > 400) {
		printk(KERN_ERR
		       "credit returns too late (%d ms), jiffies: %lu, get_tick: %lu, want_tick: %lu\n",
		       jiffies_to_msecs(delta_time), jiffies,
		       crdt_mgmt->get_tick, crdt_mgmt->want_tick);

		printk(KERN_ERR
		       "crdt_num=%u->%u | G0: %u/%u/%lu-%u/%u/%lu-%u/%u/%lu-%u/%u/%lu | G1: %u/%u/%lu-%u/%u/%lu-%u/%u/%lu-%u/%u/%lu\n",
		       ori_crdt_num, crdt_mgmt->drv_crdt_num,
		       crdt_grp[0].credit[0], crdt_grp[0].lend[0],
		       crdt_grp[0].tick[0], crdt_grp[0].credit[1],
		       crdt_grp[0].lend[1], crdt_grp[0].tick[1],
		       crdt_grp[0].credit[2], crdt_grp[0].lend[2],
		       crdt_grp[0].tick[2], crdt_grp[0].credit[3],
		       crdt_grp[0].lend[3], crdt_grp[0].tick[3],
		       crdt_grp[1].credit[0], crdt_grp[1].lend[0],
		       crdt_grp[1].tick[0], crdt_grp[1].credit[1],
		       crdt_grp[1].lend[1], crdt_grp[1].tick[1],
		       crdt_grp[1].credit[2], crdt_grp[1].lend[2],
		       crdt_grp[1].tick[2], crdt_grp[1].credit[3],
		       crdt_grp[1].lend[3], crdt_grp[1].tick[3]);
	}

	if (ori_crdt_num <= TX_CREDIT_LOG_THR) {
		WQ_DBG(DM_CRDT, DL_VRB,
		       "%s: crdt=%u->%u | grp0=%u/%u/%u/%u, grp1=%u/%u/%u/%u | G0: %u/%u-%u/%u-%u/%u-%u/%u | G1: %u/%u-%u/%u-%u/%u-%u/%u\n",
		       __func__, ori_crdt_num, crdt_mgmt->drv_crdt_num, grp0[0],
		       grp0[1], grp0[2], grp0[3], grp1[0], grp1[1], grp1[2],
		       grp1[3], crdt_grp[0].credit[0], crdt_grp[0].lend[0],
		       crdt_grp[0].credit[1], crdt_grp[0].lend[1],
		       crdt_grp[0].credit[2], crdt_grp[0].lend[2],
		       crdt_grp[0].credit[3], crdt_grp[0].lend[3],
		       crdt_grp[1].credit[0], crdt_grp[1].lend[0],
		       crdt_grp[1].credit[1], crdt_grp[1].lend[1],
		       crdt_grp[1].credit[2], crdt_grp[1].lend[2],
		       crdt_grp[1].credit[3], crdt_grp[1].lend[3]);
	} else {
		WQ_DBG(DM_CRDT, DL_VRB,
		       "%s: crdt=%u->%u | grp0=%u/%u/%u/%u, grp1=%u/%u/%u/%u | G0: %u/%u-%u/%u-%u/%u-%u/%u | G1: %u/%u-%u/%u-%u/%u-%u/%u\n",
		       __func__, ori_crdt_num, crdt_mgmt->drv_crdt_num, grp0[0],
		       grp0[1], grp0[2], grp0[3], grp1[0], grp1[1], grp1[2],
		       grp1[3], crdt_grp[0].credit[0], crdt_grp[0].lend[0],
		       crdt_grp[0].credit[1], crdt_grp[0].lend[1],
		       crdt_grp[0].credit[2], crdt_grp[0].lend[2],
		       crdt_grp[0].credit[3], crdt_grp[0].lend[3],
		       crdt_grp[1].credit[0], crdt_grp[1].lend[0],
		       crdt_grp[1].credit[1], crdt_grp[1].lend[1],
		       crdt_grp[1].credit[2], crdt_grp[1].lend[2],
		       crdt_grp[1].credit[3], crdt_grp[1].lend[3]);
	}

	if (in_irq())
		spin_unlock(&crdt_mgmt->credit_mgmt_lock);
	else
		spin_unlock_bh(&crdt_mgmt->credit_mgmt_lock);

	return true;
}
EXPORT_SYMBOL(rwnx_return_dev_credit);

bool rwnx_return_dev_credit_ex(struct rwnx_hw *rwnx_hw, u8 grp, u8 type_id)
{
	struct credit_mgmt *crdt_mgmt = &rwnx_hw->crdt_mgmt;
	struct credit_grp *crdt_grp = &(crdt_mgmt->credit_grp[0]);
	uint8_t ori_crdt_num = 0;

	if (!crdt_mgmt->enabled)
		return true;

	spin_lock_bh(&crdt_mgmt->credit_mgmt_lock);

	ori_crdt_num = crdt_mgmt->drv_crdt_num;

	crdt_grp[grp].credit[type_id] += 1;
	crdt_mgmt->drv_crdt_num += 1;

	if (crdt_grp[grp].lend[type_id] >= 1)
		crdt_grp[grp].lend[type_id] -= 1;
	else
		crdt_grp[grp].lend[type_id] = 0;

	if (ori_crdt_num <= TX_CREDIT_LOG_THR) {
		WQ_DBG(DM_CRDT, DL_VRB,
		       "%s: crdt=%u->%u | grp=%u, type_id=%u | G0: %u/%u-%u/%u-%u/%u-%u/%u | G1: %u/%u-%u/%u-%u/%u-%u/%u\n",
		       __func__, ori_crdt_num, crdt_mgmt->drv_crdt_num, grp,
		       type_id, crdt_grp[0].credit[0], crdt_grp[0].lend[0],
		       crdt_grp[0].credit[1], crdt_grp[0].lend[1],
		       crdt_grp[0].credit[2], crdt_grp[0].lend[2],
		       crdt_grp[0].credit[3], crdt_grp[0].lend[3],
		       crdt_grp[1].credit[0], crdt_grp[1].lend[0],
		       crdt_grp[1].credit[1], crdt_grp[1].lend[1],
		       crdt_grp[1].credit[2], crdt_grp[1].lend[2],
		       crdt_grp[1].credit[3], crdt_grp[1].lend[3]);
	} else {
		WQ_DBG(DM_CRDT, DL_VRB,
		       "%s: crdt=%u->%u | grp=%u, type_id=%u | G0: %u/%u-%u/%u-%u/%u-%u/%u | G1: %u/%u-%u/%u-%u/%u-%u/%u\n",
		       __func__, ori_crdt_num, crdt_mgmt->drv_crdt_num, grp,
		       type_id, crdt_grp[0].credit[0], crdt_grp[0].lend[0],
		       crdt_grp[0].credit[1], crdt_grp[0].lend[1],
		       crdt_grp[0].credit[2], crdt_grp[0].lend[2],
		       crdt_grp[0].credit[3], crdt_grp[0].lend[3],
		       crdt_grp[1].credit[0], crdt_grp[1].lend[0],
		       crdt_grp[1].credit[1], crdt_grp[1].lend[1],
		       crdt_grp[1].credit[2], crdt_grp[1].lend[2],
		       crdt_grp[1].credit[3], crdt_grp[1].lend[3]);
	}

	spin_unlock_bh(&crdt_mgmt->credit_mgmt_lock);

	return true;
}
WQ_TX_CREDIT_API(rwnx_return_dev_credit_ex);

bool rwnx_renew_dev_credit_mapping(struct rwnx_hw *rwnx_hw, bool qos_flag,
				   u8 grp, u32 *ac_param)
{
	struct credit_mgmt *crdt_mgmt = &rwnx_hw->crdt_mgmt;
	struct credit_grp *crdt_grp = &(crdt_mgmt->credit_grp[0]);
	u32 aifs_ecw_bitmap = 0xFFF;
	u32 aifs_ecw_value[AC_MAX] = { 0x432, 0x432, 0x43A, 0x432 };
	int i = 0;
	bool change_flag = true;

	if (!crdt_mgmt->enabled)
		return true;

	spin_lock_bh(&crdt_mgmt->credit_mgmt_lock);

	if (qos_flag) {
		for (i = 0; i < AC_MAX; i++) {
			if ((ac_param[i] & aifs_ecw_bitmap) !=
			    aifs_ecw_value[i]) {
				change_flag = false;
				break;
			}
		}

		if (change_flag) {
			crdt_grp[grp].type[RWNX_HWQ_BE] = RWNX_HWQ_VI;
			crdt_grp[grp].type[RWNX_HWQ_VI] = RWNX_HWQ_BE;

			WQ_DBG(DM_CRDT, DL_WRN,
			       "%s: grp=%u, type: %u-%u-%u-%u-%u\n", __func__,
			       grp, crdt_grp[grp].type[RWNX_HWQ_BK],
			       crdt_grp[grp].type[RWNX_HWQ_BE],
			       crdt_grp[grp].type[RWNX_HWQ_VI],
			       crdt_grp[grp].type[RWNX_HWQ_VO],
			       crdt_grp[grp].type[RWNX_HWQ_BCMC]);
		} else {
			for (i = 0; i < RWNX_HWQ_NB; i++) {
				if (i == RWNX_HWQ_BCMC) {
					crdt_grp[grp].type[i] = 0;
				} else {
					crdt_grp[grp].type[i] = i;
				}
			}
		}
	} else {
		for (i = 0; i < RWNX_HWQ_NB; i++) {
			if (i == RWNX_HWQ_BCMC) {
				crdt_grp[grp].type[i] = 0;
			} else {
				crdt_grp[grp].type[i] = i;
			}
		}
	}

	spin_unlock_bh(&crdt_mgmt->credit_mgmt_lock);

	return true;
}

void rwnx_credit_dump_info(struct rwnx_hw *rwnx_hw)
{
	struct credit_mgmt *crdt_mgmt = &rwnx_hw->crdt_mgmt;
	struct credit_grp *crdt_grp = &(crdt_mgmt->credit_grp[0]);
	unsigned long flags;

	if (!crdt_mgmt->enabled)
		return;

	spin_lock_irqsave(&crdt_mgmt->credit_mgmt_lock, flags);

	WQ_DBG(DM_CRDT, DL_ERR,
	       "[auto]%s[%lu]: crdt sz=%u, crdt_num=%u | act grp=%u | T get=%lu, want=%lu\n",
	       __func__, jiffies, crdt_mgmt->dev_credit_sz,
	       crdt_mgmt->drv_crdt_num, crdt_mgmt->active_group,
	       crdt_mgmt->get_tick, crdt_mgmt->want_tick);
	WQ_DBG(DM_CRDT, DL_ERR,
	       "%s: G0: %u/%u/%u/%lu-%u/%u/%u/%lu-%u/%u/%u/%lu-%u/%u/%u/%lu | G1: %u/%u/%u/%lu-%u/%u/%u/%lu-%u/%u/%u/%lu-%u/%u/%u/%lu\n",
	       __func__, crdt_grp[0].size[0], crdt_grp[0].credit[0],
	       crdt_grp[0].lend[0], crdt_grp[0].tick[0], crdt_grp[0].size[1],
	       crdt_grp[0].credit[1], crdt_grp[0].lend[1], crdt_grp[0].tick[1],
	       crdt_grp[0].size[2], crdt_grp[0].credit[2], crdt_grp[0].lend[2],
	       crdt_grp[0].tick[2], crdt_grp[0].size[3], crdt_grp[0].credit[3],
	       crdt_grp[0].lend[3], crdt_grp[0].tick[3], crdt_grp[1].size[0],
	       crdt_grp[1].credit[0], crdt_grp[1].lend[0], crdt_grp[1].tick[0],
	       crdt_grp[1].size[1], crdt_grp[1].credit[1], crdt_grp[1].lend[1],
	       crdt_grp[1].tick[1], crdt_grp[1].size[2], crdt_grp[1].credit[2],
	       crdt_grp[1].lend[2], crdt_grp[1].tick[2], crdt_grp[1].size[3],
	       crdt_grp[1].credit[3], crdt_grp[1].lend[3], crdt_grp[1].tick[3]);

	spin_unlock_irqrestore(&crdt_mgmt->credit_mgmt_lock, flags);
}

void rwnx_credit_enable(struct rwnx_hw *rwnx_hw, int enable)
{
	struct credit_mgmt *crdt_mgmt = &rwnx_hw->crdt_mgmt;

	crdt_mgmt->enabled = !!enable;

	WQ_DBG(DM_CRDT, DL_INF, "%s: %s\n", __func__,
	       enable ? "enabled" : "disabled");
}

void wq_reschedule_hwq(struct wq_core *core)
{
	struct rwnx_hw *rwnx_hw = core->hw;
	struct credit_mgmt *crdt_mgmt = &rwnx_hw->crdt_mgmt;

	if (crdt_mgmt->enabled)
		tasklet_hi_schedule(&core->hw->credit_task);
}
EXPORT_SYMBOL(wq_reschedule_hwq);
