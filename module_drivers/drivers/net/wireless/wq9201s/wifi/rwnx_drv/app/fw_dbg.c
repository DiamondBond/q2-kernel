/** @file dtop_chardev.c
  *
  * @brief This file contains the char device function calls
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

#include <linux/path.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/skbuff.h>
#include <linux/poll.h>
#include <linux/netdevice.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#endif

#include "proc.h"

#include "dtop_main.h"
#include "fw_dbg.h"

#include "wq_fw.h"

/** DTOP interface name */
static char *dtop_name;

/** chrdev */
#define MODULE_NAME "dtop_ch_wq"

static int dtop_chardev_major = DTOP_CHARDEV_MAJOR_NUM;

typedef struct htc_msg_desc {
	uint8_t msg_idx;
	uint8_t msg_len;
	uint8_t reseverd_1;
	uint8_t reseverd_2;
} htc_msg_desc_t;

typedef struct mp_info {
	uint16_t param_len;
	uint8_t param[128];
	uint16_t Frame_tail;
} mp_info_t;

/**
 * @brief  Set scheduled timeout
 *
 * @param millisec	   Time unit in ms
 *
 * @return				N/A
 */
static inline void os_sched_timeout(u32 millisec)
{
	set_current_state(TASK_INTERRUPTIBLE);

	schedule_timeout((millisec * HZ) / 1000);
}

/**
 *  @brief This function sends console cmd to chip
 *
 *  @param buf	 The console cmd.
 *  @param length  The length of cmd.
 *
 *  @return		N/A
 */
static void send_console_cmd(struct wq_core *core, u8 *buf, u16 length)
{
	u8 *pbuf;
	u32 len;
	driver_msg_header_t *msg_head;
	int ret = 0;

	ENTER();

	if (core == NULL) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: core is null\n", __func__);
		return;
	}

	WQ_DBG(DM_TRBUS, DL_INF, "Send console cmd %s\n", buf);

	ret = hif_autopm_get(core);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: auto pm, ret=%d\n", __func__,
		       ret);
		goto done;
	}

	msg_head = (driver_msg_header_t *)kzalloc(
		sizeof(driver_msg_header_t) + length, GFP_ATOMIC | GFP_DMA);
	if (!msg_head) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "Allocate buffer for msg_head failed!\n");
		return;
	}

	memset(msg_head, 0, sizeof(driver_msg_header_t));
	msg_head->magic = __swab16(DTOP_MSG_MAGIC);
	msg_head->version = __swab16(DTOP_MSG_VERSION);
	msg_head->msg_type = __swab16(DTOP_MSG_CONSOLE_CMD);
	msg_head->msg_length = __swab16(length);

	memcpy(msg_head->data, buf, length);

	pbuf = (u8 *)msg_head;
	len = sizeof(driver_msg_header_t) + length;

	ret = core->hif_ops->dtop_bulk_send(core, pbuf, len,
					    DRIVER_BULK_MSG_TIMEOUT);

	//check result
	if (ret != 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: failed - ret =%d\n", __func__,
		       ret);
	} else {
		WQ_DBG(DM_TRBUS, DL_WRN, "succeed to send console cmd.\n");
	}

	if (msg_head) {
		kfree(msg_head);
	}

	hif_autopm_put(core);

done:
	LEAVE();
}

/**
 *  @brief This function sends vendor cmd to chip
 *
 *  @param buf	 The vendor cmd.
 *  @param length  The length of vendor cmd.
 *
 *  @return		N/A
 */
static void send_vendor_cmd(struct wq_core *core, u8 *buf, u16 length)
{
	u8 *pbuf;
	u32 len;
	driver_msg_header_t *msg_head;
	int ret = 0;

	ENTER();

	if (core == NULL) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: core is null\n", __func__);
		return;
	}

	WQ_DBG(DM_TRBUS, DL_INF, "Send vendor cmd %s.\n", buf);

	ret = hif_autopm_get(core);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: auto pm, ret=%d\n", __func__,
		       ret);
		goto done;
	}

	msg_head = (driver_msg_header_t *)kzalloc(
		sizeof(driver_msg_header_t) + length, GFP_ATOMIC | GFP_DMA);
	if (!msg_head) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "Allocate buffer for msg_head failed!\n");
		return;
	}

	memset(msg_head, 0, sizeof(driver_msg_header_t));
	msg_head->magic = __swab16(DTOP_MSG_MAGIC);
	msg_head->version = __swab16(DTOP_MSG_VERSION);
	msg_head->msg_type = __swab16(DTOP_MSG_CMD);
	msg_head->msg_length = __swab16(length);

	memcpy(msg_head->data, buf, length);

	pbuf = (u8 *)msg_head;
	len = sizeof(driver_msg_header_t) + length;

	ret = core->hif_ops->dtop_bulk_send(core, pbuf, len,
					    DRIVER_BULK_MSG_TIMEOUT);

	//check result
	if (ret != 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: failed - ret =%d\n", __func__,
		       ret);
	} else {
		WQ_DBG(DM_TRBUS, DL_WRN, "succeed to send vendor cmd.\n");
	}

	if (msg_head) {
		kfree(msg_head);
	}

	hif_autopm_put(core);

