/****************************
 * Include
 ****************************/
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include "wq_wifi_priv.h"
#include "wq_log.h"
#include "rwnx_msg_tx.h"

extern struct net init_net;
struct wq_wifi_priv_global_info_tag wq_wifi_priv_global_info;

/*
test: HID2D_CONN_START_REQ
cmd:wq_wifi_priv hid2d_net_dev_name hid2d_conn_start
*/
int wq_wifi_priv_netdev_hml_conn_start(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_conn_start::argc=%d, CONN_START_REQ=%d, agrv[1]=[%s]\n",
	       argc, HML_TEST_MSG_CONN_START_REQ, argv[1]);

	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
				       HML_TEST_MSG_CONN_START_REQ, NULL, 0);
	return 0;
}

/*
test: hml_concurrentcy_info
cmd:wq_wifi_priv hid2d_net_dev_name hid2d_conn_start
*/
int wq_wifi_priv_netdev_hml_concurrentcy_info(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_concurrentcy_info::argc=%d, HML_TEST_MSG_CONCURRENTCY_INFO=%d, agrv[1]=[%s]\n",
	       argc, HML_TEST_MSG_CONCURRENTCY_INFO, argv[1]);

	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
				       HML_TEST_MSG_CONCURRENTCY_INFO, NULL, 0);
	return 0;
}

/*
test: assoc_frame_build
cmd:hml_build_assoc_req xx:xx:xx:xx:xx:xx change_ssid_en
*/
int wq_wifi_priv_netdev_hml_build_assoc_req(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	u8 mac_addr[ETH_ALEN];
	unsigned int value_num;
	struct hml_api_test_build_assoc_req req;

	if (argc < 4) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_build_assoc_req:: argc=%d, agrv[1]=[%s] param is invalid\n",
		       argc, argv[1]);
		return 0;
	}

	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_build_assoc_req::Begin, argc=%d, agrv[1]=[%s], argv[2]=[%s], argv[3]=%s\n",
	       argc, argv[1], argv[2], argv[3]);

	//mac
	if (sscanf(argv[2], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", req.mac_addr + 0,
		   req.mac_addr + 1, req.mac_addr + 2, req.mac_addr + 3,
		   req.mac_addr + 4, req.mac_addr + 5) != ETH_ALEN) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_build_assoc_req::parase mac error\n");
		return 0;
	}
	//change ssid en
	req.change_ssid_en = 0;
	if (0 == kstrtouint(argv[3], 10, &value_num)) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_build_assoc_req::need_cb=%d\n",
		       value_num);
		req.change_ssid_en = (u8)value_num;
	}

	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
				       HML_TEST_MSG_ASSOC_FRAME_BUILD,
				       (char *)&req, sizeof(req));

	return 0;
}

/*
test: disassoc_frame_build
cmd:hml_build_assoc_req xx:xx:xx:xx:xx:xx change_ssid_en
*/
int wq_wifi_priv_netdev_hml_build_disassoc_req(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	u8 mac_addr[ETH_ALEN];
	unsigned int value_num;
	struct hml_api_test_build_disassoc_req req;

	if (argc < 4) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_build_disassoc_req:: argc=%d, agrv[1]=[%s] param is invalid\n",
		       argc, argv[1]);
		return 0;
	}

	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_build_disassoc_req::Begin, argc=%d, agrv[1]=[%s], argv[2]=[%s], argv[3]=%s\n",
	       argc, argv[1], argv[2], argv[3]);

	//mac
	if (sscanf(argv[2], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", req.mac_addr + 0,
		   req.mac_addr + 1, req.mac_addr + 2, req.mac_addr + 3,
		   req.mac_addr + 4, req.mac_addr + 5) != ETH_ALEN) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_build_disassoc_req::parase mac error\n");
		return 0;
	}
	//change ssid en
	req.reason_code = 0;
	if (0 == kstrtouint(argv[3], 10, &value_num)) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_build_disassoc_req::need_cb=%d\n",
		       value_num);
		req.reason_code = (u16)value_num;
	}
	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
				       HML_TEST_MSG_DISASSOC_FRAME_BUILD,
				       (char *)&req, sizeof(req));

	return 0;
}

