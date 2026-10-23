/** @file coex.h
  *
  * @brief This file defines all the data structures and all the APIs for coex
  *
  *  Copyright (C) 2022, WuQi Technologies. ALL RIGHTS RESERVED.
  *
  *  This Information is proprietary to WuQi Technologies and MAY NOT
  *  be copied by any method or incorporated into another program without
  *  the express written consent of WuQi. This Information or any portion
  *  thereof remains the property of WuQi. The Information contained herein
  *  is believed to be accurate and WuQi assumes no responsibility or
  *  liability for its use in any way and conveys no license or title under
  *  any patent or copyright and makes no representation or warranty that this
  *  Information is free from patent or copyright infringement.
  *
  */
#ifndef _WQ_COEX_H
#define _WQ_COEX_H

#define COEX_MAX_WIFI_AC 11

#define PTA_ABORT_PROP_NUM 39

enum {
    WQ_BT_PTI_TYPE_BT,
    WQ_BT_PTI_TYPE_LE,
};

typedef enum {
	COEX_WIFI_CUS_SCENE_UPD,// wifi stack -> dtop
	COEX_BT_CUS_SCENE_UPD,  // bt stack -> dtop
	COEX_WIFI_STAT_UPD,     // wifi stack -> dtop
	COEX_CFG_UPD,           // dtop host -> dtop
	COEX_BT_STAT_UPD,       // bt stack -> dtop
	COEX_WIFI_PTI_UPD,      // dtop host -> dtop -> wifi stack
	COEX_BT_PTI_UPD,        // dtop host -> dtop -> bt stack
	COEX_WIFI_PTI_UPD_DONE, // wifi stack -> dtop
	COEX_BT_PTI_UPD_DONE,   // bt stack -> dtop
	COEX_WIFI_CHAN_OP_UPD,  // wifi stack -> bt
	COEX_WIFI_BT_TEST_MODE, // wifi stack -> dtop, only same with rom1.0, will delete.

	COEX_SET_MARGIN,        // wifi stack -> dtop //I will delete it
	COEX_SET_ABORT_PROP,    // dtop host -> dtop

	COEX_CMD_PARSE,         // wifi stack -> dtop
	COEX_WIFI_PHY_UPD,      // wifi stack -> dtop
	COEX_WIFI_RSSI_UPD,
	COEX_BT_RSSI_UPD,
	COEX_CALIBRATION_START, // wifi->wifi_soc->bt_soc->bt stack
	COEX_CALIBRATION_END,   // wifi->wifi_soc->bt_soc->bt stack
	COEX_ANTENNA_MODE,
	COEX_ANT2_MODE_UPD,
	COEX_WIFI_VIF_COUNT_UPD,
	COEX_SET_WIFI_MODE,
} COEX_MSG_ID_e;

typedef enum {
	WIFI_STA_ONLY,
	WIFI_SOFTAP_ONLY,
	WIFI_P2PGC_ONLY,
	WIFI_P2PGO_ONLY,
	WIFI_STA_SOFTAP,
	WIFI_STA_P2PGC,
	WIFI_STA_P2PGO,
	WIFI_UNKNOW_SCE,
	WIFI_CUS_SCE_MAX = WIFI_UNKNOW_SCE,
} COEX_WIFI_CUS_SCE_e;

typedef enum {
	BT_ACL1,
	BT_ACL2,
	// 3 link and more
	BT_ACL3,
	BT_ACL1_SCO,
	BT_ACL2_SCO,
	BT_ACL3_SCO,
	BT_UNKNOW_SCE,
	BT_CUS_SCE_MAX = BT_UNKNOW_SCE,
} COEX_BT_CUS_SCE_e;

typedef enum {
    BLE_CON_ONLY,
    BLE_BIS_ONLY,
    BLE_CIS_ONLY,
    BLE_CON_BIS,
    BLE_CON_CIS,
    BLE_BIS_CIS,
    BLE_CON_BIS_CIS,
    BLE_UNKNOW_SCE,
    BLE_CUS_SCE_MAX = BLE_UNKNOW_SCE,
} COEX_BLE_CUS_SCE_e;

typedef struct le_pti_struct {
	uint8_t tx;
	uint8_t rx;
	uint8_t ifs;
	uint8_t null_pkt;
} le_pti_t;

typedef struct bt_prio0_struct {
	uint8_t page_resp : 4;
	uint8_t master_connect_pending : 4;
	uint8_t page_scan_resp : 4;
	uint8_t slv_connect_pending : 4;
	uint8_t master_slv_switch : 4;
	uint8_t sniff_anchor_point : 4;
	uint8_t sync_train_conless_slave_bdcast : 4;
	uint8_t sniff_attempt : 4;
} bt_prio0_s;

typedef struct bt_prio1_struct {
	uint8_t sync_train_scan : 4;
	uint8_t poll_interval : 4;
	uint8_t page : 4;
	uint8_t page_scan : 4;
	uint8_t inquiry : 4;
	uint8_t inquiry_scan : 4;
	uint8_t inquiry_resp : 4;
	uint8_t sco_esco_resvd_slot : 4;
} bt_prio1_s;

typedef struct bt_prio2_struct {
	uint8_t esco_re_trans : 4;
	uint8_t acl_trans : 4;
	uint8_t acl_re_trans : 4;
	uint8_t lmp : 4;
	uint8_t bdcast_or_bdcast_scan : 4;
	uint8_t resevd_0 : 4;
	uint8_t resevd_1 : 4;
	uint8_t default_prio_unsupport : 4;
} bt_prio2_s;

typedef struct bt_pti_struct {
	bt_prio0_s prio0;
	bt_prio1_s prio1;
	bt_prio2_s prio2;
} bt_pti_t;

typedef struct wifi_pti_struct {
	uint8_t pti_ifs;
	uint8_t pti_rx_listen;
	uint8_t pti_idle;
	uint8_t pti_tx_protect;
	uint8_t pti_tx_wait_ack;
	uint8_t pti_tx_data;
	uint8_t pti_tx_rts_cts;
	uint8_t pti_tx_cca;
	uint8_t pti_auto_tx_cts;
	uint8_t pti_auto_tx_ba;
	uint8_t pti_auto_tx_ack;
	uint8_t pti_rx_normal;
	uint8_t pti_rx_beacon;
	uint8_t pti_ac[COEX_MAX_WIFI_AC];
} wifi_pti_t;

typedef struct wifi_pti_upd {
    uint8_t wifi_status;
    wifi_pti_t pti;
} wifi_pti_upd_t;

typedef struct bt_pti_upd {
	uint8_t pti_type; //bt, ble
	uint8_t bt_type; //e.g acl sco
	bt_pti_t prio;
} bt_pti_upd_t;

typedef struct le_pti_upd {
	uint8_t pti_type; //bt, ble
	uint8_t bt_type; //e.g bis cis con
	le_pti_t prio;
} le_pti_upd_t;

struct coex_abort_prop_s {
	uint32_t prop[PTA_ABORT_PROP_NUM];
};

typedef struct coex_cfg_struct {
	uint8_t timeslice_eb;
	uint8_t reserve[3]; /*align 4 byte*/
	uint32_t wifi_time_dura;
	uint32_t bt_time_dura;
} coex_cfg_t;

typedef struct coex_msg_s {
	uint8_t msg_id;
	uint8_t data[0];
} __attribute((packed)) coex_msg_t;

void coex_msg_parse(struct mm_coex_info_upd *ind);
void coex_init(struct wq_core *core);
void coex_cmd_proc(struct wq_core *core);
void coex_deinit(void);
#endif
