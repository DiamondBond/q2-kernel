/*
 * Copyright (C) 2024 WUQI Microelectronics, Inc.
 * ALL RIGHTS RESERVED.
 */

#include "bmi_core.h"
#include "bmi_cmd.h"
#include <linux/string.h>
#include "hif_api.h"
#include "wq_log.h"

typedef struct {
	u32 reserved;
	u8 tx_msg[BMI_MSG_LEN_MAX];
	u8 rx_msg[BMI_MSG_LEN_MAX];
} bmi_state_t;

static bmi_state_t bmi_state;

int bmi_cmd_exchange(struct wq_core *core, uint8_t cmd, const void *req,
		     size_t req_len, void *rsp, size_t rsp_len_max, int timeout)
{
	int ret;
	bmi_msg_t *tx_msg, *rx_msg;
	u32 rx_len, rx_size;

	if (cmd != WQ_BMI_CMD_DNLD_FW)
		WQ_DBG(DM_TRBUS, DL_WRN, "%s CMD:0x%x req len:%d \n", __func__,
		       cmd, (u32)req_len);

	if (req && req_len > BMI_DATA_SIZE_MAX)
		return -EINVAL;

	(void)memset(bmi_state.tx_msg, 0, BMI_MSG_LEN_MAX);
	tx_msg = (bmi_msg_t *)bmi_state.tx_msg;

	tx_msg->magic = cpu_to_le32(BMI_MSG_MAGIC);
	tx_msg->cmd = cmd;
	tx_msg->fragment = 0;

	if (req && req_len) {
		(void)memcpy(tx_msg->data, req, req_len);
		tx_msg->size = cpu_to_le32(req_len);
	}

	ret = wq_hif_bmi_exchange(core, tx_msg,
				  sizeof(bmi_msg_t) + tx_msg->size,
				  bmi_state.rx_msg, BMI_MSG_LEN_MAX, timeout);

	if (ret < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s CMD:0x%x failed! \n", __func__,
		       cmd);
		return ret;
	}

	rx_len = ret;
	if (rx_len < sizeof(bmi_msg_t)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s rx_len too short :%x\n", __func__,
		       rx_len);
		return -EINVAL;
	}

	rx_msg = (bmi_msg_t *)bmi_state.rx_msg;

	if (rx_msg->magic != cpu_to_le32(BMI_MSG_MAGIC)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s magic id erro :%x\n", __func__,
		       rx_msg->magic);
		return -EINVAL;
	}

	if (rx_msg->status) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s status erro :%d\n", __func__,
		       rx_msg->status);
		return -EINVAL;
	}

	rx_size = le32_to_cpu(rx_msg->size);

	if (rx_size > rx_len - sizeof(bmi_msg_t)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s rx data len error :%d\n", __func__,
		       rx_size);
		return -EINVAL;
	}

	/* TODO: crc */

	if (rx_msg->cmd != WQ_BMI_CMD_DNLD_FW)
		WQ_DBG(DM_TRBUS, DL_WRN, "%s CMD:0x%x rsp len:%d \n", __func__,
		       rx_msg->cmd, (u32)rx_size);

	if (rsp && rsp_len_max) {
		if (rx_size > rsp_len_max)
			rx_size = rsp_len_max;
		(void)memcpy(rsp, rx_msg->data, rx_size);
		return rx_size;
	}

	return 0;
}
