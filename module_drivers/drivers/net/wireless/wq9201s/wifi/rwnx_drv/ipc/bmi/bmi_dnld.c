/** @file bmi_dnld.c
 *
 *  @brief This file contains FW download functions.
 *
 * Copyright (C) 2016-2022, WuQi Ltd.
 *
 * This software file (the "File") is distributed by WuQi Ltd.
 * Under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 *
 */

#include <linux/firmware.h>
#include <linux/workqueue.h>
#include <linux/crc32.h>
#include <linux/module.h>

#include "core.h"
#include "bmi_core.h"
#include "bmi_cmd.h"

#include "coex.h"
#include "wq_log.h"
#include "wq_wifi_dbg.h"
#include "wq_fw.h"

static int wifi_module_id = -1;

#define wq_fw_module_id_get_from_phy(data)	((int)((data[13] << 8) | data[12]) & 0xFFFF)

char *wifi_phy_name = NULL;
module_param(wifi_phy_name, charp, 0);
MODULE_PARM_DESC(wifi_phy_name,
		 "phy firmware name for any host interface, default: null");

char *oem_name = NULL;
module_param(oem_name, charp, 0);
MODULE_PARM_DESC(oem_name,
		 "oem firmware name for any host interface, default: null");

extern char fw_wifi_name[128];
extern char fw_bt_name[128];

extern int wq_fw_header_verify(struct wq_core *core, const struct firmware *fw,
			       u8 fw_type);

static int wq_fw_start_load(struct wq_core *core)
{
	int ret;
	int loop = 0;

	WQ_DBG(DM_GENERIC, DL_VRB, " start load! \n");
	do {
		ret = bmi_start_load(core);
		if (ret < 0) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "%s: other fw is downloading, retry requesting again\n",
			       __func__);
		} else {
			ret = 0;
			break;
		}

		msleep(500);
		loop++;
		if (loop > 10) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s: wait for request download timeout\n",
			       __func__);
			ret = -1;
			break;
		}

	} while (1);

	return ret;
}
static void wq_fw_finish_load(struct wq_core *core)
{
	(void)bmi_finish_load(core);

	WQ_DBG(DM_GENERIC, DL_VRB, " finish load! \n");
	return;
}

static int wq_fw_info_dnld(struct wq_core *core, WQ_FW_TYPE fw_sys)
{
	int ret = 0;
	wq_fw_info_t fw_info;

	fw_info.sys = fw_sys;
	fw_info.start_pc = core->wq_dnld->start_pc;
	fw_info.dl_addr = core->wq_dnld->dl_addr;
	fw_info.fw_len = core->wq_dnld->fw_len;
	fw_info.fw_crc = core->wq_dnld->fw_crc;

	ret = bmi_set_fw_info(core, &fw_info);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s: fw:%d download error\n",
		       __func__, fw_sys);
		return ret;
	}

	return 0;
}

static int wq_fw_data_dnld(struct wq_core *core)
{
	int ret = 0;

	ret = bmi_dnld_fw(core, core->wq_dnld->fw, core->wq_dnld->fw_len);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s: error! \n", __func__);
		return ret;
	}

	return 0;
}

static int wq_fw_verify(struct wq_core *core)
{
	return bmi_verify_fw(core);
}

static int wq_fw_sys_start(struct wq_core *core, WQ_FW_TYPE fw_type)
{
	int ret = 0;

	switch (fw_type) {
	case WQ_FW_WIFI:
		ret = bmi_start_wifi_sys(core);
		break;
	case WQ_FW_BT:
		ret = bmi_start_bt_sys(core);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int wq_fw_bt_ready(struct wq_core *core)
{
	u32 run_state = 0;
	int loop_cnt = 0;
	int ret = 0;

	do {
		loop_cnt++;
		msleep(100);

		if (bmi_get_sys_state(core, WQ_FW_BT, &run_state) < 0) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "%s: bmi get device state failed !\n", __func__);
			ret = -EINTR;
			break;
		}
		if (run_state) {
			ret = 0;
			WQ_DBG(DM_GENERIC, DL_WRN, "%s: get bt state:%x\n",
			       __func__, run_state);
			break;
		}
		WQ_DBG(DM_GENERIC, DL_VRB, "%s: get bt state, loop_cnt=%d\n",
		       __func__, loop_cnt);
	} while (loop_cnt < 10);

	return ret;
}