/*
test: hml_external_auth_req
cmd:hml_external_auth_req xx:xx:xx:xx:xx:xx ssid
*/
int wq_wifi_priv_netdev_hml_external_auth_req(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	u8 mac_addr[ETH_ALEN];
	u8 ssid_len;
	struct hml_api_test_external_auth_req req;

	if (argc < 4) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_external_auth_req:: argc=%d, agrv[1]=[%s] param is invalid\n",
		       argc, argv[1]);
		return 0;
	}

	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_external_auth_req::Begin, argc=%d, agrv[1]=[%s], argv[2]=[%s], argv[3]=%s\n",
	       argc, argv[1], argv[2], argv[3]);

	memset(&req, 0, sizeof(req));
	//mac
	if (sscanf(argv[2], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", req.mac_addr + 0,
		   req.mac_addr + 1, req.mac_addr + 2, req.mac_addr + 3,
		   req.mac_addr + 4, req.mac_addr + 5) != ETH_ALEN) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_external_auth_req::parase mac error\n");
		return 0;
	}

	//ssid
	req.ssid.length = strlen(argv[3]);
	if (req.ssid.length > MAC_SSID_LEN) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_external_auth_req::ssid_len=%d\n",
		       req.ssid.length);
		return 0;
	}
	memcpy(req.ssid.array, argv[3], req.ssid.length);

	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
				       HML_TEST_MSG_EXTERNAL_AUTH_REQ,
				       (char *)&req, sizeof(req));

	return 0;
}

/*
test: hml_to_vendor_sample
cmd:wq_wifi_priv hid2d_net_dev_name hml_to_vendor_sample 0 |1
*/
int wq_wifi_priv_netdev_hml_to_vendor_sample(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	unsigned int sample_value;
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_to_vendor_sample::argc=%d, agrv[1]=[%s]\n",
	       argc, argv[1]);
	if (0 == kstrtouint(argv[2], 10, &sample_value)) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_to_vendor_sample::need_cb=%d\n",
		       sample_value);
		rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif,
					       DBG_WQ_PRIV_TO_HML_TASK,
					       HML_TO_VENDOR_MSG_SAMPLE,
					       (char *)&sample_value,
					       sizeof(sample_value));
	} else {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_to_vendor_sample::kstrtouint run fail\n");
	}

	return 0;
}

/*
test: hml_module_init
cmd:wq_wifi_priv hid2d_net_dev_name hml_module_init device_type
*/
int wq_wifi_priv_netdev_hml_module_init(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	u8 device_type;
	unsigned int tmp;
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_module_init::argc= %d, argv[1]=[%s]\n",
	       argc, argv[1]);
	if (0 == kstrtouint(argv[2], 10, &tmp)) {
		device_type = (u8)tmp;
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_module_init::device_type=%d\n",
		       device_type);
		rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif,
					       DBG_WQ_PRIV_TO_HML_TASK,
					       HML_TO_VENDOR_MSG_MODULE_INIT,
					       (char *)&device_type,
					       sizeof(device_type));
	} else {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_module_init::kstrtouint run fail\n");
	}
	return 0;
}

/*
test: hml_set_battery
cmd:wq_wifi_priv hid2d_net_dev_name hml_set_battery percent
*/
int wq_wifi_priv_netdev_hml_set_battery(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	u8 battery;
	unsigned int tmp;
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_set_battery::argc=%d, argv[1]=[%s]\n",
	       argc, argv[1]);
	if (0 == kstrtouint(argv[2], 10, &tmp)) {
		battery = (u8)tmp;
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_set_battery::percent=%d\n",
		       battery);
		rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif,
					       DBG_WQ_PRIV_TO_HML_TASK,
					       HML_TO_VENDOR_MSG_SET_BATTERY,
					       (char *)&battery,
					       sizeof(battery));
	} else {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_set_battery::kstrtouint run fail\n");
	}

	return 0;
}

