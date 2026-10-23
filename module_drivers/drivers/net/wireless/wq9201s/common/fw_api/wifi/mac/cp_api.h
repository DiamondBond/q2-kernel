#ifndef WQ_FW_WIFI_CP_API_H_
#define WQ_FW_WIFI_CP_API_H_

#include "fw_api/wifi/mac/api.h"
#include "fw_api/wifi/phy/api.h"

#define IPV4_IP_LEN 4
#define IPV6_IP_LEN 16
#define MM_INNER_MSG_RESERVED 10

/// Message Identifier. The number of messages is limited to 0x7FFF.
/// The message ID is divided in two parts:
/// - bits[14..10] : task ID (no more than 32 tasks supported).
/// - bits[9..0] : message index (no more that 1024 messages per task).
typedef u16 ke_msg_id_t;

/// Build the first message ID of a task.
#define KE_FIRST_MSG(task)	(((task) & 0x1F) << 10)
/// Get the task ID from lmac_msg_id_t.
#define MSG_T(msg)		(((msg) >> 10) & 0x1F)
/// Get the msg index from lmac_msg_id_t.
#define MSG_I(msg)		((msg) & 0x3FF)

typedef u16 ke_task_id_t;

#define IPC_A2E_MSG_PARAM_SIZE	((u32)sizeof(u32) * 127) /* total ipc_a2e_msg is up to 512 bytes */
#define IPC_E2A_MSG_PARAM_SIZE	((u32)sizeof(u32) * 256) /* FIXME: USB MTU may be exceeded */

#define IPC_A2E_MSG_HDR_LEN	((u32)offsetof(struct ipc_a2e_msg, param))
#define IPC_E2A_MSG_HDR_LEN	((u32)offsetof(struct ipc_e2a_msg, param))

#define PRIV_TEST_PARAM_COUNT 3

enum
{
	FORCE_PCIE_LINK_SPEED_ADAPTIVE,
	FORCE_PCIE_LINK_SPEED_2_5_G,
	FORCE_PCIE_LINK_SPEED_5_G,
};

/// Message structure between EMB and APP (subset of struct ke_msg)
#define ipc_a2e_msg	ipc_e2a_msg	/* alias */
struct ipc_e2a_msg
{
	ke_msg_id_t	id;		///< Message id.
	ke_task_id_t	dest_id;	///< Destination kernel identifier.
	ke_task_id_t	src_id;		///< Source kernel identifier.
	u16	param_len;	///< Parameter embedded struct length (in bytes).
	u32	param[];	///< Parameter embedded struct. Must be word-aligned.
};

/* Task identifiers for communication between LMAC and DRIVER */
enum
{
	TASK_NONE = (u8)-1,

	// MAC Management task.
	TASK_MM = 0,
	// DEBUG task
	TASK_DBG,
	/// SCAN task
	TASK_SCAN,
	/// TDLS task
	TASK_TDLS,
	/// SCANU task
	TASK_SCANU,
	/// ME task
	TASK_ME,
	/// SM task
	TASK_SM,
	/// APM task
	TASK_APM,
	/// BAM task
	TASK_BAM,
	/// MESH task
	TASK_MESH,
	/// RXU task
	TASK_RXU,
	/// RM task
	TASK_RM,
	/// TWT task
	TASK_TWT,

	///Verdor task start.
	TASK_VENDOR = 20,
	/// Vendor task
	TASK_VENDOR_HML = TASK_VENDOR,

	// This is used to define the last task that is running on the EMB processor
	TASK_LAST_EMB = TASK_VENDOR_HML,

	///Host task start.
	TASK_HOST = 31,
	// nX API task
	TASK_HOST_API = TASK_HOST,
	TASK_MAX,
};

//* **************** MM task start ****************** *//

#define WPA_KCK_LEN			16
#define WPA_KEK_LEN			16
#define WPA_REPLAY_CTR_LEN		8

/// MM_INFO_NOTIFY GET type
enum
{
	MM_INFO_NOTIFY_GET_RSSI = 1,
	MM_INFO_NOTIFY_GET_MAX
};

/// Remain on channel operation codes
enum mm_remain_on_channel_op
{
	MM_ROC_OP_START = 0,
	MM_ROC_OP_CANCEL,

	MM_ROC_OP_MAX
};

/// List of messages related to the task.
enum mm_msg_tag
{
	/// RESET Request.
	MM_RESET_REQ = KE_FIRST_MSG(TASK_MM),
	/// RESET Confirmation.
	MM_RESET_CFM,
	/// START Request.
	MM_START_REQ,
	/// START Confirmation.
	MM_START_CFM,
	/// Read Version Request.
	MM_VERSION_REQ,
	/// Read Version Confirmation.
	MM_VERSION_CFM,
	/// ADD INTERFACE Request.
	MM_ADD_IF_REQ,
	/// ADD INTERFACE Confirmation.
	MM_ADD_IF_CFM,
	/// REMOVE INTERFACE Request.
	MM_REMOVE_IF_REQ,
	/// REMOVE INTERFACE Confirmation.
	MM_REMOVE_IF_CFM,
	/// STA ADD Request.
	MM_STA_ADD_REQ, //10
	/// STA ADD Confirm.
	MM_STA_ADD_CFM,
	/// STA DEL Request.
	MM_STA_DEL_REQ,
	/// STA DEL Confirm.
	MM_STA_DEL_CFM,
	/// RX FILTER CONFIGURATION Request.
	MM_SET_FILTER_REQ,
	/// RX FILTER CONFIGURATION Confirmation.
	MM_SET_FILTER_CFM,
	/// CHANNEL CONFIGURATION Request.
	MM_SET_CHANNEL_REQ,
	/// CHANNEL CONFIGURATION Confirmation.
	MM_SET_CHANNEL_CFM,
	/// DTIM PERIOD CONFIGURATION Request.
	MM_SET_DTIM_REQ,
	/// DTIM PERIOD CONFIGURATION Confirmation.
	MM_SET_DTIM_CFM,
	/// BEACON INTERVAL CONFIGURATION Request.
	MM_SET_BEACON_INT_REQ, //20
	/// BEACON INTERVAL CONFIGURATION Confirmation.
	MM_SET_BEACON_INT_CFM,
	/// BASIC RATES CONFIGURATION Request.
	MM_SET_BASIC_RATES_REQ,
	/// BASIC RATES CONFIGURATION Confirmation.
	MM_SET_BASIC_RATES_CFM,
	/// BSSID CONFIGURATION Request.
	MM_SET_BSSID_REQ,
	/// BSSID CONFIGURATION Confirmation.
	MM_SET_BSSID_CFM,
	/// EDCA PARAMETERS CONFIGURATION Request.
	MM_SET_EDCA_REQ,
	/// EDCA PARAMETERS CONFIGURATION Confirmation.
	MM_SET_EDCA_CFM,
	/// ABGN MODE CONFIGURATION Request.
	MM_SET_MODE_REQ,
	/// ABGN MODE CONFIGURATION Confirmation.
	MM_SET_MODE_CFM,
	/// Request setting the VIF active state (i.e associated or AP started)
	MM_SET_VIF_STATE_REQ, //30
	/// Confirmation of the @ref MM_SET_VIF_STATE_REQ message.
	MM_SET_VIF_STATE_CFM,
	/// SLOT TIME PARAMETERS CONFIGURATION Request.
	MM_SET_SLOTTIME_REQ,
	/// SLOT TIME PARAMETERS CONFIGURATION Confirmation.
	MM_SET_SLOTTIME_CFM,
	/// Power Mode Change Request.
	MM_SET_IDLE_REQ,
	/// Power Mode Change Confirm.
	MM_SET_IDLE_CFM,
	/// KEY ADD Request.
	MM_KEY_ADD_REQ,
	/// KEY ADD Confirm.
	MM_KEY_ADD_CFM,
	/// KEY DEL Request.
	MM_KEY_DEL_REQ,
	/// KEY DEL Confirm.
	MM_KEY_DEL_CFM,
	/// Block Ack agreement info addition
	MM_BA_ADD_REQ, //40
	/// Block Ack agreement info addition confirmation
	MM_BA_ADD_CFM,
	/// Block Ack agreement info deletion
	MM_BA_DEL_REQ,
	/// Block Ack agreement info deletion confirmation
	MM_BA_DEL_CFM,
	/// Indication of the primary TBTT to the upper MAC. Upon the reception of this
	// message the upper MAC has to push the beacon(s) to the beacon transmission queue.
	MM_PRIMARY_TBTT_IND,
	/// Indication of the secondary TBTT to the upper MAC. Upon the reception of this
	// message the upper MAC has to push the beacon(s) to the beacon transmission queue.
	MM_SECONDARY_TBTT_IND,
	/// Request for changing the TX power
	MM_SET_POWER_REQ,
	/// Confirmation of the TX power change
	MM_SET_POWER_CFM,
	/// Request to the LMAC to trigger the embedded logic analyzer and forward the debug
	/// dump.
	MM_DBG_TRIGGER_REQ,
	/// Set Power Save mode
	MM_SET_PS_MODE_REQ,
	/// Set Power Save mode confirmation
	MM_SET_PS_MODE_CFM, //50
	/// Request to add a channel context
	MM_CHAN_CTXT_ADD_REQ,
	/// Confirmation of the channel context addition
	MM_CHAN_CTXT_ADD_CFM,
	/// Request to delete a channel context
	MM_CHAN_CTXT_DEL_REQ,
	/// Confirmation of the channel context deletion
	MM_CHAN_CTXT_DEL_CFM,
	/// Request to link a channel context to a VIF
	MM_CHAN_CTXT_LINK_REQ,
	/// Confirmation of the channel context link
	MM_CHAN_CTXT_LINK_CFM,
	/// Request to unlink a channel context from a VIF
	MM_CHAN_CTXT_UNLINK_REQ,
	/// Confirmation of the channel context unlink
	MM_CHAN_CTXT_UNLINK_CFM,
	/// Request to update a channel context
	MM_CHAN_CTXT_UPDATE_REQ,
	/// Confirmation of the channel context update
	MM_CHAN_CTXT_UPDATE_CFM, //60
	/// Request to schedule a channel context
	MM_CHAN_CTXT_SCHED_REQ,
	/// Confirmation of the channel context scheduling
	MM_CHAN_CTXT_SCHED_CFM,
	/// Request to change the beacon template in LMAC
	MM_BCN_CHANGE_REQ,
	/// Confirmation of the beacon change
	MM_BCN_CHANGE_CFM,
	/// Request to update the TIM in the beacon (i.e to indicate traffic bufferized at AP)
	MM_TIM_UPDATE_REQ,
	/// Confirmation of the TIM update
	MM_TIM_UPDATE_CFM,
	/// Connection loss indication
	MM_CONNECTION_LOSS_IND,
	/// Channel context switch indication to the upper layers
	MM_CHANNEL_SWITCH_IND,
	/// Channel context pre-switch indication to the upper layers
	MM_CHANNEL_PRE_SWITCH_IND,
	/// Request to remain on channel or cancel remain on channel
	MM_REMAIN_ON_CHANNEL_REQ, //70
	/// Confirmation of the (cancel) remain on channel request
	MM_REMAIN_ON_CHANNEL_CFM,
	/// Remain on channel expired indication
	MM_REMAIN_ON_CHANNEL_EXP_IND,
	/// Indication of a PS state change of a peer device
	MM_PS_CHANGE_IND,
	/// Indication that some buffered traffic should be sent to the peer device
	MM_TRAFFIC_REQ_IND,
	/// Request to modify the STA Power-save mode options
	MM_SET_PS_OPTIONS_REQ,
	/// Confirmation of the PS options setting
	MM_SET_PS_OPTIONS_CFM,
	/// Indication of PS state change for a P2P VIF
	MM_P2P_VIF_PS_CHANGE_IND,
	/// Indication that CSA counter has been updated
	MM_CSA_COUNTER_IND,
	/// Channel occupation report indication
	MM_CHANNEL_SURVEY_IND,
	/// Message containing Beamformer Information
	MM_BFMER_ENABLE_REQ, //80
	/// Request to Start/Stop/Update NOA - GO Only
	MM_SET_P2P_NOA_REQ,
	/// Request to Start/Stop/Update Opportunistic PS - GO Only
	MM_SET_P2P_OPPPS_REQ,
	/// Start/Stop/Update NOA Confirmation
	MM_SET_P2P_NOA_CFM,
	/// Start/Stop/Update Opportunistic PS Confirmation
	MM_SET_P2P_OPPPS_CFM,
	/// P2P NoA Update Indication - GO Only
	MM_P2P_NOA_UPD_IND,
	/// Request to set RSSI threshold and RSSI hysteresis
	MM_CFG_RSSI_REQ,
	/// Indication that RSSI level is below or above the threshold
	MM_RSSI_STATUS_IND,
	/// Indication that CSA is done
	MM_CSA_FINISH_IND,
	/// Indication that CSA is in prorgess (resp. done) and traffic must be stopped (resp. restarted)
	MM_CSA_TRAFFIC_IND,
	/// Request to update the group information of a station
	MM_MU_GROUP_UPDATE_REQ, //90
	/// Confirmation of the @ref MM_MU_GROUP_UPDATE_REQ message
	MM_MU_GROUP_UPDATE_CFM,
	/// Request to initialize the antenna diversity algorithm
	MM_ANT_DIV_INIT_REQ,
	/// Request to stop the antenna diversity algorithm
	MM_ANT_DIV_STOP_REQ,
	/// Request to update the antenna switch status
	MM_ANT_DIV_UPDATE_REQ,
	/// Request to switch the antenna connected to path_0
	MM_SWITCH_ANTENNA_REQ,
	/// Indication that a packet loss has occurred
	MM_PKTLOSS_IND,
	/// MU EDCA PARAMETERS Configuration Request.
	MM_SET_MU_EDCA_REQ,
	/// MU EDCA PARAMETERS Configuration Confirmation.
	MM_SET_MU_EDCA_CFM,
	/// UORA PARAMETERS Configuration Request.
	MM_SET_UORA_REQ,
	/// UORA PARAMETERS Configuration Confirmation.
	MM_SET_UORA_CFM, //100
	/// TXOP RTS THRESHOLD Configuration Request.
	MM_SET_TXOP_RTS_THRES_REQ,
	/// TXOP RTS THRESHOLD Configuration Confirmation.
	MM_SET_TXOP_RTS_THRES_CFM,
	/// HE BSS Color Configuration Request.
	MM_SET_BSS_COLOR_REQ,
	/// HE BSS Color Configuration Confirmation.
	MM_SET_BSS_COLOR_CFM,
	/// Register Write Request.
	MM_REG_WRITE_REQ,
	/// Register Write Confirmation.
	MM_REG_WRITE_CFM,
	/// Register Read Request.
	MM_REG_READ_REQ,
	/// Register Read Confirmation.
	MM_REG_READ_CFM,
	/// Set ip address for arp offload
	MM_SET_IP_REQ,
	/// Set ip address Confirmation
	MM_SET_IP_CFM, //110
	/// info notify req
	MM_INFO_NOTIFY_REQ,
	/// info notify confirmation
	MM_INFO_NOTIFY_CFM,
	/// MAX number of messages
	MM_MAX,
};

