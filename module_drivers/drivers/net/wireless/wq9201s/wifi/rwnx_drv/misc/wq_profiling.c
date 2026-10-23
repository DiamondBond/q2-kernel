#include "wq_profiling.h"
#include "wq_log.h"

struct wq_prof_config wq_prof_configs[SW_PROF_MAX];

void wq_prof_configs_init(void)
{
	int i;

	for (i = 0; i < SW_PROF_MAX; i++) {
		wq_prof_configs[i].gpio = -1;
		wq_prof_configs[i].used = false;
	}

#ifdef CONFIG_PROF_RK3588
	wq_prof_configs[SW_PROF_RX_DATA_IND].gpio = 47;
	wq_prof_configs[SW_PROF_RX_DATA_IND].used = true;
	wq_prof_configs[SW_PROF_IPC_RX_PKT].gpio = 46;
	wq_prof_configs[SW_PROF_IPC_RX_PKT].used = true;
	wq_prof_configs[SW_PROF_TX_CFM_HDL].gpio = 57;
	wq_prof_configs[SW_PROF_TX_CFM_HDL].used = true;
	wq_prof_configs[SW_PROF_IEEE80211RX].gpio = 56;
	wq_prof_configs[SW_PROF_IEEE80211RX].used = true;
	wq_prof_configs[SW_PROF_IPC_TX_PKT].gpio = 59;
	wq_prof_configs[SW_PROF_IPC_TX_PKT].used = true;
	wq_prof_configs[SW_PROF_TX_PROCESS].gpio = 58;
	wq_prof_configs[SW_PROF_TX_PROCESS].used = true;
	wq_prof_configs[SW_PROF_FLOW_CTRL].gpio = 132;
	wq_prof_configs[SW_PROF_FLOW_CTRL].used = true;
	wq_prof_configs[SW_PROF_PCIE_CE_TX_DONE].gpio = 131;
	wq_prof_configs[SW_PROF_PCIE_CE_TX_DONE].used = true;
#elif defined(CONFIG_PROF_GOKE)
	wq_prof_configs[SW_PROF_SDIO_MAIN_PROCESS].gpio = 44;
	wq_prof_configs[SW_PROF_SDIO_MAIN_PROCESS].used = false;
	wq_prof_configs[SW_PROF_SDIO_MAIN_RX].gpio = 34;
	wq_prof_configs[SW_PROF_SDIO_MAIN_RX].used = true;
	wq_prof_configs[SW_PROF_SDIO_MAIN_RX_INFO].gpio = 44;
	wq_prof_configs[SW_PROF_SDIO_MAIN_RX_INFO].used = true;
	wq_prof_configs[SW_PROF_SDIO_MAIN_TX].gpio = 35;
	wq_prof_configs[SW_PROF_SDIO_MAIN_TX].used = true;
	wq_prof_configs[SW_PROF_SDIO_INTERRUPT].gpio = 44;
	wq_prof_configs[SW_PROF_SDIO_INTERRUPT].used = false;
	wq_prof_configs[SW_PROF_NAPI_RX_SOFTIRQ].gpio = 44;
	wq_prof_configs[SW_PROF_NAPI_RX_SOFTIRQ].used = false;
	wq_prof_configs[SW_PROF_START_XMIT].gpio = 44;
	wq_prof_configs[SW_PROF_START_XMIT].used = false;
	wq_prof_configs[SW_PROF_IPC_RX_PKT].gpio = 44;
	wq_prof_configs[SW_PROF_IPC_RX_PKT].used = false;
	wq_prof_configs[SW_PROF_SDIO_TX_AGGR].gpio = 35;
	wq_prof_configs[SW_PROF_SDIO_TX_AGGR].used = false;
	wq_prof_configs[SW_PROF_SDIO_XMIT_AGGR].gpio = 35;
	wq_prof_configs[SW_PROF_SDIO_XMIT_AGGR].used = false;
#elif defined(CONFIG_PROF_RK3568)
	wq_prof_configs[SW_PROF_SDIO_MAIN_PROCESS].gpio = 40;
	wq_prof_configs[SW_PROF_SDIO_MAIN_PROCESS].used = false;
	wq_prof_configs[SW_PROF_SDIO_MAIN_RX].gpio = 41;
	wq_prof_configs[SW_PROF_SDIO_MAIN_RX].used = true;
	wq_prof_configs[SW_PROF_SDIO_MAIN_RX_INFO].gpio = 40;
	wq_prof_configs[SW_PROF_SDIO_MAIN_RX_INFO].used = true;
	wq_prof_configs[SW_PROF_SDIO_MAIN_TX].gpio = 42;
	wq_prof_configs[SW_PROF_SDIO_MAIN_TX].used = true;
	wq_prof_configs[SW_PROF_SDIO_INTERRUPT].gpio = 36;
	wq_prof_configs[SW_PROF_SDIO_INTERRUPT].used = false;
	wq_prof_configs[SW_PROF_NAPI_RX_SOFTIRQ].gpio = 40;
	wq_prof_configs[SW_PROF_NAPI_RX_SOFTIRQ].used = false;
	wq_prof_configs[SW_PROF_START_XMIT].gpio = 41;
	wq_prof_configs[SW_PROF_START_XMIT].used = false;
	wq_prof_configs[SW_PROF_IPC_RX_PKT].gpio = 42;
	wq_prof_configs[SW_PROF_IPC_RX_PKT].used = false;
	wq_prof_configs[SW_PROF_SDIO_TX_AGGR].gpio = 40;
	wq_prof_configs[SW_PROF_SDIO_TX_AGGR].used = false;
	wq_prof_configs[SW_PROF_SDIO_XMIT_AGGR].gpio = 41;
	wq_prof_configs[SW_PROF_SDIO_XMIT_AGGR].used = false;
#endif
}

void wq_profiling_init(void)
{
	int i;

	wq_prof_configs_init();

	for (i = 0; i < SW_PROF_MAX; i++) {
		int gpio = wq_prof_configs[i].gpio;
		bool used = wq_prof_configs[i].used;
		int err;

		if (gpio < 0)
			continue;

		if (!used)
			continue;

		err = gpio_request(gpio, GPIO_MODULE_NAME);

		WQ_DBG(DM_IEEE80211, DL_ERR, "%s: gpio(%d) request ret:%d\n",
		       __func__, gpio, err);
		if (err < 0) {
			WQ_DBG(DM_IEEE80211, DL_ERR,
			       "%s: gpio(%d) request fail\n", __func__, gpio);
		} else {
			gpio_direction_output(gpio, 0);
		}
	}
}

void wq_profiling_set_value(int prof_id, int value)
{
	int gpio = wq_prof_configs[prof_id].gpio;
	bool used = wq_prof_configs[prof_id].used;

	if (used && gpio >= 0) {
		gpio_set_value(gpio, !!value);
	}
}