/*
test: rx_mgmt_cb_set
cmd:hml_rx_mgmt_cb 0[set NULL]|1[set cb]
*/
int wq_wifi_priv_netdev_hml_rx_mgmt_cb_set(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	unsigned int hml_stoi;
	u8 need_cb;
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hid2d_rx_mgmt_cb_set::Begin, argc=%d, agrv[1]=[%s]\n",
	       argc, argv[1]);
	if (0 == kstrtouint(argv[2], 10, &hml_stoi)) {
		need_cb = (u8)hml_stoi;
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hid2d_rx_mgmt_cb_set::need_cb=%d\n",
		       need_cb);
		rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif,
					       DBG_WQ_PRIV_HML_TEST,
					       HML_TEST_MSG_RX_MGMT_CB_SET,
					       &need_cb, sizeof(need_cb));
	} else {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hid2d_rx_mgmt_cb_set::kstrtouint run fail\n");
	}
	return 0;
}

/*
test: wq_mgmt_frame_transmit, to send one packet beacon frame
*/
int wq_wifi_priv_netdev_hml_tx_mgmt(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_tx_mgmt::Begin, argc=%d, agrv[1]=[%s]HML_TEST_MSG_TX_MGMT=%d\n",
	       argc, argv[1], HML_TEST_MSG_TX_MGMT);

	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
				       HML_TEST_MSG_TX_MGMT, NULL, 0);

	return 0;
}

/*
test: EROC_REQ
cmd:hml_eroc_req opcode prim20_freq duration_ms
*/
int wq_wifi_priv_netdev_hml_eroc_start(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	u8 need_cb;
	unsigned int tmp;
	struct hml_api_test_eroc_req req;
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_eroc_start::Begin, argc=%d, agrv[1]=[%s]\n",
	       argc, argv[1]);

	//opcode
	if (0 == kstrtouint(argv[2], 0, (unsigned int *)&tmp)) {
		req.opcode = (u8)tmp;
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_eroc_start::opcode=%d\n",
		       req.opcode);
	}

	//prim20_freq
	if (0 == kstrtouint(argv[3], 0, (unsigned int *)&tmp)) {
		req.prim20_freq = (u16)tmp;
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_eroc_start::prim20_freq=%d\n",
		       req.prim20_freq);
	}

	//duration_ms
	if (0 == kstrtouint(argv[4], 0, (unsigned int *)&tmp)) {
		req.duration_ms = (u32)tmp;
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_eroc_start::duration_ms=%d\n",
		       req.duration_ms);
	}

	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
				       HML_TEST_MSG_EROC_START_REQ,
				       (char *)&req, sizeof(req));

	return 0;
}

/*
test: hml_get_mac_cap
*/
int wq_wifi_priv_netdev_hml_get_mac_cap(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_get_mac_cap::Begin, argc=%d, agrv[1]=[%s]\n",
	       argc, argv[1]);

	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
				       HML_TEST_MSG_GET_MAC_CAP, NULL, 0);

	return 0;
}

/*
test: set_bssid_api
cmd:hml_set_bssid xx:xx:xx:xx:xx:xx
*/
int wq_wifi_priv_netdev_hml_set_bssid(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	u8 mac_addr[ETH_ALEN];
	unsigned int tmp;
	if (argc < 3) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_set_bssid:: argc=%d, agrv[1]=[%s] param is invalid\n",
		       argc, argv[1]);
		return 0;
	}

	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_set_bssid::Begin, argc=%d, agrv[1]=[%s], argv[2]=[%s]\n",
	       argc, argv[1], argv[2]);

	//mac
	if (sscanf(argv[2], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", mac_addr + 0,
		   mac_addr + 1, mac_addr + 2, mac_addr + 3, mac_addr + 4,
		   mac_addr + 5) != ETH_ALEN) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_set_bssid::parase mac error\n");
		return 0;
	}
	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
				       HML_TEST_MSG_SET_BSSID, (char *)mac_addr,
				       ETH_ALEN);

	return 0;
}