static int wq_fw_bt_download(struct wq_core *core, const char *fw_name)
{
	int ret;
	u32 run_state;
	const struct firmware *bt_fw;

	WQ_DBG(DM_GENERIC, DL_WRN, "bmi load fw bt name:%s\n", fw_name);

	ret = bmi_get_sys_state(core, WQ_FW_BT, &run_state);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: quit by get runtime fail (dev=%s, ret=%d)\n",
		       __func__, dev_name(core->dev), ret);
		goto exit_dnld;
	}

	WQ_DBG(DM_GENERIC, DL_INF, "fw state:%x\n", run_state);
	if ((run_state & BIT(WQ_FW_BT)) == 0) {
		ret = 0;
	} else {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "%s: wlan fw existing (dev=%s),dont need to download again\n",
		       __func__, dev_name(core->dev));
		ret = 0;
		goto exit_dnld;
	}

	ret = request_firmware(&bt_fw, fw_name, core->dev);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "request_firmware() failed, error code = %d\n", ret);
		goto exit_dnld;
	}

	/* TODO: bmi download  retry */
	ret = wq_fw_start_load(core);
	if (ret < 0)
		goto load_erro;

	ret = wq_fw_header_verify(core, bt_fw, WQ_FW_BT);
	if (ret < 0)
		goto load_erro;

	ret = wq_fw_info_dnld(core, WQ_FW_BT);
	if (ret < 0)
		goto load_erro;

	ret = wq_fw_data_dnld(core);
	if (ret < 0)
		goto load_erro;

	ret = wq_fw_sys_start(core, WQ_FW_BT);
	if (ret < 0)
		goto load_erro;

	WQ_DBG(DM_GENERIC, DL_WRN, "BT system startup!\n");
	ret = wq_fw_bt_ready(core);
	if (ret < 0)
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "Dont receive fw start message, load faild \n");

load_erro:
	if (bt_fw)
		release_firmware(bt_fw);

	wq_fw_finish_load(core);

exit_dnld:
	return ret;
}

static int wq_fw_sys_param_config(struct wq_core *core)
{
	int ret = 0;
	u8 msg_buf[64];
	coex_msg_t *coex_msg = (coex_msg_t *)msg_buf;

	coex_msg->msg_id = COEX_ANTENNA_MODE;
	coex_msg->data[0] = (u8)wq_conf.coex_ant_mode;
	WQ_DBG(DM_GENERIC, DL_WRN, "load ant mode cfg:%d \n",
	       wq_conf.coex_ant_mode);
	ret = bmi_load_sys_param_config(core, SYS_CFG_COEX, msg_buf, 2);
	if (ret < 0)
		WQ_DBG(DM_GENERIC, DL_ERR, "load ant mode cfg faild\n");

	return ret;
}

