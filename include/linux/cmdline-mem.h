#ifndef _LINUX_CMDLINE_MEM_H
#define _LINUX_CMDLINE_MEM_H

#include <linux/device.h>
#include <linux/types.h>
/**
 * @brief 为设备 初始化 cmdline 传入的预留内存
 *
 * 从设备的 "memory-region" 属性 解析 memory-region="cmdline,<name>",
 * 并匹配内核命令行中的 <name>=... 指定的物理内存区域，
 * 将其注册为该设备的 DMA coherent 内存池
 *
 * @param dev:要绑定预留内存的设备
 * @return 返回 0 表示成功，负值表示错误
 */
int of_cmdline_mem_device_init(struct device *dev);

/**
 * 解除设备与 cmdline DMA 内存池的绑定。
 *
 * 共享 DMA 内存池由 cmdline 内存表持有，设备注销时只解除设备绑定，
 * 不释放内存池，以便其他设备继续使用。
 */
void of_cmdline_mem_device_release(struct device *dev);

/**
 * @brief 检索cmdline中是否含有 name 字段
 *
 * @param name:要检索的字段
 * @return 返回 true 表示检索匹配成功，false 表示检索不到
 * */
bool cmdline_mem_has_name(const char *name);

/**
 * 检查 cmdline 内存表中是否存在与指定物理区间重叠的区域。
 *
 * @name: 可选，返回重叠区域的 cmdline 名称
 */
bool cmdline_mem_region_overlaps(phys_addr_t base, phys_addr_t size, const char **name);

#endif /* _LINUX_CMDLINE_MEM_H */
