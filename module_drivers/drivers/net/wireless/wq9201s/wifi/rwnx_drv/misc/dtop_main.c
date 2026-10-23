/** @file dtop.c
  *
  * @brief This file contains the major functions for firmware
  * download.
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
#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/swab.h>
#include "proc.h"
#include "dtop_main.h"
#include "coex.h"
#include "fw_dbg.h"

#include "wq_fw.h"
#include "fw_log.h"

struct dtop_proc_data dtop_config_data = { 0x0, { 0 }, { 0 }, { 0 },
					   { 0 }, { 0 } };

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
 * autopm
 */
static int wq_proc_autopm_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int wq_proc_autopm_open(struct inode *inode, struct file *file)
{
	return single_open(file, wq_proc_autopm_show, PDE_DATA(inode));
}

static ssize_t wq_proc_autopm_write(struct file *file,
				    const char __user *buffer, size_t len,
				    loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct wq_core *core = seq->private;
	struct dtop_proc_data *pdata =
		(struct dtop_proc_data *)core->driver.p_proc_data;

	char tmp_buf[10] = { 0 };
	char *line = NULL;
	unsigned char config_data = 0;

	if (len == 1 || len > 5) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "ex: echo \"0x01\" > /proc/driver/dtop_usb/autopm\n");
		return -EINVAL;
	}

	if (copy_from_user(tmp_buf, buffer, len))
		return -EFAULT;

	/* Config operation */
	line = tmp_buf;

	if (kstrtou8(line, 0, &config_data))
		return -EINVAL;

	WQ_DBG(DM_TRBUS, DL_INF, "Request autopm=%d\n", config_data);
	pdata->autopm = config_data;

	hif_autopm_enable(core, config_data);

	return len;
}

/**
 *  @brief This function handle generic proc file read
 *
 *  @param file		A pointer to file structure
 *  @param buffer	A pointer to output buffer
 *  @param len		number of byte to read
 *  @param offset	A pointer to offset of file
 *  @return			number of output data
 */
static ssize_t wq_proc_autopm_read(struct file *file, char __user *buffer,
				   size_t len, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct wq_core *core = seq->private;
	struct dtop_proc_data *pdata =
		(struct dtop_proc_data *)core->driver.p_proc_data;
	char info[64];

	if (*pos > 0) {
		return 0;
	}

	snprintf(info, sizeof(info), "autopm: %d\n", pdata->autopm);

	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops wq_proc_autopm_fops = {
	.owner = THIS_MODULE,
	.open = wq_proc_autopm_open,
	.write = wq_proc_autopm_write,
	.read = wq_proc_autopm_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops wq_proc_autopm_fops = {
	.proc_open = wq_proc_autopm_open,
	.proc_write = wq_proc_autopm_write,
	.proc_read = wq_proc_autopm_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

static inline void dtop_deactivate_thread(struct driver_dtop_handle *handle)
{
	handle->pid = 0;
	return;
}

static inline void wq_create_thread(int (*dtop_func)(void *),
				    struct wq_core *core, char *name)
{
	struct driver_dtop_handle *handle = core->driver.p_handle;
	handle->task = kthread_run(dtop_func, (void *)core, "%s", name);
}

static int dtop_service_main_thread(void *data)
{
	struct wq_core *core = (struct wq_core *)data;
	DECLARE_WAITQUEUE(wait, current);
	struct sk_buff *skb;
	driver_msg_header_t *dtop_logger_header;
	struct driver_dtop_handle *handle = NULL;
	struct sk_buff *skb_log;

	handle = core->driver.p_handle;

	init_waitqueue_entry(&wait, current);
	current->flags |= PF_NOFREEZE;

	/* Waiting for FW download Done */
	if (kthread_should_stop() || handle->SurpriseRemoved) {
		WQ_DBG(DM_TRBUS, DL_INF,
		       "main-thread: break from main thread: "
		       "SurpriseRemoved=0x%x\n",
		       handle->SurpriseRemoved);
		handle->SurpriseRemoved = true;
		goto exit;
	}
	/** Record the thread pid */
	handle->pid = current->pid;

	for (;;) {
		add_wait_queue(&core->driver.main_waitQ, &wait);
		set_current_state(TASK_INTERRUPTIBLE);

		if (skb_queue_empty(&core->driver.main_rx_Q)) {
			WQ_DBG(DM_TRBUS, DL_INF, "Main: Thread sleeping...\n");
			schedule();
		}

		set_current_state(TASK_RUNNING);
		remove_wait_queue(&core->driver.main_waitQ, &wait);
		if (kthread_should_stop() || handle->SurpriseRemoved) {
			WQ_DBG(DM_TRBUS, DL_INF,
			       "main-thread: break from main thread: "
			       "SurpriseRemoved=0x%x\n",
			       handle->SurpriseRemoved);
			break;
		}

		WQ_DBG(DM_TRBUS, DL_INF, "Main: Thread waking up...\n");
		if (!skb_queue_empty(&core->driver.main_rx_Q)) {
			skb = skb_dequeue(&core->driver.main_rx_Q);
			if (skb) {
				dtop_logger_header =
					(driver_msg_header_t *)skb->data;
				if ((__swab16(dtop_logger_header->magic) ==
				     DTOP_MSG_MAGIC) ||
				    (__swab16(dtop_logger_header->magic) ==
				     DTOP_MSG_MAGIC_TEMP)) {
					if (__swab16(dtop_logger_header
							     ->version) ==
					    DTOP_MSG_VERSION) {
						switch (__swab16(
							dtop_logger_header
								->msg_type)) {
						case DTOP_MSG_TYPE_LOG:
							// Do Nothing
							skb_log = skb_clone(
								skb,
								GFP_KERNEL);
							wq_fw_log_push(
								core, skb_log,
								skb_log->len);
							break;
						default:
							/* Do nothing */
							break;
						}
					}
				}

				dev_kfree_skb_any(skb);
			}
		}
	}

	goto exit;

exit:
	dtop_deactivate_thread(handle);
	return 0;
}

void dtop_init(struct wq_core *core)
{
	struct driver_dtop_handle *dtop_handle = NULL;
	WQ_DBG(DM_TRBUS, DL_WRN, "dtop_init!\n");

	/* Allocate buffer for driver_dtop_handle */
	if (!(dtop_handle =
		      kmalloc(sizeof(struct driver_dtop_handle), GFP_KERNEL))) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "Allocate buffer for dtop_handle failed!\n");
		return;
	}

	/* Init driver_dtop_handle */
	memset(dtop_handle, 0, sizeof(struct driver_dtop_handle));

	core->driver.p_handle = dtop_handle;
	dtop_handle->SurpriseRemoved = false;

	wq_create_thread(dtop_service_main_thread, core, "wq_dtop_msg_service");

	/* wait for mainthread to up */
	while (!dtop_handle->pid) {
		if (dtop_handle->SurpriseRemoved) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "%s: dtop_service_main_thread init failed\n",
			       __func__);
			return;
		}
		os_sched_timeout(10);
	}

	/** create char device */
	if (dtop_register_mdev(core) != 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "Register Mdev Failed\n");
		goto err_mdev;
	}

	/* Init */
	dtop_proc_init(core);

	/*TODO: coex bus api need adjust */
	// coex_init(core);

	return;