enum mm_msg_ext_tag
{
	/// spatial stream update indication
	MM_NSS_UPDATE_IND = MM_MAX + MM_INNER_MSG_RESERVED,
	/// Read Version Ext Request.
	MM_VERSION_EXT_REQ,
	/// Read Version Ext Confirmation.
	MM_VERSION_EXT_CFM,
	/// Coex infomation update indication
	MM_COEX_INFO_UPDATE_IND,
	/// info pwr from ini
	MM_INFO_PWR_REQ,
	/// ini conf info
	MM_INI_CONF_REQ,
	/// Set Antenna Req
	MM_SET_ANT_REQ,
	/// Set Antenna confirmation
	MM_SET_ANT_CFM,
	/// Set chan pwr info REQ
	MM_CHAN_PWR_INFO_REQ,
	///set reg domain country req
	MM_REG_DM_CODE_REQ,
	/// Set mmode req
	MM_SET_MMODE_REQ,
	///CCA set enable/disable
	MM_SET_CCA_CAP_REQ,
	///CCA set confirmation
	MM_SET_CCA_CAP_CFM,
	///CCA get info
	MM_GET_CCA_CAP_REQ,
	///CCA get confirmation
	MM_GET_CCA_CAP_CFM,
	/// MAX number of messages
	MM_EXT_MAX,
};

/// Features supported by LMAC - Positions
enum mm_features
{
	/// Beaconing
	MM_FEAT_BCN_BIT = 0,
	/// Autonomous Beacon Transmission
	MM_FEAT_AUTOBCN_BIT,
	/// Scan in LMAC
	MM_FEAT_HWSCAN_BIT,
	/// Connection Monitoring
	MM_FEAT_CMON_BIT,
	/// Multi Role
	MM_FEAT_MROLE_BIT,
	/// Radar Detection
	MM_FEAT_RADAR_BIT,
	/// Power Save
	MM_FEAT_PS_BIT,
	/// UAPSD
	MM_FEAT_UAPSD_BIT,
	/// DPSM
	MM_FEAT_DPSM_BIT,
	/// A-MPDU
	MM_FEAT_AMPDU_BIT,
	/// A-MSDU
	MM_FEAT_AMSDU_BIT,
	/// Channel Context
	MM_FEAT_CHNL_CTXT_BIT,
	/// Packet reordering
	MM_FEAT_REORD_BIT,
	/// P2P
	MM_FEAT_P2P_BIT,
	/// P2P Go
	MM_FEAT_P2P_GO_BIT,
	/// UMAC Present
	MM_FEAT_UMAC_BIT,
	/// VHT support
	MM_FEAT_VHT_BIT,
	/// Beamformee
	MM_FEAT_BFMEE_BIT,
	/// Beamformer
	MM_FEAT_BFMER_BIT,
	/// WAPI
	MM_FEAT_WAPI_BIT,
	/// MFP
	MM_FEAT_MFP_BIT,
	/// Mu-MIMO RX support
	MM_FEAT_MU_MIMO_RX_BIT,
	/// Mu-MIMO TX support
	MM_FEAT_MU_MIMO_TX_BIT,
	/// Wireless Mesh Networking
	MM_FEAT_MESH_BIT,
	/// One channel mode support
	MM_FEAT_INTF_SINGLE_CHAN_BIT = MM_FEAT_MESH_BIT,
	/// TDLS support
	MM_FEAT_TDLS_BIT,
	/// Antenna Diversity support
	MM_FEAT_ANT_DIV_BIT,
	/// UF support
	MM_FEAT_UF_BIT,
	/// A-MSDU maximum size (bit0)
	MM_AMSDU_MAX_SIZE_BIT0,
	/// A-MSDU maximum size (bit1)
	MM_AMSDU_MAX_SIZE_BIT1,
	/// MON_DATA support
	MM_FEAT_MON_DATA_BIT,
	/// HE (802.11ax) support
	MM_FEAT_HE_BIT,
	/// TWT support
	MM_FEAT_TWT_BIT,
};

/* MAC feature (bits in version_machw_1 or register NXMAC_VERSION_1_ADDR) */
enum mac_feat1 {
	MAC_FEAT1_QOS_BIT = 0,
	MAC_FEAT1_EDCA_BIT,
	MAC_FEAT1_HCCA_BIT,
	MAC_FEAT1_SME_BIT,
	MAC_FEAT1_SECURITY_BIT,
	MAC_FEAT1_WEP_BIT,
	MAC_FEAT1_TKIP_BIT,
	MAC_FEAT1_CCMP_BIT,
	MAC_FEAT1_RCE_BIT,
	MAC_FEAT1_GCMP_BIT,
	MAC_FEAT1_HT_BIT,
	MAC_FEAT1_VHT_BIT,
	MAC_FEAT1_TPC_BIT,
	MAC_FEAT1_WAPI_BIT,
	MAC_FEAT1_COEX_BIT,
	MAC_FEAT1_HE_BIT,
	MAC_FEAT1_MAC_80211MH_FORMAT_BIT,
	MAC_FEAT1_BFMEE_BIT,
	MAC_FEAT1_BFMER_BIT,
	MAC_FEAT1_MU_MIMO_TX_BIT,
	MAC_FEAT1_COMPRESS_TXDESC_BIT,
};

/// Power Save mode setting
enum mm_mode_state
{
	/// Power-save off
	PS_MODE_OFF = 0,
	///Power-save on - Normal mode
	PS_MODE_ON,
	///Power-save on - Dynamic mode
	PS_MODE_ON_DYN,
};

struct host_data_ring_free_req
{
	u32 mac_id	: 4,
	    buf_rd_idx	: 25,
	    use_backup_ring	: 1,
	    reserved	: 2;
};

struct secure_param_set_req {
	u8 vif_index;
	u8 proto;
	u32 akm;
	u32 pairwise_cipher;
	u32 group_cipher;
};

struct rekey_data_set_req {
	u8 vif_index;
	u8 kck[WPA_KCK_LEN];
	u8 kek[WPA_KEK_LEN];
	u8 replay_counter[WPA_REPLAY_CTR_LEN];
};

/// Structure containing the parameters of the @ref MM_START_REQ message
struct mm_start_req
{
	/// PHY configuration
	struct phy_cfg_tag phy_cfg;
	/// UAPSD timeout
	u32 uapsd_timeout;
	/// Local LP clock accuracy (in ppm)
	u16 lp_clk_accuracy;
	/// Array of TX timeout values (in ms, one per TX queue) - 0 sets default value
	u16 tx_timeout[AC_MAX];
	// RAE LL mode host buf ring addr (obsolete, use me_extend_set_host_data_ring_req)
	u32 host_buf_ring_addr;
	// RAE LL mode host buf ring size (obsolete, use me_extend_set_host_data_ring_req)
	u32 host_buf_ring_sz;
    /// Narrow BandWidth type
    u8 nbw_type;
};

/// Structure containing the parameters of the @ref MM_SET_CHANNEL_REQ message
struct mm_set_channel_req
{
	/// Channel information
	struct mac_chan_op chan;
	/// Index of the RF for which the channel has to be set (0: operating (primary), 1: secondary
	/// RF (used for additional radar detection). This parameter is reserved if no secondary RF
	/// is available in the system
	u8 index;
};

/// Structure containing the parameters of the @ref MM_SET_CHANNEL_CFM message
struct mm_set_channel_cfm
{
	/// Radio index to be used in policy table
	u8 radio_idx;
	/// TX power configured (in dBm)
	u8 power;
};

/// Structure containing the parameters of the @ref MM_SET_DTIM_REQ message
struct mm_set_dtim_req
{
	/// DTIM period
	u8 dtim_period;
};

/// Structure containing the parameters of the @ref MM_SET_POWER_REQ message
struct mm_set_power_req
{
	/// Index of the interface for which the parameter is configured
	u8 inst_nbr;
	/// TX power (in dBm)
	s8 power;
};

/// Structure containing the parameters of the @ref MM_SET_POWER_CFM message
struct mm_set_power_cfm
{
	/// Radio index to be used in policy table
	u8 radio_idx;
	/// TX power configured (in dBm)
	s8 power;
};

struct mm_pwr_info_req
{
    u8 tx_pwr_force_ena;
    u8 tx_pwr_force_dbm;
};

struct mm_ini_conf_req
{
    u8 tx_pwr_force_dbm;
    u8 tx_pwr_force_ena:1;
    u8 tx_ampdu_disable:1;
    u8 force_edca_vo:1;
    u8 force_ignore_nav:1;
    u8 underrun_adapt_tx_rate : 1;
    u8 dynbw_enable:1;
    u8 mmode:2;
    u16 max_support_ba_bitmap;
    u16 extension[16]; //reserved for future use, to avoid to patch in ini conf handler in fw
};

struct mm_reg_dm_code_req
{
    u8 reg_dm_code;
};

