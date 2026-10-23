#ifndef _WQ_VIRTUALWIFI_H
#define _WQ_VIRTUALWIFI_H

#include <linux/kernel.h>
#include <linux/if_ether.h> /* for ETH_ALEN */
#include "config.h"

enum wq_debug_module {
	DM_NONE = 0,

	DM_IPC = BIT(0),
	DM_TRBUS = BIT(1),
	DM_RX = BIT(2),
	DM_TX = BIT(3),
	DM_IEEE80211 = BIT(4),
	DM_GENERIC = BIT(5),
	DM_CRDT = BIT(6),
	DM_PKTDUMP = BIT(7),

	DM_ALL = (DM_IPC | DM_TRBUS | DM_RX | DM_TX | DM_IEEE80211 |
		  DM_GENERIC | DM_CRDT | DM_PKTDUMP),
	DM_ANY = 0xff,
};

enum wq_debug_level {
	DL_NONE = 0,
	DL_OOPS,
	DL_ERR,
	DL_WRN,
	DL_INF,
	DL_VRB,
	DL_MAX,
};

enum wq_misc_ctrl {
	SCAN_DISABLE = BIT(0),
	PS_DISABLE = BIT(1),
	AUTOPM_DISABLE = BIT(2),
};

enum wq_wifi_ctrl {
	HE_DISABLE = BIT(0),
	BFMEE_DISABLE = BIT(1),
	MURX_DISABLE = BIT(2),
	HE_MCS_MAP_RX_DISABLE = BIT(3),
	HE_MCS_MAP_TX_DISABLE = BIT(4),
};

enum wq_rx_ctrl {
	TCP_DROP_DISABLE = BIT(0),
	TIMEOUT_DISABLE = BIT(1),
	REORDER_DISABLE = BIT(2),
	RX_AMPDU_DISABLE = BIT(3),
	RX_RATE_LOG_DISABLE = BIT(4),
	RX_AMSDU_DISABLE = BIT(5),
};

#ifndef WQ_LOG_DM
#define WQ_LOG_DM DM_GENERIC
#endif

#ifndef WQ_LOG_LEVEL
#define WQ_LOG_LEVEL DL_VRB
#endif

#define ENTER() WQ_DBG(WQ_LOG_DM, WQ_LOG_LEVEL, WQ_FN_ENTRY_STR)
#define LEAVE() WQ_DBG(WQ_LOG_DM, WQ_LOG_LEVEL, WQ_FN_LEAVE_STR)

#define WQ_FN_ENTRY_STR "--> %s()\n", __func__
#define WQ_FN_LEAVE_STR "<-- %s()\n", __func__

#define WQ_LOG_PREFIX "[WQ_WLAN]"

/*
 * set KERN_ERR here. because console_loglevel of most android is 4
 *      (please refer to `cat /proc/sys/kernel/printk`),
 * that's to say, console never show logs lower than KERN_WARNING.
 */
#if 1
#define WQ_DBG(m, lv, fmt, ...)                                                \
	do {                                                                   \
		if ((wq_conf.wq_dbg_mod & (m)) &&                              \
		    ((wq_conf.wq_dbg_lv >= (lv)))) {                           \
			printk(KERN_ERR WQ_LOG_PREFIX fmt, ##__VA_ARGS__);     \
		}                                                              \
		if ((lv) == DL_OOPS)                                           \
			BUG();                                                 \
	} while (0)
#define WQ_DUMP_DBG(m, lv, fmt, ...)                                           \
	do {                                                                   \
		if ((wq_conf.wq_dbg_mod & (m)) &&                              \
		    ((wq_conf.wq_dbg_lv >= (lv)))) {                           \
			printk(KERN_CONT WQ_LOG_PREFIX fmt, ##__VA_ARGS__);    \
		}                                                              \
		if ((lv) == DL_OOPS)                                           \
			BUG();                                                 \
	} while (0)
#else
#define WQ_DBG(m, lv, fmt, ...)                                                \
	do {                                                                   \
		if ((wq_conf.wq_dbg_mod & (m)) &&                              \
		    ((wq_conf.wq_dbg_lv >= (lv)))) {                           \
			pr_cont("[%d][%s] ", current->pid, current->comm);     \
			pr_cont(fmt, ##__VA_ARGS__);                           \
		}                                                              \
		if ((lv) == DL_OOPS)                                           \
			BUG();                                                 \
	} while (0)
#endif

#define WQ_DBG_MAC(m, lv, _sta, _fmt, ...)                                     \
	do {                                                                   \
		if ((wq_conf.wq_dbg_mod & (m)) &&                              \
		    ((wq_conf.wq_dbg_lv >= (lv))))                             \
			printk(KERN_ERR "[%pM] " _fmt, (_sta)->mac_addr,       \
			       ##__VA_ARGS__);                                 \
	} while (0)

#define WQ_ASSERT(a, _fmt, ...)                                                \
	do {                                                                   \
		if (!(a)) {                                                    \
			printk(KERN_EMERG _fmt, ##__VA_ARGS__);                \
			BUG_ON(!(a));                                          \
		}                                                              \
	} while (0)

#define HEX_DUMP(lvl, prefix, buf, len, ascii)                                 \
	do {                                                                   \
		if (wq_conf.wq_dbg_lv >= (lvl)) {                              \
			print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_NONE, 16, \
				       1, buf, len, ascii);                    \
		}                                                              \
	} while (0)

static inline void dump_bytes(uint8_t lvl, char *note, uint8_t *data, int len)
{
	WQ_DBG(DM_GENERIC, lvl, "%s : dump addr=%p len=%d\n", note, data, len);
	HEX_DUMP(lvl, "", data, len, true);
}

#endif /* _WQ_VIRTUALWIFI_H */
