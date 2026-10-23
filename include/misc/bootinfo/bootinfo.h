#ifndef __LINUX_BOOTINFO_H
#define __LINUX_BOOTINFO_H

#include <linux/types.h>

#define MAX_SN_LEN 512
static char bootcmd_sn_str[MAX_SN_LEN];
/**
 * get_early_sn - 获取启动时传入的 SN 值
 * @buf: 用户提供的缓冲区
 * @size: 缓冲区大小
 *
 * 返回值：
 *   成功返回实际拷贝的字节数（不包括 '\0'）
 *   失败（如 NULL 指针）返回负数
 */
ssize_t get_early_sn(char *buf, size_t size);

#endif //__LINUX_BOOTINFO_H