done:
	LEAVE();
}

/**
 *	@brief Get fw version from controller
 *
 *	@param handle		A pointer to driver_handle structure
 *	@param wq_fw_type	The type of the fw
 *	@param fw_info		The fw info of version(OUT param)
 *	@return				ERR num
 */
static int dtop_get_fw_info(struct wq_core *core, wq_fw_type_e wq_fw_type,
			    wq_fw_info_t *fw_info)
{
	int ret = 0;
	wq_fw_info_t fw_info_data;
	u16 request;
	ret = hif_autopm_get(core);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: auto pm, ret=%d\n", __func__,
		       ret);
		goto autopm_fail;
	}

	switch (wq_fw_type) {
	case WQ_FW_TYPE_DTOP:
		request = WQ_VREQ_ID_GET_FW_DTOP_FLASH_INFO;
		break;
	default:
		ret = -EINVAL;
		goto done;
	}

	ret = bmi_cmd(core, request, &fw_info_data, sizeof(wq_fw_info_t), NULL,
		      0, CMD_MSG_TIMEOUT);

	if (ret < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: get fw_info failed (%d)!\n",
		       __func__, ret);
		goto done;
	} else {
		memcpy(fw_info, &fw_info_data, sizeof(wq_fw_info_t));
		WQ_DBG(DM_TRBUS, DL_INF,
		       "%s: chip_ver = %d,magic_val_str = %s,fw_ver_str = %s,build_str = %s,suffix_str = %s\n",
		       __func__, fw_info->chip_ver, fw_info->magic_val_str,
		       fw_info->fw_ver_str, fw_info->build_str,
		       fw_info->suffix_str);
	}

done:
	hif_autopm_put(core);
autopm_fail:
	return ret;
}

/**
 *  @brief This function sends upgrade config cmd to chip
 *
 *  @param uprade  The upgrade mode.
 *
 *  @return		N/A
 */
static void dtop_send_boot_type_config(struct wq_core *core,
				       fw_boot_mode uprade)
{
	int ret = 0;
	u32 param;

	WQ_DBG(DM_TRBUS, DL_INF, "Config uprade fw_boot_mode %d .\n", uprade);

	ret = hif_autopm_get(core);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: auto pm, ret=%d\n", __func__,
		       ret);
		goto done;
	}

	/* send dbglog config cmd to device */
	param = (u32)uprade; // value<<16 | index
	ret = bmi_cmd(core, WQ_VREQ_ID_CONFIG_BOOT_TYPE, &param, sizeof(param),
		      NULL, 0, CMD_MSG_TIMEOUT);

	if (ret < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: failed - ret =%d\n", __func__,
		       ret);
	} else {
		WQ_DBG(DM_TRBUS, DL_INF, "succeed to config uprade.\n");
	}

	hif_autopm_put(core);
done:
	return;
}

/**
 *  @brief This function sends mp_test cmd to chip
 *
 *  @param buf	 The vendor cmd.
 *  @param length  The length of vendor cmd.
 *
 *  @return		N/A
 */
static void dtop_send_mp_test_cmd(struct wq_core *core, u8 *buf, u16 length)
{
	u8 *cmd = NULL;
	int ret = DRIVER_STATUS_SUCCESS;

	ENTER();

	WQ_DBG(DM_TRBUS, DL_INF, "Send vendor cmd %s.\n", buf);

	ret = hif_autopm_get(core);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: auto pm, ret=%d\n", __func__,
		       ret);
		goto done;
	}

	cmd = (u8 *)kzalloc(length, GFP_ATOMIC | GFP_DMA);
	if (!cmd) {
		WQ_DBG(DM_TRBUS, DL_ERR, "Allocate buffer for cmd failed!\n");
		return;
	}

	memset(cmd, 0, length);
	memcpy(cmd, buf, length);

	ret = core->hif_ops->dtop_bulk_send(core, cmd, length,
					    DRIVER_BULK_MSG_TIMEOUT);

	//check result
	if (ret != DRIVER_STATUS_SUCCESS) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: failed - ret =%d\n", __func__,
		       ret);
	} else {
		WQ_DBG(DM_TRBUS, DL_INF, "succeed to send vendor cmd.\n");
	}

	hif_autopm_put(core);

done:
	LEAVE();
}

/**
 *  @brief This function handles the wrapper_dev ioctl
 *
 *  @param core	A pointer to wq_core structure
 *  @cmd			ioctl cmd
 *  @arg			argument
 *  @return			-ENOIOCTLCMD
 */
