/**
 ******************************************************************************
 *
 * rwnx_cmds.c
 *
 * Handles queueing (push to IPC, ack/cfm from IPC) of commands issued to
 * LMAC FW
 *
 * Copyright (C) RivieraWaves 2014-2020
 *
 ******************************************************************************
 */

#include <linux/list.h>

#include "rwnx_cmds.h"
#include "rwnx_defs.h"
#include "rwnx_strs.h"
#define CREATE_TRACE_POINTS
#include "rwnx_events.h"
#include "wq_ipc.h"
#include "wq_log.h"
#include "wq_wifi_dbg.h"

#include "hif_api.h"

#ifndef list_entry_is_head
#define list_entry_is_head(pos, head, member) (&(pos)->member == (head))
#endif

#define cmd_mgr_is_timeout(tm_jf) time_after(jiffies, tm_jf)

static void cmd_mgr_print(struct rwnx_cmd_mgr *cmd_mgr);

static void cmd_dump(const struct rwnx_cmd *cmd, int error)
{
	if (!error && (cmd->id == ME_FREE_HOST_DATA_RING_REQ ||
		       cmd->id == ME_TRAFFIC_IND_REQ))
		return; /* avoid log flood, it's too much */

	WQ_DBG(DM_IPC, DL_WRN,
	       "tkn[%d]  flags:0x%02x  result:%3d  cmd:%4d-%-24s - reqcfm(%4d-%-s)\n",
	       cmd->tkn, cmd->flags, cmd->result, cmd->id, RWNX_ID2STR(cmd->id),
	       cmd->cfm_id,
	       (cmd->flags & RWNX_CMD_FLAG_REQ_CFM) ? RWNX_ID2STR(cmd->cfm_id) :
							    "----");
}

static inline void cmd_free(struct rwnx_cmd *cmd)
{
	kfree(cmd->a2e_msg);
	kfree(cmd);
}

static void cmd_complete(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd)
{
	struct rwnx_hw *rwnx_hw =
		container_of(cmd_mgr, struct rwnx_hw, cmd_mgr);

	lockdep_assert_held(&cmd_mgr->lock);

	list_del(&cmd->list);
	cmd_mgr->queue_sz--;

	if (cmd->flags & RWNX_CMD_FLAG_REQ_CFM) {
		cmd_mgr->cmd_block_size--;
	} else {
		cmd_mgr->cmd_nonblock_size--;
	}

	WQ_DBG(DM_GENERIC, DL_WRN, "%s: block %d, nonblock %d\n", __func__,
	       cmd_mgr->cmd_block_size, cmd_mgr->cmd_nonblock_size);

	if (cmd->flags & RWNX_CMD_FLAG_AUTOPM_PUT)
		hif_autopm_put(rwnx_hw->core);

	cmd_free(cmd);
}

static int cmd_push(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd)
{
	struct rwnx_hw *rwnx_hw =
		container_of(cmd_mgr, struct rwnx_hw, cmd_mgr);
	struct ipc_a2e_msg *msg = cmd->a2e_msg;
	int already_wait =
		(cmd->flags & RWNX_CMD_FLAG_WAIT_ALL) == RWNX_CMD_FLAG_WAIT_ALL;
	int ret;

	lockdep_assert_held(&cmd_mgr->lock);

	WQ_DBG(DM_GENERIC, DL_INF,
	       "%s: msg id:%d, dest_id:%d, src_id:%d, len:%d [%*ph]\n",
	       __func__, msg->id, msg->dest_id, msg->src_id, msg->param_len,
	       IPC_A2E_MSG_HDR_LEN + msg->param_len, msg);

	cmd->flags &= ~RWNX_CMD_FLAG_WAIT_PUSH;
	ret = wq_ipc_tx_msg(rwnx_hw->core, cmd->flags & RWNX_CMD_FLAG_PWR,
			    (u8 *)msg, IPC_A2E_MSG_HDR_LEN + msg->param_len);

	// save result, or result is always -EINTR even cmd send success
	cmd->result = ret;

	if (ret) {
		cmd_dump(cmd, ret);
		cmd->flags &= ~RWNX_CMD_FLAG_WAIT_ALL;
		if (already_wait) {
			complete(&cmd->complete);
		} else {
			cmd_complete(cmd_mgr, cmd);
		}
	}

	return ret;
}

