/*
 * Copyright (C) 2024 WUQI Microelectronics, Inc.
 * ALL RIGHTS RESERVED.
 */

#ifndef _BMI_CORE_H_
#define _BMI_CORE_H_

#include <linux/types.h>
#include "hif_api.h"

#define BMI_EXCHANGE_TIMEOUT_MS 1500

/*
ASCII:
W: 0x57
Q: 0x51
M: 0x4d
G: 0x47
*/
#define BMI_MSG_MAGIC 0x474d5157 /* little endian "WQMG" */

#define BMI_DATA_SIZE_MAX 1000

#if BMI_DATA_SIZE_MAX % 4 != 0
#error "BMI_DATA_SIZE_MAX missing align"
#endif

#define BMI_MSG_LEN_MAX (sizeof(bmi_msg_t) + BMI_DATA_SIZE_MAX)

typedef struct {
	uint32_t magic;
	uint8_t cmd;
	uint8_t fragment;
	uint16_t crc;
	uint32_t size;
	union {
		uint32_t reserved0;
		struct {
			uint8_t status;
			uint8_t reserved1[3];
		};
	};
	uint8_t data[0];
} bmi_msg_t;

static inline int bmi_cmd(struct wq_core *core, u8 cmd, const void *param,
			  u16 p_size, void *resp, u16 r_size, int timeout)
{
	return core->hif_ops->bmi_cmd(core, cmd, param, p_size, resp, r_size,
				      timeout);
}

static inline int bmi_xfer(struct wq_core *core, enum wq_bmi_xfer_type type,
			   const u8 *data, int len, int timeout)
{
	return core->hif_ops->bmi_xfer(core, type, data, len, timeout);
}

int bmi_cmd_exchange(struct wq_core *core, uint8_t cmd, const void *req,
		     size_t req_len, void *rsp, size_t rsp_len_max,
		     int timeout);

int wq_fw_name_update(struct wq_core *core, char *dtop_name, char *wifi_name);
int wq_fw_dtop_init(struct wq_core *core);
int wq_fw_init(struct wq_core *core);

#endif /* _BMI_CORE_H_ */
