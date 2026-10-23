/**
 ******************************************************************************
 *
 * rwnx_cmds.h
 *
 * Copyright (C) RivieraWaves 2014-2020
 *
 ******************************************************************************
 */

#ifndef _RWNX_CMDS_H_
#define _RWNX_CMDS_H_

#include <linux/spinlock.h>
#include <linux/completion.h>
#include "fw_api/wifi/mac/cp_api.h"

#ifdef CONFIG_RWNX_SDM
#define RWNX_80211_CMD_TIMEOUT_MS (20 * 300)
#else
#ifdef WQ_WLAN_FPGA
/* EMU calibration is very slow so far, then Enlarge timeout enough to
 * allow ~40 2g/5g channels' scan on FPGA. */
#define RWNX_80211_CMD_TIMEOUT_MS 30000
#else
#define RWNX_80211_CMD_TIMEOUT_MS 20000
#endif
#endif

#define RWNX_CMD_FLAG_REQ_CFM BIT(0)
#define RWNX_CMD_FLAG_PWR BIT(1)
#define RWNX_CMD_FLAG_AUTOPM_PUT BIT(2)

#define RWNX_CMD_FLAG_WAIT_PUSH BIT(4)
#define RWNX_CMD_FLAG_WAIT_ACK BIT(5)
#define RWNX_CMD_FLAG_WAIT_CFM BIT(6)

#define RWNX_CMD_FLAG_WAIT_ALL                                                 \
	(RWNX_CMD_FLAG_WAIT_PUSH | RWNX_CMD_FLAG_WAIT_ACK |                    \
	 RWNX_CMD_FLAG_WAIT_CFM)

#define RWNX_CMD_MAX_QUEUED 256
#define RWNX_CMD_MAX_TIMEOUT_CNT 10

struct rwnx_hw;
struct rwnx_cmd;
typedef int (*msg_cb_fct)(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd,
			  struct ipc_e2a_msg *msg);

enum rwnx_cmd_mgr_state {
	RWNX_CMD_MGR_STATE_READY,
	RWNX_CMD_MGR_STATE_CRASHED,
	RWNX_CMD_MGR_STATE_DETACH
};

struct rwnx_cmd {
	struct list_head list;
	ke_msg_id_t id;
	ke_msg_id_t cfm_id;
	struct ipc_a2e_msg *a2e_msg;

	/* used to store param part of ipc_e2a_msg (the confirmation of above e2a_msg) */
	u8 *cfm;
	u16 cfm_len;

	u16 flags;
	u32 tkn;

	struct completion complete;
	int result;
};

struct rwnx_cmd_mgr {
	spinlock_t lock;
	u32 next_tkn;
	volatile u32 queue_sz;
	u32 max_queue_sz;
	u32 cmd_block_size;
	u32 cmd_nonblock_size;
	u32 timeout_cnt;

	enum rwnx_cmd_mgr_state state;
	struct timer_list recovery_timer;

	struct list_head cmds;
};

void rwnx_cmd_mgr_init(struct rwnx_cmd_mgr *cmd_mgr);
void rwnx_cmd_mgr_deinit(struct rwnx_cmd_mgr *cmd_mgr);
void rwnx_cmd_mgr_drain(struct rwnx_cmd_mgr *cmd_mgr);

int cmd_mgr_queue(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd);
int cmd_mgr_msg_cfm(struct rwnx_cmd_mgr *cmd_mgr, struct ipc_e2a_msg *msg,
		    msg_cb_fct cb);
int cmd_mgr_msg_ack(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *ack);

int cmd_mgr_suspend (struct rwnx_cmd_mgr *cmd_mgr, u32 timeout_ms);

int cmd_mgr_resume (struct rwnx_cmd_mgr *cmd_mgr);

#endif /* _RWNX_CMDS_H_ */