static bool cmd_is_ind(ke_msg_id_t id)
{
	bool ret = false;

	switch (id) {
	case MM_PRIMARY_TBTT_IND:
	case MM_SECONDARY_TBTT_IND:
	case MM_CONNECTION_LOSS_IND:
	case MM_CHANNEL_SWITCH_IND:
	case MM_CHANNEL_PRE_SWITCH_IND:
	case MM_REMAIN_ON_CHANNEL_EXP_IND:
	case MM_PS_CHANGE_IND:
	case MM_TRAFFIC_REQ_IND:
	case MM_P2P_VIF_PS_CHANGE_IND:
	case MM_CSA_COUNTER_IND:
	case MM_CHANNEL_SURVEY_IND:
	case MM_P2P_NOA_UPD_IND:
	case MM_RSSI_STATUS_IND:
	case MM_CSA_FINISH_IND:
	case MM_CSA_TRAFFIC_IND:
	case MM_PKTLOSS_IND:
		ret = true;
		break;

	default:
		break;
	}

	return ret;
}

int cmd_mgr_queue(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd)
{
	struct rwnx_hw *rwnx_hw =
		container_of(cmd_mgr, struct rwnx_hw, cmd_mgr);
	int ret = 0, need_cfm = (cmd->flags & RWNX_CMD_FLAG_REQ_CFM) ? 1 : 0;

	ENTER();
	trace_msg_send(cmd->id);

	if (!wq_core_is_hif_ready(rwnx_hw->core)) {
		WQ_DBG(DM_GENERIC, DL_ERR, "hif is not ready\n");
		cmd_free(cmd);
		return -EPIPE;
	}

	spin_lock_bh(&cmd_mgr->lock);

	if (cmd_mgr->state == RWNX_CMD_MGR_STATE_CRASHED) {
		spin_unlock_bh(&cmd_mgr->lock);
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: cmd queue crashed\n", __func__);
		cmd_free(cmd);
		return -EPIPE;
	}

	if (cmd_mgr->state == RWNX_CMD_MGR_STATE_DETACH) {
		spin_unlock_bh(&cmd_mgr->lock);
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: cmd queue detach, dropped:\n",
		       __func__);
		cmd_dump(cmd, -1);
		cmd_free(cmd);
		return -EPIPE;
	}

	if (cmd_mgr->queue_sz == cmd_mgr->max_queue_sz) {
		spin_unlock_bh(&cmd_mgr->lock);
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "Too many cmds (%d) already queued\n",
		       cmd_mgr->max_queue_sz);
		cmd_mgr_print(cmd_mgr);
		cmd_free(cmd);
		return -ENOMEM;
	}

	if (!list_empty(&cmd_mgr->cmds)) {
		struct rwnx_cmd *last =
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
			list_last_entry(&cmd_mgr->cmds, struct rwnx_cmd, list);
#else
			list_entry((&cmd_mgr->cmds)->prev, struct rwnx_cmd,
				   list);
#endif

		if (last->flags & RWNX_CMD_FLAG_WAIT_ALL && need_cfm) {
			/* defer to push new command since the last one is not done */
			cmd->flags |= RWNX_CMD_FLAG_WAIT_PUSH;
		}
	} else {
		cmd_mgr->cmd_block_size = 0;
		cmd_mgr->cmd_nonblock_size = 0;
	}

	cmd->flags |= RWNX_CMD_FLAG_WAIT_ACK;
	if (need_cfm) {
		cmd->flags |= RWNX_CMD_FLAG_WAIT_CFM;
		init_completion(&cmd->complete);
	}

	cmd->tkn = cmd_mgr->next_tkn++;
	cmd->result = -EINTR;

	if (!(cmd->flags & RWNX_CMD_FLAG_PWR)) {
		ret = hif_autopm_get(rwnx_hw->core);

		if (ret)
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s: auto pm get failed, ret=%d\n", __func__,
			       ret);
		else
			cmd->flags |= RWNX_CMD_FLAG_AUTOPM_PUT;
	}

	/* do not push cmd when last block cmd not done */
	if (cmd_mgr->cmd_block_size && need_cfm) {
		cmd->flags |= RWNX_CMD_FLAG_WAIT_PUSH;
	}

	list_add_tail(&cmd->list, &cmd_mgr->cmds);
	cmd_mgr->queue_sz++;

	if (cmd->flags & RWNX_CMD_FLAG_REQ_CFM) {
		cmd_mgr->cmd_block_size++;
	} else {
		cmd_mgr->cmd_nonblock_size++;
	}

	WQ_DBG(DM_GENERIC, DL_WRN, "%s: block %d, nonblock %d\n", __func__,
	       cmd_mgr->cmd_block_size, cmd_mgr->cmd_nonblock_size);

	cmd_dump(cmd, 0);

	if (!(cmd->flags & RWNX_CMD_FLAG_WAIT_PUSH)) {
		ret = cmd_push(cmd_mgr, cmd);
		if (ret) {
			spin_unlock_bh(&cmd_mgr->lock);
			return ret;
		}
	}
	spin_unlock_bh(&cmd_mgr->lock);

	if (need_cfm) {
		long remainder = wait_for_completion_timeout(
			&cmd->complete,
			msecs_to_jiffies(RWNX_80211_CMD_TIMEOUT_MS *
					 cmd_mgr->queue_sz));

		/* treat it as timeout if the completion is killed (remainder < 0). */
		if (remainder <= 0) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "[auto]ASSERT : %d-%s cmd timed-out, flags %x, tkn[%d]\n",
			       cmd->id, RWNX_ID2STR(cmd->id), cmd->flags, cmd->tkn);
			WQ_ERROR_CMD_TIMEOUT(cmd->id);
			cmd_dump(cmd, remainder);

			//send trigger pattern
			hif_send_trigger(rwnx_hw->core, WQ_USB_TRI_CMD_TIMEOUT,
					 cmd->tkn + 1);

			/* cmd_mgr will recovery from crashed to ready by timer when cmd timeout */
			if (cmd_mgr->timeout_cnt++ > RWNX_CMD_MAX_TIMEOUT_CNT) {
				/* set cmd_mgr state crashed when timeout cnt reach max limit */
				cmd_mgr->state = RWNX_CMD_MGR_STATE_CRASHED;
				cmd_mgr->timeout_cnt = 0;
				mod_timer(&cmd_mgr->recovery_timer,
					  (jiffies +
					   msecs_to_jiffies(
						   RWNX_80211_CMD_TIMEOUT_MS)));
			}

			ret = remainder < 0 ? -ERESTARTSYS : -ETIMEDOUT;
		} else {
			ret = cmd->result;

			/* reset timeout cnt when cmd cfm */
			cmd_mgr->timeout_cnt = 0;
		}

		spin_lock_bh(&cmd_mgr->lock);

		cmd->flags &= ~RWNX_CMD_FLAG_WAIT_CFM;

		/* ignore the msg ack when receive msg cfm first */
		if (cmd->flags & RWNX_CMD_FLAG_WAIT_ALL) {
			WQ_DBG(DM_IPC, DL_WRN,
			       "msg cfm invalid! id:%d, flags:0x%x, tkn:[%d]\n",
			       cmd->cfm_id, cmd->flags, cmd->tkn);
		}

		cmd_complete(cmd_mgr, cmd);
		/**
         * fix: kernel panic when next cmd free
         *
         * next cmd may be free when cmd_mgr unlock
         * get first cmd to push from cmd_mgr cmds
        */
		if (!list_empty(&cmd_mgr->cmds)) {
			struct rwnx_cmd *first = list_first_entry(
				&cmd_mgr->cmds, struct rwnx_cmd, list);

			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s: first->id: %d, first->cfm_id: %d, first->flags: 0x%x\n",
			       __func__, first->id, first->cfm_id,
			       first->flags);

			if (first->flags & RWNX_CMD_FLAG_WAIT_PUSH) {
				cmd_push(cmd_mgr, first);
			}
		}
		spin_unlock_bh(&cmd_mgr->lock);
	} else {
		/* non-blocking case: the command is pushed or only queued (later push it into IPC) */
		ret = 0;
	}

	return ret;
}