static int mdev_ioctl(struct wq_core *core, unsigned int cmd, void *arg)
{
	int ret = 0;
	u16 boot_type = 0; /*refers to fw_boot_mode*/
#ifdef CONFIG_DL_TO_FLASH
	fw_dnld_param_t fw_dnld;
#endif
	wq_fw_info_t wq_fw_info;
	char cmd_str[BULK_MAX_SIZE + 1];

	WQ_DBG(DM_TRBUS, DL_INF, "IOCTL: cmd=0x%x\n", cmd);
	switch (cmd) {
	/* Following cmds communicate with the dtop */
	case DTOP_CHARDEV_IOCTL_GET_DTOP_VERSION:
		WQ_DBG(DM_TRBUS, DL_INF,
		       "DTOP_CHARDEV_IOCTL_GET_DTOP_VERSION\n");
		ret = dtop_get_fw_info(core, WQ_FW_TYPE_DTOP, &wq_fw_info);
		if (ret == 0) {
			ret = copy_to_user((void __user *)arg, &wq_fw_info,
					   sizeof(wq_fw_info_t));
		}
		break;
	case DTOP_CHARDEV_IOCTL_BOOT_STATE_SET:
		WQ_DBG(DM_TRBUS, DL_INF, "DTOP_CHARDEV_IOCTL_BOOT_STATE_SET\n");
		ret = copy_from_user(&boot_type, (void __user *)arg,
				     sizeof(boot_type));
		if (ret == 0) {
			WQ_DBG(DM_TRBUS, DL_INF,
			       "DTOP_CHARDEV_IOCTL_BOOT_STATE_SET, boot_type = %d\n",
			       boot_type);
			dtop_send_boot_type_config(core, boot_type);
		}
		break;
	case DTOP_CHARDEV_IOCTL_CONSOLE_CMD:
		WQ_DBG(DM_TRBUS, DL_INF, "DTOP_CHARDEV_IOCTL_CONSOLE_CMD\n");
		ret = copy_from_user(cmd_str, (void __user *)arg,
				     sizeof(cmd_str));
		if (ret == 0) {
			cmd_str[BULK_MAX_SIZE] = '\0';
			WQ_DBG(DM_TRBUS, DL_INF,
			       "DTOP_CHARDEV_IOCTL_CONSOLE_CMD, cmd %s\n",
			       cmd_str);
			send_console_cmd(core, cmd_str, strlen(cmd_str));
		}
		break;
	case DTOP_CHARDEV_IOCTL_MP_TEST_CMD:
		WQ_DBG(DM_TRBUS, DL_INF, "DTOP_CHARDEV_IOCTL_MP_TEST_CMD\n");
		ret = copy_from_user(
			cmd_str, (void __user *)arg,
			(sizeof(mp_info_t) + sizeof(htc_msg_desc_t)));
		if (ret == 0) {
			cmd_str[BULK_MAX_SIZE] = '\0';
			dtop_send_mp_test_cmd(
				core, cmd_str,
				(sizeof(mp_info_t) + sizeof(htc_msg_desc_t)));
		}
		break;
	case DTOP_CHARDEV_IOCTL_CMD:
		WQ_DBG(DM_TRBUS, DL_INF, "DTOP_CHARDEV_IOCTL_CMD\n");
		ret = copy_from_user(cmd_str, (void __user *)arg,
				     sizeof(cmd_str));
		if (ret == 0) {
			cmd_str[BULK_MAX_SIZE] = '\0';
			WQ_DBG(DM_TRBUS, DL_INF,
			       "DTOP_CHARDEV_IOCTL_CMD, cmd %s\n", cmd_str);
			send_vendor_cmd(core, cmd_str, strlen(cmd_str));
		}
		break;
		/* Following cmds communicate with the sbl */
#ifdef CONFIG_DL_TO_FLASH
	case DTOP_CHARDEV_IOCTL_FLASH_FW_DL:
		WQ_DBG(DM_TRBUS, DL_INF, "DTOP_CHARDEV_IOCTL_FLASH_FW_DL\n");
		memset(&fw_dnld, 0, sizeof(fw_dnld_param_t));
		ret = copy_from_user(&fw_dnld, (void __user *)arg,
				     sizeof(fw_dnld_param_t));
		if (ret == 0) {
			WQ_DBG(DM_TRBUS, DL_INF,
			       "DTOP_CHARDEV_IOCTL_FLASH_FW_DL, type = %d, name %s, dl_addr 0x%x, fw_load_addr 0x%x, area_size 0x%x\n",
			       fw_dnld.fw_type, fw_dnld.fw_name,
			       fw_dnld.fw_dl_addr, fw_dnld.fw_load_addr,
			       fw_dnld.area_size);
			dtop_upgrade_fw_flash(m_dev->handle, &fw_dnld);
		}
		break;
	case DTOP_CHARDEV_IOCTL_FLASH_FW_DL_FINISH:
		WQ_DBG(DM_TRBUS, DL_INF,
		       "DTOP_CHARDEV_IOCTL_FLASH_FW_DL_FINISH\n");
		ret = dtop_upgrade_fw_to_flash_finish(m_dev->handle);
		break;
#endif
	default:
		WQ_DBG(DM_TRBUS, DL_ERR, "Ioctl for unknown cmd (cmd=0x%x)\n",
		       cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 *  @brief This function closes the wrapper device
 *
 *  @param dev   A pointer to char_dev structure
 *
 *  @return	DRIVER_STATUS_SUCCESS
 */
static int mdev_close(struct wq_core *core, struct char_dev *dev)
{
	ENTER();

	mdev_req_lock(dev);

	if (!test_and_clear_bit(CHAR_DEV_UP, &dev->flags)) {
		mdev_req_unlock(dev);
		LEAVE();
		return 0;
	}

	module_put(THIS_MODULE);
	dev->flags = 0;
	mdev_req_unlock(dev);

	LEAVE();

	return DRIVER_STATUS_SUCCESS;
}

/**
 *  @brief This function opens the wrapper device
 *
 *  @param dev   A pointer to char_dev structure
 *
 *  @return	DRIVER_STATUS_SUCCESS  or other
 */
static int mdev_open(struct char_dev *dev)
{
	ENTER();

	if (try_module_get(THIS_MODULE) == 0)
		return DRIVER_STATUS_FAILURE;

	set_bit(CHAR_DEV_RUNNING, &dev->flags);

	LEAVE();
	return DRIVER_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the device transmit
 *
 *  @param handle   A pointer to core structure
 *  @param buf	  A pointer to data buffer
 *  @param len	  The length of the data buffer
 *
 *  @return	DRIVER_STATUS_SUCCESS or DRIVER_STATUS_FAILURE
 */
static int mdev_send(struct wq_core *core, u8 *buf, u16 len)
{
	int ret = DRIVER_STATUS_SUCCESS;
	driver_chrdev_type chrdev_type;

	ENTER();

	chrdev_type = *((unsigned char *)buf);

	WQ_DBG(DM_TRBUS, DL_INF, "Write: pkt_type: 0x%x, len=%d\n", chrdev_type,
	       len);
	DBG_HEXDUMP("chardev_write", buf, len);
	/*TBD: pkt enqueue*/

	switch (chrdev_type) {
	case DRIVER_CHRDEV_TYPE_CONSOLE_CMD:
		send_console_cmd(core, buf + 1, len - 1);
		break;
	case DRIVER_CHRDEV_TYPE_VENDOR_CMD:
		send_vendor_cmd(core, buf + 1, len - 1);
		break;
	default:
		WQ_DBG(DM_TRBUS, DL_ERR, "dev_send: Invalid chrdev_type %d.\n",
		       chrdev_type);
		ret = DRIVER_STATUS_FAILURE;
		break;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief  Gets char device structure
 *
 *  @param dev		A pointer to char_dev
 *
 *  @return			kobject structure
 */
static struct kobject *chardev_get(struct char_dev *dev)
{
	struct kobject *kobj;

	kobj = kobject_get(&dev->kobj);
	return kobj;
}

/**
 *  @brief  Prints char device structure
 *
 *  @param dev	  A pointer to char_dev
 *
 *  @return		 N/A
 */
static void chardev_put(struct char_dev *dev)
{
	if (dev) {
		WQ_DBG(DM_TRBUS, DL_INF, "dev put kobj\n");
		kobject_put(&dev->kobj);
	}
}
/**
 *  @brief Changes permissions of the dev
 *
 *  @param name pointer to character
 *  @param mode	 mode_t type data
 *  @return		 0--success otherwise failure
 */
static int dtop_chardev_chmod(char *name, mode_t mode)
{
	struct path path;
	struct inode *inode;
	struct iattr newattrs;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		struct mnt_idmap *idmap = NULL;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	struct super_block *sb;
	struct user_namespace *user_ns;
#endif
	int ret;
	int retrycount = 0;

	ENTER();
	do {
		os_sched_timeout(30);
		ret = kern_path(name, LOOKUP_FOLLOW, &path);
		if (++retrycount >= 10) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "dtop_chardev_chmod(): fail to get kern_path\n");
			LEAVE();
			return -EFAULT;
		}
	} while (ret);
	inode = path.dentry->d_inode;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
	mutex_lock(&inode->i_mutex);
#else
	inode_lock(inode);
#endif
	ret = mnt_want_write(path.mnt);
	if (ret)
		goto out_unlock;
	newattrs.ia_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
	if (inode->i_op->setattr)
		ret = inode->i_op->setattr(path.dentry, &newattrs);
	else
		ret = simple_setattr(path.dentry, &newattrs);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
	mutex_unlock(&inode->i_mutex);
#else
	inode_unlock(inode);
#endif

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		idmap = mnt_idmap(path.mnt);
		if (inode->i_op->setattr)
			ret = inode->i_op->setattr(idmap, path.dentry, &newattrs);
		else
			ret = simple_setattr(idmap, path.dentry, &newattrs);
	
		inode_unlock(inode);

#else //LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	sb = inode->i_sb;
	user_ns = sb->s_user_ns;

	if (inode->i_op->setattr)
		ret = inode->i_op->setattr(user_ns, path.dentry, &newattrs);
	else
		ret = simple_setattr(user_ns, path.dentry, &newattrs);

	inode_unlock(inode);
#endif //LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)

	mnt_drop_write(path.mnt);

	path_put(&path);
	LEAVE();
	return ret;
out_unlock:
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
	mutex_unlock(&inode->i_mutex);
#else
	inode_unlock(inode);
#endif
	mnt_drop_write(path.mnt);
	path_put(&path);
	return ret;
}
/**
 *  @brief Changes ownership of the dev
 *
 *  @param name		pointer to character
 *  @param user		uid_t type data
 *  @param group	gid_t type data
 *  @return			0--success otherwise failure
 */
int dtop_chardev_chown(char *name, uid_t user, gid_t group)
{
	struct path path;
	struct inode *inode = NULL;
	struct iattr newattrs;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		struct mnt_idmap *idmap = NULL;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	struct super_block *sb;
	struct user_namespace *user_ns;
#endif
	int ret = 0;
	int retrycount = 0;

	ENTER();
	do {
		os_sched_timeout(30);
		ret = kern_path(name, LOOKUP_FOLLOW, &path);
		if (++retrycount >= 10) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "dtop_chardev_chown(): fail to get kern_path\n");
			LEAVE();
			return -EFAULT;
		}
	} while (ret);
	inode = path.dentry->d_inode;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
	mutex_lock(&inode->i_mutex);
#else
	inode_lock(inode);
#endif
	ret = mnt_want_write(path.mnt);
	if (ret)
		goto out_unlock;
	newattrs.ia_valid = ATTR_CTIME;
	if (user != (uid_t)(-1)) {
		newattrs.ia_valid |= ATTR_UID;
		newattrs.ia_uid = KUIDT_INIT(user);
	}
	if (group != (gid_t)(-1)) {
		newattrs.ia_valid |= ATTR_GID;
		newattrs.ia_gid = KGIDT_INIT(group);
	}
	if (!S_ISDIR(inode->i_mode))
		newattrs.ia_valid |=
			ATTR_KILL_SUID | ATTR_KILL_SGID | ATTR_KILL_PRIV;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
	if (inode->i_op->setattr)
		ret = inode->i_op->setattr(path.dentry, &newattrs);
	else
		ret = simple_setattr(path.dentry, &newattrs);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
	mutex_unlock(&inode->i_mutex);
#else
	inode_unlock(inode);
#endif
#else

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		idmap = mnt_idmap(path.mnt);
		if (inode->i_op->setattr)
			ret = inode->i_op->setattr(idmap, path.dentry, &newattrs);
		else
			ret = simple_setattr(idmap, path.dentry, &newattrs);
#else //LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)

	sb = inode->i_sb;
	user_ns = sb->s_user_ns;

	if (inode->i_op->setattr)
		ret = inode->i_op->setattr(user_ns, path.dentry, &newattrs);
	else
		ret = simple_setattr(user_ns, path.dentry, &newattrs);

	inode_unlock(inode);
#endif //LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)

#endif

	mnt_drop_write(path.mnt);

	path_put(&path);
	LEAVE();
	return ret;
out_unlock:
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
	mutex_unlock(&inode->i_mutex);
#else
	inode_unlock(inode);
#endif
	mnt_drop_write(path.mnt);
	path_put(&path);
	return ret;
}

/**
 *  @brief write handler for char dev
 *
 *  @param filp		pointer to structure file
 *  @param buf		pointer to char buffer
 *  @param count	size of receive buffer
 *  @param f_pos	pointer to loff_t type data
 *  @return			number of bytes written
 */
static ssize_t chardev_write(struct file *filp, const char *buf, size_t count,
			     loff_t *f_pos)
{
	int nwrite = 0;
	unsigned char *buffer;
	struct wq_core *core = (struct wq_core *)filp->private_data;
	struct driver_dtop_handle *handle = core->driver.p_handle;
	struct char_dev *dev = handle->char_dev;

	ENTER();

	if (!dev) {
		LEAVE();
		return -ENXIO;
	}

	if (!test_bit(CHAR_DEV_UP, &dev->flags)) {
		LEAVE();
		return -EBUSY;
	}
	nwrite = count;

	buffer = kmalloc(count, GFP_KERNEL);
	if (!buffer) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "chardev_write(): fail to alloc buffer\n");
		LEAVE();
		return -ENOMEM;
	}

	if (copy_from_user((void *)buffer, buf, count)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "chardev_write(): cp_from_user failed\n");
		kfree(buffer);
		nwrite = -EFAULT;
		goto exit;
	}

	/* Send buffer to mdev */
	if (mdev_send(core, buffer, nwrite)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "Write: Fail\n");
		nwrite = 0;
	}

	/* Send failed */
	kfree(buffer);