err_mdev:
	return;
}
EXPORT_SYMBOL(dtop_init);

void dtop_deinit(struct wq_core *core)
{
	struct driver_dtop_handle *dtop_handle = NULL;
	dtop_handle = core->driver.p_handle;

	WQ_DBG(DM_TRBUS, DL_WRN, "dtop_deinit!\n");

	if (!dtop_handle)
		goto exit_remove;

	if (dtop_handle->SurpriseRemoved)
		goto exit_remove;

	dtop_handle->SurpriseRemoved = true;
	wake_up_interruptible(&core->driver.main_waitQ);
	while (dtop_handle->pid) {
		os_sched_timeout(1);
		wake_up_interruptible(&core->driver.main_waitQ);
	}

	/* Drop queues */
	skb_queue_purge(&core->driver.main_rx_Q);

	/* Unregister char device */
	clean_up_mdev(core);

	dtop_proc_remove();

	/*TODO: coex bus api need adjust */
	/* Free coex used memory */
	// coex_deinit();

	kfree(dtop_handle);

exit_remove:
	return;
}
EXPORT_SYMBOL(dtop_deinit);

/**
 *  @brief This function initializes proc entry
 *
 *  @param priv	 A pointer to dtop_handle structure
 *  @param name	A pointer to name
 *
 *  @return	DRIVER_STATUS_SUCCESS or DRIVER_STATUS_FAILURE
 */
int dtop_proc_init(struct wq_core *core)
{
	WQ_DBG(DM_TRBUS, DL_INF, "dtop proc init\n");
	core->driver.p_proc_data = &dtop_config_data;

	proc_mkdir(DTOP_PROC_DIR, NULL);
	proc_mkdir(PROC_DIR_FW, NULL);

	proc_create_data(DTOP_PROC_DIR "/autopm", S_IFREG | 0644, NULL,
			 &wq_proc_autopm_fops, core);

	fw_dbglog_cmd_proc(core);
	coex_cmd_proc(core);

	return 0;
}

/**
 *  @brief This function initializes proc entry
 *
 *  @param priv	 A pointer to dtop_handle structure
 *  @param name	A pointer to name
 *
 *  @return	DRIVER_STATUS_SUCCESS or DRIVER_STATUS_FAILURE
 */
int dtop_proc_remove(void)
{
	WQ_DBG(DM_TRBUS, DL_INF, "remove dtop proc\n");

	remove_proc_entry(DTOP_PROC_DIR "/autopm", NULL);
	remove_proc_entry(PROC_DIR_FW "/console_cmd", NULL);
	remove_proc_entry(PROC_DIR_FW "/vendor_cmd", NULL);
	remove_proc_entry(PROC_DIR_FW "/coex_cmd", NULL);

	remove_proc_entry(DTOP_PROC_DIR, NULL);
	remove_proc_entry(PROC_DIR_FW, NULL);

	return 0;
}
