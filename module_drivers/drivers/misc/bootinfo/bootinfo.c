/*
 * bootinfo.c
 *
 * @brief
 * @author MPU系统软件部团队 Yolanda
 *
 * @copyright 版权所有 (北京君正集成电路股份有限公司) {2025}
 * @copyright Copyright© 2025 Ingenic Semiconductor Co.,Ltd
 *
 *
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <misc/bootinfo/bootinfo.h>

#ifdef CONFIG_BOOTINFO_EARLY_SN
static int __init sn_param(char *arg)
{
	memset(bootcmd_sn_str, 0, MAX_SN_LEN);
    // 从屏，传递过来的是地址
    if (arg[0] == '0' && (arg[1] == 'X' || arg[1] == 'x')) {
        unsigned long sn_phys_addr;
        void __iomem *sn_virt_addr;
        int i;
        sn_phys_addr = simple_strtol(arg, NULL, 16);
        pr_info("SN address: 0x%08lx\n", sn_phys_addr);
        sn_virt_addr = ioremap(sn_phys_addr, MAX_SN_LEN);
        if (!sn_virt_addr) {
            pr_err("Failed to ioremap SN address\n");
            return -ENOMEM;
        }
        for (i = 0; i < MAX_SN_LEN - 1; i++) {
            bootcmd_sn_str[i] = ioread8(sn_virt_addr + i);
		       if (bootcmd_sn_str[i] == 0x0a) break; //'\0'
        }
        bootcmd_sn_str[i] = '\0';  // 确保字符串结束
		pr_info("Read SN len = %d ", i);
        iounmap(sn_virt_addr);
    } else {
        // 传递的是直接字符串数据
        size_t len = strlen(arg);
        if (len >= MAX_SN_LEN) {
            pr_warn("SN string too long, will be truncated\n");
            len = MAX_SN_LEN - 1;
        }

        memcpy(bootcmd_sn_str, arg, len);
        bootcmd_sn_str[len] = '\0';  // 添加字符串结尾
		pr_info("Read SN len = %d ", len);
    }
    pr_info("Read SN string: %s\n", bootcmd_sn_str);
    return 0;
}
early_param("sn", sn_param);

ssize_t get_early_sn(char *buf, size_t size)
{
    ssize_t len;

    if (!buf || size <= 0)
        return -EINVAL;

    len = strlen(bootcmd_sn_str);
    if (len >= size) {
        memcpy(buf, bootcmd_sn_str, size - 1);
        buf[size - 1] = '\0';
        return size - 1;
    }

    memcpy(buf, bootcmd_sn_str, len + 1); // 包含 '\0'
    return len;
}
EXPORT_SYMBOL_GPL(get_early_sn);
#endif //CONFIG_BOOTINFO_EARLY_SN
