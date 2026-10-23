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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/kfifo.h>
#include <linux/crc32.h>
#include <linux/proc_fs.h>
#include <linux/cdev.h>
#include "proc.h"

#include "wq_log.h"
#include "fw_log.h"
#include "bmi_core.h"
#include "bmi_cmd.h"
#include "core.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0))
#define wq_proc_ops file_operations
#else
#define wq_proc_ops proc_ops
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

#define FW_LOG_MAGIC_NUMBER 0x4C6C5878

#define FW_LOG_FILE_NAME "wq_fw.log"
#define FW_LOG_FILE_PREV_NAME "wq_fw.log.prev"

#define FW_LOG_PROC_DIR_NAME "driver/fw_log"

#define FW_LOG_USB_SKB_SIZE 2048

enum {
	FW_LOG_HIF_NONE = 0,
	FW_LOG_HIF_UART,
	FW_LOG_HIF_USB,
	FW_LOG_HIF_SDIO,
	FW_LOG_HIF_PCIE,
	FW_LOG_HIF_MAX,
} FW_LOG_HIF_TYPE;

enum {
	FW_LOG_LEVEL_NONE,
	FW_LOG_LEVEL_CRIT,
	FW_LOG_LEVEL_ERROR,
	FW_LOG_LEVEL_WARNING,
	FW_LOG_LEVEL_INFO,
	FW_LOG_LEVEL_DEBUG,
	FW_LOG_LEVEL_ALL,
} FW_LOG_LEVEL;

enum {
	/* 0 - 9 */
	FW_LOG_MODULE_APP = 0,
	FW_LOG_MODULE_DRIVER,
	FW_LOG_MODULE_LIB,

	/* 10 - 23 */
	FW_LOG_MODULE_WIFI_PS = 10,
	FW_LOG_MODULE_WIFI_KE,
	FW_LOG_MODULE_WIFI_DBG,
	FW_LOG_MODULE_WIFI_IPC,
	FW_LOG_MODULE_WIFI_DTEST,
	FW_LOG_MODULE_WIFI_MAC,
	FW_LOG_MODULE_WIFI_TX,
	FW_LOG_MODULE_WIFI_RX,
	FW_LOG_MODULE_WIFI_PHY,
	FW_LOG_MODULE_WIFI_HAL,
	FW_LOG_MODULE_WIFI_CHAN,
	FW_LOG_MODULE_WIFI_SCAN,
	FW_LOG_MODULE_WIFI_RC,
	FW_LOG_MODULE_WIFI_VENDOR,

	/* 24 - 31 */
	FW_LOG_MODULE_BT = 24,
	FW_LOG_MODULE_BT_PHY,
	FW_LOG_MODULE_MAX = 32,
} FW_LOG_MODULE;

enum {
	FW_LOG_NONE = 0,
	FW_LOG_TTY,
	FW_LOG_FILE,
} FW_LOG_TYPE;

enum {
	FW_LOG_CMD_PLACEHOLDER = 0,
	FW_LOG_CMD_HIF_ENABLE,
	FW_LOG_CMD_LEVEL,
	FW_LOG_CMD_MODULE,
};

union module_map_union {
	u32 w;
	struct {
		u32 app : 1;
		u32 driver : 1;
		u32 lib : 1;
		u32 rsvd1 : 1;
		u32 rsvd2 : 1;
		u32 rsvd3 : 1;
		u32 rsvd4 : 1;
		u32 rsvd5 : 1;
		u32 rsvd6 : 1;
		u32 rsvd7 : 1;

		u32 ps : 1;
		u32 ke : 1;
		u32 dbg : 1;
		u32 ipc : 1;
		u32 dtest : 1;
		u32 mac : 1;
		u32 tx : 1;
		u32 rx : 1;
		u32 phy : 1;
		u32 hal : 1;
		u32 chan : 1;
		u32 scan : 1;
		u32 rc : 1;
		u32 vendor : 1;

		u32 bt : 1;
		u32 bt_phy : 1;
		u32 rsvd8 : 1;
		u32 rsvd9 : 1;
		u32 rsvd10 : 1;
		u32 rsvd11 : 1;
		u32 rsvd12 : 1;
		u32 rsvd13 : 1;
	} bitmap;
};

static spinlock_t push_lock;

static bool fw_log_q_is_empty(struct fw_log *wq_log)
{
	return (wq_log->q.head == wq_log->q.tail);
}

static bool fw_log_q_is_full(struct fw_log *wq_log)
{
	return (((wq_log->q.head + 1) % FW_LOG_QUEUE_NUM) == wq_log->q.tail);
}

