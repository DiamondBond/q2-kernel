#ifndef _WQ_WIFI_PRIV_H
#define _WQ_WIFI_PRIV_H

#include "rwnx_defs.h"
#include "proc.h"

extern _WQ_PROC_OPS_DEF(wq_wifi_priv_proc);

#define WQ_PRIV_CMD_ARGC_MAX 8
#define WQ_PRIV_CMD_ARGV_LEN 64

typedef enum {
#if 0
    HML_TEST_MSG_RX_MGMT_CB_SET = 0,
    HML_TEST_MSG_TX_MGMT,
    HML_TEST_MSG_EROC_START_REQ,
    HML_TEST_MSG_STA_ADD,
    HML_TEST_MSG_STA_DEL,
    HML_TEST_MSG_GET_MAC_CAP,
    HML_TEST_MSG_SET_BSSID,
    HML_TEST_MSG_CONN_START_REQ,
    HML_TEST_MSG_CONCURRENTCY_INFO,
    HML_TEST_MSG_ASSOC_FRAME_BUILD,
    HML_TEST_MSG_DISASSOC_FRAME_BUILD,
    HML_TEST_MSG_EXTERNAL_AUTH_REQ,
#endif
	HML_TEST_RECOVER_TEST_MODE_SET = 0,
	HML_TEST_PHY_CMD_SET,

	HML_TEST_MSG_MAX
} HML_TEST_MSG_ID_ENUM;

typedef enum {
	HML_TO_VENDOR_MSG_SAMPLE = 0,
	HML_TO_VENDOR_MSG_MODULE_INIT,
	HML_TO_VENDOR_MSG_SET_BATTERY,
	HML_TO_VENDOR_TX_MGMT,
	HML_TO_VENDOR_MSG_CONN_NOTIFY,
	HML_TO_VENDOR_MSG_CONN_REQ,
	HML_TO_VENDOR_MSG_AUTH_RSP,
	HML_TO_VENDOR_MSG_STA_DEL,

	HML_TO_VENDOR_MSG_MAX
} HML_TO_VENDOR_MSG_ID_ENUM;

struct hml_api_test_eroc_req {
	u8 opcode;
	u16 prim20_freq;
	u32 duration_ms;
};

struct hml_api_test_build_assoc_req {
	u8 mac_addr[ETH_ALEN];
	;
	u8 change_ssid_en;
};

struct hml_api_test_build_disassoc_req {
	u8 mac_addr[ETH_ALEN];
	;
	u16 reason_code;
};

struct hml_api_test_external_auth_req {
	u8 mac_addr[ETH_ALEN];
	struct mac_ssid ssid;
};

struct wq_wifi_priv_global_info_tag {
	u8 hml_flag_test;
};

struct wq_wifi_priv_netdev_cmd {
	const char *cmd_name;
	int (*handler)(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
		       int argc,
		       char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN]);
	const char *usage;
};

u8 wq_wifi_priv_hml_flag_test_get(void);

#endif //_WQ_WIFI_PRIV_H