static int wq_fw_oem_download(struct wq_core *core, const char *fw_name)
{
	int ret;
	u32 run_state;
	const struct firmware *oem_fw;

	WQ_DBG(DM_GENERIC, DL_WRN, "bmi load fw oem name:%s\n", fw_name);

	ret = bmi_get_sys_state(core, WQ_FW_BOOT_CFG, &run_state);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: quit by get runtime fail (dev=%s, ret=%d)\n",
		       __func__, dev_name(core->dev), ret);
		goto exit_dnld;
	}

	WQ_DBG(DM_GENERIC, DL_INF, "fw state:%x\n", run_state);
	if ((run_state & BIT(WQ_FW_BOOT_CFG)) == 0) {
		ret = 0;
	} else {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "%s: wlan fw existing (dev=%s),dont need to download again\n",
		       __func__, dev_name(core->dev));
		ret = 0;
		goto exit_dnld;
	}

	ret = request_firmware(&oem_fw, fw_name, core->dev);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "request_firmware() failed, error code = %d\n", ret);
		goto exit_dnld;
	}

	/* TODO: bmi download  retry */
	ret = wq_fw_start_load(core);
	if (ret < 0)
		goto load_erro;

	ret = wq_fw_header_verify(core, oem_fw, WQ_FW_BOOT_CFG);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_WRN, "use oem default fw info!\n");
		core->wq_dnld->dl_addr = 0;
		core->wq_dnld->start_pc = 0;
		core->wq_dnld->fw = oem_fw->data;
		core->wq_dnld->fw_len = oem_fw->size;
		core->wq_dnld->fw_crc = ~crc32_le(0xFFFFFFFF, core->wq_dnld->fw,
						  core->wq_dnld->fw_len);
	}

	ret = wq_fw_info_dnld(core, WQ_FW_BOOT_CFG);
	if (ret < 0)
		goto load_erro;

	ret = wq_fw_data_dnld(core);
	if (ret < 0)
		goto load_erro;

	ret = wq_fw_verify(core);
	if (ret < 0)
		goto load_erro;

load_erro:
	if (oem_fw)
		release_firmware(oem_fw);

	wq_fw_finish_load(core);

exit_dnld:
	return ret;
}

static int wq_fw_phy_download(struct wq_core *core, const char *fw_name)
{
	int ret;
	const struct firmware *phy_fw;

	WQ_DBG(DM_GENERIC, DL_WRN, "bmi load fw wifi phy name:%s\n", fw_name);

	wifi_module_id = -1;

	ret = request_firmware(&phy_fw, fw_name, core->dev);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "request_firmware() failed, error code = %d\n", ret);
		goto exit_dnld;
	}

	/* TODO: bmi download  retry */
	ret = wq_fw_start_load(core);
	if (ret < 0)
		goto load_erro;

	core->wq_dnld->dl_addr = 0;
	core->wq_dnld->start_pc = 0;
	core->wq_dnld->fw = phy_fw->data;
	core->wq_dnld->fw_len = phy_fw->size;
	core->wq_dnld->fw_crc = 0;

	ret = wq_fw_info_dnld(core, WQ_FW_WIFI_PHY);
	if (ret < 0)
		goto load_erro;

	ret = wq_fw_data_dnld(core);
	if (ret < 0)
		goto load_erro;

	wifi_module_id = wq_fw_module_id_get_from_phy(phy_fw->data);

load_erro:
	if (phy_fw)
		release_firmware(phy_fw);

	wq_fw_finish_load(core);

exit_dnld:
	return ret;
}

static int wq_fw_wifi_download(struct wq_core *core, const char *fw_name)
{
	int ret;
	u32 run_state;
	const struct firmware *wifi_fw;

	WQ_DBG(DM_GENERIC, DL_WRN, "bmi load fw wifi name:%s\n", fw_name);

	ret = bmi_get_sys_state(core, WQ_FW_WIFI, &run_state);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: quit by get runtime fail (dev=%s, ret=%d)\n",
		       __func__, dev_name(core->dev), ret);
		goto exit_dnld;
	}

	WQ_DBG(DM_GENERIC, DL_INF, "fw state:%x\n", run_state);
	if ((run_state & BIT(WQ_FW_WIFI)) == 0) {
		ret = 0;
	} else {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "%s: wlan fw existing (dev=%s),dont need to download again\n",
		       __func__, dev_name(core->dev));
		ret = 0;
		goto exit_dnld;
	}

	ret = request_firmware(&wifi_fw, fw_name, core->dev);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "request_firmware() failed, error code = %d\n", ret);
		goto exit_dnld;
	}

	/* TODO: bmi download  retry */
	ret = wq_fw_start_load(core);
	if (ret < 0)
		goto load_erro;

	ret = wq_fw_header_verify(core, wifi_fw, WQ_FW_WIFI);
	if (ret < 0)
		goto load_erro;

	ret = wq_fw_info_dnld(core, WQ_FW_WIFI);
	if (ret < 0)
		goto load_erro;

	ret = wq_fw_data_dnld(core);
	if (ret < 0)
		goto load_erro;

	ret = wq_fw_sys_start(core, WQ_FW_WIFI);
	if (ret < 0)
		goto load_erro;

	WQ_DBG(DM_GENERIC, DL_WRN, "WIFI system startup!\n");
