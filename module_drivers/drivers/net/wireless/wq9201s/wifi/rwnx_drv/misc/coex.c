/** @file coex.c
  *
  * @brief This file contains the major functions for coex
  *
  *  Copyright (C) 2022, WuQi Technologies. ALL RIGHTS RESERVED.
  *
  *  This Information is proprietary to WuQi Technologies and MAY NOT
  *  be copied by any method or incorporated into another program without
  *  the express written consent of WuQi. This Information or any portion
  *  thereof remains the property of WuQi. The Information contained herein
  *  is believed to be accurate and WuQi assumes no responsibility or
  *  liability for its use in any way and conveys no license or title under
  *  any patent or copyright and makes no representation or warranty that this
  *  Information is free from patent or copyright infringement.
  *
  */
#include <linux/firmware.h>
#include <linux/module.h>

#include "rwnx_compat.h"

#include "proc.h"
#include "dtop_main.h"
#include "coex.h"

#define BT_PTI_LOW(x) ((x)&0xf)
#define BT_PTI_HIGH(x) ((x) >> 4 & 0xf)

/** Coex config name */
char *coex_cfg_name = NULL;
/** coex config name */
uint8_t *g_pcoex_cfg_buff = NULL;

/**
 *  @brief driver calls this function to parse coex msg
 *
 *  @param handle A pointer to driver_handle structure
 *  @param buffer A pointer to data buffer
 *  @param msg_len  The length of the data buffer
 *  @return None
 */
void coex_msg_parse(struct mm_coex_info_upd *ind)
{
	WQ_DBG(DM_TRBUS, DL_WRN, "coex info upd:%d\n", ind->msg_type);
	switch (ind->msg_type) {
	case COEX_WIFI_CUS_SCENE_UPD:
		break;
	case COEX_BT_CUS_SCENE_UPD:
		break;
	default:
		WQ_DBG(DM_TRBUS, DL_ERR, "Unknow coex msg:%d\n", ind->msg_type);
		break;
	}
}

/**
 *  @brief This function sends wifi pti to chip
 *
 *  @param core A pointer to wq_core structure
 *  @param buf      The coex pti.
 *
 *  @return     N/A
 */
static void send_wifi_pti(struct wq_core *core, u8* buf)
{
    char *line, *tmp_char;
	uint8_t *tmp_buf;
    uint8_t msg[BULK_MAX_SIZE + 1];
    uint8_t tmp = 0;
    uint8_t cnt = 0;
	wifi_pti_upd_t wifi_pti;

    coex_msg_t *coex_msg = (coex_msg_t *)msg;
	coex_msg->msg_id = COEX_WIFI_PTI_UPD;
	tmp_buf = (uint8_t *)(&(wifi_pti.pti));

    line = buf;

	while((tmp_char = strsep(&line, " \n"))) {
        if (strlen(tmp_char) == 0)
            continue;

        if ((kstrtou8(tmp_char, 0, &tmp)) == 0) {
            wifi_pti.wifi_status = tmp;
            WQ_DBG(DM_TRBUS, DL_WRN, "wifi_status: %d!\n",tmp);
            break;
        } else {
            WQ_DBG(DM_TRBUS, DL_ERR, "wifi_status kstrtou8 err!\n");
            return;
        }
    }

    while ((tmp_char = strsep(&line, " \n"))) {
        if (strlen(tmp_char) == 0)
            continue;

        if (kstrtou8(tmp_char, 0, &tmp)) {
            WQ_DBG(DM_TRBUS, DL_ERR, "%s kstrtou8 err!\n",
			       __func__);
            return;
        }

        tmp_buf[cnt++] = tmp;
    }

    if (cnt != sizeof(wifi_pti_t)) {
        WQ_DBG(DM_TRBUS, DL_ERR, "wifi err input\n");
        return;
    }

	memcpy(coex_msg->data, &wifi_pti, sizeof(wifi_pti_upd_t));
    // TODO:send to fw
}

/**
 *  @brief This function sends bt pti to chip
 *
 *  @param core	A pointer to wq_core structure
 *  @param buf		The coex pti.
 *
 *  @return		N/A
 */
