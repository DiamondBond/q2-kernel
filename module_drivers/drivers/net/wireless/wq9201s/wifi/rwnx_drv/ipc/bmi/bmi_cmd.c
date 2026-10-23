/*
 * Copyright (C) 2024 WUQI Microelectronics, Inc.
 * ALL RIGHTS RESERVED.
 */

#include "bmi_cmd.h"
#include "bmi_core.h"
#include "wq_fw.h"

int bmi_get_sys_state(struct wq_core *core, WQ_FW_TYPE fw_type, u32 *run_state)
{
	int ret;
	u32 req_param = fw_type;

	ret = bmi_cmd_exchange(core, WQ_BMI_CMD_GET_SYS_STATE, &req_param,
			       sizeof(u32), run_state, sizeof(u32),
			       BMI_EXCHANGE_TIMEOUT_MS);

	if (ret < 0)
		return ret;

	return 0;
}

int bmi_start_load(struct wq_core *core)
{
	int ret;

	ret = bmi_cmd_exchange(core, WQ_BMI_CMD_START_LOAD, NULL, 0, NULL, 0,
			       BMI_EXCHANGE_TIMEOUT_MS);

	if (ret < 0)
		return ret;

	return 0;
}

int bmi_finish_load(struct wq_core *core)
{
	int ret;

	ret = bmi_cmd_exchange(core, WQ_BMI_CMD_FINISH_LOAD, NULL, 0, NULL, 0,
			       BMI_EXCHANGE_TIMEOUT_MS);

	if (ret < 0)
		return ret;

	return 0;
}

int bmi_get_chip_info(struct wq_core *core, struct wq_chip_info *info)
{
	int ret;

	ret = bmi_cmd_exchange(core, WQ_BMI_CMD_GET_CHIP_INFO, NULL, 0, info,
			       sizeof(struct wq_chip_info),
			       BMI_EXCHANGE_TIMEOUT_MS);

	if (ret < 0)
		return ret;

	return 0;
}

int bmi_set_fw_info(struct wq_core *core, const wq_fw_info_t *fw_info)
{
	int ret;

	ret = bmi_cmd_exchange(core, WQ_BMI_CMD_SET_FW_INFO, fw_info,
			       sizeof(wq_fw_info_t), NULL, 0,
			       BMI_EXCHANGE_TIMEOUT_MS);

	if (ret < 0)
		return ret;

	return 0;
}

int bmi_dnld_fw(struct wq_core *core, const void *data, size_t size)
{
	int ret;
	size_t i, last;

	last = size % BMI_DATA_SIZE_MAX;
	size -= last;

	for (i = 0; i < size; i += BMI_DATA_SIZE_MAX) {
		ret = bmi_cmd_exchange(core, WQ_BMI_CMD_DNLD_FW,
				       (uint8_t *)data + i, BMI_DATA_SIZE_MAX,
				       NULL, 0, BMI_EXCHANGE_TIMEOUT_MS);
		if (ret < 0)
			return ret;
	}

	if (last) {
		ret = bmi_cmd_exchange(core, WQ_BMI_CMD_DNLD_FW,
				       (uint8_t *)data + size, last, NULL, 0,
				       BMI_EXCHANGE_TIMEOUT_MS);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int bmi_verify_fw(struct wq_core *core)
{
	int ret;

	ret = bmi_cmd_exchange(core, WQ_BMI_CMD_VERIFY_FW, NULL, 0, NULL, 0,
			       BMI_EXCHANGE_TIMEOUT_MS);

	if (ret < 0)
		return ret;

	return 0;
}

int bmi_start_dtop_sys(struct wq_core *core)
{
	int ret;

	ret = bmi_cmd_exchange(core, WQ_BMI_CMD_START_DTOP_SYS, NULL, 0, NULL,
			       0, BMI_EXCHANGE_TIMEOUT_MS);

	if (ret < 0)
		return ret;

	return 0;
}

int bmi_start_wifi_sys(struct wq_core *core)
{
	int ret;

	ret = bmi_cmd_exchange(core, WQ_BMI_CMD_START_WIFI_SYS, NULL, 0, NULL,
			       0, BMI_EXCHANGE_TIMEOUT_MS);

	if (ret < 0)
		return ret;

	return 0;
}

int bmi_start_bt_sys(struct wq_core *core)
{
	int ret;

	ret = bmi_cmd_exchange(core, WQ_BMI_CMD_START_BT_SYS, NULL, 0, NULL, 0,
			       BMI_EXCHANGE_TIMEOUT_MS);

	if (ret < 0)
		return ret;

	return 0;
}

int bmi_unload_wifi(struct wq_core *core)
{
	int ret;

	ret = bmi_cmd_exchange(core, WQ_BMI_CMD_UNLOAD_WIFI, NULL, 0, NULL, 0,
			       BMI_EXCHANGE_TIMEOUT_MS);

	if (ret < 0)
		return ret;

	return 0;
}

int bmi_unload_dtop(struct wq_core *core)
{
	int ret;

	ret = bmi_cmd_exchange(core, WQ_BMI_CMD_UNLOAD_DTOP, NULL, 0, NULL, 0,
			       BMI_EXCHANGE_TIMEOUT_MS);

	if (ret < 0)
		return ret;

	return 0;
}

int bmi_load_sys_param_config(struct wq_core *core, WQ_SYS_CFG_CMD_E cmd,
			      void *data, size_t size)
{
	u8 *cfg_buf = NULL;
	u32 cfg_size = 0;
	int ret;
	struct wq_sys_cfg_t *fw_cfg = NULL;

	cfg_size = size + sizeof(struct wq_sys_cfg_t);
	if (cfg_size > BMI_DATA_SIZE_MAX)
		return -EINVAL;

	cfg_buf = kmalloc(cfg_size, GFP_KERNEL);
	if (!cfg_buf)
		return -ENOMEM;

	fw_cfg = (struct wq_sys_cfg_t *)cfg_buf;
	fw_cfg->cmd = cmd;
	fw_cfg->len = size;

	if (size)
		memcpy(fw_cfg->data, data, size);

	ret = bmi_cmd_exchange(core, WQ_BMI_CMD_SYS_PARAM_CFG, cfg_buf,
				cfg_size, NULL, 0, BMI_EXCHANGE_TIMEOUT_MS);

	if (cfg_buf)
		kfree(cfg_buf);

	return ret;
}

int bmi_log_ctrl(struct wq_core *core, void *data, size_t size)
{
	return bmi_cmd_exchange(core, WQ_BMI_CMD_LOG_CTRL, data, size, NULL, 0,
				BMI_EXCHANGE_TIMEOUT_MS);
}

int bmi_ps_mode_config(struct wq_core *core, void *data, size_t size)
{
	return bmi_cmd_exchange(core, WQ_BMI_CMD_PS_MODE_CFG, data, size, NULL,
				0, BMI_EXCHANGE_TIMEOUT_MS);
}

int bmi_system_suspend(struct wq_core *core)
{
	uint8_t is_suspend = true;
	return bmi_cmd_exchange(core, WQ_BMI_CMD_SYS_SUSPEND_RESUME,
				&is_suspend, sizeof(is_suspend), NULL, 0,
				BMI_EXCHANGE_TIMEOUT_MS);
}

int bmi_system_resume(struct wq_core *core)
{
	uint8_t is_suspend = false;
	return bmi_cmd_exchange(core, WQ_BMI_CMD_SYS_SUSPEND_RESUME,
				&is_suspend, sizeof(is_suspend), NULL, 0,
				BMI_EXCHANGE_TIMEOUT_MS);
}