static bool fw_log_q_is_equal_or_max_than(struct fw_log *log, u32 cnt)
{
	if (((log->q.head + FW_LOG_QUEUE_NUM - log->q.tail) %
	     FW_LOG_QUEUE_NUM) >= cnt) {
		return true;
	}

	return false;
}

static void fw_log_usb_ep_msg_in_cb(struct urb *urb)
{
	struct fw_log *log = (struct fw_log *)urb->context;
	struct wq_core *core = log->parent;

	if (urb->status != 0) {
		WQ_DBG(DM_TRBUS, DL_INF, "%s fw log urb status error %d\n",
		       __func__, urb->status);
		dev_kfree_skb_any(log->skb);
	} else {
		wq_fw_log_push(core, log->skb, urb->actual_length);
	}

	log->skb = NULL;

	/* disable EP13 IN token when host hif log disable */

	if (log->type == FW_LOG_NONE) {
		log->usb_prepare = false;
		return;
	}

	fw_log_usb_ep_msg_in(log);
}

int fw_log_usb_ep_msg_in(struct fw_log *log)
{
	int err;
	struct urb *urb;
	struct wq_core *core = log->parent;
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);

	if (!wq_usb->usbdev)
		return -ENODEV;

	urb = usb_alloc_urb(0, GFP_ATOMIC); /* no wait */
	if (!urb) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: alloc urb fail\n", __func__);
		return -ENOMEM;
	}

	if (!log->skb)
		log->skb = dev_alloc_skb(FW_LOG_USB_SKB_SIZE);

	if (!log->skb) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: alloc skb fail\n", __func__);
		usb_free_urb(urb);
		return -ENOMEM;
	}

	usb_fill_bulk_urb(urb, wq_usb->usbdev,
			  usb_rcvbulkpipe(wq_usb->usbdev, log->usb_in_ep),
			  log->skb->data, skb_tailroom(log->skb),
			  fw_log_usb_ep_msg_in_cb, log);

	usb_anchor_urb(urb, &log->anchor);
	err = usb_submit_urb(urb, GFP_ATOMIC); /* no wait */
	if (err) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s submit urb fail %d", __func__,
		       err);
		usb_unanchor_urb(urb);
	}
	usb_free_urb(urb);

	return 0;
}

static int fw_log_usb_ep_in_init(struct fw_log *log)
{
	fw_log_usb_ep_msg_in(log);

	return 0;
}

static void fw_log_usb_ep_in_deinit(struct fw_log *log)
{
	usb_kill_anchored_urbs(&log->anchor);

	if (log->skb)
		dev_kfree_skb_any(log->skb);
}

static void fw_log_do_work(struct work_struct *w)
{
	u8 *data;
	u32 size;
	u32 copied;
	unsigned long flags;
	struct fw_log *log;
	struct tty_port *tty_port;

	log = container_of(w, struct fw_log, worker);

	if (fw_log_q_is_empty(log)) {
		WQ_DBG(DM_TRBUS, DL_INF, "fw log queue empty, do nothing\n");
		return;
	}

	while (!fw_log_q_is_empty(log)) {
		u32 crc32 = 0;
		u32 curr_size = log->q.siz[log->q.tail];
		struct sk_buff *curr_log = log->q.log[log->q.tail];
		fw_hif_header_t *header = (fw_hif_header_t *)curr_log->data;

		data = (u8 *)header + sizeof(fw_hif_header_t);
		size = curr_size - sizeof(fw_hif_header_t);

		crc32 = ~crc32_le(0xFFFFFFFF, data, size);

		if (crc32 != header->crc32) {
			WQ_DBG(DM_TRBUS, DL_WRN,
			       "%s: seq%d crc32 err, %x - %x - %d, discard\n",
			       __func__, header->sequence, header->crc32, crc32,
			       curr_size);
			dev_kfree_skb_any(log->q.log[log->q.tail]);
			log->q.tail = (log->q.tail + 1) % FW_LOG_QUEUE_NUM;
			continue;
		}

		if (log->type == FW_LOG_TTY) {
			if (log->tty_open_cnt) {
				copied = 0;
				tty_port = &log->tty_port;
				spin_lock_irqsave(&log->lock, flags);
				/* only one chance to insert data. */
				copied = tty_insert_flip_string(tty_port, data,
								size);
				spin_unlock_irqrestore(&log->lock, flags);
				if (copied != 0) {
					tty_flip_buffer_push(tty_port);
				}
				if (copied < size) {
					WQ_DBG(DM_TRBUS, DL_WRN,
					       "%s: [FW_LOG] copy data to tty buffer fail, size %d\n",
					       __func__, size - copied);
				}
				WQ_DBG(DM_TRBUS, DL_INF,
				       "%s: parsing packet sequence %d, size %4d, skb %px\n",
				       __func__, header->sequence,
				       copied + (u32)sizeof(fw_hif_header_t),
				       log->q.log[log->q.tail]);
				dev_kfree_skb_any(log->q.log[log->q.tail]);
				log->q.tail =
					(log->q.tail + 1) % FW_LOG_QUEUE_NUM;
				WQ_DBG(DM_TRBUS, DL_INF,
				       "%s: parsing packet sequence %d\n",
				       __func__, header->sequence);
			} else {
				/**
				 * if tty device node not open, just return.
				 * so we can save max to 2048 hif package
				 */
				return;
			}
		} else if (log->type == FW_LOG_FILE) {
			if (!log->file_shrinked && log->file_exist) {
				fw_log_file_shrink((char *)log->file_name, 0);
				log->file_shrinked = true;
			}

			if (log->pos + size <= log->file_max_size) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
				kernel_write(log->filp, data, size, &log->pos);
#else
				kernel_write(log->filp, data, size, log->pos);
#endif
			} else {
				u32 to_end = log->file_max_size - log->pos;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
				kernel_write(log->filp, data, to_end, &log->pos);
#else
				kernel_write(log->filp, data, to_end, log->pos);
#endif
				log->pos = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
				kernel_write(log->filp, data + to_end, size - to_end, &log->pos);
#else
				kernel_write(log->filp, data + to_end, size - to_end, log->pos);
#endif
			}

			WQ_DBG(DM_TRBUS, DL_INF, "file size %lld\n", log->pos);

			dev_kfree_skb_any(log->q.log[log->q.tail]);
			log->q.tail = (log->q.tail + 1) % FW_LOG_QUEUE_NUM;
		}
	}
}

