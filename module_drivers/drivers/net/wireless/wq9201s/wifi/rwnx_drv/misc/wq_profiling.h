#ifndef _WQ_PROFILING_H_
#define _WQ_PROFILING_H_

#include <linux/gpio.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
#include <linux/gpio/consumer.h>
#endif

#define CONFIG_PROF_NONE
// #define CONFIG_PROF_RK3588
// #define CONFIG_PROF_GOKE
// #define CONFIG_PROF_RK3568

enum wq_prof_id {
	SW_PROF_IEEE80211RX = 0,
	SW_PROF_CHAN_PRE_SWITCH,
	SW_PROF_CHAN_SWITCH_IND,
	SW_PROF_RX_DATA_IND,
	SW_PROF_IPC_RX_PKT,
	SW_PROF_TX_CFM_HDL,
	SW_PROF_TX_PROCESS,
	SW_PROF_FLOW_CTRL,
	SW_PROF_PCIE_CE_TX_DONE,
	SW_PROF_START_XMIT,
	SW_PROF_IPC_TX_PKT,
	SW_PROF_SDIO_MAIN_PROCESS,
	SW_PROF_SDIO_MAIN_TX,
	SW_PROF_SDIO_MAIN_RX,
	SW_PROF_SDIO_MAIN_RX_INFO,
	SW_PROF_SDIO_INTERRUPT,
	SW_PROF_SDIO_TX_AGGR,
	SW_PROF_SDIO_XMIT_AGGR,
	SW_PROF_NAPI_RX_SOFTIRQ,
	SW_PROF_MAX
};

struct wq_prof_config {
	int gpio;
	bool used;
};

extern struct wq_prof_config wq_prof_configs[SW_PROF_MAX];

void wq_profiling_set_value(int prof_id, int value);

#ifndef CONFIG_PROF_NONE
#define PROFILING_SET(prof_id) wq_profiling_set_value(prof_id, 1)
#define PROFILING_CLR(prof_id) wq_profiling_set_value(prof_id, 0)
#else
#define PROFILING_SET(prof_id)                                                 \
	do {                                                                   \
	} while (0)
#define PROFILING_CLR(prof_id)                                                 \
	do {                                                                   \
	} while (0)
#endif

#ifdef CONFIG_PROF_RK3588

#define GPIO_MODULE_NAME "rk3588xx_gpio"

#elif defined(CONFIG_PROF_GOKE) /* goke platform */

#define GPIO_MODULE_NAME "goke_gpio"

#elif defined(CONFIG_PROF_RK3568) /* RK3568 platform */

#define GPIO_MODULE_NAME "rk3568xx_gpio"

#else

#define GPIO_MODULE_NAME "none_gpio"

#endif

void wq_profiling_init(void);

#endif /* _WQ_PROFILING_H_ */