exit:
	LEAVE();
	return nwrite;
}

/**
 *  @brief ioctl core handler for char dev
 *
 *  @param filp		pointer to structure file
 *  @param cmd		contains the IOCTL
 *  @param arg		contains the arguement
 *  @return		0--success otherwise failure
 */
static long char_ioctl(struct file *filp, unsigned int cmd, void *arg)
{
	struct wq_core *core = (struct wq_core *)filp->private_data;
	struct driver_dtop_handle *handle = core->driver.p_handle;
	struct char_dev *dev = handle->char_dev;
	// struct char_dev *dev = (struct char_dev *)filp->private_data;

	if (!dev) {
		WQ_DBG(DM_TRBUS, DL_ERR, "unknown device (char_dev=NULL)\n");
		return -ENXIO;
	}

	if (!test_bit(CHAR_DEV_RUNNING, &dev->flags)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "CHAR_DEV_RUNNING not set, flag=0x%lx\n", dev->flags);
		return -EBUSY;
	}

	WQ_DBG(DM_TRBUS, DL_INF, "IOCTL: cmd=0x%x\n", cmd);
	switch (cmd) {
	case DTOP_CHARDEV_IOCTL_RELEASE:
		mdev_close(core, dev);
		break;
	default:
		mdev_ioctl(core, cmd, arg);
		break;
	}
	return 0;
}