static int wq_log_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct fw_log *log = (struct fw_log *)driver->driver_state;
	tty_standard_install(driver, tty);
	tty->driver_data = log;
	return 0;
}

static int wq_log_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct fw_log *log = (struct fw_log *)tty->driver_data;

	down(&log->sem);
	log->tty_open_cnt++;
	up(&log->sem);

	return 0;
}

static void wq_log_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct fw_log *log = (struct fw_log *)tty->driver_data;

	down(&log->sem);
	log->tty_open_cnt--;
	up(&log->sem);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static ssize_t wq_log_tty_write(struct tty_struct *tty, const unsigned char *buf,
			    size_t count)
#else
static int wq_log_tty_write(struct tty_struct *tty, const unsigned char *buf,
			    int count)
#endif
{
	return count;
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 11, 0)
static int wq_log_tty_write_room(struct tty_struct *tty)
#else
static unsigned int wq_log_tty_write_room(struct tty_struct *tty)
#endif
{
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static void wq_log_tty_set_termios(struct tty_struct *tty, const struct ktermios *old)
#else
static void wq_log_tty_set_termios(struct tty_struct *tty, struct ktermios *old)
#endif
{
}

static int wq_log_tty_tiocmget(struct tty_struct *tty)
{
	return 0;
}

static int wq_log_tty_tiocmset(struct tty_struct *tty, unsigned int set,
			       unsigned int clear)
{
	return 0;
}

static int wq_log_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
			    unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static const struct tty_operations fw_log_tty_ops = {
	.install = wq_log_tty_install,
	.open = wq_log_tty_open,
	.close = wq_log_tty_close,
	.write = wq_log_tty_write,
	.write_room = wq_log_tty_write_room,
	.set_termios = wq_log_tty_set_termios,
	.tiocmget = wq_log_tty_tiocmget,
	.tiocmset = wq_log_tty_tiocmset,
	.ioctl = wq_log_tty_ioctl,
};

static int fw_log_tty_device_init(struct fw_log *log)
{
	int ret;
	struct device *tty_device;
	struct tty_driver *tty_driver;

	tty_driver = tty_alloc_driver(1, TTY_DRIVER_REAL_RAW |
						 TTY_DRIVER_DYNAMIC_DEV |
						 TTY_DRIVER_UNNUMBERED_NODE);
	if (IS_ERR(tty_driver)) {
		ret = PTR_ERR(tty_driver);
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: alloc tty driver fail\n",
		       __func__);
		goto fail;
	}

	tty_driver->driver_name = "wq_tty_drv";
	tty_driver->name = "ttyLog";
	tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	tty_driver->subtype = SERIAL_TYPE_NORMAL;
	tty_driver->init_termios = tty_std_termios;
	tty_driver->init_termios.c_iflag = 0;
	tty_driver->init_termios.c_oflag = OPOST;
	tty_driver->init_termios.c_cflag = B115200 | CS8 | CREAD;
	tty_driver->init_termios.c_lflag = 0;
	tty_driver->init_termios.c_ispeed = 115200;
	tty_driver->init_termios.c_ospeed = 115200;
	tty_driver->driver_state = log;
	tty_set_operations(tty_driver, &fw_log_tty_ops);

	ret = tty_register_driver(tty_driver);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: register tty driver fail\n",
		       __func__);
		goto tty_driver_free;
	}

	tty_port_init(&log->tty_port);
	tty_port_link_device(&log->tty_port, tty_driver, 0);

	tty_device = tty_register_device(tty_driver, 0, NULL);
	if (IS_ERR(tty_device)) {
		ret = PTR_ERR(tty_device);
		WQ_DBG(DM_TRBUS, DL_ERR, "%s - register tty device fail\n",
		       __func__);
		goto tty_driver_unregister;
	}

	log->tty_driver = tty_driver;
	log->tty_open_cnt = 0;

	return 0;