int cmd_mgr_msg_ack(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *ack)
{
	struct rwnx_cmd_mgr *cmd_mgr = &rwnx_hw->cmd_mgr;
	struct rwnx_cmd *cur, *acked = NULL;
	struct wq_htc_v0 *htc_v0 = ((struct wq_htc_v0 *)ack) - 1;

	ENTER();

	spin_lock_bh(&cmd_mgr->lock);
	list_for_each_entry (cur, &cmd_mgr->cmds, list) {
		/* avoid log flood for ME_FREE_HOST_DATA_RING_REQ */
		if (ack->id != ME_FREE_HOST_DATA_RING_REQ) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s: cur->id: %d, cur->cfm_id: %d, cur->flags: %x, ack->id: %d, ack seq: %d\n",
			       __func__, cur->id, cur->cfm_id, cur->flags,
			       ack->id, WQ_IPC_SEQ(htc_v0->flags));
		}

		if (ack->id == ME_RC_SET_RATE_REQ) {
			if (rwnx_hw->monitor_param.tmp_mcs_idx != 0xFF) {
				rwnx_hw->monitor_param.tx_mcs_idx = rwnx_hw->monitor_param.tmp_mcs_idx;
			}
		}

		if ((cur->flags & RWNX_CMD_FLAG_WAIT_ACK) &&
		    (cur->id == ack->id)) {
			acked = cur;
			break;
		}
	}

	if (!acked) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "Error: acked cmd %s(%x) not found\n",
		       RWNX_ID2STR(ack->id), ack->id);
	} else {
		acked->flags &= ~RWNX_CMD_FLAG_WAIT_ACK;
		if (!(acked->flags & RWNX_CMD_FLAG_WAIT_CFM)) {
			cmd_complete(cmd_mgr, acked);

			if (!list_empty(&cmd_mgr->cmds)) {
				struct rwnx_cmd *first = list_first_entry(
					&cmd_mgr->cmds, struct rwnx_cmd, list);

				WQ_DBG(DM_GENERIC, DL_WRN,
				       "%s: first->id: %d, first->cfm_id: %d, first->flags: 0x%x\n",
				       __func__, first->id, first->cfm_id,
				       first->flags);

				if (first->flags & RWNX_CMD_FLAG_WAIT_PUSH) {
					cmd_push(cmd_mgr, first);
				}
			}
		}
	}

	spin_unlock_bh(&cmd_mgr->lock);

	return 0;
}

