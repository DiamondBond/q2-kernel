/** @file fw_dbg.h
  *
  * @brief This file contains dtop_chardev driver specific defines etc
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
#ifndef _FW_LOG_H_
#define _FW_LOG_H_

#include "usb.h"

#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>

#define FW_LOG_QUEUE_NUM 2048
#define FW_LOG_FILE_NAME_SIZE 128

typedef enum {
	MSG_TYPE_BASE = 0,
	/* EVENT */
	MSG_TYPE_LOG,
	MSG_TYPE_COEX,

	/* CMD */
	MSG_TYPE_CONSOLE_CMD = 256,
	MSG_TYPE_CMD,
	MSG_TYPE_PTA,
	MSG_TYPE_UT_TP,
} MSG_TYPE_e;

typedef struct {
	u16 magic;
	u16 version;
	u16 msg_type;
	u16 msg_length;
	u8 data[0];
} __attribute__((__packed__)) wq_msg_header_t;

typedef struct {
	wq_msg_header_t header;
	u32 crc32;
	u32 sequence;
} __attribute__((__packed__)) fw_hif_header_t;

struct fw_log {
	u32 magic;
	struct wq_core *parent;

	spinlock_t lock;
	struct semaphore sem;

	u32 module;
	u8 level;
	u8 type;

	u8 uart_en;
	u8 hif_en;
	u8 tty_en;
	u8 file_en;

	u8 usb_prepare;
	u8 usb_in_ep;
	struct urb* urb;
	struct usb_anchor anchor;
	struct sk_buff *skb;
	/* work queue */
	struct work_struct worker;

	/* file */
	u8 file_shrinked;
	u8 file_exist;
	u32 file_max_size;
	char file_name[FW_LOG_FILE_NAME_SIZE];
	struct file *filp;
	loff_t pos; /* file position (offset) */

	/* queue */
	// struct __kfifo log_fifo;
	struct {
		int head;
		int tail;
		struct sk_buff *log[FW_LOG_QUEUE_NUM];
		uint32_t siz[FW_LOG_QUEUE_NUM];
	} q;

	/* tty device */
	struct tty_driver *tty_driver;
	struct tty_port tty_port;
	int tty_open_cnt;
};

int wq_fw_log_proc_init(struct wq_core *core);
int wq_fw_log_proc_deinit(struct wq_core *core);
int wq_fw_log_push(struct wq_core *core, struct sk_buff *skb, uint32_t size);

int fw_log_file_check_exist(const char *path);
int fw_log_file_shrink(const char *path, loff_t size);

int fw_log_usb_ep_msg_in(struct fw_log* log);

#endif		/* _FW_LOG_H_ */