/// Structure containing the parameters of the @ref MM_SET_BEACON_INT_REQ message
struct mm_set_beacon_int_req
{
	/// Beacon interval
	u16 beacon_int;
	/// Index of the interface for which the parameter is configured
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_BASIC_RATES_REQ message
struct mm_set_basic_rates_req
{
	/// Basic rate set (as expected by bssBasicRateSet field of Rates MAC HW register)
	u32 rates;
	/// Index of the interface for which the parameter is configured
	u8 inst_nbr;
	/// Band on which the interface will operate
	u8 band;
};

/// Structure containing the parameters of the @ref MM_SET_BSSID_REQ message
struct mm_set_bssid_req
{
	/// BSSID to be configured in HW
	struct mac_addr bssid;
	/// Index of the interface for which the parameter is configured
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_FILTER_REQ message
struct mm_set_filter_req
{
	/// RX filter to be put into rxCntrlReg HW register
	u32 filter;
};

/// Structure containing the parameters of the @ref MM_ADD_IF_REQ message.
struct mm_add_if_req
{
	/// Type of the interface (AP, STA, ADHOC, ...)
	u8 type;
	/// MAC ADDR of the interface to start
	struct mac_addr addr;
	/// P2P Interface
	bool p2p;
	u8 bit_hml_flag : 1,
	   bit_reserved : 7;
};

/// Structure containing the parameters of the @ref MM_SET_EDCA_REQ message
struct mm_set_edca_req
{
	/// EDCA parameters of the queue (as expected by edcaACxReg HW register)
	u32 ac_param;
	/// Flag indicating if UAPSD can be used on this queue
	bool uapsd;
	/// HW queue for which the parameters are configured
	u8 hw_queue;
	/// Index of the interface for which the parameters are configured
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_MU_EDCA_REQ message
struct mm_set_mu_edca_req
{
	/// MU EDCA parameters of the different HE queues
	u32 param[AC_MAX];
};

/// Structure containing the parameters of the @ref MM_SET_UORA_REQ message
struct mm_set_uora_req
{
	/// Minimum exponent of OFDMA Contention Window.
	u8 eocw_min;
	/// Maximum exponent of OFDMA Contention Window.
	u8 eocw_max;
};

/// Structure containing the parameters of the @ref MM_SET_TXOP_RTS_THRES_REQ message
struct mm_set_txop_rts_thres_req
{
	/// TXOP RTS threshold
	u16 txop_dur_rts_thres;
	/// Index of the interface for which the parameter is configured
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_BSS_COLOR_REQ message
struct mm_set_bss_color_req
{
	/// HE BSS color, formatted as per BSS_COLOR MAC HW register
	u32 bss_color;
};

/// Structure containing the parameters of the @ref MM_SET_SLOTTIME_REQ message
struct mm_set_slottime_req
{
	/// Slot time expressed in us
	u8 slottime;
};

/// Structure containing the parameters of the @ref MM_SET_MODE_REQ message
struct mm_set_mode_req
{
	/// abgnMode field of macCntrl1Reg register
	u8 abgnmode;
};

/// Structure containing the parameters of the @ref MM_SET_VIF_STATE_REQ message
struct mm_set_vif_state_req
{
	/// Association Id received from the AP (valid only if the VIF is of STA type)
	u16 aid;
	/// Flag indicating if the VIF is active or not
	bool active;
	/// Interface index
	u8 inst_nbr;
};


#define FORMAT_MAX  4
#define BW_MAX      3
#define RATE_MAX    12

///struct containing the parameters of the @ref MM_CHAN_PWR_INFO_REQ message.
struct mm_chan_pwr_info_req
{
    u8 vif_idx;
    u8 channel;
    u8 enable;
    u8 chan_pwr_tab[FORMAT_MAX][BW_MAX][RATE_MAX];
};

/// Structure containing the parameters of the @ref MM_ADD_IF_CFM message.
struct mm_add_if_cfm
{
	/// Status of operation (different from 0 if unsuccessful)
	u8 status;
	/// Interface index assigned by the LMAC
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_REMOVE_IF_REQ message.
struct mm_remove_if_req
{
	/// Interface index assigned by the LMAC
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_VERSION_CFM message.
struct mm_version_cfm
{
	/// Version of the LMAC FW
	u32 version_lmac;
	/// Version1 of the MAC HW (as encoded in version1Reg MAC HW register)
	u32 version_machw_1;
	/// Version2 of the MAC HW (as encoded in version2Reg MAC HW register)
	u32 version_machw_2;
	/// Version1 of the PHY (depends on actual PHY)
	u32 version_phy_1;
	/// Version2 of the PHY (depends on actual PHY)
	u32 version_phy_2;
	/// Supported Features
	u32 features;
	/// Maximum number of supported stations
	u16 max_sta_nb;
	/// Maximum number of supported virtual interfaces
	u8 max_vif_nb;
	u8 country_code[2];
	u8 nss;
	u8 spatial_stream_mode;
	/// Maximum mcs of supported virtual interfaces (VHT and HE), default : 0xFF
	u8 max_mcs;
	/// Phy band support. bit0:2.4g, bit1:5g
	u8 phy_band_support;
};

/// Structure containing the parameters of the @ref MM_VERSION_EXT_CFM message.
struct mm_version_ext_cfm
{
    // Magic number to indicate use EFUSE mac_addr
    u8 format;
    u8 mac_addr[6];
    u8 extension[16];
};

/// Structure containing the parameters of the @ref MM_STA_ADD_REQ message.
struct mm_sta_add_req
{
	/// Bitfield showing some capabilities of the STA (@ref enum mac_sta_flags)
	u32 capa_flags;
	/// Maximum A-MPDU size, in bytes, for HE frames
	u32 ampdu_size_max_he;
	/// Maximum A-MPDU size, in bytes, for VHT frames
	u32 ampdu_size_max_vht;
	/// PAID/GID
	u32 paid_gid;
	/// Maximum A-MPDU size, in bytes, for HT frames
	u16 ampdu_size_max_ht;
	/// MAC address of the station to be added
	struct mac_addr mac_addr;
	/// A-MPDU spacing, in us
	u8 ampdu_spacing_min;
	/// Interface index
	u8 inst_nbr;
	/// TDLS station
	bool tdls_sta;
	/// Indicate if the station is TDLS link initiator station
	bool tdls_sta_initiator;
	/// Indicate if the TDLS Channel Switch is allowed
	bool tdls_chsw_allowed;
	/// nonTransmitted BSSID index, set to the BSSID index in case the STA added is an AP
	/// that is a nonTransmitted BSSID. Should be set to 0 otherwise
	u8 bssid_index;
	/// Maximum BSSID indicator, valid if the STA added is an AP that is a nonTransmitted
	/// BSSID
	u8 max_bssid_ind;
};

/// Structure containing the parameters of the @ref MM_STA_ADD_CFM message.
struct mm_sta_add_cfm
{
	/// Status of the operation (different from 0 if unsuccessful)
	u8 status;
	/// Index assigned by the LMAC to the newly added station
	u8 sta_idx;
	/// MAC HW index of the newly added station
	u8 hw_sta_idx;
};

/// Structure containing the parameters of the @ref MM_STA_DEL_REQ message.
struct mm_sta_del_req
{
	/// Index of the station to be deleted
	u8 sta_idx;
};

/// Structure containing the parameters of the @ref MM_STA_DEL_CFM message.
struct mm_sta_del_cfm
{
	/// Status of the operation (different from 0 if unsuccessful)
	u8 status;
};

/// Structure containing the parameters of the @ref MM_KEY_ADD REQ message.
struct mm_key_add_req
{
	/// Key index (valid only for default keys)
	u8 key_idx;
	/// STA index (valid only for pairwise or mesh group keys)
	u8 sta_idx;
	/// Key material
	struct mac_sec_key key;
	/// Cipher suite (WEP64, WEP128, TKIP, CCMP)
	u8 cipher_suite;
	/// Index of the interface for which the key is set (valid only for default keys or mesh group keys)
	u8 inst_nbr;
	/// A-MSDU SPP parameter
	u8 spp;
	/// Indicate if provided key is a pairwise key or not
	bool pairwise;
};

/// Structure containing the parameters of the @ref MM_KEY_ADD_CFM message.
struct mm_key_add_cfm
{
	/// Status of the operation (different from 0 if unsuccessful)
	u8 status;
	/// HW index of the key just added
	u8 hw_key_idx;
};

/// Structure containing the parameters of the @ref MM_KEY_DEL_REQ message.
struct mm_key_del_req
{
	/// HW index of the key to be deleted
	u8 hw_key_idx;
};

/// Structure containing the parameters of the @ref MM_BA_ADD_REQ message.
struct mm_ba_add_req
{
	///Type of agreement (0: TX, 1: RX)
	u8 type;
	///Index of peer station with which the agreement is made
	u8 sta_idx;
	///TID for which the agreement is made with peer station
	u8 tid;
	///unused
	u8 reserved;
	///Buffer size - number of MPDUs that can be held in its buffer per TID
	u16 bufsz;
	/// Start sequence number negotiated during BA setup - the one in first aggregated MPDU counts more
	u16 ssn;
};

/// Structure containing the parameters of the @ref MM_BA_ADD_CFM message.
struct mm_ba_add_cfm
{
	///Index of peer station for which the agreement is being confirmed
	u8 sta_idx;
	///TID for which the agreement is being confirmed
	u8 tid;
	/// Status of ba establishment
	u8 status;
};

/// Structure containing the parameters of the @ref MM_BA_DEL_REQ message.
struct mm_ba_del_req
{
	///Type of agreement (0: TX, 1: RX)
	u8 type;
	///Index of peer station for which the agreement is being deleted
	u8 sta_idx;
	///TID for which the agreement is being deleted
	u8 tid;
};

/// Structure containing the parameters of the @ref MM_BA_DEL_CFM message.
struct mm_ba_del_cfm
{
	///Index of peer station for which the agreement deletion is being confirmed
	u8 sta_idx;
	///TID for which the agreement deletion is being confirmed
	u8 tid;
	/// Status of ba deletion
	u8 status;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_ADD_REQ message
struct mm_chan_ctxt_add_req
{
	/// Operating channel
	struct mac_chan_op chan;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_ADD_REQ message
struct mm_chan_ctxt_add_cfm
{
	/// Status of the addition
	u8 status;
	/// Index of the new channel context
	u8 index;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_DEL_REQ message
struct mm_chan_ctxt_del_req
{
	/// Index of the new channel context to be deleted
	u8 index;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_LINK_REQ message
struct mm_chan_ctxt_link_req
{
	/// VIF index
	u8 vif_index;
	/// Channel context index
	u8 chan_index;
	/// Indicate if this is a channel switch (unlink current ctx first if true)
	u8 chan_switch;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_UNLINK_REQ message
struct mm_chan_ctxt_unlink_req
{
	/// VIF index
	u8 vif_index;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_UPDATE_REQ message
struct mm_chan_ctxt_update_req
{
	/// VIF index
	u8 vif_index;
	/// Channel context index
	u8 chan_index;
	/// New channel information
	struct mac_chan_op chan;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_SCHED_REQ message
struct mm_chan_ctxt_sched_req
{
	/// VIF index
	u8 vif_index;
	/// Channel context index
	u8 chan_index;
	/// Type of the scheduling request (0: normal scheduling, 1: derogatory
	/// scheduling)
	u8 type;
};

/// Structure containing the parameters of the @ref MM_CHANNEL_SWITCH_IND message
struct mm_channel_switch_ind
{
	/// Index of the channel context we will switch to
	u8 chan_index;
	/// Indicate if the switch has been triggered by a Remain on channel request
	bool roc;
	/// VIF on which remain on channel operation has been started (if roc == 1)
	u8 vif_index;
	/// Indicate if the switch has been triggered by a TDLS Remain on channel request
	bool roc_tdls;
};

/// Structure containing the parameters of the @ref MM_CHANNEL_PRE_SWITCH_IND message
struct mm_channel_pre_switch_ind
{
	/// Index of the channel context we will switch to
	u8 chan_index;
};

/// Structure containing the parameters of the @ref MM_CONNECTION_LOSS_IND message.
struct mm_connection_loss_ind
{
	/// VIF instance number
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_DBG_TRIGGER_REQ message.
struct mm_dbg_trigger_req
{
	/// Error trace to be reported by the LMAC
	char error[64];
};

/// Structure containing the parameters of the @ref MM_SET_PS_MODE_REQ message.
struct mm_set_ps_mode_req
{
	/// Power Save is activated or deactivated
	u8 new_state;
};

/// Structure containing the parameters of the @ref MM_BCN_CHANGE_REQ message.
#define BCN_MAX_CSA_CPT 2
struct mm_bcn_change_req
{
	/// Pointer, in host memory, to the new beacon template
	//u32 bcn_ptr;
	/// Length of the beacon template
	u16 bcn_len;
	/// Offset of the TIM IE in the beacon
	u16 tim_oft;
	/// Length of the TIM IE
	u8 tim_len;
	/// Index of the VIF for which the beacon is updated
	u8 inst_nbr;
	/// Offset of CSA (channel switch announcement) counters (0 means no counter)
	u8 csa_oft[BCN_MAX_CSA_CPT];

