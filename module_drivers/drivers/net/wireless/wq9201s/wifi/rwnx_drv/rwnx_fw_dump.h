/**
 ****************************************************************************************
 *
 * @file ipc_shared.h
 *
 * @brief Shared data between both IPC modules.
 *
 * Copyright (C) RivieraWaves 2011-2020
 *
 ****************************************************************************************
 */

#ifndef _IPC_SHARED_H_
#define _IPC_SHARED_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

/*
 * DEFINES AND MACROS
 ****************************************************************************************
 */

// Comes from la.h
/// Length of the configuration data of a logic analyzer
#define LA_CONF_LEN 10

/// Structure containing the configuration data of a logic analyzer
struct la_conf_tag {
	u32 conf[LA_CONF_LEN];
	u32 trace_len;
	u32 diag_conf;
};

/// Size of a logic analyzer memory
#define LA_MEM_LEN (1024 * 1024)

/// Type of errors
enum {
	/// Recoverable error, not requiring any action from Upper MAC
	DBG_ERROR_RECOVERABLE = 0,
	/// Fatal error, requiring Upper MAC to reset Lower MAC and HW and restart operation
	DBG_ERROR_FATAL
};

/// Maximum length of the SW diag trace
#define DBG_SW_DIAG_MAX_LEN 1024

/// Maximum length of the error trace
#define DBG_ERROR_TRACE_SIZE 256

/// Number of MAC diagnostic port banks
#define DBG_DIAGS_MAC_MAX 48

/// Number of PHY diagnostic port banks
#define DBG_DIAGS_PHY_MAX 32

/// Maximum size of the RX header descriptor information in the debug dump
#define DBG_RHD_MEM_LEN (5 * 1024)

/// Maximum size of the RX buffer descriptor information in the debug dump
#define DBG_RBD_MEM_LEN (5 * 1024)

/// Maximum size of the TX header descriptor information in the debug dump
#define DBG_THD_MEM_LEN (10 * 1024)

/// Structure containing the information about the PHY channel that is used
struct phy_channel_info {
	/// PHY channel information 1
	u32 info1;
	/// PHY channel information 2
	u32 info2;
};

/// Debug information forwarded to host when an error occurs
struct dbg_debug_info_tag {
	/// Type of error (0: recoverable, 1: fatal)
	u32 error_type;
	/// Pointer to the first RX Header Descriptor chained to the MAC HW
	u32 rhd;
	/// Size of the RX header descriptor buffer
	u32 rhd_len;
	/// Pointer to the first RX Buffer Descriptor chained to the MAC HW
	u32 rbd;
	/// Size of the RX buffer descriptor buffer
	u32 rbd_len;
	/// Pointer to the first TX Header Descriptors chained to the MAC HW
	u32 thd[NX_TXQ_CNT];
	/// Size of the TX header descriptor buffer
	u32 thd_len[NX_TXQ_CNT];
	/// MAC HW diag configuration
	u32 hw_diag;
	/// Error message
	u32 error[DBG_ERROR_TRACE_SIZE / 4];
	/// SW diag configuration length
	u32 sw_diag_len;
	/// SW diag configuration
	u32 sw_diag[DBG_SW_DIAG_MAX_LEN / 4];
	/// PHY channel information
	struct phy_channel_info chan_info;
	/// Embedded LA configuration
	struct la_conf_tag la_conf;
	/// MAC diagnostic port state
	u16 diags_mac[DBG_DIAGS_MAC_MAX];
	/// PHY diagnostic port state
	u16 diags_phy[DBG_DIAGS_PHY_MAX];
	/// MAC HW RX Header descriptor pointer
	u32 rhd_hw_ptr;
	/// MAC HW RX Buffer descriptor pointer
	u32 rbd_hw_ptr;
};

/// Full debug dump that is forwarded to host in case of error
struct dbg_debug_dump_tag {
	/// Debug information
	struct dbg_debug_info_tag dbg_info;

	/// RX header descriptor memory
	u32 rhd_mem[DBG_RHD_MEM_LEN / 4];

	/// RX buffer descriptor memory
	u32 rbd_mem[DBG_RBD_MEM_LEN / 4];

	/// TX header descriptor memory
	u32 thd_mem[NX_TXQ_CNT][DBG_THD_MEM_LEN / 4];

	/// Logic analyzer memory
	u32 la_mem[LA_MEM_LEN / 4];
};

#endif // _IPC_SHARED_H_