/*
test: HID2D_STA_ADD
cmd:hml_sta_add xx:xx:xx:xx:xx:xx
*/
int wq_wifi_priv_netdev_hml_sta_add(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	u8 mac_addr[ETH_ALEN];
	unsigned int tmp;
	if (argc < 3) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_sta_add:: argc=%d, agrv[1]=[%s] param is invalid\n",
		       argc, argv[1]);
		return 0;
	}

	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_sta_add::Begin, argc=%d, agrv[1]=[%s], argv[2]=[%s]\n",
	       argc, argv[1], argv[2]);

	//mac
	if (sscanf(argv[2], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", mac_addr + 0,
		   mac_addr + 1, mac_addr + 2, mac_addr + 3, mac_addr + 4,
		   mac_addr + 5) != ETH_ALEN) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_sta_add::parase mac error\n");
		return 0;
	}
	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
				       HML_TEST_MSG_STA_ADD, (char *)mac_addr,
				       ETH_ALEN);

	return 0;
}

/*
test: HID2D_STA_DEL
cmd:hml_sta_del xx:xx:xx:xx:xx:xx
*/
int wq_wifi_priv_netdev_hml_sta_del(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	u8 mac_addr[ETH_ALEN];
	unsigned int tmp;
	if (argc < 3) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_sta_del:: argc=%d, agrv[1]=[%s] param is invalid\n",
		       argc, argv[1]);
		return 0;
	}

	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hml_sta_del::Begin, argc=%d, agrv[1]=[%s], argv[2]=[%s]\n",
	       argc, argv[1], argv[2]);

	//mac
	if (sscanf(argv[2], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", mac_addr + 0,
		   mac_addr + 1, mac_addr + 2, mac_addr + 3, mac_addr + 4,
		   mac_addr + 5) != ETH_ALEN) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_netdev_hml_sta_del::parase mac error\n");
		return 0;
	}
	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
				       HML_TEST_MSG_STA_DEL, (char *)mac_addr,
				       ETH_ALEN);

	return 0;
}

struct wq_wifi_priv_netdev_cmd wq_wifi_priv_netdev_cmd_info[] = {
	{ "hml_rx_mgmt_cb", wq_wifi_priv_netdev_hml_rx_mgmt_cb_set,
	  "hml_rx_mgmt_cb 0[set NULL]|1[set cb]" },
	{ "hml_tx_mgmt", wq_wifi_priv_netdev_hml_tx_mgmt, "hml_tx_mgmt" },
	{ "hml_eroc_req", wq_wifi_priv_netdev_hml_eroc_start,
	  "hml_eroc_req opcode prim20_freq duration_ms" },
	{ "hml_sta_add", wq_wifi_priv_netdev_hml_sta_add,
	  "hml_sta_add xx:xx:xx:xx:xx:xx" },
	{ "hml_sta_del", wq_wifi_priv_netdev_hml_sta_del,
	  "hml_sta_del xx:xx:xx:xx:xx:xx" },
	{ "hml_get_mac_cap", wq_wifi_priv_netdev_hml_get_mac_cap,
	  "hml_get_mac_cap" },
	{ "hml_set_bssid", wq_wifi_priv_netdev_hml_set_bssid,
	  "hml_set_bssid xx:xx:xx:xx:xx:xx" },
	{ "hml_conn_start", wq_wifi_priv_netdev_hml_conn_start,
	  "hml_conn_start " },
	{ "hml_concurrentcy_info", wq_wifi_priv_netdev_hml_concurrentcy_info,
	  "hml_concurrentcy_info" },
	{ "hml_to_vendor_sample", wq_wifi_priv_netdev_hml_to_vendor_sample,
	  "hml_to_vendor_sample 0|1" },
	{ "hml_build_assoc_req", wq_wifi_priv_netdev_hml_build_assoc_req,
	  "hml_build_assoc_req xx:xx:xx:xx:xx:xx 0|1" },
	{ "hml_build_disassoc_req", wq_wifi_priv_netdev_hml_build_disassoc_req,
	  "hml_build_assoc_req xx:xx:xx:xx:xx:xx reaseon" },
	{ "hml_external_auth_req", wq_wifi_priv_netdev_hml_external_auth_req,
	  "hml_external_auth_req xx:xx:xx:xx:xx:xx ssid" },
	{ "hml_module_init", wq_wifi_priv_netdev_hml_module_init,
	  "hml_module_init device_type" },
	{ "hml_set_battery", wq_wifi_priv_netdev_hml_set_battery,
	  "hml_set_battery percent" },

	{ "", NULL, NULL }
};