	u8 bcn_buf[384];
};

/// Structure containing the parameters of the @ref MM_TIM_UPDATE_REQ message.
struct mm_tim_update_req
{
	/// Association ID of the STA the bit of which has to be updated (0 for BC/MC traffic)
	u16 aid;
	/// Flag indicating the availability of data packets for the given STA
	u8 tx_avail;
	/// Index of the VIF for which the TIM is updated
	u8 inst_nbr;
};

/// Structure containing the parameters of the @ref MM_REMAIN_ON_CHANNEL_REQ message.
struct mm_remain_on_channel_req
{
	/// Operation Code
	u8 op_code;
	/// VIF Index
	u8 vif_index;
	/// Channel parameter
	struct mac_chan_op chan;
	/// Duration (in ms)
	u32 duration_ms;
};

/// Structure containing the parameters of the @ref MM_REMAIN_ON_CHANNEL_CFM message
struct mm_remain_on_channel_cfm
{
	/// Operation Code
	u8 op_code;
	/// Status of the operation
	u8 status;
	/// Channel Context index
	u8 chan_ctxt_index;
};

/// Structure containing the parameters of the @ref MM_REMAIN_ON_CHANNEL_EXP_IND message
struct mm_remain_on_channel_exp_ind
{
	/// VIF Index
	u8 vif_index;
	/// Channel Context index
	u8 chan_ctxt_index;
};

/// Structure containing the parameters of the @ref MM_PS_CHANGE_IND message
struct mm_ps_change_ind
{
	/// Index of the peer device that is switching its PS state
	u8 sta_idx;
	/// New PS state of the peer device (0: active, 1: sleeping)
	u8 ps_state;
};

/// Structure containing the parameters of the @ref MM_TRAFFIC_REQ_IND message
struct mm_traffic_req_ind
{
	/// Index of the peer device that needs traffic
	u8 sta_idx;
	/// Number of packets that need to be sent (if 0, all buffered traffic shall be sent and
	/// if set to @ref PS_SP_INTERRUPTED, it means that current service period has been interrupted)
	u8 pkt_cnt;
	/// Flag indicating if the traffic request concerns U-APSD queues or not
	bool uapsd;
};

/// Structure containing the parameters of the @ref MM_SET_PS_OPTIONS_REQ message.
struct mm_set_ps_options_req
{
	/// VIF Index
	u8 vif_index;
	/// Listen interval (0 if wake up shall be based on DTIM period)
	u16 listen_interval;
	/// Flag indicating if we shall listen the BC/MC traffic or not
	bool dont_listen_bc_mc;
};

/// Structure containing the parameters of the @ref MM_CSA_COUNTER_IND message
struct mm_csa_counter_ind
{
	/// Index of the VIF
	u8 vif_index;
	/// Updated CSA counter value
	u8 csa_count;
};

/// Structure containing the parameters of the @ref MM_CHANNEL_SURVEY_IND message
struct mm_channel_survey_ind
{
	/// Frequency of the channel
	u16 freq;
	/// Noise in dbm
	s8 noise_dbm;
	/// Amount of time spent of the channel (in ms)
	u32 chan_time_ms;
	/// Amount of time the primary channel was sensed busy
	u32 chan_time_busy_ms;
};

/// Structure containing the parameters of the @ref MM_BFMER_ENABLE_REQ message.
struct mm_bfmer_enable_req
{
	/**
	 * Address of the beamforming report space allocated in host memory
	 * (Valid only if vht_su_bfmee is true)
	 */
	u32 host_bfr_addr;
	/**
	 * Size of the beamforming report space allocated in host memory. This space should
	 * be twice the maximum size of the expected beamforming reports as the FW will
	 * divide it in two in order to be able to upload a new report while another one is
	 * used in transmission
	 */
	u16 host_bfr_size;
	/// AID
	u16 aid;
	/// Station Index
	u8 sta_idx;
	/// Maximum number of spatial streams the station can receive
	u8 rx_nss;
	/**
	 * Indicate if peer STA is MU Beamformee (VHT) capable
	 * (Valid only if vht_su_bfmee is true)
	 */
	bool vht_mu_bfmee;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_NOA_REQ message.
struct mm_set_p2p_noa_req
{
	/// VIF Index
	u8 vif_index;
	/// Allocated NOA Instance Number - Valid only if count = 0
	u8 noa_inst_nb;
	/// Count
	u8 count;
	/// Indicate if NoA can be paused for traffic reason
	bool dyn_noa;
	/// Duration (in us)
	u32 duration_us;
	/// Interval (in us)
	u32 interval_us;
	/// Start Time offset from next TBTT (in us)
	u32 start_offset;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_OPPPS_REQ message.
struct mm_set_p2p_oppps_req
{
	/// VIF Index
	u8 vif_index;
	/// CTWindow
	u8 ctwindow;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_NOA_CFM message.
struct mm_set_p2p_noa_cfm
{
	/// Request status
	u8 status;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_OPPPS_CFM message.
struct mm_set_p2p_oppps_cfm
{
	/// Request status
	u8 status;
};

/// Structure containing the parameters of the @ref MM_P2P_NOA_UPD_IND message.
struct mm_p2p_noa_upd_ind
{
	/// VIF Index
	u8 vif_index;
	/// NOA Instance Number
	u8 noa_inst_nb;
	/// NoA Type
	u8 noa_type;
	/// Count
	u8 count;
	/// Duration (in us)
	u32 duration_us;
	/// Interval (in us)
	u32 interval_us;
	/// Start Time
	u32 start_time;
};

struct mm_p2p_vif_ps_change_ind
{
	/// Index of the P2P VIF that is switching its PS state
	u8 vif_index;
	/// New PS state of the P2P VIF interface (0: active, 1: sleeping)
	u8 ps_state;
};

/// Structure containing the parameters of the @ref MM_CFG_RSSI_REQ message
struct mm_cfg_rssi_req
{
	/// Index of the VIF
	u8 vif_index;
	/// RSSI threshold
	s8 rssi_thold;
	/// RSSI hysteresis
	u8 rssi_hyst;
};

/// Structure containing the parameters of the @ref MM_RSSI_STATUS_IND message
struct mm_rssi_status_ind
{
	/// Index of the VIF
	u8 vif_index;
	/// Status of the RSSI
	bool rssi_status;
	/// Current RSSI
	s8 rssi;
};

/// Structure containing the parameters of the @ref MM_PKTLOSS_IND message
struct mm_pktloss_ind
{
	/// Index of the VIF
	u8 vif_index;
	/// Address of the STA for which there is a packet loss
	struct mac_addr mac_addr;
	/// Number of packets lost
	u32 num_packets;
};

/// Structure containing the parameters of the @ref MM_CSA_FINISH_IND message
struct mm_csa_finish_ind
{
	/// Index of the VIF
	u8 vif_index;
	/// Status of the operation
	u8 status;
	/// New channel ctx index
	u8 chan_idx;
};

/// Structure containing the parameters of the @ref MM_CSA_TRAFFIC_IND message
struct mm_csa_traffic_ind
{
	/// Index of the VIF
	u8 vif_index;
	/// Is tx traffic enable or disable
	bool enable;
};

/// Structure containing the parameters of the @ref MM_MU_GROUP_UPDATE_REQ message.
/// Size allocated for the structure depends of the number of group
struct mm_mu_group_update_req
{
	/// Station index
	u8 sta_idx;
	/// Number of groups the STA belongs to
	u8 group_cnt;
	/// Group information
	struct
	{
		/// Group Id
		u8 group_id;
		/// User position
		u8 user_pos;
	} groups[0];
};

enum info_set_msg_type {
	MSG_TYPE_TX_STATICS,
	MSG_TYPE_GET_RSSI,
	MSG_TYPE_DFX_EDCA,
	MSG_TYPE_MAX_AGGR_NUM,
	MSG_TYPE_STA_TRX_STATS,
	MSG_TYPE_TX_AMPDU_DISABLE,
	MSG_TYPE_AGC_GAIN,
	MSG_TYPE_TX_IPV6_ADDR,
	MSG_TYPE_AINA_ENABLE,
	MSG_TYPE_CHAN_NOISE_INFO,
	MSG_TYPE_CHAN_UTIL_INFO,
	MSG_TYPE_AC_DELAY_TIME,
	MSG_TYPE_SYS_PWR_SHUTDOWN,
	MSG_TYPE_CRC_STATS,
	MSG_TYPE_PHY_RF_TRX_STATE,
	MSG_TYPE_PHY_SIGNAL_STATS,
	MSG_TYPE_AGC_LOCK_STATS,
	MSG_TYPE_FREQ_DC_STATE,
	MSG_TYPE_SET_HE_LTF_GI,
	MSG_TYPE_SEND_CUSTOM_CMD,
	//TODO: will not used, delete it in future
	MSG_TYPE_SET_COEX_SCENE,
	//TODO: will not used, delete it in future
	MSG_TYPE_WIFI_PTI,
	MSG_TYPE_AMSDU_CNTRL,
	MSG_TYPE_RECOVER_TEST,
	MSG_TYPE_RFCAL_CNTRL,
	MSG_TYPE_AMSDU_IN_AMPDU_ENABLE,
	MSG_TYPE_SET_HE_BCC_CNTRL,
	MSG_TYPE_RX_AMPDU_DISABLE,
	MSG_TYPE_TEST_MODE_SET,
	MSG_TYPE_MPINFO_CNTRL,
	MSG_TYPE_DEBUG_FLAG_SET,
	MSG_TYPE_CONCURRENT_ENDING,
	MSG_TYPE_SET_COEX_INFO,
	MSG_TYPE_ENABLE_PEER_TX_INFO,
};

struct mm_info_notify_req {
	u8 set;
	u8 vif_index;
	u8 msg_type;		/* refer to enum info_set_msg_type */
	u8 reserved;		/* padding for 4 bytes alignment */
	u8 info_param[32];
};

struct mm_info_notify_cfm {
	u8 msg_type;
	u8 ret_value;
	u16 result_len;
	u8 info_param[0];
};

/* FIXME: this should be replaced by mm_info_notify_cfm */
struct mm_info_notify_get_cfm {
	s8 result;		/* refer to enum mm_reg_cfm_result */
	u8 vif_index;
	u8 result_type;
	u8 result_len;
	s8 result_value[0];
};

struct mm_set_ant_req {
	u8 ant;
};

enum mm_reg_cfm_result {
	REG_CFM_FAIL = -1,
	REG_CFM_SUCC = 0,
};

struct rwnx_write_reg_cfg {
	u32 addr;
	u32 value;
};

struct rwnx_write_reg_cfm {
	s8 result;		/* refer to enum mm_reg_cfm_result */
};

struct rwnx_read_reg_cfg {
	u32 addr;
	u32 len;
};

struct rwnx_read_reg_cfm {
	s8 result;		/* refer to enum mm_reg_cfm_result */
	u32 value;
} __attribute__((packed));

struct mm_start_ip_req
{
	u8 vif_index;
	u8 ip_address[IPV4_IP_LEN];
};

struct mm_nss_update_ind
{
	u8 vif_index;
	u8 nss;
	u8 mode;
};

struct ipv6_info
{
	u8 ipv6_address[IPV6_IP_LEN];
	u8 ipv6_type;
};

struct mm_coex_info_upd
{
	u8 msg_type;
	u8 info_param[12];
};

enum ipv6_type
{
	IPV6_LINKLOCAL_ADDRESS,
	IPV6_GLOBAL_ADDRESS,
};

///MM_SET_CCA_CAP_REQ
struct mm_set_cca_req {
	u8 vif;
	u8 ena;
	/// Period of capture timer, unit in Ms.
	u16 period;
};

///MM_GET_CCA_CAP_REQ
struct mm_get_cca_req {
	u8 vif;
};

///MM_GET_CCA_CAP_CFM
struct mm_get_cca_cfm {
	u8 vif;
	///If following data valid.
	u8 valid;

	///Token in capture list.
	u16 token;
	/// Frequency of the channe]
	u16 freq;

	///Amount of period the primary channel was sensed busy, unit in Us.
	u32 busy_time;
	/// Period ofcapture timer, unit in Us.
	u32 period;
};

//* **************** MM task end ****************** *//

//* **************** SCAN task start ****************** *//
enum scan_msg_tag
{
	/// Scanning start Request.
	SCAN_START_REQ = KE_FIRST_MSG(TASK_SCAN),
	/// Scanning start Confirmation.
	SCAN_START_CFM,
	/// End of scanning indication.
	SCAN_DONE_IND,
	/// Cancel scan request
	SCAN_CANCEL_REQ,
	/// Cancel scan confirmation
	SCAN_CANCEL_CFM,