load_erro:
	if (wifi_fw)
		release_firmware(wifi_fw);

	wq_fw_finish_load(core);

exit_dnld:
	return ret;
}

/*
 * @brief: obtain the oem_cfg firmware file name from chip info data.
 * @param: info -> struct wq_chip_info. get from devices
 * @param: req_name -> firmware name to be requested.
 * */
static void wq_fw_oem_name_get(const struct wq_chip_info *info, char *req_name)
{
	int cnt;
	char auto_name[64];
	u16 major = (u16)((info->rom_ver >> 16) & 0xffff);
	u16 minor = (u16)(info->rom_ver & 0xffff);

	cnt = sprintf(auto_name, "%s_%s_%x_%x_%04X.bin", WQ_CHIP_NAME, "oem",
		      major, minor, (info->module_id & 0x0fff));
	if (cnt > 0) {
		auto_name[cnt] = '\0';
	} else {
		BUG_ON(1);
	}
	WQ_DBG(DM_GENERIC, DL_WRN, "matching oem_cfg name:%s\n", auto_name);
	memcpy(req_name, auto_name,
	       strlen(auto_name) + 1); /* +1 ：include the null terminator */

	return;
}

/*
 * @brief: obtain the phy_cfg firmware file name from chip info data.
 * @param: info -> struct wq_chip_info. get from devices
 * @param: req_name -> firmware name to be requested.
 * */
static void wq_fw_phy_name_get(const struct wq_chip_info *info, char *req_name)
{
	int cnt;
	char auto_name[64];
	u16 major = (u16)((info->rom_ver >> 16) & 0xffff);
	u16 minor = (u16)(info->rom_ver & 0xffff);

	cnt = sprintf(auto_name, "%s_%s_%x_%x_%04X_%04X.bin", WQ_CHIP_NAME,
		      "phy", major, minor, info->module_id, info->phy_version);
	if (cnt > 0) {
		auto_name[cnt] = '\0';
	} else {
		BUG_ON(1);
	}
	WQ_DBG(DM_GENERIC, DL_WRN, "matching phy_cfg name:%s\n", auto_name);
	memcpy(req_name, auto_name,
	       strlen(auto_name) + 1); /* +1 ：include the null terminator */

	return;
}

static int wq_ps_mode_config(struct wq_core *core)
{
	u8 wq_ps_mode = wq_conf.ps_mode;
	if (wq_ps_mode) {
		return bmi_ps_mode_config(core, &wq_ps_mode,
					  sizeof(wq_ps_mode));
	}
	return 0;
}

