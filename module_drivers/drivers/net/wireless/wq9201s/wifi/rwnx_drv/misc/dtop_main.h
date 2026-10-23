/** @file dtop_main.h
  *
  * @brief This file defines all the data structures and all the APIs for coex
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

#ifndef _WQ_DTOP_MAIN_H
#define _WQ_DTOP_MAIN_H

#include <linux/cdev.h>
#include "wq_log.h"
#include "bmi_core.h"

/* The magic id for dtop msg structure */
#define DTOP_MSG_MAGIC 0x7771 /* wq */
#define DTOP_MSG_VERSION 0x0001

/* temporary use */
#define DTOP_MSG_MAGIC_TEMP 0x676D /* mg */

/** Magic of bulk msg header */
#define BULK_MAX_SIZE 256

/** Length of device name */
#define DEV_NAME_LEN 32

/** Timeout in milliseconds for usb_bulk_msg function */
#define DRIVER_BULK_MSG_TIMEOUT 100
#define DRIVER_BULK_MSG_LONG_TIMEOUT 1000

#define CMD_MSG_TIMEOUT 200

#ifndef DTOP_PROC_DIR
#define DTOP_PROC_DIR "driver/dtop_usb"
#endif

#ifndef PROC_DIR_FW
#define PROC_DIR_FW "driver/fw"
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0))
#define wq_proc_ops file_operations
#else
#define wq_proc_ops proc_ops
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

struct dtop_proc_data {
	/** autopm config command */
	u8 autopm;
	/** console cmd command */
	u8 console_cmd[256];
	/** vendor cmd command */
	u8 vendor_cmd[256];
	/** coex cmd command */
	u8 coex_cmd[256];
	/** coex pti command */
	u8 coex_pti[256];
	/** coex abort command */
	u8 coex_abort[256];
};

/* Type of dtop msg */
typedef enum {
	DTOP_MSG_TYPE_BASE = 0,
	/* EVENT */
	DTOP_MSG_TYPE_LOG,
	DTOP_MSG_TYPE_COEX, // delete it

	/* CMD */
	DTOP_MSG_CONSOLE_CMD = 256,
	DTOP_MSG_CMD,
	DTOP_MSG_PTA
} DTOP_MSG_TYPE_e;

/* dtop msg structure */
typedef struct dtop_logger_header_struct {
	u16 magic;
	u16 version;
	u16 msg_type;
	u16 msg_length;
	u8 data[0];
} __attribute__((__packed__)) driver_msg_header_t;

/** driver_status */
typedef enum _driver_status {
	DRIVER_STATUS_FAILURE = 0xffffffff,
	DRIVER_STATUS_SUCCESS = 0,
	DRIVER_STATUS_PENDING,
} driver_status;

/** Declaration of char_dev struct */
struct char_dev {
	char name[DEV_NAME_LEN];
	unsigned long flags;
	struct semaphore req_lock;
	atomic_t extra_cnt;

	int dev_type;
	int minor;
	int dev_num;
	struct cdev cdev;
	struct kobject kobj;

	struct wq_core *core;
};

/** dtop Handle structure */
struct driver_dtop_handle {
	/** Task */
	struct task_struct *task;
	/** PID */
	pid_t pid;
	u8 SurpriseRemoved;
	/** Interface specific variables */
	int mdtopchar_minor;
	/** Declaration of chardev class */
	struct class *chardev_class;
	/* char_dev struct */
	struct char_dev *char_dev;
};

void dtop_init(struct wq_core *core);
void dtop_deinit(struct wq_core *core);
int dtop_proc_init(struct wq_core *core);
int dtop_proc_remove(void);

#endif //_WQ_DTOP_MAIN_H