tty_driver_unregister:
	tty_unregister_driver(tty_driver);
	tty_port_destroy(&log->tty_port);
tty_driver_free:
	tty_driver_kref_put(tty_driver);
fail:
	return ret;
}

static void fw_log_tty_device_deinit(struct fw_log *log)
{
	tty_unregister_device(log->tty_driver, 0);
	tty_unregister_driver(log->tty_driver);

	tty_port_destroy(&log->tty_port);
	tty_driver_kref_put(log->tty_driver);
}

int fw_log_file_shrink(const char *path, loff_t size)
{
	struct file *file;
	int err;

	file = filp_open(path, O_RDWR, 0644);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		WQ_DBG(DM_TRBUS, DL_ERR, "%s - Error opening file: %d\n",
		       __func__, err);
		return err;
	}

	err = vfs_truncate(&file->f_path, size);
	if (err) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s - Error shrinking file: %d\n",
		       __func__, err);
		filp_close(file, NULL);
		return err;
	}

	filp_close(file, NULL);
	return 0;
}

int fw_log_file_check_exist(const char *path)
{
	struct file *fp = NULL;

	fp = filp_open(path, O_RDONLY, 0664);
	if (IS_ERR(fp)) {
		printk("open failed reason: %ld\n", PTR_ERR(fp));
		return false;
	}

	filp_close(fp, NULL);

	return true;
}

static int fw_log_file_init(struct fw_log *log)
{
	int ret = 0;
	struct file *filp;

	memset(log->file_name, 0, FW_LOG_FILE_NAME_SIZE);
	strcpy((char *)log->file_name, wq_conf.fw_log_path);
	strcat((char *)log->file_name, FW_LOG_FILE_NAME);

	log->file_exist = fw_log_file_check_exist((char *)log->file_name);
	log->file_shrinked = false;
	log->file_max_size = wq_conf.fw_log_file_size * 1024 * 1024;

	filp = filp_open(log->file_name, O_RDWR | O_CREAT, 0664);
	if (IS_ERR(filp)) {
		ret = PTR_ERR(filp);
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: open file %s fail(%d)\n",
		       __func__, log->file_name, ret);
		goto out;
	}

	log->filp = filp;

out:
	return ret;
}

static void fw_log_file_deinit(struct fw_log *log)
{
	if (log->filp)
		filp_close(log->filp, NULL);
}

static struct fw_log *fw_log_alloc(void)
{
	struct fw_log *log = kzalloc(sizeof(struct fw_log), GFP_KERNEL);
	if (!log) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: alloc wq_log fail, ENOMEM\n",
		       __func__);
		return ERR_PTR(-ENOMEM);
	}

	log->magic = FW_LOG_MAGIC_NUMBER;
	log->q.head = 0;
	log->q.tail = 0;

	log->module = 0xffffffff; /* default enable all modules */
	log->level = FW_LOG_LEVEL_INFO;
	log->type = FW_LOG_NONE;

	log->uart_en = true;
	log->hif_en = false;
	log->tty_en = false;
	log->file_en = false;

	log->usb_prepare = false;
	log->usb_in_ep = 13; /* USB host log use EP13 */

	sema_init(&log->sem, 1);
	INIT_WORK(&log->worker, fw_log_do_work);
	init_usb_anchor(&log->anchor);

	return log;
}