static void send_bt_pti(struct wq_core *core, u8 *buf)
{
	char *line, *tmp_char;
	uint8_t msg[BULK_MAX_SIZE + 1];
	uint8_t tmp = 0;
	uint8_t cnt = 0;
	uint8_t *tmp_buf;
	uint8_t tmp_l = 0;
	uint8_t tmp_h = 0;

	coex_msg_t *coex_msg = (coex_msg_t *)msg;
	bt_pti_upd_t bt_pti;

	coex_msg->msg_id = COEX_BT_PTI_UPD;
	bt_pti.pti_type = WQ_BT_PTI_TYPE_BT;
	tmp_buf = (uint8_t *)(&(bt_pti.prio));

	line = buf;

	while((tmp_char = strsep(&line, " \n"))) {
        if (strlen(tmp_char) == 0)
            continue;

        if ((kstrtou8(tmp_char, 0, &tmp)) == 0) {
            bt_pti.bt_type = tmp;
            WQ_DBG(DM_TRBUS, DL_WRN, "link_type: %d!\n",tmp);
            break;
        } else {
            WQ_DBG(DM_TRBUS, DL_ERR, "link_type kstrtou8 err!\n");
            return;
        }
    }

	while ((tmp_char = strsep(&line, " \n"))) {
		if (strlen(tmp_char) == 0)
			continue;

		if (kstrtou8(tmp_char, 0, &tmp)) {
			WQ_DBG(DM_TRBUS, DL_ERR, "%s kstrtou8 err!\n",
			       __func__);
			return;
		}

		if (cnt % 2) {
			tmp_h = tmp;
			tmp_buf[(cnt / 2)] =
				((0xf & tmp_h) << 4) + (0xf & tmp_l);
			cnt++;
		} else {
			tmp_l = tmp;
			cnt++;
		}
	}

	if (cnt != (2 * sizeof(bt_pti_t))) {
		WQ_DBG(DM_TRBUS, DL_ERR, "bt err input\n");
		return;
	}

	memcpy(coex_msg->data, &bt_pti, sizeof(bt_pti_upd_t));
	// TODO:send to fw
}

/**
 *  @brief This function sends bt pti to chip
 *
 *  @param core	A pointer to wq_core structure
 *  @param scene_id	bt need to set scene id
 *  @param buf		The coex pti.
 *
 *  @return		N/A
 */
static void send_le_pti(struct wq_core *core, u8 *buf)
{
	char *line, *tmp_char;
	uint8_t msg[BULK_MAX_SIZE + 1];
	uint8_t tmp = 0;
	uint8_t cnt = 0;
	uint8_t *tmp_buf;

	coex_msg_t *coex_msg = (coex_msg_t *)msg;
	le_pti_upd_t le_pti;

	coex_msg->msg_id = COEX_BT_PTI_UPD;
	le_pti.pti_type = WQ_BT_PTI_TYPE_LE;
	tmp_buf = (uint8_t *)(&(le_pti.prio));
	
	line = buf;

	while((tmp_char = strsep(&line, " \n"))) {
        if (strlen(tmp_char) == 0)
            continue;

        if ((kstrtou8(tmp_char, 0, &tmp)) == 0) {
            le_pti.bt_type = tmp;
            WQ_DBG(DM_TRBUS, DL_WRN, "link_type: %d!\n",tmp);
            break;
        } else {
            WQ_DBG(DM_TRBUS, DL_ERR, "link_type kstrtou8 err!\n");
            return;
        }
    }

	while ((tmp_char = strsep(&line, " \n"))) {
		if (strlen(tmp_char) == 0)
			continue;

		if (kstrtou8(tmp_char, 0, &tmp)) {
			WQ_DBG(DM_TRBUS, DL_ERR, "%s kstrtou8 err!\n",
			       __func__);
			return;
		}

		tmp_buf[cnt++] = tmp;
	}

	if (cnt != sizeof(le_pti_t)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "le err input\n");
		return;
	}

	memcpy(coex_msg->data, &le_pti, sizeof(le_pti_upd_t));
	// TODO:send to fw
}

/**
 * coex_cmd
 */
static int wq_proc_coex_cmd_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int wq_proc_coex_cmd_open(struct inode *inode, struct file *file)
{
	return single_open(file, wq_proc_coex_cmd_show, PDE_DATA(inode));
}

