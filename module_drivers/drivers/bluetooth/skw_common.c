/******************************************************************************
 *
 *  Copyright (C) 2020-2023 SeekWave Technology
 *
 *
 ******************************************************************************/

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/time.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>

#include "skw_common.h"
#include "skw_btsnoop.h"

#if ((BT_HCI_LOG_EN == 1) || (BT_CP_LOG_EN == 1))

#define FILE_RW_ENABLE

#endif

#ifndef BD_ADDR_FILE_PATH
//#define BD_ADDR_FILE_PATH SEEKWAVE_BT_LOG_PATH
#else

#endif

#define BD_ADDR_FILE_PATH "/dev/"


static unsigned char bdaddr_lap[4] = {0x12, 0x24, 0x56};
static char bdaddr_valid = 0;
static unsigned int randseed;




mm_segment_t skwbt_get_fs(void)
{
    mm_segment_t oldfs;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
    oldfs = force_uaccess_begin();
#else
    oldfs = get_fs();
    set_fs(KERNEL_DS);
#endif

    return oldfs;
}

void skwbt_set_fs(mm_segment_t fs)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
    force_uaccess_end(fs);
#else
    set_fs(fs);
#endif

}


ssize_t skw_file_write(struct file *fp, const void *buf, size_t len)
{
    ssize_t res_len = 0;
    loff_t pos = fp->f_pos;
    mm_segment_t fs = skwbt_get_fs();
#ifdef FILE_RW_ENABLE
    res_len = skw_write(fp, buf, len, &pos);
#endif	
    fp->f_pos = pos;
    skwbt_set_fs(fs);

    return res_len;
}
EXPORT_SYMBOL_GPL(skw_file_write);


ssize_t skw_file_read(struct file *fp, void *buf, size_t len)
{
    ssize_t res_len = 0;
    loff_t pos = fp->f_pos;
    mm_segment_t fs = skwbt_get_fs();
#ifdef FILE_RW_ENABLE
    res_len = skw_read(fp, buf, len, &pos);
#endif
    fp->f_pos = pos;
    skwbt_set_fs(fs);
    return res_len;
}
EXPORT_SYMBOL_GPL(skw_file_read);



/*
file copy
return 1:success
*/
char skw_file_copy(char *scr_file, char *des_file)
{
#ifdef FILE_RW_ENABLE	
    struct file *src_fp = filp_open(scr_file, O_RDONLY, 0644);
    struct file *des_fp = filp_open(des_file, O_RDWR | O_CREAT, 0644);
    char *pld_buf;
    int len;

    if(IS_ERR(src_fp) || (IS_ERR(des_fp)))
    {
        return -1;
    }
    pld_buf = (char *)kzalloc(1025, GFP_KERNEL);

    while(1)
    {
        len = skw_file_read(src_fp, pld_buf, 1024);
        if(len <= 0)
        {
            break;
        }
        skw_file_write(des_fp, pld_buf, len);
    }

    kfree(pld_buf);
    filp_close(src_fp, NULL);
    filp_close(des_fp, NULL);
#endif
    return 1;
}
EXPORT_SYMBOL_GPL(skw_file_copy);



unsigned int skw_rand(void)
{
    unsigned int r;// = randseed = randseed * 1103515245 + 12345;

    do
    {
        r = randseed = randseed * 1103515245 + 12345;
        r = (r << 16) | ((r >> 16) & 0xFFFF);
    } while(r == 0);

    return r;
}

void skw_srand(void)
{
    randseed = (unsigned int) ktime_get_ns();
    skw_rand();
    skw_rand();
    skw_rand();
}


void skw_bd_addr_gen_init(void)
{
#ifdef BD_ADDR_FILE_PATH

#ifdef FILE_RW_ENABLE
    struct file *fp = NULL;
    char file_path[256] = {0};
    if(bdaddr_valid)
    {
        return ;
    }
    skw_srand();

    snprintf(file_path, 256, "%s/skwbdaddr", BD_ADDR_FILE_PATH);

    SKWBT_INFO("skwbdaddr init path:%s\n", file_path);

    fp = filp_open(file_path, O_RDWR, 0666);
    if((fp == NULL) || IS_ERR(fp))
    {
        fp = filp_open(file_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if((fp == NULL) || IS_ERR(fp))
        {
            SKWBT_INFO("skwbdaddr open err:%ld\n", PTR_ERR(fp));
        }
        else
        {
            bdaddr_lap[0] = (unsigned char)(skw_rand() & 0xFF);
            bdaddr_lap[1] = (unsigned char)(skw_rand() & 0xFF);
            bdaddr_lap[2] = (unsigned char)(skw_rand() & 0xFF);
            SKWBT_INFO("skwbd addr:%x\n", *((u32 *)bdaddr_lap));
            if(skw_file_write(fp, bdaddr_lap, 3) != 3)
            {
                SKWBT_INFO("skwbd addr write err:%ld\n", PTR_ERR(fp));
            }
            bdaddr_valid = 1;
            filp_close(fp, NULL);

        }
    }
    else
    {
        if(skw_file_read(fp, bdaddr_lap, 3) > 0)
        {
            bdaddr_valid = 1;
        }

        filp_close(fp, NULL);
    }
#endif	
#endif
}
EXPORT_SYMBOL_GPL(skw_bd_addr_gen_init);


char skw_get_bd_addr(unsigned char *buffer)
{
    if(bdaddr_valid > 0)
    {
        buffer[0] = bdaddr_lap[0];
        buffer[1] = bdaddr_lap[1];
        buffer[2] = bdaddr_lap[2];
        return 1;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(skw_get_bd_addr);