	/// MAX number of messages
	SCAN_MAX,
};

/// Maximum number of SSIDs in a scan request
#define SCAN_SSID_MAX	2

/// Maximum number of channels in a scan request
#define SCAN_CHANNEL_MAX (MAC_DOMAINCHANNEL_24G_MAX + MAC_DOMAINCHANNEL_5G_MAX)

/// Structure containing the parameters of the @ref SCAN_START_REQ message
struct scan_start_req
{
	/// List of channel to be scanned
	struct mac_chan_def chan[SCAN_CHANNEL_MAX];
	/// List of SSIDs to be scanned
	struct mac_ssid ssid[SCAN_SSID_MAX];
	/// BSSID to be scanned
	struct mac_addr bssid;
	/// Pointer (in host memory) to the additional IEs that need to be added to the ProbeReq
	/// (following the SSID element)
	u32 add_ies;
	/// Length of the additional IEs
	u16 add_ie_len;
	/// Index of the VIF that is scanning
	u8 vif_idx;
	/// Number of channels to scan
	u8 chan_cnt;
	/// Number of SSIDs to scan for
	u8 ssid_cnt;
	/// no CCK - For P2P frames not being sent at CCK rate in 2GHz band.
	bool no_cck;
	/// Scan duration, in us
	u32 duration;
};

/// Structure containing the parameters of the @ref SCAN_START_CFM message
struct scan_start_cfm
{
	/// Status of the request
	u8 status;
};

/// Structure containing the parameters of the @ref SCAN_CANCEL_REQ message
struct scan_cancel_req
{
};

/// Structure containing the parameters of the @ref SCAN_START_CFM message
struct scan_cancel_cfm
{
	/// Status of the request
	u8 status;
};

struct abort_scan_req
{
	///Index of the VIF that abort scan task
	u8 vif_idx;
        bool host_abort_flag;
};

//* **************** SCAN task end ****************** *//

//* **************** SCANU task start ****************** *//

/// Maximum length of the additional ProbeReq IEs (FullMAC mode)
#define SCANU_MAX_IE_LEN	200

#define MATCH_SET_MAX		2
#define SCHED_SCAN_PLAN_MAX	2

/// Messages that are logically related to the task.
enum
{
	/// Scan request from host.
	SCANU_START_REQ = KE_FIRST_MSG(TASK_SCANU),
	/// Scanning start Confirmation.
	SCANU_START_CFM,
	/// Join request
	SCANU_JOIN_REQ,
	/// Join confirmation.
	SCANU_JOIN_CFM,
	/// Scan result indication.
	SCANU_RESULT_IND,
	/// Fast scan request from any other module.
	SCANU_GET_SCAN_RESULT_REQ,
	/// Confirmation of fast scan request.
	SCANU_GET_SCAN_RESULT_CFM,
        /// Abort scan request.
	SCANU_ABORT_REQ,
	/// Abort scan confirmation.
	SCANU_ABORT_CFM,
	/// MAX number of messages
	SCANU_MAX,
};

///Sched scan match_set def
struct mac_match_set
{
	/// SSID to be matched
	struct mac_ssid ssid;
	/// BSSID to be matched
	u8 match_bssid[ETH_ALEN];

	s32 rssi_thold;
};

///Sched scan plan def
struct mac_sched_scan_plan
{
	///interval between sched scan iterations. In seconds.
	u32 interval;
	/// num of scan iterations in this scan plan.
	u32 iterations;
};

/// Structure containing the parameters of the @ref SCAN_SCHED_SCAN_START_REQ and
/// @ref SCHED_SCAN_START_REQ messages
struct sched_scan_parameter
{
	///The req id
	u32 reqid;
	///MATCH SETS
	u8 match_set_cnt;
	struct mac_match_set match_set[MATCH_SET_MAX];
	///SCAN PLANS
	u8 scan_plan_cnt;
	struct mac_sched_scan_plan scan_plan[SCHED_SCAN_PLAN_MAX];
};

/// Structure containing the parameters of the @ref SCANU_START_REQ message
struct scanu_start_req
{
	/// List of channel to be scanned
	struct mac_chan_def chan[SCAN_CHANNEL_MAX];
	/// List of SSIDs to be scanned
	struct mac_ssid ssid[SCAN_SSID_MAX];
	/// BSSID to be scanned (or WILDCARD BSSID if no BSSID is searched in particular)
	struct mac_addr bssid;
	/// Address (in host memory) of the additional IEs that need to be added to the ProbeReq
	/// (following the SSID element)
	/* this field is obsoleted, always store ies in the following ies[] */
	u32 add_ies;
	/// Length of the additional IEs
	u16 add_ie_len;
	/// Index of the VIF that is scanning
	u8 vif_idx;
	/// Number of channels to scan
	u8 chan_cnt;
	/// Number of SSIDs to scan for
	u8 ssid_cnt;
	/// no CCK - For P2P frames not being sent at CCK rate in 2GHz band.
	bool no_cck;
	/// Scan duration, in us
	u32 duration;
	// Payload for add_ie
	u8 ies[SCANU_MAX_IE_LEN];

	struct sched_scan_parameter sscan_param;
};

/// Structure containing the parameters of the @ref SCANU_START_CFM message
struct scanu_start_cfm
{
	/// Index of the VIF that was scanning
	u8 vif_idx;
	/// Status of the request
	u8 status;
	/// Number of scan results available
	u8 result_cnt;
};

/// Parameters of the @SCANU_RESULT_IND message
struct scanu_result_ind
{
	/// Length of the frame
	u16 length;
	/// Frame control field of the frame.
	u16 framectrl;
	/// Center frequency on which we received the packet
	u16 center_freq;
	/// PHY band
	u8 band;
	/// Index of the station that sent the frame. 0xFF if unknown.
	u8 sta_idx;
	/// Index of the VIF that received the frame. 0xFF if unknown.
	u8 inst_nbr;
	/// RSSI of the received frame.
	s8 rssi;
	/// Frame payload.
	u32 payload[];
};

//* **************** SCANU task end ****************** *//

//* **************** ME task start ****************** *//
/// Messages that are logically related to the task.
enum
{
	/// Configuration request from host.
	ME_CONFIG_REQ = KE_FIRST_MSG(TASK_ME),
	/// Configuration confirmation.
	ME_CONFIG_CFM,
	/// Configuration request from host.
	ME_CHAN_CONFIG_REQ,
	/// Configuration confirmation.
	ME_CHAN_CONFIG_CFM,
	/// Set control port state for a station.
	ME_SET_CONTROL_PORT_REQ,
	/// Control port setting confirmation.
	ME_SET_CONTROL_PORT_CFM,
	/// TKIP MIC failure indication.
	ME_TKIP_MIC_FAILURE_IND,
	/// Add a station to the FW (AP mode)
	ME_STA_ADD_REQ,
	/// Confirmation of the STA addition
	ME_STA_ADD_CFM,
	/// Delete a station from the FW (AP mode)
	ME_STA_DEL_REQ,
	/// Confirmation of the STA deletion
	ME_STA_DEL_CFM,
	/// Indication of a TX RA/TID queue credit update
	ME_TX_CREDITS_UPDATE_IND,
	/// Request indicating to the FW that there is traffic buffered on host
	ME_TRAFFIC_IND_REQ,
	/// Confirmation that the @ref ME_TRAFFIC_IND_REQ has been executed
	ME_TRAFFIC_IND_CFM,
	/// Request of RC statistics to a station
	ME_RC_STATS_REQ,
	/// RC statistics confirmation
	ME_RC_STATS_CFM,
	/// RC fixed rate request
	ME_RC_SET_RATE_REQ,
	/// Configure monitor interface
	ME_CONFIG_MONITOR_REQ,
	/// Configure monitor interface response
	ME_CONFIG_MONITOR_CFM,
	/// Setting power Save mode request from host
	ME_SET_PS_MODE_REQ,
	/// Set power Save mode confirmation
	ME_SET_PS_MODE_CFM,

	/*
	 * Section of internal ME messages. No ME API messages should be defined below this point
	 */
	/// Internal request to indicate that a VIF needs to get the HW going to ACTIVE or IDLE
	ME_SET_ACTIVE_REQ,
	/// Confirmation that the switch to ACTIVE or IDLE has been executed
	ME_SET_ACTIVE_CFM,
	/// Internal request to indicate that a VIF desires to de-activate/activate the Power Save mode
	ME_SET_PS_DISABLE_REQ,
	/// Confirmation that the PS state de-activate/activate has been executed
	ME_SET_PS_DISABLE_CFM,

	/// TX credit size request from host
	ME_TX_CREDIT_SIZE_REQ,
	/// TX credit size confirmation
	ME_TX_CREDIT_SIZE_CFM,

	/// Setting bus power state from host
	ME_SET_BUS_PWR_STATE_REQ,
	/// Set bus power state confirmation
	ME_SET_BUS_PWR_STATE_CFM,
	/// Set WOWLAN Request
	ME_SET_WOWLAN_REQ,
	/// Set WOWLAN Confirmation
	ME_SET_WOWLAN_CFM,

	/// Host data ring free request
	ME_FREE_HOST_DATA_RING_REQ,
	/// Host data ring free confirmation
	ME_FREE_HOST_DATA_RING_CFM,

	/// Set secure param request
	ME_SET_SECURE_PARAM_REQ,
	/// Set secure param confirmation
	ME_SET_SECURE_PARAM_CFM,

	/// Set rekey data request
	ME_SET_REKEY_DATA_REQ,
	/// Set rekey data confirmation
	ME_SET_REKEY_DATA_CFM,

	// set txq ring addr request
	ME_SET_TXQ_RING_ADDR_REQ,
	// set txq ring addr confirmation
	ME_SET_TXQ_RING_ADDR_CFM,

	/// Host resume indication
	ME_WOW_RESUME_IND,

	/// MAX number of messages
	ME_MAX,
};

enum {
	// set host data ring request
	ME_EXTEND_SET_HOST_DATA_RING_REQ = ME_WOW_RESUME_IND + 1,
	// set host data ring confimation
	ME_EXTEND_SET_HOST_DATA_RING_CFM,

	// Free host data ring immediatelly indication
	ME_EXTEND_FREE_HOST_DATA_RING_NOW_IND,

	// set usb param request
	ME_EXTEND_SET_USB_PARAM_REQ,
	// set usb param confimation
	ME_EXTEND_SET_USB_PARAM_CFM,

	// force pcie link speed request
	ME_EXTEND_FORCE_PCIE_LINK_SPEED_REQ,
	// force pcie link speed confimation
	ME_EXTEND_FORCE_PCIE_LINK_SPEED_CFM,