static int cmd_mgr_run_callback(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd,
				struct ipc_e2a_msg *msg, msg_cb_fct cb)
{
	int res;

	if (!cb)
		return 0;

	spin_lock_bh(&rwnx_hw->cb_lock);
	res = cb(rwnx_hw, cmd, msg);
	spin_unlock_bh(&rwnx_hw->cb_lock);

	return res;
}

int cmd_mgr_msg_cfm(struct rwnx_cmd_mgr *cmd_mgr, struct ipc_e2a_msg *msg,
		    msg_cb_fct cb)
{
	struct rwnx_hw *rwnx_hw =
		container_of(cmd_mgr, struct rwnx_hw, cmd_mgr);
	struct rwnx_cmd *cmd, *next = NULL;
	struct list_head *head = &cmd_mgr->cmds;
	struct wq_htc_v0 *htc_v0 = ((struct wq_htc_v0 *)msg) - 1;
	bool found = false;

	ENTER();
	trace_msg_recv(msg->id);

	spin_lock_bh(&cmd_mgr->lock);
	list_for_each_entry_safe (cmd, next, head, list) {
		/* avoid log flood for ME_FREE_HOST_DATA_RING_CFM and ind msg */
		if ((cmd->id != ME_FREE_HOST_DATA_RING_REQ) &&
		    !cmd_is_ind(msg->id)) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s: cmd->id: %d, cmd->cfm_id: %d, msg->id: %d, msg seq: %d\n",
			       __func__, cmd->id, cmd->cfm_id, msg->id,
			       WQ_IPC_SEQ(htc_v0->flags));
		}

		if (cmd->cfm_id == msg->id &&
		    (cmd->flags & (RWNX_CMD_FLAG_WAIT_CFM))) {
			// BUG_ON here to check if cb run failed
			if (cmd_mgr_run_callback(rwnx_hw, cmd, msg, cb) != 0) {
				WQ_DBG(DM_IPC, DL_ERR,
				       "msg cfm cb failed! id:%d, flags:0x%x, tkn:[%d]\n",
				       cmd->cfm_id, cmd->flags, cmd->tkn);
				BUG_ON(1);
			}

			found = true;

			if (WARN((msg->param_len > IPC_E2A_MSG_PARAM_SIZE),
				 "Unexpect E2A msg len %d > %d\n",
				 msg->param_len, IPC_E2A_MSG_PARAM_SIZE)) {
				msg->param_len = IPC_E2A_MSG_PARAM_SIZE;
			}

			if (cmd->cfm) {
				if (!WARN(msg->param_len > cmd->cfm_len,
					  "%s: cfm param len %d > prepared %d!\n",
					  __func__, msg->param_len,
					  cmd->cfm_len))
					cmd->cfm_len =
						msg->param_len; /* save actual length */

				if (cmd->cfm_len)
					memcpy(cmd->cfm, msg->param,
					       cmd->cfm_len);
			}

			cmd->result = 0;
			complete(&cmd->complete);
			break;
		}
	}
	// lock only for list handle, or dead lock happens when send msg in rx msg cb func
	spin_unlock_bh(&cmd_mgr->lock);

	if (!found)
		cmd_mgr_run_callback(rwnx_hw, NULL, msg, cb);

	return 0;
}

