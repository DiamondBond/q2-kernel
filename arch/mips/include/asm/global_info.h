/* Global infomation structure. */

#ifndef __GLOBAL_INFO_H__
#define __GLOBAL_INFO_H__

#define CONFIG_GINFO_ADDR 0x14

struct jz_global_info {
	uint32_t version;
	uint32_t extal;
	uint32_t cpufreq;
	uint32_t ddrfreq;
	uint32_t uart_idx;
	uint32_t baud_rate;
	uint32_t sfc_params_addr;
};
#endif /* __GLOBAL_INFO_H__ */