static int fw_log_send_cmd_to_fw(struct wq_core *core, u32 cmd, u32 data)
{
	u32 param[2];
	param[0] = data;
	param[1] = cmd;

	return bmi_log_ctrl(core, &param[0], sizeof(u32) * 2);
}

static int fw_log_hif_enable(struct fw_log *log, u8 enable, u8 uart)
{
	int ret = 0;
	u8 hif = WQ_HIF_NONE;
	struct wq_core *core = log->parent;

	if (uart) {
		ret = fw_log_send_cmd_to_fw(core, FW_LOG_CMD_HIF_ENABLE,
					    FW_LOG_HIF_UART << 16 | enable);
		if (ret)
			goto out;

		log->uart_en = enable;
		return ret;
	}

	if (core->hif_ops->hif == WQ_HIF_USB) {
		hif = FW_LOG_HIF_USB;
	} else if (core->hif_ops->hif == WQ_HIF_SDIO) {
		hif = FW_LOG_HIF_SDIO;
	} else if (core->hif_ops->hif == WQ_HIF_PCIE) {
		hif = FW_LOG_HIF_PCIE;
	} else {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s - unknow hit type %d\n", __func__,
		       hif);
		return -ENODEV;
	}

	if (log->hif_en == enable)
		goto out;

	ret = fw_log_send_cmd_to_fw(core, FW_LOG_CMD_HIF_ENABLE,
				    hif << 16 | enable);
	if (ret)
		goto out;

	log->hif_en = enable;

out:
	return ret;
}

static int fw_log_set_level(struct fw_log *log, u8 level)
{
	return fw_log_send_cmd_to_fw(log->parent, FW_LOG_CMD_LEVEL, level);
}

static int fw_log_set_module(struct fw_log *log, u32 module_map)
{
	return fw_log_send_cmd_to_fw(log->parent, FW_LOG_CMD_MODULE,
				     module_map);
}

static int fw_log_type_change(struct fw_log *log, u8 next_type)
{
	int ret = 0;
	struct wq_core *core = log->parent;

	WQ_DBG(DM_TRBUS, DL_WRN, "%s: curr %d, next %d\n", __func__, log->type,
	       next_type);

	down(&log->sem);

	if (log->type == next_type) {
		goto out;
	}

	if (next_type == FW_LOG_NONE) {
		log->type = FW_LOG_NONE;
		goto out;
	} else {
		if (core->hif_ops->hif == WQ_HIF_USB && !log->usb_prepare) {
			fw_log_usb_ep_in_init(log);
			log->usb_prepare = true;
		}
	}

	if (!log->tty_en && next_type == FW_LOG_TTY) {
		ret = fw_log_tty_device_init(log);
		if (ret) {
			WQ_DBG(DM_TRBUS, DL_ERR, "creat tty fail, ret = %d\n",
			       ret);
			goto out;
		}

		log->tty_en = true;
	}

	if (!log->file_en && next_type == FW_LOG_FILE) {
		ret = fw_log_file_init(log);
		if (ret) {
			WQ_DBG(DM_TRBUS, DL_ERR, "creat file fail, ret = %d\n",
			       ret);
			goto out;
		}

		log->file_en = true;
	}

	log->type = next_type;

	if (!log->hif_en) {
		ret = fw_log_hif_enable(log, true, false);
		if (ret) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "bmi enable hif log fail, ret = %d\n", ret);
			goto out;
		}
	}

out:
	up(&log->sem);
	return ret;
}

static int fw_log_level_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int fw_log_level_open(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	return single_open(file, fw_log_level_show, pde_data(inode));
#else
	return single_open(file, fw_log_level_show, PDE_DATA(inode));
#endif
}

static ssize_t fw_log_level_write(struct file *file, const char __user *buffer,
				  size_t len, loff_t *pos)
{
	int ret;
	u32 level;
	struct seq_file *seq = file->private_data;
	struct fw_log *log = seq->private;
	char cfg[128] = { 0 };

	const char *usage = "error usage: echo \"0 - 6\" > level";

	if (len != 2) {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s\n", usage);
		goto out;
	}

	if (copy_from_user(cfg, buffer, len))
		goto out;

	if (cfg[0] < '0' || cfg[0] > '6') {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s\n", usage);
		goto out;
	}

	ret = kstrtouint(cfg, 10, &level);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_WRN, "kstrtouint fail, level %d, ret %d\n",
		       level, ret);
		goto out;
	} else {
		ret = fw_log_set_level(log, level);
		if (ret) {
			WQ_DBG(DM_TRBUS, DL_WRN,
			       "set fw log level to %d fail, ret %d\n", level,
			       ret);
			goto out;
		}
	}

	WQ_DBG(DM_TRBUS, DL_WRN, "set fw log level to %d\n", level);
	log->level = level;

