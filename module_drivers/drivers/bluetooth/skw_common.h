/******************************************************************************
 *
 *  Copyright (C) 2020-2023 SeekWave Technology
 *
 *
 ******************************************************************************/

#ifndef __SKW_COMMON_H__
#define __SKW_COMMON_H__

#include <linux/types.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/module.h>


#define BT_HCI_LOG_EN  1
#define BT_CP_LOG_EN   1

#define BT_PROC_LOG_EN 1


#ifndef INCLUDE_NEW_VERSION
#define INCLUDE_NEW_VERSION 0
#endif


#define MAX_BT_LOG_SIZE (5*1024*1024) //500M


//#define SEEKWAVE_BT_LOG_PATH     "/mnt/skwbt"
#define SEEKWAVE_BT_LOG_PATH     "/mnt/"
#define NV_FILE_NAME             "sv6160.nvbin"
#define NV_FILE_NAME_6316        "sv6316.nvbin"
#define NV_FILE_NAME_6160_LITE   "sv6160lite.nvbin"

//#define BD_ADDR_FILE_PATH        ""


#define BD_ADDR_LEN 6

#define LOG_TYPE_HCI 0x01
#define LOG_TYPE_CP  0x07


#define SKWBT_INFO(format, ...) 	pr_info("[SKWBT_INFO] "format, ##__VA_ARGS__)
#define SKWBT_ERROR(format, ...) 	pr_err("[SKWBT_ERROR] "format, ##__VA_ARGS__)

#define SKW_CHIPID_6316       0x5301
#define SKW_CHIPID_6160       0x0017
#define SKW_CHIPID_6160_LITE  0x5302



#define NV_FILE_RD_BLOCK_SIZE    252

#define HCI_CMD_READ_LOCAL_VERSION_INFO 0x1001

#define HCI_CMD_SKW_BT_NVDS      0xFC80
#define HCI_CMD_WRITE_BD_ADDR    0xFC82
#define HCI_CMD_WRITE_BT_STATE   0xFE80

#define HCI_COMMAND_COMPLETE_EVENT      0x0E
#define HCI_EVT_HARDWARE_ERROR          0x10




#if KERNEL_VERSION(4, 14, 0) <= LINUX_VERSION_CODE
#define skw_read   kernel_read
#define skw_write  kernel_write
#else
#define skw_read   vfs_read
#define skw_write  vfs_write
#endif

#ifdef CONFIG_NO_GKI
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif

typedef struct{
	uint8_t  type;
	uint8_t  evt_op;
	uint8_t  len;
	uint8_t  nums;
	uint16_t cmd_op;
	uint8_t  status;
}hci_cmd_cmpl_evt_st;


ssize_t skw_file_write(struct file *, const void *, size_t);

ssize_t skw_file_read(struct file *fp, void *buf, size_t len);

char skw_file_copy(char *scr_file, char *des_file);

void skw_bd_addr_gen_init(void);

char skw_get_bd_addr(unsigned char *buffer);



#endif