/*
test: hml_flag_test
cmd:hml_flag_test 0 |1
*/
static int wq_wifi_priv_global_hml_flag_test_set(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	unsigned int hml_flag;
	if (argc != 3) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_global_hml_flag_test_set::argv error. usage: global hml_set_flag 1|0\n");
		return 0;
	}

	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_global_hml_flag_test_set::Begin,argc=%d,agrv[1]=[%s],argv[2]=[%s]\n",
	       argc, argv[1], argv[2]);
	if (0 == kstrtouint(argv[2], 10, &hml_flag)) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_global_hml_flag_test_set::hml_flag=%d\n",
		       hml_flag);
		wq_wifi_priv_global_info.hml_flag_test = (u8)hml_flag;
	} else {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_global_hml_flag_test_set::kstrtouint run fail\n");
	}
	return 0;
}

struct wq_wifi_priv_netdev_cmd wq_wifi_priv_global_cmd_info[] = {
	{ "hml_flag_set", wq_wifi_priv_global_hml_flag_test_set,
	  "hml_flag_set 0|1[go is set]" },

	{ "", NULL, NULL }
};

/*
global use guid
*/
static void wq_wifi_priv_global_cmd_use(void)
{
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv global cmd_name [value] ....\n");
}

/*
netdev use guid
*/
static void wq_wifi_priv_netdev_cmd_use(void)
{
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv netdevice_name cmd_name [value] ....\n");
}

/*
use guid
*/
void wq_wifi_priv_use(void)
{
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv global cmd_name [value] ....\n");
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv netdevice_name cmd_name [value] ....\n");
}

/*
driver/kiwi_usb/wqpriv_cmd write global hanlder
rwnx_hw: is NULL
rwnx_vif: is NULL
*/
void wq_wifi_priv_global_hanlder(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	int i;
	int cmd_num;
	struct wq_wifi_priv_netdev_cmd *cmd_info;

	cmd_num = sizeof(wq_wifi_priv_global_cmd_info) /
		  sizeof(struct wq_wifi_priv_netdev_cmd);
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_global_hanlder::argc=%d, cmd_num=%d, argv[1]=[%s]\n",
	       argc, cmd_num, argv[1]);

	cmd_info = wq_wifi_priv_global_cmd_info;
	for (i = 0; i < cmd_num; i++) {
		if ((strcmp(argv[1], cmd_info[i].cmd_name) == 0) &&
		    cmd_info[i].handler != NULL) {
			WQ_DBG(DM_GENERIC, DL_INF,
			       "wq_wifi_priv_global_hanlder::cmd_info[%d].cmd_name =[%s]\n",
			       i, cmd_info[i].cmd_name);
			cmd_info[i].handler(rwnx_hw, rwnx_vif, argc, argv);
			return;
		}
	}
	wq_wifi_priv_global_cmd_use();
}

/*
driver/kiwi_usb/wqpriv_cmd write netdev hanlder
*/
void wq_wifi_priv_netdev_hanlder(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int argc,
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN])
{
	int i;
	int cmd_num;
	struct wq_wifi_priv_netdev_cmd *cmd_info;

	cmd_num = sizeof(wq_wifi_priv_netdev_cmd_info) /
		  sizeof(struct wq_wifi_priv_netdev_cmd);
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_netdev_hanlde::argc=%d, cmd_num=%d, argv[1]=[%s]\n",
	       argc, cmd_num, argv[1]);

	cmd_info = wq_wifi_priv_netdev_cmd_info;
	for (i = 0; i < cmd_num; i++) {
		if ((strcmp(argv[1], cmd_info[i].cmd_name) == 0) &&
		    cmd_info[i].handler != NULL) {
			WQ_DBG(DM_GENERIC, DL_INF,
			       "wq_wifi_priv_netdev_hanlde::cmd_info[%d].cmd_name =[%s]\n",
			       i, cmd_info[i].cmd_name);
			cmd_info[i].handler(rwnx_hw, rwnx_vif, argc, argv);
			return;
		}
	}
	wq_wifi_priv_netdev_cmd_use();
}

