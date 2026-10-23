/*
 * Copyright (C) 2024 WUQI Microelectronics, Inc.
 * ALL RIGHTS RESERVED.
 */

#ifndef _BMI_CMD_H_
#define _BMI_CMD_H_

#include <linux/types.h>
#include "core.h"
#include "wq_fw.h"

enum { WQ_BMI_CMD_BEG_ID = 0,

       WQ_BMI_CMD_GET_ROM_VER = 0x01,
       WQ_BMI_CMD_GET_SYS_STATE = 0x02,
       WQ_BMI_CMD_SET_FW_INFO = 0x03,
       WQ_BMI_CMD_START_LOAD = 0x05,
       WQ_BMI_CMD_FINISH_LOAD = 0x06,
       WQ_BMI_CMD_GET_CHIP_INFO = 0x07,

       WQ_BMI_CMD_LOG_CTRL = 0x20,
       WQ_BMI_CMD_START_DTOP_SYS = 0x21,
       WQ_BMI_CMD_PS_MODE_CFG = 0x22,
       WQ_BMI_CMD_SYS_SUSPEND_RESUME = 0x23,

       WQ_BMI_CMD_SYS_PARAM_CFG = 0x30,

       WQ_BMI_CMD_DNLD_FW = 0xA0,
       WQ_BMI_CMD_VERIFY_FW = 0xA1,
       WQ_BMI_CMD_START_WIFI_SYS = 0xA2,
       WQ_BMI_CMD_START_BT_SYS = 0xA3,

       WQ_BMI_CMD_UNLOAD_DTOP = 0xB0,
       WQ_BMI_CMD_UNLOAD_WIFI = 0xB1,
       WQ_BMI_CMD_UNLOAD_BT = 0xB2,

       WQ_BMI_CMD_MP = 0xF0,

       WQ_BMI_CMD_UNKNOWN = 255,
       WQ_BMI_CMD_END_ID = WQ_BMI_CMD_UNKNOWN,
};

typedef struct {
	uint8_t sys;
	uint8_t reserved[3];
	uint32_t start_pc;
	uint32_t dl_addr;
	uint32_t fw_len;
	uint32_t fw_crc;
} wq_fw_info_t;

struct wq_chip_info {
	uint16_t module_id;
	uint16_t phy_version;
	uint8_t wifi_mac[6];
	uint8_t bt_mac[6];
	uint32_t rom_ver;
	uint8_t metal_id;
	uint8_t chip_footprint;
	uint8_t part_number;
	uint8_t board_type;
	uint8_t reserved[8];
} __packed;

typedef enum sys_cfg_cmd_e {
	SYS_CFG_COEX = 0x01,

	SYS_CFG_CMD_END = 0xFF
} WQ_SYS_CFG_CMD_E;

struct wq_sys_cfg_t {
	uint8_t cmd;
	uint8_t reserved[3];
	uint32_t len;

	uint8_t data[0];
} __packed;

int bmi_get_sys_state(struct wq_core *core, WQ_FW_TYPE fw_type, u32 *run_state);
int bmi_start_load(struct wq_core *core);

int bmi_set_fw_info(struct wq_core *core, const wq_fw_info_t *fw_info);

int bmi_dnld_fw(struct wq_core *core, const void *data, size_t size);
int bmi_start_dtop_sys(struct wq_core *core);
int bmi_start_wifi_sys(struct wq_core *core);
int bmi_start_bt_sys(struct wq_core *core);
int bmi_finish_load(struct wq_core *core);
int bmi_verify_fw(struct wq_core *core);
int bmi_unload_wifi(struct wq_core *core);
int bmi_unload_dtop(struct wq_core *core);
int bmi_get_chip_info(struct wq_core *core, struct wq_chip_info *info);

int bmi_load_sys_param_config(struct wq_core *core, WQ_SYS_CFG_CMD_E cmd,
			      void *data, size_t size);
int bmi_log_ctrl(struct wq_core *core, void *data, size_t size);
int bmi_ps_mode_config(struct wq_core *core, void *data, size_t size);

int bmi_system_suspend(struct wq_core *core);
int bmi_system_resume(struct wq_core *core);

#endif /* _BMI_CMD_H_ */