/**
 *  @brief ioctl handler for char dev
 *
 *  @param filp		pointer to structure file
 *  @param cmd		contains the IOCTL
 *  @param arg		contains the arguement
 *  @return		0--success otherwise failure
 */
static long chardev_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	return char_ioctl(filp, cmd, (void *)arg);
}

/**
 *  @brief open handler for char dev
 *
 *  @param inode	pointer to structure inode
 *  @param filp		pointer to structure file
 *  @return		0--success otherwise failure
 */
static int chardev_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct cdev *cdev = NULL;
	struct char_dev *dev = NULL;
	struct driver_dtop_handle *handle = NULL;

	ENTER();

	cdev = inode->i_cdev;
	dev = (struct char_dev *)container_of(cdev, struct char_dev, cdev);

	if (!chardev_get(dev)) {
		LEAVE();
		return -ENXIO;
	}

	filp->private_data = dev->core; /* for other methods */
	handle = dev->core->driver.p_handle;
	mdev_req_lock(dev);
	if (test_bit(CHAR_DEV_UP, &dev->flags)) {
		atomic_inc(&dev->extra_cnt);
		goto done;
	}
	if (mdev_open(dev)) {
		ret = -EIO;
		goto done;
	}
	set_bit(CHAR_DEV_UP, &dev->flags);