static void rwnx_cmd_recovery_timer_cb(struct timer_list *t)
{
	struct rwnx_cmd_mgr *cmd_mgr = from_timer(cmd_mgr, t, recovery_timer);

	spin_lock_bh(&cmd_mgr->lock);
	if (cmd_mgr->state == RWNX_CMD_MGR_STATE_CRASHED) {
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: cmd crashed to ready\n",
		       __func__);
		cmd_mgr->state = RWNX_CMD_MGR_STATE_READY;
	}
	spin_unlock_bh(&cmd_mgr->lock);
}

static void cmd_mgr_print(struct rwnx_cmd_mgr *cmd_mgr)
{
	struct rwnx_cmd *cur;

	spin_lock_bh(&cmd_mgr->lock);
	WQ_DBG(DM_IPC, DL_WRN, "q_sz/max: %2d / %2d - next tkn: %d\n",
	       cmd_mgr->queue_sz, cmd_mgr->max_queue_sz, cmd_mgr->next_tkn);
	list_for_each_entry (cur, &cmd_mgr->cmds, list) {
		cmd_dump(cur, -1);
	}
	spin_unlock_bh(&cmd_mgr->lock);
}

static void cmd_mgr_drain(struct rwnx_cmd_mgr *cmd_mgr)
{
	struct rwnx_cmd *cur, *nxt;

	ENTER();

	spin_lock_bh(&cmd_mgr->lock);
	list_for_each_entry_safe (cur, nxt, &cmd_mgr->cmds, list) {
		if (cur->flags & RWNX_CMD_FLAG_WAIT_CFM)
			complete(&cur->complete);
		else
			cmd_complete(cmd_mgr, cur);
	}
	spin_unlock_bh(&cmd_mgr->lock);
}

void rwnx_cmd_mgr_drain(struct rwnx_cmd_mgr *cmd_mgr)
{
	cmd_mgr_print(cmd_mgr);
	cmd_mgr_drain(cmd_mgr);
	cmd_mgr_print(cmd_mgr);
}

int cmd_mgr_suspend(struct rwnx_cmd_mgr *cmd_mgr, u32 timeout_ms)
{
	int ret = 0;
	unsigned long end_jf = jiffies + msecs_to_jiffies(timeout_ms);

	WQ_DBG(DM_IPC, DL_WRN, "cmd_mgr_suspend: queue cnt %d/%d !\n",
	       cmd_mgr->queue_sz, cmd_mgr->max_queue_sz);

	/* This will wait timer callback done. */
	del_timer_sync(&cmd_mgr->recovery_timer);

	/* Set command module unready. */
	spin_lock_bh(&cmd_mgr->lock);
	cmd_mgr->state = RWNX_CMD_MGR_STATE_DETACH;
	spin_unlock_bh(&cmd_mgr->lock);

	/* No need to lock. */
	while (cmd_mgr->queue_sz > 0) {
		if (cmd_mgr_is_timeout(end_jf)) {
			WQ_DBG(DM_GENERIC, DL_ERR, "%s: Timeout %dms!\n",
			       __func__, timeout_ms);
			cmd_mgr_print(cmd_mgr);
			ret = -ETIMEDOUT;
			break;
		}
	}

	return ret;
}

int cmd_mgr_resume(struct rwnx_cmd_mgr *cmd_mgr)
{
	WQ_DBG(DM_IPC, DL_WRN, "cmd_mgr_resume !\n");

	/* Set command module ready. */
	spin_lock_bh(&cmd_mgr->lock);
	cmd_mgr->state = RWNX_CMD_MGR_STATE_READY;
	spin_unlock_bh(&cmd_mgr->lock);

	return 0;
}

void rwnx_cmd_mgr_init(struct rwnx_cmd_mgr *cmd_mgr)
{
	ENTER();

	INIT_LIST_HEAD(&cmd_mgr->cmds);
	spin_lock_init(&cmd_mgr->lock);
	cmd_mgr->max_queue_sz = RWNX_CMD_MAX_QUEUED;
	cmd_mgr->cmd_block_size = 0;
	cmd_mgr->cmd_nonblock_size = 0;
	cmd_mgr->timeout_cnt = 0;
	cmd_mgr->state = RWNX_CMD_MGR_STATE_READY;
	timer_setup(&cmd_mgr->recovery_timer, rwnx_cmd_recovery_timer_cb, 0);
}

void rwnx_cmd_mgr_deinit(struct rwnx_cmd_mgr *cmd_mgr)
{
	del_timer_sync(&cmd_mgr->recovery_timer);
	cmd_mgr_print(cmd_mgr);
	cmd_mgr_drain(cmd_mgr);
	cmd_mgr_print(cmd_mgr);
	memset(cmd_mgr, 0, sizeof(*cmd_mgr));
	cmd_mgr->state = RWNX_CMD_MGR_STATE_DETACH;
}