	/// MAX number of extern messages
	ME_EXTEND_MAX,
};

/// Structure containing the parameters of the @ref ME_START_REQ message
struct me_config_req
{
	/// HT Capabilities
	struct mac_htcapability ht_cap;
	/// VHT Capabilities
	struct mac_vhtcapability vht_cap;
	/// HE capabilities
	struct mac_hecapability he_cap;
	/// Lifetime of packets sent under a BlockAck agreement (expressed in TUs)
	u16 tx_lft;
	/// Maximum supported BW
	u8 phy_bw_max;
	/// Boolean indicating if HT is supported or not
	bool ht_supp;
	/// Boolean indicating if VHT is supported or not
	bool vht_supp;
	/// Boolean indicating if HE is supported or not
	bool he_supp;
	/// Boolean indicating if HE OFDMA UL is enabled or not
	bool he_ul_on;
	/// Boolean indicating if PS mode shall be enabled or not
	bool ps_on;
	/// Boolean indicating if Antenna Diversity shall be enabled or not
	bool ant_div_on;
	/// Boolean indicating if Dynamic PS mode shall be used or not
	bool dpsm;
};

/// Structure containing the parameters of the @ref ME_CHAN_CONFIG_REQ message
struct me_chan_config_req
{
	/// List of 2.4GHz supported channels
	struct mac_chan_def chan2G4[MAC_DOMAINCHANNEL_24G_MAX];
	/// List of 5GHz supported channels
	struct mac_chan_def chan5G[MAC_DOMAINCHANNEL_5G_MAX];
	/// Number of 2.4GHz channels in the list
	u8 chan2G4_cnt;
	/// Number of 5GHz channels in the list
	u8 chan5G_cnt;
};

/// structure containing the parameters of the @ref ME RC SET RATE REQ message.
struct me_rc_set_rate_req
{
	u8 vif_idx:4,
		band:4;
	/// Index of the station for which the fixed rate is reguested.
	u8 sta_idx;
	/// Fixed rate configuration.
	u16 fixed_rate_cfg;
};

/// Structure containing the parameters of the @ref ME_SET_CONTROL_PORT_REQ message
struct me_set_control_port_req
{
	/// Index of the station for which the control port is opened
	u8 sta_idx;
	/// Control port state
	bool control_port_open;
};

/// Structure containing the parameters of the @ref ME_TKIP_MIC_FAILURE_IND message
struct me_tkip_mic_failure_ind
{
	/// Address of the sending STA
	struct mac_addr addr;
	/// TSC value
	u64 tsc;
	/// Boolean indicating if the packet was a group or unicast one (true if group)
	bool ga;
	/// Key Id
	u8 keyid;
	/// VIF index
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref ME_STA_ADD_REQ message
struct me_sta_add_req
{
	/// MAC address of the station to be added
	struct mac_addr mac_addr;
	/// Supported legacy rates
	struct mac_rateset rate_set;
	/// HT Capabilities
	struct mac_htcapability ht_cap;
	/// VHT Capabilities
	struct mac_vhtcapability vht_cap;
	/// HE capabilities
	struct mac_hecapability he_cap;
	/// Flags giving additional information about the station (@ref mac_sta_flags)
	u32 flags;
	/// Association ID of the station
	u16 aid;
	/// Bit field indicating which queues have U-APSD enabled
	u8 uapsd_queues;
	/// Maximum size, in frames, of a APSD service period
	u8 max_sp_len;
	/// Operation mode information (valid if bit @ref STA_OPMOD_NOTIF is
	/// set in the flags)
	u8 opmode;
	/// Index of the VIF the station is attached to
	u8 vif_idx;
	/// Whether the the station is TDLS station
	bool tdls_sta;
	/// Indicate if the station is TDLS link initiator station
	bool tdls_initiator;
	/// Indicate if the TDLS Channel Switch is allowed
	bool tdls_chsw_allowed;
};

/// Structure containing the parameters of the @ref ME_STA_ADD_CFM message
struct me_sta_add_cfm
{
	/// Station index
	u8 sta_idx;
	/// Status of the station addition
	u8 status;
	/// PM state of the station
	u8 pm_state;
};

/// Structure containing the parameters of the @ref ME_STA_DEL_REQ message.
struct me_sta_del_req
{
	/// Index of the station to be deleted
	u8 sta_idx;
	/// Whether the the station is TDLS station
	bool tdls_sta;
};

/// Structure containing the parameters of the @ref ME_TX_CREDITS_UPDATE_IND message.
struct me_tx_credits_update_ind
{
	/// Index of the station for which the credits are updated
	u8 sta_idx;
	/// TID for which the credits are updated
	u8 tid;
	/// Offset to be applied on the credit count
	s8 credits;
};

/// Structure containing the parameters of the @ref ME_TRAFFIC_IND_REQ message.
struct me_traffic_ind_req
{
	/// Index of the station for which UAPSD traffic is available on host
	u8 sta_idx;
	/// Flag indicating the availability of UAPSD packets for the given STA
	u8 tx_avail;
	/// Indicate if traffic is on uapsd-enabled queues
	bool uapsd;
};

/// Structure containing the parameters of the @ref ME_RC_STATS_REQ message.
struct me_rc_stats_req
{
	/// Index of the station for which the RC statistics are requested
	u8 sta_idx;
};

/// Structure containing the rate control statistics
struct rc_rate_stats
{
	/// Number of attempts (per sampling interval)
	u32 attempts;
	/// Number of success (per sampling interval)
	u32 success;
	/// Estimated probability of success (EWMA)
	u16 probability;
	/// Rate configuration of the sample
	u16 rate_config;
	union
	{
		struct {
			/// Number of times the sample has been skipped (per sampling interval)
			u8 sample_skipped;
			/// Whether the old probability is available
			bool old_prob_available;
			/// Whether the rate can be used in the retry chain
			bool rate_allowed;
		};
		struct {
			/// RU size and UL length received in the latest HE trigger frame
			u16 ru_and_length;
		};
	};
};

/// Number of RC samples
#define RC_MAX_N_SAMPLE 10

/// Structure containing the parameters of the @ref ME_RC_STATS_CFM message.
struct me_rc_stats_cfm
{
	/// Index of the station for which the RC statistics are provided
	u8 sta_idx;
	/// Number of samples used in the RC algorithm
	u16 no_samples;
	/// Number of MPDUs transmitted (per sampling interval)
	u16 ampdu_len;
	/// Number of AMPDUs transmitted (per sampling interval)
	u16 ampdu_packets;
	/// Average number of MPDUs in each AMPDU frame (EWMA)
	u32 avg_ampdu_len;
	// Current step 0 of the retry chain
	u8 sw_retry_step;
	/// Trial transmission period
	u8 sample_wait;
	/// Retry chain steps
	u16 retry_step_idx[4];
	/// RC statistics - Max number of RC samples, plus one for the HE TB statistics
	struct rc_rate_stats rate_stats[RC_MAX_N_SAMPLE + 1];
	/// Throughput - Max number of RC samples, plus one for the HE TB statistics
	u32 tp[RC_MAX_N_SAMPLE + 1];
};

/// Structure containing the parameters of the @ref ME_CONFIG_MONITOR_REQ message.
struct me_config_monitor_req
{
	/// Channel to configure
	struct mac_chan_op chan;
	/// Is channel data valid
	bool chan_set;
	/// Enable report of unsupported HT frames
	bool uf;
};

/// Structure containing the parameters of the @ref ME_CONFIG_MONITOR_CFM message.
struct me_config_monitor_cfm
{
	/// Channel context index
	u8 chan_index;
	/// Channel parameters
	struct mac_chan_op chan;
};

/// Structure containing the parameters of the @ref ME_SET_PS_MODE_REQ message.
struct me_set_ps_mode_req
{
	/// Power Save is activated or deactivated
	u8 ps_state;
};

/// Structure containing the parameters of the @ref ME_TX_CREDIT_SIZE_CFM message.
struct me_tx_credit_size_cfm
{
	/// Total TX credit size
	u8 tx_credit_size;
};

/// Structure containing the parameters of the @ref ME_SET_BUS_PWR_STATE_REQ message.
struct me_set_bus_pwr_state_req
{
	/// Bus power state: resume(0), suspend(1)
	u8 pwr_state;
};

/// Structure containing the parameters of the @ref ME_SET_WOWLAN_REQ message.
struct me_set_wowlan_req
{
	u32 wakeup_type;
	void *ext;
};

struct me_extend_set_host_data_ring_req
{
	// 0: ROM 1.0, ROM 1.1
	u32 ver;
	// RAE LL mode host buf ring addr
	u32 host_buf_ring_addr[2];
	// RAE LL mode host buf ring size
	u32 host_buf_ring_sz[2];
	// RAE LL mode backup host buf ring addr
	u32 backup_host_buf_ring_addr[2];
	// RAE LL mode backup host buf ring size
	u32 backup_host_buf_ring_sz[2];
};

struct me_extend_set_host_data_ring_cfm
{
	bool rx_ll_support;
};

struct me_extend_free_host_data_ring_now_ind
{
	uint32_t last_buf_idx[2];
	uint32_t is_backup_ring : 1,
		 reserved : 31;
};

struct me_extend_set_usb_param_req
{
	// Maximum size of bundle supported in reception
	u32 max_bundle_size;
	u32 reserved;
};

//* **************** ME task end ****************** *//

//* **************** TWT task start ****************** *//
/// Message API of the TWT task
enum
{
	/// Request Individual TWT Establishment
	TWT_SETUP_REQ = KE_FIRST_MSG(TASK_TWT),
	/// Confirm Individual TWT Establishment
	TWT_SETUP_CFM,
	/// Indicate TWT Setup response from peer
	TWT_SETUP_IND,
	/// Request to destroy a TWT Establishment or all of them
	TWT_TEARDOWN_REQ,
	/// Confirm to destroy a TWT Establishment or all of them
	TWT_TEARDOWN_CFM,

	/// MAX number of messages
	TWT_MAX,
};

/// TWT Setup command
enum twt_setup_types
{
	MAC_TWT_SETUP_REQUEST = 0,
	MAC_TWT_SETUP_SUGGEST,
	MAC_TWT_SETUP_DEMAND,
	MAC_TWT_SETUP_GROUPING,
	MAC_TWT_SETUP_ACCEPT,
	MAC_TWT_SETUP_ALTERNATE,
	MAC_TWT_SETUP_DICTATE,
	MAC_TWT_SETUP_REJECT,
};

///TWT Flow configuration
struct twt_conf_tag
{
	/// Flow Type (0: Announced, 1: Unannounced)
	u8 flow_type;
	/// Wake interval Exponent
	u8 wake_int_exp;
	/// Unit of measurement of TWT Minimum Wake Duration (0:256us, 1:tu)
	bool wake_dur_unit;
	/// Nominal Minimum TWT Wake Duration
	u8 min_twt_wake_dur;
	/// TWT Wake Interval Mantissa
	u16 wake_int_mantissa;
};

///TWT Setup request message structure
struct twt_setup_req
{
	/// VIF Index
	u8 vif_idx;
	/// Setup request type
	u8 setup_type;
	/// TWT Setup configuration
	struct twt_conf_tag conf;
};

///TWT Setup confirmation message structure
struct twt_setup_cfm
{
	/// Status (0 = TWT Setup Request has been transmitted to peer)
	u8 status;
};

///TWT Setup indication message structure
struct twt_setup_ind
{
	/// Response type
	u8 resp_type;
	/// STA Index
	u8 sta_idx;
	/// TWT Setup configuration
	struct twt_conf_tag conf;
};

/// TWT Teardown request message structure
struct twt_teardown_req
{
	/// TWT Negotiation type
	u8 neg_type;
	/// All TWT
	u8 all_twt;
	/// TWT flow ID
	u8 id;
	/// VIF Index
	u8 vif_idx;
};

///TWT Teardown confirmation message structure
struct twt_teardown_cfm
{
	/// Status (0 = TWT Teardown Request has been transmitted to peer)
	u8 status;
};

//* **************** TWT task end ****************** *//

//* **************** SM task start ****************** *//
/// Message API of the SM task
enum sm_msg_tag
{
	/// Request to connect to an AP
	SM_CONNECT_REQ = KE_FIRST_MSG(TASK_SM),
	/// Confirmation of connection
	SM_CONNECT_CFM,
	/// Indicates that the SM associated to the AP
	SM_CONNECT_IND,
	/// Request to disconnect
	SM_DISCONNECT_REQ,
	/// Confirmation of disconnection
	SM_DISCONNECT_CFM,
	/// Indicates that the SM disassociated the AP
	SM_DISCONNECT_IND,
	/// Request to start external authentication
	SM_EXTERNAL_AUTH_REQUIRED_IND,
	/// Response to external authentication request
	SM_EXTERNAL_AUTH_REQUIRED_RSP,
	/// Request to update assoc elements after FT over the air authentication
	SM_FT_AUTH_IND,
	/// Response to FT authentication with updated assoc elements
	SM_FT_AUTH_RSP,

	/// MAX number of messages
	SM_MAX,
};

enum sm_msg_ext_tag
{
	SM_CONNECT_EXT_IND = SM_MAX + 1, /// SM_MAX alredy used for sm inner msg
	SM_EXT_MAX,
};

/// Structure containing the parameters of @ref SM_CONNECT_REQ and SM_FT_AUTH_RSP message.
struct sm_connect_req
{
	/// SSID to connect to
	struct mac_ssid ssid;
	/// BSSID to connect to (if not specified, set this field to WILDCARD BSSID)
	struct mac_addr bssid;
	/// Channel on which we have to connect (if not specified, set -1 in the chan.freq field)
	struct mac_chan_def chan;
	/// Connection flags (see @ref mac_connection_flags)
	u32 flags;
	/// Control port Ethertype (in network endianness)
	u16 ctrl_port_ethertype;
	/// Listen interval to be used for this connection
	u16 listen_interval;
	/// Flag indicating if the we have to wait for the BC/MC traffic after beacon or not
	bool dont_wait_bcmc;
	/// Authentication type
	u8 auth_type;
	/// UAPSD queues (bit0: VO, bit1: VI, bit2: BE, bit3: BK)
	u8 uapsd_queues;
	/// VIF index
	u8 vif_idx;
	/// Length of the association request IEs
	u16 ie_len;
	/// Buffer containing the additional information elements to be put in the
	/// association request
	u32 ie_buf[0];
};

/// Structure containing the parameters of the @ref SM_CONNECT_CFM message.
struct sm_connect_cfm
{
	/// Status. If 0, it means that the connection procedure will be performed and that
	/// a subsequent @ref SM_CONNECT_IND message will be forwarded once the procedure is
	/// completed
	u8 status;
};

#define SM_ASSOC_IE_LEN		800
/// Structure containing the parameters of the @ref SM_CONNECT_IND message.
struct sm_connect_ind
{
	/// Status code of the connection procedure
	u16 status_code;
	/// BSSID
	struct mac_addr bssid;
	/// Flag indicating if the indication refers to an internal roaming or from a host request
	bool roamed;
	/// Index of the VIF for which the association process is complete
	u8 vif_idx;
	/// Index of the STA entry allocated for the AP
	u8 ap_idx;
	/// Index of the LMAC channel context the connection is attached to
	u8 ch_idx;
	/// Flag indicating if the AP is supporting QoS
	bool qos;
	/// ACM bits set in the AP WMM parameter element
	u8 acm;
	/// Length of the AssocReq IEs
	u16 assoc_req_ie_len;
	/// Length of the AssocRsp IEs
	u16 assoc_rsp_ie_len;
	/// Association Id allocated by the AP for this connection
	u16 aid;
	/// AP operating channel
	struct mac_chan_op chan;
	/// EDCA parameters
	u32 ac_param[AC_MAX];
	/// IE buffer
	u32 assoc_ie_buf[0];
};

/// Structure containing the parameters of the @ref SM_CONNECT_EXT_IND message.
struct sm_connect_ext_ind
{
    /// Indicate dbdc is enabled
	u8 dbdc_chan_enabled;
    /// Index of DBDC mode
	u8 dbdc_vif_idx;
    /// Index of mac0 role
	u8 mac0_vif_idx;
    /// Channel on DBDC mode
	struct mac_chan_op dbdc_chan;
    /// Channel on mac0 mode
	struct mac_chan_op mac0_chan;
};

/// Structure containing the parameters of the @ref SM_DISCONNECT_REQ message.
struct sm_disconnect_req
{
	/// Reason of the deauthentication.
	u16 reason_code;
	/// Index of the VIF.
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref SM_DISCONNECT_IND message.
struct sm_disconnect_ind
{
	/// Reason of the disconnection.
	u16 reason_code;
	/// Index of the VIF.
	u8 vif_idx;
	/// Disconnection happen before a re-association
	bool reassoc;
};

/// Structure containing the parameters of the @ref SM_EXTERNAL_AUTH_REQUIRED_IND
struct sm_external_auth_required_ind
{
	/// Index of the VIF.
	u8 vif_idx;
	/// SSID to authenticate to
	struct mac_ssid ssid;
	/// BSSID to authenticate to
	struct mac_addr bssid;
	/// AKM suite of the respective authentication
	u32 akm;
	u8 bit_hml_external_auth_req : 1,
	   bit_reserved : 7;
};

/// Structure containing the parameters of the @ref SM_EXTERNAL_AUTH_REQUIRED_RSP
struct sm_external_auth_required_rsp
{
	/// Index of the VIF.
	u8 vif_idx;
	/// Authentication status
	u16 status;
};

/// Structure containing the parameters of the @ref SM_FT_AUTH_IND
struct sm_ft_auth_ind
{
	/// Index of the VIF.
	u8 vif_idx;
	/// Size of the FT elements
	u16 ft_ie_len;
	/// Fast Transition elements in the authentication
	u32 ft_ie_buf[0];
};

//* **************** SM task end ****************** *//

//* **************** APM task start ****************** *//
/// Message API of the APM task
enum apm_msg_tag
{
	/// Request to start the AP.
	APM_START_REQ = KE_FIRST_MSG(TASK_APM),
	/// Confirmation of the AP start.
	APM_START_CFM,
	/// Request to stop the AP.
	APM_STOP_REQ,
	/// Confirmation of the AP stop.
	APM_STOP_CFM,
	/// Request to start CAC
	APM_START_CAC_REQ,
	/// Confirmation of the CAC start
	APM_START_CAC_CFM,
	/// Request to stop CAC
	APM_STOP_CAC_REQ,
	/// Confirmation of the CAC stop
	APM_STOP_CAC_CFM,
	/// Request to Probe Client
	APM_PROBE_CLIENT_REQ,
	/// Confirmation of Probe Client
	APM_PROBE_CLIENT_CFM,
	/// Indication of Probe Client status
	APM_PROBE_CLIENT_IND,

