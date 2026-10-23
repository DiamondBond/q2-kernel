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
#ifndef __FW_DBG_H__
#define __FW_DBG_H__

#include <linux/cdev.h>
#include <linux/device.h>

/** Define ioctl */
#define DTOP_CHARDEV_IOCTL_RELEASE _IO('W', 1)
#define DTOP_CHARDEV_IOCTL_GET_DTOP_VERSION _IOR('W', 2, wq_fw_info_t)
#define DTOP_CHARDEV_IOCTL_BOOT_STATE_SET _IOW('W', 3, unsigned short int)
#define DTOP_CHARDEV_IOCTL_FLASH_FW_DL _IOW('W', 4, fw_dnld_param_t)
#define DTOP_CHARDEV_IOCTL_FLASH_FW_DL_FINISH _IO('W', 5)
#define DTOP_CHARDEV_IOCTL_CONSOLE_CMD _IOW('W', 6, char[256])
#define DTOP_CHARDEV_IOCTL_CMD _IOW('W', 7, char[256])
#define DTOP_CHARDEV_IOCTL_MP_TEST_CMD _IOW('W', 8, char[256])

#define DTOP_CHARDEV_MAJOR_NUM (0)

/** Interface specific macros */
#define MDTOPCHAR_MINOR_BASE (0)
#define FMCHAR_MINOR_BASE (10)
#define NFCCHAR_MINOR_BASE (20)
#define DEBUGCHAR_MINOR_BASE (30)

/** Define lock/unlock wrapper */
#define mdev_req_lock(d) down(&d->req_lock)
#define mdev_req_unlock(d) up(&d->req_lock)

/** Define dev type */
#define DTOP_TYPE 1

/** Running flags */
#define CHAR_DEV_UP 0
#define CHAR_DEV_RUNNING 1

/** BIT value */
#define MBIT(x) (((u32)1) << (x))

/** Debug level bit definition */
/** Debug level : Message */
#define MMSG MBIT(0)
/** Debug level : Fatal */
#define MFATAL MBIT(1)
/** Debug level : Error */
#define MERROR MBIT(2)
/** Debug level : Interface Debug */
#define MIF_D MBIT(20)
/** Debug level : Entry */
#define MENTRY MBIT(28)
/** Debug level : Warning */
#define MWARN MBIT(29)
/** Debug level : Info */
#define MINFO MBIT(30)
/** Debug level : Hex Dump */
#define MHEX_DUMP MBIT(31)

#define DEFAULT_DEBUG_MASK (MMSG | MFATAL | MERROR)

/** Debug dump buffer length */
#define DBG_DUMP_BUF_LEN 64
/** Maximum number of dump per line */
#define MAX_DUMP_PER_LINE 16
/** Maximum data dump length */
#define MAX_DATA_DUMP_LEN 48

/**
 * @brief Prints buffer data upto provided length
 *
 * @param prompt		Char pointer
 * @param buf			Buffer
 * @param len			Length
 *
 * @return				N/A
 */
static inline void hexdump(char *prompt, u8 *buf, int len)
{
	int i;
	char dbgdumpbuf[DBG_DUMP_BUF_LEN];
	char *ptr = dbgdumpbuf;

	printk(KERN_DEBUG "%s: len=%d\n", prompt, len);
	for (i = 1; i <= len; i++) {
		ptr += snprintf(ptr, 4, "%02x ", *buf);
		buf++;
		if (i % MAX_DUMP_PER_LINE == 0) {
			*ptr = 0;
			printk(KERN_DEBUG "%s\n", dbgdumpbuf);
			ptr = dbgdumpbuf;
		}
	}
	if (len % MAX_DUMP_PER_LINE) {
		*ptr = 0;
		printk(KERN_DEBUG "%s\n", dbgdumpbuf);
	}
}

/** Debug hexdump */
#define DBG_HEXDUMP(x, y, z)                                                   \
	do {                                                                   \
		if (DEFAULT_DEBUG_MASK & MHEX_DUMP)                            \
			hexdump(x, y, z);                                      \
	} while (0)

typedef enum _fw_boot_mode {
	WQ_BOOT_MODE_NORMAL, // jump to app.bin
	WQ_BOOT_MODE_OTA, // not use
	WQ_BOOT_APP_UPGRADE, // preapare to upgrade
	WQ_BOOT_DL_COMP, // upgrade complete and prepare to jump
	WQ_BOOT_APP_RUN_FAIL, // upgrated without confirm
	WQ_BOOT_MODE_UNKONWN = 0xFF, // same with upgrade when flash is empty
} fw_boot_mode;

typedef enum _wq_fw_type {
	WQ_FW_TYPE_ANY,
	WQ_FW_TYPE_DTOP,
	WQ_FW_TYPE_WIFI,
	WQ_FW_TYPE_BT,
	WQ_FW_TYPE_NUM
} wq_fw_type_e;

/** The type of the chrdev type */
typedef enum _driver_chrdev_type {
	DRIVER_CHRDEV_TYPE_CONSOLE_CMD = 1,
	DRIVER_CHRDEV_TYPE_VENDOR_CMD,
	DRIVER_CHRDEV_TYPE_MAX
} driver_chrdev_type;

//transport by control 0,less then 64 bytes
typedef struct _wq_fw_info {
	u16 chip_ver;
	u8 magic_val_str[14];
	u8 fw_ver_str[16]; //major.minor.micro.build:"xx.xxx.xx.xxxxx"
	u8 build_str[8]; //"release"/"develop"/"debug"
	u8 suffix_str[4]; //"SDK"
	//u8  extra_str[16];
} wq_fw_info_t;

/** Changes ownership of the dev */
int dtop_chardev_chown(char *name, uid_t user, gid_t group);
void fw_dbglog_cmd_proc(struct wq_core *core);

int dtop_register_mdev(struct wq_core *core);
void clean_up_mdev(struct wq_core *core);

#endif /*__FW_DBG_H__*/