done:
	mdev_req_unlock(dev);
	if (ret)
		chardev_put(dev);
	LEAVE();
	return ret;
}

/**
 *  @brief release handler for char dev
 *
 *  @param inode	pointer to structure inode
 *  @param filp		pointer to structure file
 *  @return		0--success otherwise failure
 */
static int chardev_release(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct wq_core *core = (struct wq_core *)filp->private_data;
	struct driver_dtop_handle *handle = core->driver.p_handle;
	struct char_dev *dev = handle->char_dev;
	ENTER();
	if (!dev) {
		LEAVE();
		return -ENXIO;
	}
	if ((atomic_dec_if_positive(&dev->extra_cnt) >= 0)) {
		chardev_put(dev);
		LEAVE();
		return ret;
	}

	ret = mdev_close(core, dev);
	filp->private_data = NULL;
	chardev_put(dev);
	LEAVE();
	return ret;
}

/* File ops for the Char driver */
const struct file_operations chardev_fops = {
	.owner = THIS_MODULE,
	// .read = chardev_read,
	.write = chardev_write,
	.unlocked_ioctl = chardev_ioctl,
	.compat_ioctl = chardev_ioctl,
	.open = chardev_open,
	.release = chardev_release,
};

/**
 *  @brief This function creates the char dev
 *
 *  @param dev			A pointer to structure char_dev
 *  @param char_class	A pointer to class struct
 *  @param mod_name		A pointer to char
 *  @param dev_name		A pointer to char
 *  @return			0--success otherwise failure
 */
static int register_char_dev(struct wq_core *core, struct class *char_class,
			     char *mod_name, char *dev_name)
{
	struct driver_dtop_handle *handle = core->driver.p_handle;
	struct char_dev *dev = handle->char_dev;
	struct device *pdevice;
	int ret = 0, dev_num;

	/* create the chrdev region */
	if (dtop_chardev_major) {
		dev_num = MKDEV(dtop_chardev_major, dev->minor);
		ret = register_chrdev_region(dev_num, 1, mod_name);
	} else {
		WQ_DBG(DM_TRBUS, DL_WRN, "chardev: no major # yet\n");
		ret = alloc_chrdev_region((dev_t *)&dev_num, dev->minor, 1,
					  mod_name);
	}

	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "chardev: create chrdev_region failed\n");
		return ret;
	}
	if (!dtop_chardev_major) {
		/* Store the allocated dev major # */
		dtop_chardev_major = MAJOR(dev_num);
	}

	cdev_init(&dev->cdev, &chardev_fops);
	dev_num = MKDEV(dtop_chardev_major, dev->minor);
	handle->char_dev->dev_num = dev_num;

	if (cdev_add(&dev->cdev, dev_num, 1)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "chardev: cdev_add failed\n");
		ret = -EFAULT;
		goto free_cdev_region;
	}

	if (dev->dev_type == DTOP_TYPE) {
		pdevice = device_create(char_class, NULL, dev_num, NULL,
					dev_name);
		if (IS_ERR(pdevice)) {
			ret = PTR_ERR(pdevice);
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "chardev: device_create failed %d\n", ret);
			goto free_cdev_region;
		}
	}
	WQ_DBG(DM_TRBUS, DL_INF, "register char dev=%s\n", dev_name);

	return ret;
