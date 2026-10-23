/** @file wq_dnld.c
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
#include <linux/crc32.h>
#include <linux/module.h>

#include "core.h"
#include "bmi_core.h"
#include "bmi_cmd.h"

#include "wq_log.h"
#include "wq_fw.h"

char *fw_bt = NULL;
module_param(fw_bt, charp, 0);
MODULE_PARM_DESC(fw_bt,
		 "bt firmware name for any host interface, default: null");

int force_reset = 0;
module_param(force_reset, int, 0);
MODULE_PARM_DESC(force_reset, "Force reset device when download FW, default: 0");

char fw_dtop_name[128];
char fw_wifi_name[128];
char fw_bt_name[128];

int wq_dnld_init(struct wq_core *core)
{
	core->wq_dnld = kzalloc(sizeof(struct fw_dnld), GFP_KERNEL);
	if (!core->wq_dnld) {
		WQ_DBG(DM_GENERIC, DL_ERR, "wq_dnld_init: alloc failed!\n");
		return -ENOMEM;
	}

	core->wq_dnld->fw = NULL;
	core->wq_dnld->fw_len = 0;
	core->wq_dnld->dl_addr = 0;
	core->wq_dnld->start_pc = 0;
	core->wq_dnld->fw_crc = 0;

	return 0;
}

void wq_dnld_deinit(struct wq_core *core)
{
	kfree(core->wq_dnld);
	core->wq_dnld = NULL;
}

int wq_fw_header_verify(struct wq_core *core, const struct firmware *fw,
			u8 fw_type)
{
	u32 crc32 = 0;
	fw_bin_ver_t ver;
	fw_bin_header_t *fw_bin_header = NULL;

	fw_bin_header = (fw_bin_header_t *)fw->data;

	if (fw_bin_header->fw_image_guard == WQ_FW_HEADER_IMG_GUARD) {
		if (fw_bin_header->fw_file_type != fw_type) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "firmware type erro! should be %d not %d\n",
			       fw_type, fw_bin_header->fw_file_type);
			return -EILSEQ;
		} else {
			wq_fw_get_ver_detail(fw_bin_header->version, &ver);
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "firmware version: %d.%d.%d.%d.\n", ver.major,
			       ver.minor, ver.rever, ver.build);
			if (fw_type == WQ_FW_DTOP) {
				core->dtop_fwver = fw_bin_header->version;
			} else if (fw_type == WQ_FW_WIFI) {
				core->wifi_fwver = fw_bin_header->version;
			}

			core->wq_dnld->dl_addr = fw_bin_header->lma;
			core->wq_dnld->start_pc = fw_bin_header->vma;
			core->wq_dnld->fw = fw->data + WQ_FW_HEADER_LEN;
			core->wq_dnld->fw_len = fw->size - WQ_FW_HEADER_LEN;

			crc32 = ~crc32_le(0xFFFFFFFF, core->wq_dnld->fw,
					  core->wq_dnld->fw_len);
			if (fw_bin_header->fw_bin_crc == crc32) {
				core->wq_dnld->fw_crc = crc32;
				return 0;
			} else {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "firmware crc verify error! should be %x, but calculated is %x\n",
				       fw_bin_header->fw_bin_crc, crc32);
				return -EINVAL;
			}
		}
	} else {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "not find guard symbol in firmware! please check it!\n");
	}

	return -EPROTO;
}

static int wq_fw_get_rom_version(struct wq_core *core,
				 struct wq_dev_rom_ver *rom_ver)
{
	int ret = 0;

	ret = bmi_cmd(core, WQ_BMI_CMD_GET_ROM_VER, NULL, 0, rom_ver,
		      sizeof(struct wq_dev_rom_ver), FW_DL_TIMEOUT);
	if (ret == 0xff) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "This version dont support get rom version now!\n");
		return 0;
	} else if (ret == 0) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "Succeed to get rom version = %d.%d, build time = %d:%d\n",
		       rom_ver->major, rom_ver->minor, rom_ver->build_hr,
		       rom_ver->build_min);
		return 0;
	} else {
		WQ_DBG(DM_GENERIC, DL_ERR, "Get rom version failed (%d)!\n",
		       ret);
		return -EIO;
	}
}

static int wq_fw_info_dnld(struct wq_core *core)
{
	int ret = 0;
	ret = bmi_cmd(core, WQ_BMI_CMD_SET_FW_INFO, NULL, 0, NULL, 0,
		      FW_DL_TIMEOUT);

	if (ret)
		WQ_DBG(DM_GENERIC, DL_ERR, "send dtop fw info failed (%d)!\n",
		       ret);

	return ret;
}

static int wq_fw_data_dnld(struct wq_core *core)
{
	int ret = 0;

	if ((core->wq_dnld->fw_len % 4) != 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: wrong firmware length (len=%d) !\n", __func__,
		       core->wq_dnld->fw_len);
		return -ENOENT;
	}

	//send fw raw data to device
	ret = bmi_xfer(core, WQ_FW_DTOP_DL, (u8 *)core->wq_dnld->fw,
		       core->wq_dnld->fw_len, FW_DL_TIMEOUT);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: firmware download failed (ret=%d)\n", __func__,
		       ret);
		return ret;
	} else {
		WQ_DBG(DM_GENERIC, DL_VRB, "%s: firmware download completed\n",
		       __func__);
	}

	return ret;
}

static int wq_fw_verify(struct wq_core *core)
{
	u16 checksum = 0;
	u32 i;
	int ret = 0;

	//send download complete request to device, verify calc checksum
	checksum = 0;
	for (i = 0; i < core->wq_dnld->fw_len; i++) {
		checksum += core->wq_dnld->fw[i];
	}

	ret = bmi_cmd(core, WQ_BMI_CMD_VERIFY_FW, &checksum, sizeof(checksum),
		      NULL, 0, FW_DL_TIMEOUT);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: send firmware download complete request failed (%d), checksum=0x%x!\n",
		       __func__, ret, checksum);
	} else {
		WQ_DBG(DM_GENERIC, DL_VRB,
		       "%s: fw download complete, checksum=0x%x\n", __func__,
		       checksum);
	}

	return ret;
}

static int wq_fw_dtop_ready(struct wq_core *core)
{
	int loop_cnt = 0;
	int ret = 0;
	u8 dstate = 0;

	do {
		loop_cnt++;
		msleep(50);
		ret = bmi_cmd(core, WQ_BMI_CMD_GET_SYS_STATE, NULL, 0, &dstate,
			      sizeof(dstate), FW_DL_TIMEOUT);
		if (ret < 0) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "%s: get device state failed (%d)!\n", __func__,
			       ret);
			continue;
		}

		if (dstate & BIT(WQ_FW_DTOP)) {
			ret = 0;
			break;
		} else {
			ret = -ENXIO;
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s: get device state=%d, loop_cnt=%d\n",
			       __func__, dstate, loop_cnt);
		}
	} while (loop_cnt < 10);

	return ret;
}

static int wq_fw_reset_device(struct wq_core *core)
{
	int ret = 0;

	ret = bmi_cmd(core, WQ_BMI_CMD_UNLOAD_DTOP, NULL, 0, NULL, 0,
		      FW_DL_TIMEOUT);

	return ret;
}

static int wq_fw_get_sys_state(struct wq_core *core, u8 *sys_state)
{
	int ret = 0;

	ret = bmi_cmd(core, WQ_BMI_CMD_GET_SYS_STATE, NULL, 0, sys_state,
		      sizeof(u8), FW_DL_TIMEOUT);

	return ret;
}

static int __wq_fw_dtop_init(struct wq_core *core, const char *fw_name)
{
	u8 sys_state = 0;
	int ret = 0;
	const struct firmware *dtop_fw;

	WQ_DBG(DM_GENERIC, DL_WRN, "load dtop name = %s\n", fw_name);

	ret = wq_fw_get_sys_state(core, &sys_state);
	if (ret)
		goto exit_dnld;

	/* verify dev_state : BootROM or DTOP */
	if (sys_state & BIT(WQ_FW_DTOP)) {
		if (sys_state & BIT(WQ_FW_WIFI)) {
			ret = wq_fw_reset_device(core);
			if (ret)
				goto exit_dnld;
			/* wait for chip reset, retriger driver*/
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "Device's run_state is abnormal, reset! \n");
			return -EPERM;
		} else {
			if (force_reset) {
				ret = wq_fw_reset_device(core);
				if (ret)
					goto exit_dnld;
				/* wait for chip reset, retriger driver*/
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "Reset device and download dtop again.\n");
				return -EPERM;
			}
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "Dtop exist! skip download!\n");
			return 0;
		}
	} else if (sys_state & BIT(WQ_FW_BOOTROM)) {
		WQ_DBG(DM_GENERIC, DL_WRN, "Ready to download Dtop!\n");
	}

	ret = request_firmware(&dtop_fw, fw_name, core->dev);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "dtop request_firmware() failed, error code = %d\n",
		       ret);
		goto exit_dnld;
	}

	ret = wq_fw_header_verify(core, dtop_fw, WQ_FW_DTOP);
	if (ret)
		goto load_erro;

	ret = wq_fw_info_dnld(core);
	if (ret)
		goto load_erro;

	ret = wq_fw_data_dnld(core);
	if (ret)
		goto load_erro;

	ret = wq_fw_verify(core);
	if (ret)
		goto load_erro;

	ret = wq_fw_dtop_ready(core);
