#ifndef _WQ_WLAN_CORE_H
#define _WQ_WLAN_CORE_H

#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/skbuff.h>
#ifdef CONFIG_RK3588_ENABLE_WAKEUP_OOB
//#include <linux/wakelock.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
#include <linux/wakelock.h>
#else
#include <linux/pm_wakeup.h>
#endif

#endif

#include "wq_compat.h"
#include "htc.h"
#include "wq_ipc.h"

#define WQ_CHIP_NAME "wq9201"

/* define the max num of tx bundle on pcie 2.5g speed */
#define NX_LL_LOW_SPEED_TX_BUNDLE_MAX 3

#ifdef WQ_WLAN_ALL_IN_ONE
#define WQ_CORE_API(x)
#define WQ_BMI_API(x)
#define WQ_TX_CREDIT_API(x)
#define WQ_PCIE_API(x)
#define WQ_PLAT_RK3588_API(x)
#define WQ_PLAT_SPACEMIT_K1_API(x)
#else
#define WQ_CORE_API(x) EXPORT_SYMBOL(x)
#define WQ_BMI_API(x) EXPORT_SYMBOL(x)
#define WQ_TX_CREDIT_API(x) EXPORT_SYMBOL(x)
#define WQ_PCIE_API(x) EXPORT_SYMBOL(x)
#define WQ_PLAT_RK3588_API(x) EXPORT_SYMBOL(x)
#define WQ_PLAT_SPACEMIT_K1_API(x) EXPORT_SYMBOL(x)
#endif

enum wq_hif_type {
	WQ_HIF_NONE = 0,
	WQ_HIF_USB,
	WQ_HIF_SDIO,
	WQ_HIF_PCIE,

	WQ_HIF_MAX,
};

enum wq_wphy_profile {
	WQ_WPHY_PF_DEFAULT = 0,
	WQ_WPHY_PF_QFN_USB,
	WQ_WPHY_PF_QFN_SDIO,
	WQ_WPHY_PF_QFN_PCIE,
	WQ_WPHY_PF_BGA_PCIE, /* smaller area and more pins */

	WQ_WPHY_PF_MAX,
};

enum wq_core_state {
	WQ_CORE_STATE_DETACH =
		0, /* detached or unplugged, or its driver is unloaded */
	WQ_CORE_STATE_FW_CRASHED, /* FW crash is detected */
	WQ_CORE_STATE_HIF_NREADY, /* IPC: disabled */
	WQ_CORE_STATE_HIF_READY, /* IPC: RX-only */
	WQ_CORE_STATE_FW_DL, /* if fail to download f/w, should return to HIF_READY */
	WQ_CORE_STATE_FW_DL_DONE,
	WQ_CORE_STATE_WLAN_FW_READY, /* IPC: bi-direction */
	WQ_CORE_STATE_WIF_NREADY, /* WLAN interface is not registered, but WLAN is created */
	WQ_CORE_STATE_WIF_READY, /* WLAN interface is registered */

	WQ_CORE_STATE_MAX,
};

struct driver_common {
	wait_queue_head_t main_waitQ;
	struct sk_buff_head main_rx_Q;

	void *p_handle;
	void *p_proc_data;
};

struct wq_hif_ops;

struct wq_core {
	const char *hif_name;
	int dev_mod_id;
	const struct wq_hif_ops *hif_ops;
	enum wq_wphy_profile wphy_profile;

	struct device *dev;

	struct htc htc;
	struct wq_ipc ipc;

	enum wq_core_state state;
	struct {
		uint32_t suspend : 1;
		uint32_t mp_test_proc_init : 1;
	} flags;

	volatile uint32_t
		in_deep_suspendding; /* Deep suspendding: System PM, not runtime. */

	struct {
		bool up_to_mcs7;
		bool dma_map; /* PCIe transfer with DMA engine */
		bool rx_ll;
		bool remap_to_be;
		bool force_rx_use_tasklet;
		bool napi_enable;
		bool ipc_rx_pkt_use_wq;
		uint8_t tx_bundle_max;
		u32 tx_bundle_expire_ns;
		u32 tx_tcpack_expire_ns;
	} config;
	struct completion fw_ready;

	struct rwnx_hw *hw;

	struct fw_dnld *wq_dnld;

	struct driver_common driver;

	void *fw_log;
	/* dtop fw version */
	u32 dtop_fwver;
	/* wifi fw version */
	u32 wifi_fwver;
#ifdef CONFIG_RK3588_ENABLE_WAKEUP_OOB
	/* wlan wakeup and reg on oob */
	u32 oob_irq_num;
	u32 oob_irq_flags;
	int gpio_wl_reg_on;
	int gpio_wl_host_wake;
	int oob_irq_wake_enabled;
	int oob_irq_registered;
	u64 last_oob_irq_isr_time;
	//struct wake_lock oob_wakelock;
//	struct wakeup_source *oob_wakelock;
#endif
	struct work_struct wq_bus_recovery_work;
};