free_cdev_region:
	unregister_chrdev_region(MKDEV(dtop_chardev_major, dev->minor), 1);
	return ret;
}

/**
 * @brief  Dynamic release of char dev
 *
 * @param kobj			A pointer to kobject structure
 *
 * @return				N/A
 */
static void char_dev_release_dynamic(struct kobject *kobj)
{
	struct char_dev *dev = container_of(kobj, struct char_dev, kobj);
	WQ_DBG(DM_TRBUS, DL_INF, "free char_dev\n");
	kfree(dev);
}

static struct kobj_type ktype_char_dev_dynamic = {
	.release = char_dev_release_dynamic,
};

/**
 * @brief  Allocation of char dev
 *
 * @param				N/A
 *
 * @return				char_dev
 */
static struct char_dev *alloc_char_dev(void)
{
	struct char_dev *dev;
	dev = kzalloc(sizeof(struct char_dev), GFP_KERNEL);
	if (dev) {
		kobject_init(&dev->kobj, &ktype_char_dev_dynamic);
		WQ_DBG(DM_TRBUS, DL_INF, "alloc char_dev\n");
	}
	return dev;
}

/**
 *  @brief Module configuration and register device
 *
 *  @param handle  A Pointer to driver_handle structure
 *  @return		DRIVER_STATUS_SUCCESS or DRIVER_STATUS_FAILURE
 */
int dtop_register_mdev(struct wq_core *core)
{
	int ret = DRIVER_STATUS_SUCCESS;
	struct char_dev *char_dev = NULL;
	char dev_file[DEV_NAME_LEN + 5];
	struct driver_dtop_handle *handle = NULL;
	struct class *chardev_class;

	handle = core->driver.p_handle;

	/** create char device class */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	chardev_class = class_create(MODULE_NAME);
#else
	chardev_class = class_create(THIS_MODULE, MODULE_NAME);
#endif
	if (IS_ERR(chardev_class)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "Unable to allocate class\n");
		ret = -PTR_ERR(chardev_class);
		;
		goto done;
	}

	/** alloc char dev node */
	char_dev = alloc_char_dev();
	if (!char_dev) {
		ret = -ENOMEM;
		goto err_kmalloc;
	}

	sema_init(&char_dev->req_lock, 1);
	char_dev->minor = MDTOPCHAR_MINOR_BASE + handle->mdtopchar_minor;
	char_dev->dev_type = DTOP_TYPE;

	if (dtop_name)
		snprintf(char_dev->name, sizeof(char_dev->name), "%s%d",
			 dtop_name, handle->mdtopchar_minor);
	else
		snprintf(char_dev->name, sizeof(char_dev->name),
			 "dtop_chardev%d", handle->mdtopchar_minor);

	snprintf(dev_file, sizeof(dev_file), "/dev/%s", char_dev->name);
	WQ_DBG(DM_TRBUS, DL_WRN, "DTOP: Create %s\n", dev_file);

	/** register DTOP char device */
	char_dev->core = core;
	handle->char_dev = char_dev;
	handle->chardev_class = chardev_class;

	/** create DTOP char device node */
	register_char_dev(core, chardev_class, MODULE_NAME,
			  handle->char_dev->name);

	/** chmod & chown for char device, Changes permissions of the dev */
	dtop_chardev_chmod(dev_file, 0666);

	return ret;

err_kmalloc:
	class_destroy(chardev_class);
done:
	return ret;
}

/**
 * console_cmd
 */
static int wq_proc_console_cmd_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int wq_proc_console_cmd_open(struct inode *inode, struct file *file)
{
	return single_open(file, wq_proc_console_cmd_show, PDE_DATA(inode));
}

static ssize_t wq_proc_console_cmd_write(struct file *file,
					 const char __user *buffer, size_t len,
					 loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct wq_core *core = seq->private;
	struct dtop_proc_data *pdata =
		(struct dtop_proc_data *)core->driver.p_proc_data;

	char tmp_buf[512] = { 0 };
	char *line = NULL;

	if (copy_from_user(tmp_buf, buffer, len))
		return -EFAULT;

	/* Config operation */
	if (!strncmp(tmp_buf, "console_cmd=", strlen("console_cmd="))) {
		line = tmp_buf;
		line += strlen("console_cmd") + 1;
		WQ_DBG(DM_TRBUS, DL_INF, "console_cmd=%s\n", line);
		memcpy(&pdata->console_cmd, line, strlen(line) + 1);
		send_console_cmd(core, line, strlen(line) + 1);
	}
	return len;
}

#if 0 //console can't read
/**
 *  @brief This function handle generic proc file read
 *
 *  @param file	A pointer to file structure
 *  @param buffer  A pointer to output buffer
 *  @param len	 number of byte to read
 *  @param offset  A pointer to offset of file
 *  @return		number of output data
 */