static ssize_t wq_proc_coex_cmd_write(struct file *file,
				      const char __user *buffer, size_t len,
				      loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct wq_core *core = seq->private;
	struct dtop_proc_data *pdata =
		(struct dtop_proc_data *)core->driver.p_proc_data;

	char tmp_buf[BULK_MAX_SIZE + 1] = { 0 };
	char *line = NULL;

	if (copy_from_user(tmp_buf, buffer, len))
		return -EFAULT;

	/* Config operation */
	if (!strncmp(tmp_buf, "wifi_pti=", strlen("wifi_pti="))) {
		line = tmp_buf;
		line += strlen("wifi_pti=");
		WQ_DBG(DM_TRBUS, DL_INF, "wifi_pti=%s\n", line);
		memcpy(&pdata->coex_pti, line, strlen(line) + 1);
		send_wifi_pti(core, line);
	} else if (!strncmp(tmp_buf, "bt_pti=", strlen("bt_pti="))) {
		line = tmp_buf;
		line += strlen("bt_pti=");
		WQ_DBG(DM_TRBUS, DL_INF, "bt_pti=%s\n", line);
		memcpy(&pdata->coex_pti, line, strlen(line) + 1);
		send_bt_pti(core, line);
	} else if (!strncmp(tmp_buf, "le_pti=", strlen("le_pti="))) {
		line = tmp_buf;
		line += strlen("le_pti=");
		WQ_DBG(DM_TRBUS, DL_INF, "le_pti=%s\n", line);
		memcpy(&pdata->coex_pti, line, strlen(line) + 1);
		send_le_pti(core, line);
	} else if (!strncmp(tmp_buf, "pta_force=", strlen("pta_force="))) {
		line = tmp_buf;
		line += strlen("pta_force=");
		WQ_DBG(DM_TRBUS, DL_INF, "pta_force=%s\n", line);
	} else {
		WQ_DBG(DM_TRBUS, DL_ERR, "support cmd:\n\twifi_pti\n\tbt_pti\n\tle_pti\n");
	}

	return len;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops wq_proc_coex_cmd_fops = {
	.owner = THIS_MODULE,
	.open = wq_proc_coex_cmd_open,
	.write = wq_proc_coex_cmd_write,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops wq_proc_coex_cmd_fops = {
	.proc_open = wq_proc_coex_cmd_open,
	.proc_write = wq_proc_coex_cmd_write,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

void coex_init(struct wq_core *core)
{
	uint8_t *pcoex_cfg_buff_t = NULL;
	const struct firmware *coex_cfg;

	if (coex_cfg_name == NULL) {
		WQ_DBG(DM_TRBUS, DL_ERR, "COEX: No receive coex_cfg_name\n");
		return;
	}

	if (request_firmware(&coex_cfg, coex_cfg_name, core->dev) < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "COEX: request_firmware() failed\n");
		return;
	}

	//alloc memory
	pcoex_cfg_buff_t =
		(uint8_t *)kzalloc(coex_cfg->size, GFP_ATOMIC | GFP_DMA);
	g_pcoex_cfg_buff = pcoex_cfg_buff_t;
	WQ_DBG(DM_TRBUS, DL_INF, "COEX: Download file: %s	length=%ld\n",
	       coex_cfg_name, (long)coex_cfg->size);
	memcpy(g_pcoex_cfg_buff, coex_cfg->data, coex_cfg->size);

	// TODO:send abort prop by bmi

	if (coex_cfg) {
		release_firmware(coex_cfg);
	}
}

void coex_cmd_proc(struct wq_core *core)
{
	proc_create_data(PROC_DIR_FW "/coex_cmd", S_IFREG | 0644, NULL,
			 &wq_proc_coex_cmd_fops, core);
}

void coex_deinit(void)
{
	//close config json, release memory used
	WQ_DBG(DM_TRBUS, DL_INF, "COEX: close config json :\n");
	if (g_pcoex_cfg_buff) {
		kfree(g_pcoex_cfg_buff);
		g_pcoex_cfg_buff = NULL;
		WQ_DBG(DM_TRBUS, DL_INF, "  release coex_cfg_buff memory\n");
	}
}

#ifdef CONFIG_WQ_DTOP
module_param(coex_cfg_name, charp, 0);
MODULE_PARM_DESC(coex_cfg_name, "COEX_CFG_Name");
#endif