static int __wq_fw_init(struct wq_core *core)
{
	int ret = 0;
	u8 load_sys = 0;
	struct wq_chip_info info;
	char fw_oem_name[64];
	char fw_phy_name[64];

	/* BIT(0):WIFI, BIT(2):BT */
	load_sys |= (wq_conf.fw_sys & BIT(0)) ? BIT(WQ_FW_WIFI) : 0;
	load_sys |= (wq_conf.fw_sys & BIT(2)) ? BIT(WQ_FW_BT) : 0;
	WQ_DBG(DM_GENERIC, DL_WRN, "wq_conf.fw_sys = 0x%x\n", wq_conf.fw_sys);

	ret = bmi_get_chip_info(core, &info);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: get chip info fail, ret:%d\n",
		       __func__, ret);
		return ret;
	}

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "Succeed to get chip info, "
	       "module id:0x%04x, phy version:0x%04x, rom_ver:0x%x, metal_id:0x%x, footprint:%d, board_type:%d, part_number:%d, "
	       "wifi mac:%02x-%02x-%02x-%02x-%02x-%02x, bt mac:%02x-%02x-%02x-%02x-%02x-%02x\n",
	       info.module_id, info.phy_version, info.rom_ver, info.metal_id,
	       info.chip_footprint, info.board_type, info.part_number, info.wifi_mac[0],
	       info.wifi_mac[1], info.wifi_mac[2], info.wifi_mac[3],
	       info.wifi_mac[4], info.wifi_mac[5], info.bt_mac[0],
	       info.bt_mac[1], info.bt_mac[2], info.bt_mac[3], info.bt_mac[4],
	       info.bt_mac[5]);

	if (oem_name) {
		memcpy(fw_oem_name, oem_name,
		       strlen(oem_name) +
			       1); /* +1 ：include the null terminator */
	} else {
		/* chip info vaild */
		if (info.module_id != 0) {
			wq_fw_oem_name_get(&info, fw_oem_name);
		} else {
			/* no any oem_cfg name, use default name */
			memcpy(fw_oem_name, WQ_CHIP_NAME "_oem.bin",
			       strlen(WQ_CHIP_NAME "_oem.bin") + 1);
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "warning: no oem_cfg name, use default name:%s\n",
			       fw_oem_name);
		}
	}

	ret = wq_fw_oem_download(core, fw_oem_name);
	if (ret) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "OEM: %s download failed! continue download fw!\n",
		       fw_oem_name);
	}

	ret = wq_fw_sys_param_config(core);
	if (ret) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "update fw sys config param failed!\n");
	}

	ret = bmi_start_dtop_sys(core);
	if (ret) {
		WQ_DBG(DM_GENERIC, DL_ERR, "start dtop fail! ret(%d) \n", ret);
		return ret;
	}

	ret = wq_ps_mode_config(core);
	if (ret) {
		WQ_DBG(DM_GENERIC, DL_ERR, "ps_mode cfg fail! ret(%d) \n", ret);
		return ret;
	}

	do {
		if (load_sys & BIT(WQ_FW_BT)) {
			ret = wq_fw_bt_download(core, fw_bt_name);
			if (ret) {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "BT download failed\n");
				return ret;
			} else {
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "BT download success\n");
			}
		}

		if (load_sys & BIT(WQ_FW_WIFI)) {
			if (wifi_phy_name) {
				/* TEMP for WARNNING */
				if (info.module_id != 0) {
					wq_fw_phy_name_get(&info, fw_phy_name);
					if (strcmp(fw_phy_name,
						   wifi_phy_name)) {
						WQ_DBG(DM_GENERIC, DL_WRN,
						       "\n");
						WQ_DBG(DM_GENERIC, DL_WRN,
						       "################################################\n");
						WQ_DBG(DM_GENERIC, DL_WRN,
						       "#phy name not match! please check the phy name!#\n");
						WQ_DBG(DM_GENERIC, DL_WRN,
						       "################################################\n");
						WQ_DBG(DM_GENERIC, DL_WRN,
						       "\n");
					}
				}
				memcpy(fw_phy_name, wifi_phy_name,
				       strlen(wifi_phy_name) +
					       1); /* +1 ：include the null terminator */
			} else {
				if (info.module_id != 0) {
					wq_fw_phy_name_get(&info, fw_phy_name);
				} else {
					WQ_DBG(DM_GENERIC, DL_ERR,
					       "phy cfg name invaild, download faild! \n");
					return -EINVAL; /* there is no valid chip info and no module parameter name */
				}
			}

			ret = wq_fw_phy_download(core, fw_phy_name);
			if (ret) {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "WIFI PHY: %s download failed ! ret(%d)\n",
				       fw_phy_name, ret);
				return ret;
			}

			ret = wq_fw_wifi_download(core, fw_wifi_name);
			if (ret) {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "WIFI download failed\n");
				return ret;
			}
			WQ_DBG(DM_GENERIC, DL_WRN, "WIFI download success\n");
		}
		WQ_DBG(DM_GENERIC, DL_INF, "fw download complete\n");
	} while (0);

	return ret;
}

int wq_fw_init(struct wq_core *core)
{
	int ret;

	ret = __wq_fw_init(core);

	core->dev_mod_id = wifi_module_id;

	return ret;
}
WQ_BMI_API(wq_fw_init);