out:
	return len;
}

static ssize_t fw_log_level_read(struct file *file, char __user *buffer,
				 size_t len, loff_t *pos)
{
	char show[128];
	struct seq_file *seq = file->private_data;
	struct fw_log *log = seq->private;
	const char *level_str[] = { "NONE", "CRIT",  "ERROR", "WARNING",
				    "INFO", "DEBUG", "ALL" };

	if (*pos > 0) {
		return 0;
	}

	snprintf(
		show, sizeof(show),
		"current: %s\n\n========== BIT MAP ==========\n"
		"0: NONE\n1: CRIT\n2: ERROR\n3: WARNING\n4: INFO\n5: DEBUG\n6: ALL\n",
		level_str[log->level]);

	if (!copy_to_user(buffer, show, strlen(show) + 1)) {
		*pos = 1;
		return strlen(show) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops fw_log_level_ops = {
	.owner = THIS_MODULE,
	.open = fw_log_level_open,
	.write = fw_log_level_write,
	.read = fw_log_level_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops fw_log_level_ops = {
	.proc_open = fw_log_level_open,
	.proc_write = fw_log_level_write,
	.proc_read = fw_log_level_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

static int fw_log_module_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int fw_log_module_open(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	return single_open(file, fw_log_module_show, pde_data(inode));
#else
	return single_open(file, fw_log_module_show, PDE_DATA(inode));
#endif
}

static ssize_t fw_log_module_write(struct file *file, const char __user *buffer,
				   size_t len, loff_t *pos)
{
	int ret;
	int i;
	u32 module;
	char cfg[128] = { 0 };
	struct seq_file *seq = file->private_data;
	struct fw_log *log = seq->private;

	const char *usage =
		"error usage: echo \"00000000 - ffffffff\" > module";

	if (len != 9) {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s\n", usage);
		goto out;
	}

	if (copy_from_user(cfg, buffer, len))
		goto out;

	for (i = 0; i < len - 1; i++) {
		if (!((cfg[i] >= '0' && cfg[i] <= '9') ||
		      (cfg[i] >= 'a' && cfg[i] <= 'f'))) {
			WQ_DBG(DM_TRBUS, DL_WRN, "%s\n", usage);
			goto out;
		}
	}

	ret = kstrtouint(cfg, 16, &module);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_WRN, "kstrtouint fail, module %x, ret %d\n",
		       module, ret);
	} else {
		ret = fw_log_set_module(log, module);
		if (ret) {
			WQ_DBG(DM_TRBUS, DL_WRN,
			       "set fw log module to %x fail, ret %d\n", module,
			       ret);
			goto out;
		}
	}

	WQ_DBG(DM_TRBUS, DL_WRN, "set fw log module to %x\n", module);
	log->module = module;

out:
	return len;
}

static ssize_t fw_log_module_read(struct file *file, char __user *buffer,
				  size_t len, loff_t *pos)
{
	char show[512];
	char *en_str[] = { "disable", "enable" };
	union module_map_union map;
	struct seq_file *seq = file->private_data;
	struct fw_log *log = seq->private;

	if (*pos > 0) {
		return 0;
	}

	map.w = log->module;

	snprintf(show, sizeof(show),
		 "current: %08x\n\n========== BIT MAP ==========\n\n"
		 "dtop cfg (bit 0-9):\n"
		 "0: APP     %s\n"
		 "1: DRIVER  %s\n"
		 "2: LIB     %s\n\n"

		 "wifi cfg (bit 10-23):\n"
		 "10: PS     %s\n"
		 "11: KE     %s\n"
		 "12: DBG    %s\n"
		 "13: IPC    %s\n"
		 "14: DTEST  %s\n"
		 "15: MAC    %s\n"
		 "16: TX     %s\n"
		 "17: RX     %s\n"
		 "18: PHY    %s\n"
		 "19: HAL    %s\n"
		 "20: CHAN   %s\n"
		 "21: SCAN   %s\n"
		 "22: RC     %s\n"
		 "23: VENDOR %s\n\n"

		 "bt cfg (bit 24-31):\n"
		 "24: BT     %s\n"
		 "25: BT_PHY %s\n",
		 log->module, en_str[map.bitmap.app], en_str[map.bitmap.driver],
		 en_str[map.bitmap.lib], en_str[map.bitmap.ps],
		 en_str[map.bitmap.ke], en_str[map.bitmap.dbg],
		 en_str[map.bitmap.ipc], en_str[map.bitmap.dtest],
		 en_str[map.bitmap.mac], en_str[map.bitmap.tx],
		 en_str[map.bitmap.rx], en_str[map.bitmap.phy],
		 en_str[map.bitmap.hal], en_str[map.bitmap.chan],
		 en_str[map.bitmap.scan], en_str[map.bitmap.rc],
		 en_str[map.bitmap.vendor], en_str[map.bitmap.bt],
		 en_str[map.bitmap.bt_phy]);

	if (!copy_to_user(buffer, show, strlen(show) + 1)) {
		*pos = 1;
		return strlen(show) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops fw_log_module_ops = {
	.owner = THIS_MODULE,
	.open = fw_log_module_open,
	.write = fw_log_module_write,
	.read = fw_log_module_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops fw_log_module_ops = {
	.proc_open = fw_log_module_open,
	.proc_write = fw_log_module_write,
	.proc_read = fw_log_module_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

static int fw_log_type_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int fw_log_type_open(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	return single_open(file, fw_log_type_show, pde_data(inode));
#else
	return single_open(file, fw_log_type_show, PDE_DATA(inode));
#endif
}

static ssize_t fw_log_type_write(struct file *file, const char __user *buffer,
				 size_t len, loff_t *pos)
{
	int ret;
	u8 next_type = FW_LOG_NONE;
	struct seq_file *seq = file->private_data;
	struct fw_log *log = seq->private;
	char cfg[128] = { 0 };
	const char *usage = "error usage: echo \"tty/file/disable\" > type";

	if (copy_from_user(cfg, buffer, len))
		goto out;

	if (!strncmp(cfg, "tty", strlen("tty"))) {
		next_type = FW_LOG_TTY;
	} else if (!strncmp(cfg, "file", strlen("file"))) {
		next_type = FW_LOG_FILE;
	} else if (!strncmp(cfg, "disable", strlen("disable"))) {
		next_type = FW_LOG_NONE;
	} else if (!strncmp(cfg, "uart disable", strlen("disable"))) {
		fw_log_hif_enable(log, false, true);
		goto out;
	} else if (!strncmp(cfg, "uart enable", strlen("disable"))) {
		fw_log_hif_enable(log, true, true);
		goto out;
	} else {
		next_type = log->type;
		WQ_DBG(DM_TRBUS, DL_WRN, "%s\n", usage);
		goto out;
	}

	ret = fw_log_type_change(log, next_type);
	if (ret)
		WQ_DBG(DM_TRBUS, DL_WRN, "fw log type change fail\n");

out:
	return len;
}

static ssize_t fw_log_type_read(struct file *file, char __user *buffer,
				size_t len, loff_t *pos)
{
	char show[128];
	const char *type_str[] = {
		"disable",
		"tty",
		"file",
	};
	const char *type_hif_str[] = { "NONE", "USB", "SDIO", "PCIE" };
	struct seq_file *seq = file->private_data;
	struct fw_log *log = seq->private;
	struct wq_core *core = log->parent;
	int hif_type = core->hif_ops->hif;

	if (*pos > 0) {
		return 0;
	}

	if (hif_type >= WQ_HIF_MAX) {
		hif_type = 0;
	}

	snprintf(show, sizeof(show), "%s\n\nhif: %s\n\nuart: %s\n",
		 type_str[log->type], type_hif_str[hif_type],
		 log->uart_en ? "enable" : "disable");

	if (!copy_to_user(buffer, show, strlen(show) + 1)) {
		*pos = 1;
		return strlen(show) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops fw_log_type_ops = {
	.owner = THIS_MODULE,
	.open = fw_log_type_open,
	.write = fw_log_type_write,
	.read = fw_log_type_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops fw_log_type_ops = {
	.proc_open = fw_log_type_open,
	.proc_write = fw_log_type_write,
	.proc_read = fw_log_type_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

static void fw_log_proc_init(struct fw_log *log)
{
	/* creat proc dir */
	proc_mkdir(FW_LOG_PROC_DIR_NAME, NULL);
	/* creat proc file */
	proc_create_data(FW_LOG_PROC_DIR_NAME "/level", S_IFREG | 0644, NULL,
			 &fw_log_level_ops, log);
	proc_create_data(FW_LOG_PROC_DIR_NAME "/module", S_IFREG | 0644, NULL,
			 &fw_log_module_ops, log);
	proc_create_data(FW_LOG_PROC_DIR_NAME "/type", S_IFREG | 0644, NULL,
			 &fw_log_type_ops, log);
}

static void fw_log_proc_deinit(void)
{
	remove_proc_entry(FW_LOG_PROC_DIR_NAME "/level", NULL);
	remove_proc_entry(FW_LOG_PROC_DIR_NAME "/module", NULL);
	remove_proc_entry(FW_LOG_PROC_DIR_NAME "/type", NULL);
	remove_proc_entry(FW_LOG_PROC_DIR_NAME, NULL);
}

int wq_fw_log_push(struct wq_core *core, struct sk_buff *skb, uint32_t size)
{
	unsigned long flags;
	fw_hif_header_t *header;
	struct fw_log *log;

	if (!skb)
		goto out;

	spin_lock_irqsave(&push_lock, flags);

	if (!core->fw_log || !size)
		goto fail;

	log = core->fw_log;

	if (log->type == FW_LOG_NONE) {
		WQ_DBG(DM_TRBUS, DL_INF,
		       "%s: fw log push fail, currenr log type %d\n", __func__,
		       log->type);
		goto fail;
	}

	if (fw_log_q_is_full(log)) {
		WQ_DBG(DM_TRBUS, DL_WRN,
		       "%s: fw log push fail, queue is full\n", __func__);
		schedule_work(&log->worker);
		goto fail;
	}

	header = (fw_hif_header_t *)skb->data;

	WQ_DBG(DM_TRBUS, DL_INF,
	       "%s: push    packet sequence %d, size %4d, skb %px\n", __func__,
	       header->sequence, size, skb);

	log->q.log[log->q.head] = skb;
	log->q.siz[log->q.head] = size;
	log->q.head = (log->q.head + 1) % FW_LOG_QUEUE_NUM;

	if (fw_log_q_is_equal_or_max_than(log, 1)) {
		schedule_work(&log->worker);
	}

	spin_unlock_irqrestore(&push_lock, flags);
	return 0;

fail:
	dev_kfree_skb_any(skb);
	spin_unlock_irqrestore(&push_lock, flags);
out:
	return -EIO;
}
WQ_CORE_API(wq_fw_log_push);

int wq_fw_log_proc_init(struct wq_core *core)
{
	int ret = 0;
	struct fw_log *log;

	log = fw_log_alloc();
	if (IS_ERR(log)) {
		ret = PTR_ERR(log);
		goto out;
	}

	core->fw_log = log;

	log->parent = core;

	fw_log_proc_init(log);
	WQ_DBG(DM_TRBUS, DL_WRN, "%s: creat fw_log proc file\n", __func__);

	ret = fw_log_set_level(log, wq_conf.fw_log_level);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_WRN,
		       "set fw log level to %d fail, ret %d\n",
		       wq_conf.fw_log_level, ret);
		goto out;
	}
	WQ_DBG(DM_TRBUS, DL_WRN, "set fw log level: %d \n",
	       wq_conf.fw_log_level);

	/* default creat tty device */
	if (wq_conf.fw_log_enable) {
		ret = fw_log_type_change(log, FW_LOG_TTY);
		if (ret)
			goto out;

		WQ_DBG(DM_TRBUS, DL_WRN, "%s: creat tty success\n", __func__);
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: enable hif log success\n",
		       __func__);
	}

out:
	return ret;
}
WQ_CORE_API(wq_fw_log_proc_init);

int wq_fw_log_proc_deinit(struct wq_core *core)
{
	struct fw_log *log;

	if (!core->fw_log)
		return 0;

	log = core->fw_log;

	flush_work(&log->worker);
	cancel_work_sync(&log->worker);

	if (log->usb_prepare) {
		fw_log_usb_ep_in_deinit(log);
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: usb ep in deinit\n", __func__);
	}

	while (!fw_log_q_is_empty(log)) {
		fw_hif_header_t *header =
			(fw_hif_header_t *)log->q.log[log->q.tail]->data;
		WQ_DBG(DM_TRBUS, DL_INF,
		       "%s: flushing fw log packet sequence %d skb %px\n",
		       __func__, header->sequence, log->q.log[log->q.tail]);

		dev_kfree_skb_any(log->q.log[log->q.tail]);
		log->q.tail = (log->q.tail + 1) % FW_LOG_QUEUE_NUM;
	}

	if (log->tty_en) {
		fw_log_tty_device_deinit(log);
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: tty deinit\n", __func__);
	}

	if (log->file_en) {
		fw_log_file_deinit(log);
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: file deinit\n", __func__);
	}

	fw_log_proc_deinit();
	WQ_DBG(DM_TRBUS, DL_WRN, "%s: remove fw_log proc file\n", __func__);

	kfree(core->fw_log);
	core->fw_log = NULL;

	return 0;
}
WQ_CORE_API(wq_fw_log_proc_deinit);
