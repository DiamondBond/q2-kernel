#ifndef _WLAN_IOCTL_H
#define _WLAN_IOCTL_H
#include "rwnx_defs.h"
#include <net/netlink.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
#define wq_access_ok(type, addr, size) access_ok(addr, size)
#else
#define wq_access_ok(type, addr, size) access_ok(type, addr, size)
#endif

#define GOOGLE_OUI 0x001A11
#define BRCM_VENDOR_EVENT_HANGED 33
/* OUI for default BCM vendor hal at rk platform */
#define BRCM_OUI   0x001018
#define WQ_OUI     BRCM_OUI 

/*
 This enum defines ranges for various commands; commands themselves
 can be defined in respective feature headers; i.e. find gscan command
 definitions in gscan.cpp
 */

typedef enum {
	/* don't use 0 as a valid subcommand */
	ANDROID_VENDOR_NL80211_SUBCMD_UNSPECIFIED,

	/* define all vendor startup commands between 0x1 and 0x0FFF */
	ANDROID_NL80211_SUBCMD_WIFI_RANGE_START = 0x0001,
	ANDROID_NL80211_SUBCMD_WIFI_RANGE_END = 0x0FFF,

	/* define all GScan related commands between 0x1000 and 0x10FF */
	ANDROID_NL80211_SUBCMD_GSCAN_RANGE_START = 0x1000,
	ANDROID_NL80211_SUBCMD_GSCAN_RANGE_END = 0x10FF,

	/* define all NearbyDiscovery related commands between 0x1100 and 0x11FF */
	ANDROID_NL80211_SUBCMD_NBD_RANGE_START = 0x1100,
	ANDROID_NL80211_SUBCMD_NBD_RANGE_END = 0x11FF,

	/* define all RTT related commands between 0x1100 and 0x11FF */
	ANDROID_NL80211_SUBCMD_RTT_RANGE_START = 0x1100,
	ANDROID_NL80211_SUBCMD_RTT_RANGE_END = 0x11FF,

	ANDROID_NL80211_SUBCMD_LSTATS_RANGE_START = 0x1200,
	ANDROID_NL80211_SUBCMD_LSTATS_RANGE_END = 0x12FF,

	/* define all Logger related commands between 0x1400 and 0x14FF */
	ANDROID_NL80211_SUBCMD_DEBUG_RANGE_START = 0x1400,
	ANDROID_NL80211_SUBCMD_DEBUG_RANGE_END = 0x14FF,

	/* define all wifi offload related commands between 0x1600 and 0x16FF */
	ANDROID_NL80211_SUBCMD_WIFI_OFFLOAD_RANGE_START = 0x1600,
	ANDROID_NL80211_SUBCMD_WIFI_OFFLOAD_RANGE_END = 0x16FF,

	/* define all NAN related commands between 0x1700 and 0x17FF */
	ANDROID_NL80211_SUBCMD_NAN_RANGE_START = 0x1700,
	ANDROID_NL80211_SUBCMD_NAN_RANGE_END = 0x17FF,

	/* define all Android Packet Filter related commands between 0x1800 and 0x18FF */
	ANDROID_NL80211_SUBCMD_PKT_FILTER_RANGE_START = 0x1800,
	ANDROID_NL80211_SUBCMD_PKT_FILTER_RANGE_END = 0x18FF,

	/* This is reserved for future usage */

} ANDROID_VENDOR_SUB_COMMAND;