static ssize_t wq_proc_console_cmd_read(struct file *file, char __user *buffer, size_t len, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct dtop_proc_data *pdata = seq->private;
	char info[64];

	if(*pos > 0){
		return 0;
	}

	snprintf(info, sizeof(info), "console_cmd:\n");

	if(!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops wq_proc_console_cmd_fops = {
	.owner = THIS_MODULE,
	.open = wq_proc_console_cmd_open,
	.write = wq_proc_console_cmd_write,
	// .read   = wq_proc_console_cmd_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops wq_proc_console_cmd_fops = {
	.proc_open = wq_proc_console_cmd_open,
	.proc_write = wq_proc_console_cmd_write,
	// .proc_read   = wq_proc_console_cmd_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

/**
 * vendor_cmd
 */
static int wq_proc_vendor_cmd_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int wq_proc_vendor_cmd_open(struct inode *inode, struct file *file)
{
	return single_open(file, wq_proc_vendor_cmd_show, PDE_DATA(inode));
}

static ssize_t wq_proc_vendor_cmd_write(struct file *file,
					const char __user *buffer, size_t len,
					loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct wq_core *core = seq->private;
	struct dtop_proc_data *pdata =
		(struct dtop_proc_data *)core->driver.p_proc_data;

	char tmp_buf[512] = { 0 };
	char *line = NULL;

	if (copy_from_user(tmp_buf, buffer, len))
		return -EFAULT;

	/* Config operation */
	if (!strncmp(tmp_buf, "vendor_cmd=", strlen("vendor_cmd="))) {
		line = tmp_buf;
		line += strlen("vendor_cmd") + 1;
		WQ_DBG(DM_TRBUS, DL_INF, "vendor_cmd=%s\n", line);
		memcpy(&pdata->console_cmd, line, strlen(line) + 1);
		send_vendor_cmd(core, line, strlen(line) + 1);
	}
	return len;
}

#if 0 //vendor cmd can't read
/**
 *  @brief This function handle generic proc file read
 *
 *  @param file	A pointer to file structure
 *  @param buffer  A pointer to output buffer
 *  @param len	 number of byte to read
 *  @param offset  A pointer to offset of file
 *  @return		number of output data
 */
static ssize_t wq_proc_vendor_cmd_read(struct file *file, char __user *buffer, size_t len, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct dtop_proc_data *pdata = seq->private;
	char info[64];

	if(*pos > 0){
		return 0;
	}

	snprintf(info, sizeof(info), "vendor_cmd: %d\n", pdata->fw_dbglog_wr);

	if(!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops wq_proc_vendor_cmd_fops = {
	.owner = THIS_MODULE,
	.open = wq_proc_vendor_cmd_open,
	.write = wq_proc_vendor_cmd_write,
	// .read   = wq_proc_vendor_cmd_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops wq_proc_vendor_cmd_fops = {
	.proc_open = wq_proc_vendor_cmd_open,
	.proc_write = wq_proc_vendor_cmd_write,
	// .proc_read   = wq_proc_vendor_cmd_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

void fw_dbglog_cmd_proc(struct wq_core *core)
{
	proc_create_data(PROC_DIR_FW "/console_cmd", S_IFREG | 0644, NULL,
			 &wq_proc_console_cmd_fops, core);
	proc_create_data(PROC_DIR_FW "/vendor_cmd", S_IFREG | 0644, NULL,
			 &wq_proc_vendor_cmd_fops, core);
}

/**
 *  @brief This function deletes the char dev
 *
 *  @param dev			A pointer to structure char_dev
 *  @param char_class	A pointer to class struct
 *  @param dev_name		A pointer to char
 *  @return			0--success otherwise failure
 */
static int unregister_char_dev(struct char_dev *dev, struct class *char_class)
{
	ENTER();
	device_destroy(char_class, dev->dev_num);
	cdev_del(&dev->cdev);
	unregister_chrdev_region(dev->dev_num, 1);
	WQ_DBG(DM_TRBUS, DL_INF, "unregister char dev=%s\n", dev->name);

	LEAVE();
	return 0;
}

/**
 *  @brief This function cleans module
 *
 *  @param core	A pointer to wq_core struct
 *  @param char_class	A pointer to class struct
 *  @return		 N/A
 */
static void chardev_cleanup_one(struct wq_core *core)
{
	struct driver_dtop_handle *handle = NULL;
	struct char_dev *dev = NULL;

	handle = core->driver.p_handle;
	dev = handle->char_dev;

	ENTER();
	unregister_char_dev(dev, handle->chardev_class);
	chardev_put(dev);

	class_destroy(handle->chardev_class);

	LEAVE();
}

/**
 *  @brief clean up m_devs
 *
 *  @return	N/A
 */
void clean_up_mdev(struct wq_core *core)
{
	struct char_dev *dev = NULL;
	struct driver_dtop_handle *handle = NULL;

	handle = core->driver.p_handle;
	dev = handle->char_dev;

	ENTER();

	WQ_DBG(DM_TRBUS, DL_INF, "DTOP: Delete %s\n", dev->name);
	mdev_close(core, dev);
	/**  unregister char_dev */
	if (handle->chardev_class) {
		chardev_cleanup_one(core);
	}

	LEAVE();
	return;
}

module_param(dtop_name, charp, 0);
MODULE_PARM_DESC(dtop_name, "DTOP interface name");