const char *wq_wphy_profile_name(enum wq_wphy_profile profile);

const char *wq_core_state2name(enum wq_core_state state);
void wq_core_state_set(struct wq_core *core, enum wq_core_state state);

static inline enum wq_core_state wq_core_state_get(struct wq_core *core)
{
	return core->state;
}

static inline int wq_core_is_detached(struct wq_core *core)
{
	return core->state <= WQ_CORE_STATE_FW_CRASHED;
}

static inline int wq_core_is_hif_ready(struct wq_core *core)
{
	return core->state >= WQ_CORE_STATE_HIF_READY;
}

static inline const char *wq_core_state_name(struct wq_core *core)
{
	return wq_core_state2name(wq_core_state_get(core));
}

struct wq_core *wq_core_create(struct wq_hif_ops *hif_ops, struct device *dev,
			       enum wq_wphy_profile profile, unsigned size);
void wq_core_destroy(struct wq_core *core);
int wq_core_suspend_set(struct wq_core *core, int enable);
int wq_core_deep_suspend_set(struct wq_core *core, int enable);
int wq_core_is_in_deep_suspend(struct wq_core *core);
int wq_core_wait_data_path_idle(struct wq_core *core);

int wq_wlan_fw_ready(struct wq_core *core, unsigned long msecs);

int wq_wlan_create(struct wq_core *core, int tx_credit_enable);
void wq_wlan_unregister(struct wq_core *core);
void wq_wlan_destroy(struct wq_core *core);
void wq_wlan_unload_wifi(struct wq_core *core);
void wq_wlan_cmd_mgr_drain(struct wq_core *core);
void wq_dev_restart(struct wq_core *core);
int wq_core_get_sys_state(struct wq_core *core, u32 *run_state);
int wq_core_sys_suspend(struct wq_core *core);
int wq_core_sys_resume(struct wq_core *core);

int wq_wlan_suspend(struct wq_core *core);
int wq_wlan_resume(struct wq_core *core);
int wq_wlan_shutdown(struct wq_core *core);

int wq_module_init(void);
void wq_module_exit(void);

enum wq_pkt_cls {
	WQ_PKT_CLS_EAPOL = 0, /* WQ_IPC_FLAGS_TX_HIGH_QUEUE */
	WQ_PKT_CLS_EAPOL_M4, /* the last (4th) message of an EAPOL session */
	WQ_PKT_CLS_ARP,
	WQ_PKT_CLS_ARP_REQ,
	WQ_PKT_CLS_ARP_REPLY,
	WQ_PKT_CLS_IP,
	WQ_PKT_CLS_ICMP,
	WQ_PKT_CLS_TCP,
	WQ_PKT_CLS_UDP,
	WQ_PKT_CLS_DHCP,
	WQ_PKT_CLS_TCP_ACK,
	WQ_PKT_CLS_IPERF_UDP_SETUP,

	WQ_PKT_CLS_80211, /* 802.11 (management) frame */
};

struct wq_skb_txcb {
	unsigned long jiffies; /* when the skb enter rwnx_start_xmit() */
	uint16_t pkt_cls; /* BIT(WQ_PKT_CLS_XXX) */
	uint16_t txq_idx; /* refer to rwnx_txq_sta_idx() */
	uint8_t qid : 3, /* enum wq_hif_qid */
		msdu_in_host : 1, /* MSDU in host memory */
		more_msdu : 1, /* has next MSDU */
		has_hif_htc : 1; /* first msdu or single msdu/message */
	dma_addr_t phyaddr; /* for PCIe only */
	uint16_t total_dma_len;
	uint8_t usb_out_bundle_num; /* usb out bundle num for a urb/transfer, usb only */
	uint8_t is_small_pkt : 1, /* for skb->len < MSDU_IN_HOST_THRESHOLD */
		resv0 : 7;
	uint32_t pkt_len; /* pkt_len for skb_bundle, usb only*/
	uint32_t tx_ms; /* record tx to hif, unit ms */
	//lg add
	u8 bundle_num;
	u16 bundle_len;
};

static inline struct wq_skb_txcb *WQ_SKB_TXCB(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct wq_skb_txcb) > sizeof(skb->cb));
	return (struct wq_skb_txcb *)skb->cb;
}

struct wq_core *wq_core_get(void);
void wq_wlan_handle_bus_recovery(struct wq_core *core);

#endif