typedef enum {
	GSCAN_SUBCMD_GET_CAPABILITIES =
		ANDROID_NL80211_SUBCMD_GSCAN_RANGE_START,

	GSCAN_SUBCMD_SET_CONFIG, /* 0x1001 */

	GSCAN_SUBCMD_SET_SCAN_CONFIG, /* 0x1002 */
	GSCAN_SUBCMD_ENABLE_GSCAN, /* 0x1003 */
	GSCAN_SUBCMD_GET_SCAN_RESULTS, /* 0x1004 */
	GSCAN_SUBCMD_SCAN_RESULTS, /* 0x1005 */

	GSCAN_SUBCMD_SET_HOTLIST, /* 0x1006 */

	GSCAN_SUBCMD_SET_SIGNIFICANT_CHANGE_CONFIG, /* 0x1007 */
	GSCAN_SUBCMD_ENABLE_FULL_SCAN_RESULTS, /* 0x1008 */
	GSCAN_SUBCMD_GET_CHANNEL_LIST, /* 0x1009 */

	WIFI_SUBCMD_GET_FEATURE_SET, /* 0x100A */
	WIFI_SUBCMD_GET_FEATURE_SET_MATRIX, /* 0x100B */
	WIFI_SUBCMD_SET_PNO_RANDOM_MAC_OUI, /* 0x100C */
	WIFI_SUBCMD_NODFS_SET, /* 0x100D */
	WIFI_SUBCMD_SET_COUNTRY_CODE, /* 0x100E */
	/* Add more sub commands here */
	GSCAN_SUBCMD_SET_EPNO_SSID, /* 0x100F */

	WIFI_SUBCMD_SET_SSID_WHITE_LIST, /* 0x1010 */
	WIFI_SUBCMD_SET_ROAM_PARAMS, /* 0x1011 */
	WIFI_SUBCMD_ENABLE_LAZY_ROAM, /* 0x1012 */
	WIFI_SUBCMD_SET_BSSID_PREF, /* 0x1013 */
	WIFI_SUBCMD_SET_BSSID_BLACKLIST, /* 0x1014 */

	GSCAN_SUBCMD_ANQPO_CONFIG, /* 0x1015 */
	WIFI_SUBCMD_SET_RSSI_MONITOR, /* 0x1016 */
	WIFI_SUBCMD_CONFIG_ND_OFFLOAD, /* 0x1017 */
	/* Add more sub commands here */

	GSCAN_SUBCMD_MAX,

	APF_SUBCMD_GET_CAPABILITIES =
		ANDROID_NL80211_SUBCMD_PKT_FILTER_RANGE_START,
	APF_SUBCMD_SET_FILTER,
} WIFI_SUB_COMMAND;

typedef enum {
	WQ_RESERVED1,
	WQ_RESERVED2,
	GSCAN_EVENT_SIGNIFICANT_CHANGE_RESULTS,
	GSCAN_EVENT_HOTLIST_RESULTS_FOUND,
	GSCAN_EVENT_SCAN_RESULTS_AVAILABLE,
	GSCAN_EVENT_FULL_SCAN_RESULTS,
	RTT_EVENT_COMPLETE,
	GSCAN_EVENT_COMPLETE_SCAN,
	GSCAN_EVENT_HOTLIST_RESULTS_LOST,
	GSCAN_EVENT_EPNO_EVENT,
	GOOGLE_DEBUG_RING_EVENT,
	GOOGLE_DEBUG_MEM_DUMP_EVENT,
	GSCAN_EVENT_ANQPO_HOTSPOT_MATCH,
	GOOGLE_RSSI_MONITOR_EVENT,
	WQ_FW_RESTART_EVENT = BRCM_VENDOR_EVENT_HANGED,
} WIFI_VENDOR_EVENT;

typedef enum wifi_attr {
	ANDR_WIFI_ATTRIBUTE_NUM_FEATURE_SET,
	ANDR_WIFI_ATTRIBUTE_FEATURE_SET,
	ANDR_WIFI_ATTRIBUTE_PNO_RANDOM_MAC_OUI,
	ANDR_WIFI_ATTRIBUTE_NODFS_SET,
	ANDR_WIFI_ATTRIBUTE_COUNTRY,
	ANDR_WIFI_ATTRIBUTE_ND_OFFLOAD_VALUE
	// Add more attribute here
} wifi_attr_t;

/*
 * miracast parameters
 * 0-Disabled
 * 1-Source
 * 2-Sink
 */
enum miracast_mode {
	WQ_MIRACAST_DISABLED = 0,
	WQ_MIRACAST_SOURCE,
	WQ_MIRACAST_SINK,
};

struct android_wifi_priv_cmd {
	char *buf;
	int used_len;
	int total_len;
};

typedef int (*wq_priv_cmd_handler)(struct net_device *netdev, uint8_t *cmd,
				   int total_len);
struct wq_drv_cmd {
	const char *cmdstr;
	wq_priv_cmd_handler handler;
	bool enable;
};

void wq_wiphy_vendor_init(struct wiphy *wiphy);

int wq_android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd);
int wq_wext_support_ioctl(struct net_device *net, struct ifreq *ifr, int cmd);
int wq_priv_support_ioctl(struct net_device *net, struct ifreq *ifr, int cmd);
void wq_subsystem_restart_evt(struct wiphy *wiphy, struct wireless_dev *wdev, char *dump_info, u32 info_len);
void rwnx_store_chan_pwr_tab(struct rwnx_vif *vif, u8 band, u32 freq, u8 *pwr_tab);
int rwnx_send_chan_pwr_info_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                                u8 *pwr, u8 band, u32 freq);
#endif