/*
driver/kiwi_usb/wqpriv_cmd write hanlder
*/
static ssize_t wq_wifi_priv_proc_write(struct file *file,
				       const char __user *buffer, size_t count,
				       loff_t *pos)
{
	char tmp_buf[256] = { 0 };
	char argv[WQ_PRIV_CMD_ARGC_MAX][WQ_PRIV_CMD_ARGV_LEN] = { 0 };
	char *tmp;
	char *value;
	int argc = 0;
	struct net_device *dev = NULL;
	struct rwnx_vif *rwnx_vif = NULL;
	struct rwnx_hw *rwnx_hw = NULL;

	WQ_DBG(DM_GENERIC, DL_INF, "wq_wifi_priv_proc_write::count=%d\n",
	       (int)count);
	if (!count || count > 256)
		return 0;

	if (copy_from_user(tmp_buf, buffer, count - 1))
		return -EFAULT;

	WQ_DBG(DM_GENERIC, DL_INF, "wq_wifi_priv_proc_write::tmp_buf=[%s]\n",
	       tmp_buf);
	tmp = tmp_buf;
	value = strsep(&tmp, " ");
	while (value) {
		if (value != NULL &&
		    (strlen(value) >= 1 &&
		     strlen(value) < WQ_PRIV_CMD_ARGV_LEN) &&
		    argc < (WQ_PRIV_CMD_ARGC_MAX - 1)) {
			sprintf(argv[argc], "%s", value);
			argc++;
		}
		value = strsep(&tmp, " ");
	}

	WQ_DBG(DM_GENERIC, DL_INF, "wq_wifi_priv_proc_write::argc=%d\n", argc);
	if (argc < 2) {
		wq_wifi_priv_use();
		return -EFAULT;
	}

	WQ_DBG(DM_GENERIC, DL_INF, "wq_wifi_priv_proc_write::argv[0]=%s\n",
	       argv[0]);
	if (strcmp(argv[0], "global") == 0) {
		wq_wifi_priv_global_hanlder(NULL, NULL, argc, argv);
		WQ_DBG(DM_GENERIC, DL_INF,
		       "wq_wifi_priv_proc_write::******global******\n");
	} else {
		if ((dev = __dev_get_by_name(&init_net, argv[0])) == NULL) {
			WQ_DBG(DM_GENERIC, DL_INF,
			       "wq_wifi_priv_proc_write::not find dev %s\n",
			       argv[0]);
			return -EFAULT;
		}

		rwnx_vif = netdev_priv(dev);
		if (rwnx_vif == NULL) {
			WQ_DBG(DM_GENERIC, DL_INF,
			       "wq_wifi_priv_proc_write::%s rwnx_vif is NULL\n",
			       argv[0]);
			return -EFAULT;
		}
		rwnx_hw = rwnx_vif->rwnx_hw;
		if (rwnx_hw == NULL) {
			WQ_DBG(DM_GENERIC, DL_INF,
			       "wq_wifi_priv_proc_write::%s rwnx_hw is NULL\n",
			       argv[0]);
			return -EFAULT;
		}
		wq_wifi_priv_netdev_hanlder(rwnx_hw, rwnx_vif, argc, argv);
	}

	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_proc_write::******END argc=%d******\n", argc);
	return count;
}

/*
driver/kiwi_usb/wqpriv_cmd read hanlder
*/
static ssize_t wq_wifi_priv_proc_read(struct file *file, char __user *buffer,
				      size_t count, loff_t *pos)
{
	char info[128];
	int offset = 0;

	if (*pos > 0) {
		return 0;
	}
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv global cmd_name [value] ....\n");
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv netdevice_name cmd_name [value] ....\n");

	offset += snprintf(info + offset, sizeof(info) - offset,
			   "wq_wifi_priv global cmd_name [value] ....\n");
	offset +=
		snprintf(info + offset, sizeof(info) - offset,
			 "wq_wifi_priv netdevice_name cmd_name [value] ....\n");

	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

u8 wq_wifi_priv_hml_flag_test_get(void)
{
	WQ_DBG(DM_GENERIC, DL_INF,
	       "wq_wifi_priv_hml_flag_test_get::hml_flag_test=%d\n",
	       wq_wifi_priv_global_info.hml_flag_test);
	return wq_wifi_priv_global_info.hml_flag_test;
}

WQ_PROC_OPS_RW(wq_wifi_priv_proc);