	/// MAX number of messages
	APM_MAX,
};

/// Structure containing the parameters of the @ref APM_START_REQ message.
struct apm_start_req
{
	/// Basic rate set
	struct mac_rateset basic_rates;
	/// Operating channel on which we have to enable the AP
	struct mac_chan_op chan;
	/// Address, in host memory, to the beacon template
	//u32 bcn_addr;
	/// Length of the beacon template
	u16 bcn_len;
	/// Offset of the TIM IE in the beacon
	u16 tim_oft;
	/// Beacon interval
	u16 bcn_int;
	/// Flags (@ref mac_connection_flags)
	u32 flags;
	/// Control port Ethertype
	u16 ctrl_port_ethertype;
	/// Length of the TIM IE
	u8 tim_len;
	/// Index of the VIF for which the AP is started
	u8 vif_idx;

	u8 bcn_buf[384];
};

/// Structure containing the parameters of the @ref APM_STOP_REQ message.
struct apm_stop_req
{
	/// Index of the VIF for which the AP has to be stopped
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref APM_START_CFM message.
struct apm_start_cfm
{
	/// Status of the AP starting procedure
	u8 status;
	/// Index of the VIF for which the AP is started
	u8 vif_idx;
	/// Index of the channel context attached to the VIF
	u8 ch_idx;
	/// Index of the STA used for BC/MC traffic
	u8 bcmc_idx;
};

/// Structure containing the parameters of the @ref APM_START_CAC_REQ message.
struct apm_start_cac_req
{
	/// Channel configuration
	struct mac_chan_op chan;
	/// Index of the VIF for which the CAC is started
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref APM_START_CAC_CFM message.
struct apm_start_cac_cfm
{
	/// Status of the CAC starting procedure
	u8 status;
	/// Index of the channel context attached to the VIF for CAC
	u8 ch_idx;
};

/// Structure containing the parameters of the @ref APM_STOP_CAC_REQ message.
struct apm_stop_cac_req
{
	/// Index of the VIF for which the CAC has to be stopped
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref APM_PROBE_CLIENT_REQ message.
struct apm_probe_client_req
{
	/// Index of the VIF
	u8 vif_idx;
	/// Index of the Station to probe
	u8 sta_idx;
};

/// Structure containing the parameters of the @ref APM_PROBE_CLIENT_CFM message.
struct apm_probe_client_cfm
{
	/// Status of the probe request
	u8 status;
	/// Unique ID to distinguish @ref APM_PROBE_CLIENT_IND message
	u32 probe_id;
};

/// Structure containing the parameters of the @ref APM_PROBE_CLIENT_CFM message.
struct apm_probe_client_ind
{
	/// Index of the VIF
	u8 vif_idx;
	/// Index of the Station to probe
	u8 sta_idx;
	/// Whether client is still present or not
	bool client_present;
	/// Unique ID as returned in @ref APM_PROBE_CLIENT_CFM
	u32 probe_id;
};

//* **************** APM task end ****************** *//

//* **************** MESH task start ****************** *//
/// Message API of the MESH task
enum mesh_msg_tag
{
	/// Request to start the MP
	MESH_START_REQ = KE_FIRST_MSG(TASK_MESH),
	/// Confirmation of the MP start.
	MESH_START_CFM,

	/// Request to stop the MP.
	MESH_STOP_REQ,
	/// Confirmation of the MP stop.
	MESH_STOP_CFM,

	// Request to update the MP
	MESH_UPDATE_REQ,
	/// Confirmation of the MP update
	MESH_UPDATE_CFM,

	/// Request information about a given link
	MESH_PEER_INFO_REQ,
	/// Response to the MESH_PEER_INFO_REQ message
	MESH_PEER_INFO_CFM,

	/// Request automatic establishment of a path with a given mesh STA
	MESH_PATH_CREATE_REQ,
	/// Confirmation to the MESH_PATH_CREATE_REQ message
	MESH_PATH_CREATE_CFM,

	/// Request a path update (delete path, modify next hop mesh STA)
	MESH_PATH_UPDATE_REQ,
	/// Confirmation to the MESH_PATH_UPDATE_REQ message
	MESH_PATH_UPDATE_CFM,

	/// Indication from Host that the indicated Mesh Interface is a proxy for an external STA
	MESH_PROXY_ADD_REQ,

	/// Indicate that a connection has been established or lost
	MESH_PEER_UPDATE_IND,
	/// Notification that a connection has been established or lost (when MPM handled by userspace)
	MESH_PEER_UPDATE_NTF = MESH_PEER_UPDATE_IND,

	/// Indicate that a path is now active or inactive
	MESH_PATH_UPDATE_IND,
	/// Indicate that proxy information have been updated
	MESH_PROXY_UPDATE_IND,

	/// MAX number of messages
	MESH_MAX,
};

/// Bit fields for mesh_update_req message's flags value
enum mesh_update_flags_bit
{
	/// Root Mode
	MESH_UPDATE_FLAGS_ROOT_MODE_BIT = 0,
	/// Gate Mode
	MESH_UPDATE_FLAGS_GATE_MODE_BIT,
	/// Mesh Forwarding
	MESH_UPDATE_FLAGS_MESH_FWD_BIT,
	/// Local Power Save Mode
	MESH_UPDATE_FLAGS_LOCAL_PSM_BIT,
};

/// Maximum length of the Mesh ID
#define MESH_MESHID_MAX_LEN	(32)

/// Structure containing the parameters of the @ref MESH_START_REQ message.
struct mesh_start_req
{
	/// Basic rate set
	struct mac_rateset basic_rates;
	/// Operating channel on which we have to enable the AP
	struct mac_chan_op chan;
	/// DTIM Period
	u8 dtim_period;
	/// Beacon Interval
	u16 bcn_int;
	/// Index of the VIF for which the MP is started
	u8 vif_index;
	/// Length of the Mesh ID
	u8 mesh_id_len;
	/// Mesh ID
	u8 mesh_id[MESH_MESHID_MAX_LEN];
	/// Address of the IEs to download
	u32 ie_addr;
	/// Length of the provided IEs
	u8 ie_len;
	/// Indicate if Mesh Peering Management (MPM) protocol is handled in userspace
	bool user_mpm;
	/// Indicate if Mesh Point is using authentication
	bool is_auth;
	/// Indicate which authentication method is used
	u8 auth_id;
};

/// Structure containing the parameters of the @ref MESH_START_CFM message.
struct mesh_start_cfm
{
	/// Status of the MP starting procedure
	u8 status;
	/// Index of the VIF for which the MP is started
	u8 vif_idx;
	/// Index of the channel context attached to the VIF
	u8 ch_idx;
	/// Index of the STA used for BC/MC traffic
	u8 bcmc_idx;
};

/// Structure containing the parameters of the @ref MESH_STOP_REQ message.
struct mesh_stop_req
{
	/// Index of the VIF for which the MP has to be stopped
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref MESH_STOP_CFM message.
struct mesh_stop_cfm
{
	/// Index of the VIF for which the MP has to be stopped
	u8 vif_idx;
	/// Status
	u8 status;
};

/// Structure containing the parameters of the @ref MESH_UPDATE_REQ message.
struct mesh_update_req
{
	/// Flags, indicate fields which have been updated
	u8 flags;
	/// VIF Index
	u8 vif_idx;
	/// Root Mode
	u8 root_mode;
	/// Gate Announcement
	bool gate_announ;
	/// Mesh Forwarding
	bool mesh_forward;
	/// Local PS Mode
	u8 local_ps_mode;
};

/// Structure containing the parameters of the @ref MESH_UPDATE_CFM message.
struct mesh_update_cfm
{
	/// Status
	u8 status;
};

/// Structure containing the parameters of the @ref MESH_PEER_INFO_REQ message.
struct mesh_peer_info_req
{
	///Index of the station allocated for the peer
	u8 sta_idx;
};

/// Structure containing the parameters of the @ref MESH_PEER_INFO_CFM message.
struct mesh_peer_info_cfm
{
	/// Response status
	u8 status;
	/// Index of the station allocated for the peer
	u8 sta_idx;
	/// Local Link ID
	u16 local_link_id;
	/// Peer Link ID
	u16 peer_link_id;
	/// Local PS Mode
	u8 local_ps_mode;
	/// Peer PS Mode
	u8 peer_ps_mode;
	/// Non-peer PS Mode
	u8 non_peer_ps_mode;
	/// Link State
	u8 link_state;
	/// Time elapsed since last received beacon (in us)
	u32 last_bcn_age;
};

/// Structure containing the parameters of the @ref MESH_PATH_CREATE_REQ message.
struct mesh_path_create_req
{
	/// Index of the interface on which path has to be created
	u8 vif_idx;
	/// Indicate if originator MAC Address is provided
	bool has_orig_addr;
	/// Path Target MAC Address
	struct mac_addr tgt_mac_addr;
	/// Originator MAC Address
	struct mac_addr orig_mac_addr;
};

/// Structure containing the parameters of the @ref MESH_PATH_CREATE_CFM message.
struct mesh_path_create_cfm
{
	/// Confirmation status
	u8 status;
	/// VIF Index
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref MESH_PATH_UPDATE_REQ message.
struct mesh_path_update_req
{
	/// Indicate if path must be deleted
	bool delete;
	/// Index of the interface on which path has to be created
	u8 vif_idx;
	/// Path Target MAC Address
	struct mac_addr tgt_mac_addr;
	/// Next Hop MAC Address
	struct mac_addr nhop_mac_addr;
};

/// Structure containing the parameters of the @ref MESH_PATH_UPDATE_CFM message.
struct mesh_path_update_cfm
{
	/// Confirmation status
	u8 status;
	/// VIF Index
	u8 vif_idx;
};

/// Structure containing the parameters of the @ref MESH_PROXY_ADD_REQ message.
struct mesh_proxy_add_req
{
	/// VIF Index
	u8 vif_idx;
	/// MAC Address of the External STA
	struct mac_addr ext_sta_addr;
};

/// Structure containing the parameters of the @ref MESH_PROXY_UPDATE_IND
struct mesh_proxy_update_ind
{
	/// Indicate if proxy information has been added or deleted
	bool delete;
	/// Indicate if we are a proxy for the external STA
	bool local;
	/// VIF Index
	u8 vif_idx;
	/// MAC Address of the External STA
	struct mac_addr ext_sta_addr;
	/// MAC Address of the proxy (only valid if local is false)
	struct mac_addr proxy_mac_addr;
};

/// Structure containing the parameters of the @ref MESH_PEER_UPDATE_IND message.
struct mesh_peer_update_ind
{
	/// Indicate if connection has been established or lost
	bool estab;
	/// VIF Index
	u8 vif_idx;
	/// STA Index
	u8 sta_idx;
	/// Peer MAC Address
	struct mac_addr peer_addr;
};

/// Structure containing the parameters of the @ref MESH_PEER_UPDATE_NTF message.
struct mesh_peer_update_ntf
{
	/// VIF Index
	u8 vif_idx;
	/// STA Index
	u8 sta_idx;
	/// Mesh Link State
	u8 state;
};

/// Structure containing the parameters of the @ref MESH_PATH_UPDATE_IND message.
struct mesh_path_update_ind
{
	/// Indicate if path is deleted or not
	bool delete;
	/// Indicate if path is towards an external STA (not part of MBSS)
	bool ext_sta;
	/// VIF Index
	u8 vif_idx;
	/// Path Index
	u8 path_idx;
	/// Target MAC Address
	struct mac_addr tgt_mac_addr;
	/// External STA MAC Address (only if ext_sta is true)
	struct mac_addr ext_sta_mac_addr;
	/// Next Hop STA Index
	u8 nhop_sta_idx;
};

//* **************** MESH task end ****************** *//

//* **************** DBG task start ****************** *//
/// Messages related to Debug Task
enum dbg_msg_tag
{
	/// Memory read request
	DBG_MEM_READ_REQ = KE_FIRST_MSG(TASK_DBG),
	/// Memory read confirm
	DBG_MEM_READ_CFM,
	/// Memory write request
	DBG_MEM_WRITE_REQ,
	/// Memory write confirm
	DBG_MEM_WRITE_CFM,
	/// Module filter request
	DBG_SET_MOD_FILTER_REQ,
	/// Module filter confirm
	DBG_SET_MOD_FILTER_CFM,
	/// Severity filter request
	DBG_SET_SEV_FILTER_REQ,
	/// Severity filter confirm
	DBG_SET_SEV_FILTER_CFM,
	/// LMAC/MAC HW fatal error indication
	DBG_ERROR_IND,
	/// Request to get system statistics
	DBG_GET_SYS_STAT_REQ,
	/// COnfirmation of system statistics
	DBG_GET_SYS_STAT_CFM,
	//WQ_PRIV TEST
	DBG_SET_WQ_PRIV_TEST_REQ,
	DBG_SET_WQ_PRIV_TEST_CFM,