load_erro:
	if (dtop_fw)
		release_firmware(dtop_fw);

exit_dnld:
	return ret;
}

int wq_fw_name_update(struct wq_core *core, char *dtop_name, char *wifi_name)
{
	int ret = 0;
	int cnt;
	struct wq_dev_rom_ver rom_ver;

	WQ_DBG(DM_GENERIC, DL_WRN, "update fw name!\n");

	ret = wq_fw_get_rom_version(core, &rom_ver);
	if (ret)
		goto exit;

	if (dtop_name) {
		memcpy(fw_dtop_name, dtop_name, strlen(dtop_name) + 1);
		WQ_DBG(DM_GENERIC, DL_WRN, "get dtop_name:%s\n", fw_dtop_name);
	} else {
		cnt = sprintf(fw_dtop_name, "%s_%s_%x_%x_%s.bin", WQ_CHIP_NAME,
			      "fw_dtop", rom_ver.major, rom_ver.minor,
			      core->hif_name);
		if (cnt > 0) {
			fw_dtop_name[cnt] = '\0';
		} else {
			BUG_ON(1);
			ret = -EPERM;
			goto exit;
		}
		WQ_DBG(DM_GENERIC, DL_WRN, "matching dtop name:%s\n",
		       fw_dtop_name);
	}

	if (wifi_name) {
		memcpy(fw_wifi_name, wifi_name, strlen(wifi_name) + 1);
		WQ_DBG(DM_GENERIC, DL_WRN, "get wifi_name:%s\n", fw_wifi_name);
	} else {
		cnt = sprintf(fw_wifi_name, "%s_%s_%x_%x_%s.bin", WQ_CHIP_NAME,
			      "fw_wifi", rom_ver.major, rom_ver.minor,
			      core->hif_name);
		if (cnt > 0) {
			fw_wifi_name[cnt] = '\0';
		} else {
			BUG_ON(1);
			ret = -EPERM;
			goto exit;
		}
		WQ_DBG(DM_GENERIC, DL_WRN, "matching wifi name:%s\n",
		       fw_wifi_name);
	}

	if (fw_bt) {
		memcpy(fw_bt_name, fw_bt, strlen(fw_bt) + 1);
	} else {
		cnt = sprintf(fw_bt_name, "%s_%s_%x_%x_%s.bin", WQ_CHIP_NAME,
			      "fw_bt", rom_ver.major, rom_ver.minor, "uart");
		if (cnt > 0) {
			fw_bt_name[cnt] = '\0';
		} else {
			BUG_ON(1);
			ret = -EPERM;
			goto exit;
		}
	}

exit:
	return ret;
}
WQ_BMI_API(wq_fw_name_update);

int wq_fw_dtop_init(struct wq_core *core)
{
	int ret;

	wq_core_state_set(core, WQ_CORE_STATE_FW_DL);

	ret = __wq_fw_dtop_init(core, fw_dtop_name);
	if (ret) {
		WQ_DBG(DM_GENERIC, DL_ERR, "DTOP download Failed\n");
		//send trigger pattern
		hif_send_trigger(core, WQ_USB_TRI_FW_DL, 0);
	} else {
		WQ_DBG(DM_GENERIC, DL_WRN, "DTOP download success\n");
	}

	return ret;
}
WQ_BMI_API(wq_fw_dtop_init);