	DBG_PKTDUMP_EN_REQ,
	DBG_PKTDUMP_EN_CFM,

	/// Max number of Debug messages
	DBG_MAX,
};

enum dbg_wq_priv_req_tag
{
	///HML_TEST
	DBG_WQ_PRIV_HML_TEST = 0,
	DBG_WQ_PRIV_TO_HML_TASK,
	DBG_WQ_PRIV_FOR_RAM_TEST,

	DBG_WQ_PRIV_MAX,
};

struct dbg_wq_priv_test_req
{
	u8 vif_id;
	u8 msg_id;
	u8 sub_msg_id;
	u8 wq_priv_msg_len;
	char wq_priv_msg[0];
};

struct dbg_tx_statics_req
{
	u8 txq_num;
	u8 mac_id;
};

struct dbg_chan_noise_info_req
{
	u8 vif_idx;
	u32 time_ms;
};

struct dbg_chan_util_info_req
{
	u8 vif_idx;
	u32 time_ms;
};

struct dbg_crc_stats_req
{
	u8 vif_idx;
	u8 type;	/* 0: enable stats, 1: disable stats, 2: get results */
};

struct dbg_phy_signal_stats_req
{
	u8 vif_idx;
	u8 type;	/* 0: enable stats, 1: disable stats, 2: get results */
};

struct dbg_agc_lock_stats_req
{
	u8 vif_idx;
	u8 type;	/* 0: enable stats, 1: disable stats, 2: get results */
};

/// Structure containing the parameters of the @ref DBG_MEM_READ_REQ message.
struct dbg_mem_read_req
{
	u32 memaddr;
};

/// Structure containing the parameters of the @ref DBG_MEM_READ_CFM message.
struct dbg_mem_read_cfm
{
	u32 memaddr;
	u32 memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_WRITE_REQ message.
struct dbg_mem_write_req
{
	u32 memaddr;
	u32 memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_WRITE_CFM message.
struct dbg_mem_write_cfm
{
	u32 memaddr;
	u32 memdata;
};

/// Structure containing the parameters of the @ref DBG_SET_MOD_FILTER_REQ message.
struct dbg_set_mod_filter_req
{
	/// Bit field indicating for each module if the traces are enabled or not
	u32 mod_filter;
};

/// Structure containing the parameters of the @ref DBG_SEV_MOD_FILTER_REQ message.
struct dbg_set_sev_filter_req
{
	/// Bit field indicating the severity threshold for the traces
	u32 sev_filter;
};

/// Structure containing the parameters of the @ref DBG_GET_SYS_STAT_CFM message.
struct dbg_get_sys_stat_cfm
{
	/// Time spent in CPU sleep since last reset of the system statistics
	u32 cpu_sleep_time;
	/// Time spent in DOZE since last reset of the system statistics
	u32 doze_time;
	/// Total time spent since last reset of the system statistics
	u32 stats_time;
};

struct dbg_pktdump_en_req
{
	u8 pktdump_en;
};

struct dbg_dfx_req_cfg
{
	u8 mac_id;
};

//* **************** DBG task end ****************** *//

//* **************** TDLS task start ****************** *//
/// List of messages related to the task.
enum tdls_msg_tag
{
	/// TDLS channel Switch Request.
	TDLS_CHAN_SWITCH_REQ = KE_FIRST_MSG(TASK_TDLS),
	/// TDLS channel switch confirmation.
	TDLS_CHAN_SWITCH_CFM,
	/// TDLS channel switch indication.
	TDLS_CHAN_SWITCH_IND,
	/// TDLS channel switch to base channel indication.
	TDLS_CHAN_SWITCH_BASE_IND,
	/// TDLS cancel channel switch request.
	TDLS_CANCEL_CHAN_SWITCH_REQ,
	/// TDLS cancel channel switch confirmation.
	TDLS_CANCEL_CHAN_SWITCH_CFM,
	/// TDLS peer power save indication.
	TDLS_PEER_PS_IND,
	/// TDLS peer traffic indication request.
	TDLS_PEER_TRAFFIC_IND_REQ,
	/// TDLS peer traffic indication confirmation.
	TDLS_PEER_TRAFFIC_IND_CFM,
	/// MAX number of messages
	TDLS_MAX
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_REQ message
struct tdls_chan_switch_req
{
	/// Index of the VIF
	u8 vif_index;
	/// STA Index
	u8 sta_idx;
	/// MAC address of the TDLS station
	struct mac_addr peer_mac_addr;
	/// Flag to indicate if the TDLS peer is the TDLS link initiator
	bool initiator;
	/// Channel configuration
	struct mac_chan_op chan;
	/// Operating class
	u8 op_class;
};

/// Structure containing the parameters of the @ref TDLS_CANCEL_CHAN_SWITCH_REQ message
struct tdls_cancel_chan_switch_req
{
	/// Index of the VIF
	u8 vif_index;
	/// STA Index
	u8 sta_idx;
	/// MAC address of the TDLS station
	struct mac_addr peer_mac_addr;
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_CFM message
struct tdls_chan_switch_cfm
{
	/// Status of the operation
	u8 status;
};

/// Structure containing the parameters of the @ref TDLS_CANCEL_CHAN_SWITCH_CFM message
struct tdls_cancel_chan_switch_cfm
{
	/// Status of the operation
	u8 status;
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_IND message
struct tdls_chan_switch_ind
{
	/// VIF Index
	u8 vif_index;
	/// Channel Context Index
	u8 chan_ctxt_index;
	/// Status of the operation
	u8 status;
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_BASE_IND message
struct tdls_chan_switch_base_ind
{
	/// VIF Index
	u8 vif_index;
	/// Channel Context index
	u8 chan_ctxt_index;
};

/// Structure containing the parameters of the @ref TDLS_PEER_PS_IND message
struct tdls_peer_ps_ind
{
	/// VIF Index
	u8 vif_index;
	/// STA Index
	u8 sta_idx;
	/// MAC ADDR of the TDLS STA
	struct mac_addr peer_mac_addr;
	/// Flag to indicate if the TDLS peer is going to sleep
	bool ps_on;
};

/// Structure containing the parameters of the @ref TDLS_PEER_TRAFFIC_IND_REQ message
struct tdls_peer_traffic_ind_req
{
	/// VIF Index
	u8 vif_index;
	/// STA Index
	u8 sta_idx;
	// MAC ADDR of the TDLS STA
	struct mac_addr peer_mac_addr;
	/// Dialog token
	u8 dialog_token;
	/// TID of the latest MPDU transmitted over the TDLS direct link to the TDLS STA
	u8 last_tid;
	/// Sequence number of the latest MPDU transmitted over the TDLS direct link
	/// to the TDLS STA
	u16 last_sn;
};

/// Structure containing the parameters of the @ref TDLS_PEER_TRAFFIC_IND_CFM message
struct tdls_peer_traffic_ind_cfm
{
	/// Status of the operation
	u8 status;
};

struct txq_ring_ind_req
{
	u32 txq_ring_addr;
};

struct txq_ring_ind_cfm
{
	u32 status;
};

//* **************** TDLS task end ****************** *//

typedef enum {
	HML_MSG_START = KE_FIRST_MSG(TASK_VENDOR),
	HML_MSG_TEST_REQ,
	HML_MSG_TEST_CFM,
	HML_MSG_TEST_FROM_WQ_PRIV,
	/// Request to ehance remain on channel or cancel ehance remain on channel
	HML_EROC_START_REQ,
	/// Confirmation of the (cancel) ehance remain on channel request
	HML_EROC_START_CFM,
	//Use eroc_end to notify the hid2d module of the end of this occupation
	HML_EROC_END_IND,
	// Request to sta add
	HML_STA_ADD_REQ,
	HML_STA_ADD_CFM,
	HML_STA_DEL_IND,
	HML_STA_DEL_REQ,
	HML_STA_DEL_CFM,
	HML_CONN_IND,
	HML_CONN_PREPAIR_IND,
	HML_CONN_START_REQ,
	HML_CONN_START_CFM,
	HML_AUTH_REQUIRED_RSP,
	HML_SET_POWER_REQ,
	HML_SET_POWER_CFM,
	/// Indication of VIF init when start AP
	HML_VIF_INIT_IND,
	/// Indication when HML VIF deleted
	HML_VIF_DEL_IND,
	HML_TX_RX_STAT_REQ,
	HML_TX_RX_STAT_CFM,
	HML_STAT_START_REQ,
	HML_STAT_START_CFM,
	HML_STAT_END_REQ,
	HML_STAT_END_CFM,
	HML_TX_RX_STAT_IND,
	HML_MPDU_STATUS_REQ,
	HML_MPDU_STATUS_CFM,
	HML_USER_STAT_INFO_REQ,
	HML_USER_STAT_INFO_CFM,
	HML_DELAY_CHECK_PARAM_SET_REQ,
	HML_DELAY_CHECK_PARAM_SET_CFM,
	HML_DELAY_IND,
	HML_CONCURRENT_INFO_REQ,
	HML_CONCURRENT_INFO_CFM,
	HML_VIF_MAC_ID_IND,
	HML_TX_ASSOC_RSP_ID_IND,
	HML_ADD_KEY_ID_IND,
	HML_MSG_SLEEP_REQ,
	HML_MSG_SLEEP_CFM,
	HML_MSG_SLEEP_IND,
	HML_DBAC_TRIGGER_EVENT_IND,
	HML_SLOT_START,
	HML_PRE_SLOT,
	HML_SCH_START,
	HML_MEAS_DONE,
	HML_SYNC_DONE,
	/* msg from host driver */
	HML_MSG_FROM_HOST,

	HML_MSG_MAX,
} vendor_msg_type;

#endif /* WQ_FW_WIFI_CP_API_H_ */
