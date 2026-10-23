#include <linux/string.h>

#include "rwnx_msg_tx.h"
#include "wlan_ioctl.h"
#include "wq_log.h"
#include "country.h"
#include "core.h"
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include "rwnx_main.h"

#define WLAN_COMMAND_ARGV_MAX 64

/* For kernel version >= 5.3, driver needs to provide policy */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0))
#define vendor_command_policy(__policy, __maxattr)                             \
	.policy = __policy, .maxattr = __maxattr
#define vendor_command_dump(_func_) .dumpit = _func_,
#else
#define vendor_command_policy(__policy, __maxattr)
#define vendor_command_dump(_func_)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
const struct nl80211_vendor_cmd_info wq_wlan_cfg80211_vendor_events[] = {
	{ .vendor_id = GOOGLE_OUI,
	  .subcmd = GSCAN_EVENT_SIGNIFICANT_CHANGE_RESULTS },
	{ .vendor_id = GOOGLE_OUI,
	  .subcmd = GSCAN_EVENT_HOTLIST_RESULTS_FOUND },
	{ .vendor_id = GOOGLE_OUI,
	  .subcmd = GSCAN_EVENT_SCAN_RESULTS_AVAILABLE },
	{ .vendor_id = GOOGLE_OUI, .subcmd = GSCAN_EVENT_FULL_SCAN_RESULTS },
	{ .vendor_id = GOOGLE_OUI, .subcmd = RTT_EVENT_COMPLETE },
	{ .vendor_id = GOOGLE_OUI, .subcmd = GSCAN_EVENT_COMPLETE_SCAN },
	{ .vendor_id = GOOGLE_OUI, .subcmd = GSCAN_EVENT_HOTLIST_RESULTS_LOST },
	{ .vendor_id = GOOGLE_OUI, .subcmd = GSCAN_EVENT_EPNO_EVENT },
	{ .vendor_id = GOOGLE_OUI, .subcmd = GOOGLE_DEBUG_RING_EVENT },
	{ .vendor_id = GOOGLE_OUI, .subcmd = GOOGLE_DEBUG_MEM_DUMP_EVENT },
	{ .vendor_id = GOOGLE_OUI, .subcmd = GSCAN_EVENT_ANQPO_HOTSPOT_MATCH },
	{ .vendor_id = GOOGLE_OUI, .subcmd = GOOGLE_RSSI_MONITOR_EVENT },
	{ .vendor_id = WQ_OUI, .subcmd = WQ_FW_RESTART_EVENT },
};
#endif

int wq_vendor_command_dump_null(struct wiphy *wiphy, struct wireless_dev *wdev,
				struct sk_buff *skb, const void *data,
				int data_len, unsigned long *storage)
{
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
static int wq_cfg80211_vendor_set_country_code(struct wiphy *wiphy,
					       struct wireless_dev *wdev,
					       const void *data, int data_len)
{
	int ret;
	struct nlattr *attr;
	uint8_t country_code[2] = { '\0' };
	struct rwnx_hw *hw = wiphy_priv(wiphy);

	if ((data == NULL) || (data_len == 0) || !hw) {
		ret = -EINVAL;
		goto exit;
	}

	attr = (struct nlattr *)data;

	if (attr->nla_type == ANDR_WIFI_ATTRIBUTE_COUNTRY) {
		country_code[0] = *((uint8_t *)nla_data(attr));
		country_code[1] = *((uint8_t *)nla_data(attr) + 1);
	} else {
		ret = -EINVAL;
		goto exit;
	}

	ret = wq_regd_set_country(hw, country_code);

	WQ_DBG(DM_GENERIC, DL_ERR, "ret %d, Country code:%c%c\n", ret,
	       country_code[0], country_code[1]);

exit:
	return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
const struct wiphy_vendor_command wq_wlan_cfg80211_vendor_commands[] = {
	{ { .vendor_id = GOOGLE_OUI, .subcmd = WIFI_SUBCMD_SET_COUNTRY_CODE },
	  .flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
	  .doit = wq_cfg80211_vendor_set_country_code,
	  vendor_command_dump(wq_vendor_command_dump_null)
		  vendor_command_policy(VENDOR_CMD_RAW_DATA, 0) },

};
#endif

void wq_wiphy_vendor_init(struct wiphy *wiphy)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	wiphy->n_vendor_commands = ARRAY_SIZE(wq_wlan_cfg80211_vendor_commands);
	wiphy->vendor_commands = wq_wlan_cfg80211_vendor_commands;

	wiphy->n_vendor_events = ARRAY_SIZE(wq_wlan_cfg80211_vendor_events);
	wiphy->vendor_events = wq_wlan_cfg80211_vendor_events;
#endif
}

/**
 * android private ioctl cmmond callback
*/
static int wq_commd_parse_argumnet(int8_t *command, int32_t *argc,
				   int8_t *argv[])
{
	int8_t **args = argv;
	int32_t nargs = 0;
	char *next = command;

	while ((args[nargs++] = strsep(&next, " ")))
		;
	*argc = nargs - 1;
	return 0;
}

static int wq_priv_set_miracast(struct net_device *netdev, uint8_t *command,
				int total_len)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(netdev);
	struct rwnx_hw *hw = rwnx_vif->rwnx_hw;
	int8_t *argv[WLAN_COMMAND_ARGV_MAX] = {};
	int32_t argc = 0, ret = 0;
	u8 miracast_mode = WQ_MIRACAST_DISABLED;

	WQ_ASSERT(hw != NULL, "%s HW is NULL", __func__);

	wq_commd_parse_argumnet(command, &argc, argv);

	if (argc >= 2) {
		ret = kstrtou8(argv[1], 0, &miracast_mode);
		if (ret) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "parse miracast_mode error"
			       " ret = %d\n",
			       ret);
			return -EINVAL;
		}
		if (hw->priv_ioctl.miracast_mode == miracast_mode) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "Already in miracast mode"
			       "[%d] SKIP\n",
			       miracast_mode);
			return 0;
		}
		WQ_DBG(DM_GENERIC, DL_WRN, "miracast_mode=%d\n", miracast_mode);

		hw->priv_ioctl.miracast_mode = miracast_mode;
		/*
                *the driver could reduce the channel dwell time during scanning
                *when acting as a source or sink to minimize impact on Miracast
                */
		switch (miracast_mode) {
		case WQ_MIRACAST_SOURCE:
			/* code */
			break;
		case WQ_MIRACAST_SINK:
			/* code */
			break;
		case WQ_MIRACAST_DISABLED:
		default:
			/* code */
			break;
		}
	}
	return 0;
}

static int wq_priv_set_country(struct net_device *netdev, uint8_t *command,
			       int total_len)
{
	int8_t *argv[WLAN_COMMAND_ARGV_MAX] = {};
	int32_t argc = 0;
	struct rwnx_vif *rwnx_vif = netdev_priv(netdev);
	struct rwnx_hw *hw = rwnx_vif->rwnx_hw;
	uint8_t country_code_tmp[4] = {}, cc_len = 0, i;
	uint16_t country_code;
	int ret = 0;

	WQ_ASSERT(hw != NULL, "%s HW is NULL", __func__);
	wq_commd_parse_argumnet(command, &argc, argv);

	if (argc >= 2) {
		cc_len = strnlen(argv[1], sizeof(country_code_tmp));

		for (i = 0; i < cc_len; i++)
			country_code_tmp[i] = argv[1][i];

		country_code = (((uint16_t)country_code_tmp[0]) << 8) |
			       ((uint16_t)country_code_tmp[1]);

		if (hw->priv_ioctl.country_code == country_code) {
			WQ_DBG(DM_GENERIC, DL_WRN, "already in country_code");
			return 0;
		}

		hw->priv_ioctl.country_code = country_code;

		ret = wq_regd_set_country(hw, country_code_tmp);
		// country_code is different with default country_code, need to Reacquire chan pwr info(SAP)
		if (rwnx_vif->ap.flags & RWNX_AP_STARTED)
		{
			u8 pwr_tab[PWR_TAB_LEN];
			u32 sap_freq;
			u8 sap_band;
			int pwr_ret = 0;
			struct cfg80211_chan_def *chandef = &(rwnx_vif->ap.chandef);
			if (!chandef){
				WQ_DBG(DM_GENERIC, DL_WRN,"wq_priv_set_country, chan info null\n");
				return ret;
			}
			sap_freq = chandef->chan->center_freq;
			sap_band = chandef->chan->band;

			rwnx_store_chan_pwr_tab(rwnx_vif, sap_band, sap_freq, pwr_tab);
			pwr_ret = rwnx_send_chan_pwr_info_req(hw, rwnx_vif, pwr_tab, sap_band, sap_freq);
			if(pwr_ret)
			{
				WQ_DBG(DM_GENERIC, DL_WRN, "%s, rwnx_send_chan_pwr_info_req faild\n", __func__);
			}
		}
	} else {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s [argc != 2] \n", command);
	}

	return ret;
}
static int wq_priv_get_country(struct net_device *netdev, uint8_t *command,
			       int total_len)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(netdev);
	struct rwnx_hw *hw = rwnx_vif->rwnx_hw;
	int8_t *argv[WLAN_COMMAND_ARGV_MAX] = {};
	int32_t argc = 0, ret = 0;
	uint32_t country = 0;

	WQ_ASSERT(hw != NULL, "%s HW is NULL", __func__);
	wq_commd_parse_argumnet(command, &argc, argv);

	if (argc == 1) {
		if (!hw->mod_params.driver_reg_enable)
			return ret;
		country = wq_get_country_code();
		ret = scnprintf(command, total_len, "Country Code: (0x%x)",
				country);
		WQ_DBG(DM_GENERIC, DL_WRN, " %s ret = %d\n", command, ret);
	}
	return ret;
}

static int wq_priv_set_suspendmode(struct net_device *netdev, uint8_t *command,
				   int total_len)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(netdev);
	struct rwnx_hw *hw = rwnx_vif->rwnx_hw;
	int8_t *argv[WLAN_COMMAND_ARGV_MAX] = {};
	int32_t argc = 0;
	int ret = 0;
	uint32_t suspendmode = 0;
	bool ps_mode_enable = false;
	u8 ps_mode;

	WQ_ASSERT(hw != NULL, "%s HW is NULL", __func__);
	wq_commd_parse_argumnet(command, &argc, argv);

	if (argc >= 2) {
		ret = kstrtou32(argv[1], 0, &suspendmode);
		if (ret) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "parse suspendmode error"
			       " ret = %d\n",
			       ret);
			return -EINVAL;
		}

		WQ_DBG(DM_GENERIC, DL_INF, "suspendmode = %d\n", suspendmode);

		ps_mode_enable = (suspendmode == 1) ? true : false;

		if (hw->priv_ioctl.suspendmode == ps_mode_enable) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "Already in suspend mode"
			       "[%d] SKIP\n",
			       ps_mode_enable);
			return 0;
		}

		hw->priv_ioctl.suspendmode = ps_mode_enable;

		/*Check whether you are entering the lower power*/
		if (!(hw->version_cfm.features & BIT(MM_FEAT_PS_BIT)))
			ps_mode_enable = false;

		//disable PS mode is ps_disable:1
		if (hw->feature.ps_disable)
			ps_mode_enable = false;

		/*DOT: INI  disable lower power*/

		ps_mode = ps_mode_enable ? PS_MODE_ON_DYN : PS_MODE_OFF;

		mutex_lock(&hw->mutex);
		ret = rwnx_send_me_set_ps_mode(hw, ps_mode);
		mutex_unlock(&hw->mutex);
	}
	return 0;
}

static int wq_priv_set_wps_p2p_ie(struct net_device *netdev, uint8_t *command,
				  int total_len)
{
	return 0;
}

static const struct wq_drv_cmd priv_cmd_handlers[] = {
	{ "MIRACAST", wq_priv_set_miracast, false },
	{ "COUNTRY", wq_priv_set_country, true },
	{ "GET_COUNTRY", wq_priv_get_country, true },
	{ "SETSUSPENDMODE", wq_priv_set_suspendmode, false },
	{ "SET_AP_WPS_P2P_IE", wq_priv_set_wps_p2p_ie, false },

};

/**
 * command entry The command parsing entry
*/
static int wq_priv_driver_cmds(struct net_device *netdev, uint8_t *command,
			       int total_len)
{
	bool cmd_found = false;
	u_int32_t i, byte_write = 0;

	/**
         * need to check whether wlan0 is read
         * Check validity of netdev, private data, and pointers
        */
	if (!netdev || !command || !netdev_priv(netdev)) {
		WQ_DBG(DM_GENERIC, DL_ERR, "not ready skip priv_driver_cmds\n");
		return -EINVAL;
	}

	for (i = 0; i < sizeof(priv_cmd_handlers) / sizeof(struct wq_drv_cmd);
	     i++) {
		if (strncasecmp(command, priv_cmd_handlers[i].cmdstr,
				strlen(priv_cmd_handlers[i].cmdstr)) == 0) {
			if (priv_cmd_handlers[i].handler != NULL) {
				if (!priv_cmd_handlers[i].enable) {
					WQ_DBG(DM_GENERIC, DL_ERR,
					       "drv cmd is not enable\n");
					return -EOPNOTSUPP;
				}
				byte_write = priv_cmd_handlers[i].handler(
					netdev, command, total_len);
				cmd_found = true;
			}
		}
	}
	if (!cmd_found) {
		WQ_DBG(DM_GENERIC, DL_ERR, "Unsupported driver command:%s \n",
		       command);
		return -EOPNOTSUPP;
	}

	return byte_write;
}

/*android private ioctl*/
int wq_android_priv_cmd(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	int ret = 0, byte_write = 0;
	char *command = NULL;
	struct android_wifi_priv_cmd priv_cmd;

	if (!ifr->ifr_data) {
		ret = -EINVAL;
		goto exit;
	}

	if (copy_from_user(&priv_cmd, ifr->ifr_data,
			   sizeof(struct android_wifi_priv_cmd))) {
		ret = -EFAULT;
		goto exit;
	}

	command = kmalloc(priv_cmd.total_len, GFP_KERNEL);
	if (!command) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: size %d failed to allocate memory\n", __func__,
		       priv_cmd.total_len);
		ret = -ENOMEM;
		goto exit;
	}

	if (!wq_access_ok(VERIFY_READ, priv_cmd.buf, priv_cmd.total_len)) {
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: failed to access memory\n",
		       __func__);
		ret = -EFAULT;
		goto exit;
	}

	if (copy_from_user(command, (char __user *)priv_cmd.buf,
			   priv_cmd.total_len)) {
		ret = -EFAULT;
		goto exit;
	}
	WQ_DBG(DM_GENERIC, DL_INF, "%s: android private cmd \"%s\" on %s\n",
	       __func__, command, ifr->ifr_name);

	command[priv_cmd.total_len - 1] = '\0';
	byte_write = wq_priv_driver_cmds(netdev, command, priv_cmd.total_len);

	if (byte_write == -EOPNOTSUPP) {
		byte_write = scnprintf(command, priv_cmd.total_len, "%s",
				       "not support");
	}

	if (byte_write >= 0) {
		if ((byte_write == 0) && priv_cmd.total_len > 0)
			command[0] = '\0';

		if (byte_write >= priv_cmd.total_len) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "%s"
			       "byte_write %d > total_len %d\n",
			       __func__, byte_write, byte_write);
			byte_write = priv_cmd.total_len;
			command[byte_write - 1] = '\0';
		} else {
			byte_write++;
		}

		priv_cmd.used_len = byte_write;
		if (copy_to_user(priv_cmd.buf, command, byte_write))
			ret = -EFAULT;
	} else {
		ret = byte_write;
	}
exit:
	kfree(command);
	return ret;
}

/* wireless extensions' ioctls
 *  0x8B00 ~ 0x8BDF, wireless extension region
*/
int wq_wext_support_ioctl(struct net_device *net, struct ifreq *ifr, int cmd)
{
	WQ_DBG(DM_GENERIC, DL_WRN, "%s: %s [0x%x]\n", __func__, ifr->ifr_name,
	       cmd);
	return -EOPNOTSUPP;
}

/* 0x8BE0 ~ 0x8BFF, private ioctl region */
int wq_priv_support_ioctl(struct net_device *net, struct ifreq *ifr, int cmd)
{
	WQ_DBG(DM_GENERIC, DL_WRN, "%s: %s [0x%x]\n", __func__, ifr->ifr_name,
	       cmd);
	return -EOPNOTSUPP;
}

static int wq_get_vendor_event_id(int event)
{
	int evt_id = 0;
	for (evt_id = 0; evt_id < ARRAY_SIZE(wq_wlan_cfg80211_vendor_events);
	     evt_id++) {
		if (wq_wlan_cfg80211_vendor_events[evt_id].subcmd == event)
			return evt_id;
	}
	return -1;
}

void wq_subsystem_restart_evt(struct wiphy *wiphy, struct wireless_dev *wdev,
			      char *dump_info, u32 info_len)
{
/* vendor specified type, 19 for brcm vendor hal*/
#define DEBUG_ATTRIBUTE_HANG_REASON 19
	struct sk_buff *skb;
	u32 tot_len;
	gfp_t kflags;
	char *data;
	data = dump_info;
	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	ENTER();
	if (!dump_info) {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s: dump_info is empty, len %u \n",
		       __func__, info_len);
		data = "wlan_fw_crashed";
	}
	tot_len = strlen(data) + 1;
	skb = cfg80211_vendor_event_alloc(
		wiphy, wdev, tot_len,
		wq_get_vendor_event_id(WQ_FW_RESTART_EVENT), kflags);
	if (!skb) {
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: skb alloc fail\n", __func__);
		return;
	}
	nla_put(skb, DEBUG_ATTRIBUTE_HANG_REASON, tot_len, data);

	cfg80211_vendor_event(skb, kflags);
	LEAVE();
}

#ifdef INCLUDE_WQ_IWPRIVE
enum {
	WQ_IWPRIV_CMD_CCA = 0,
	/// Should less than SIOCIWLASTPRIV - SIOCIWFIRSTPRIV = 35839 - 35808 + 1
	WQ_IWPRIV_CMD_MAX,
};

#define DEF_WQ_IWPRIV_FUNC_FMT(func_name) func_name(struct net_device *dev, struct iw_request_info *info,\
		union iwreq_data *wrqu, char *cmd_string)

#define DEF_WQ_IWPRIV_FUNC_NAME(name) wq_iwpriv_##name
#define DEF_WQ_IWPRIV_FUNC(name) DEF_WQ_IWPRIV_FUNC_FMT(DEF_WQ_IWPRIV_FUNC_NAME(name))

static int DEF_WQ_IWPRIV_FUNC(cca) {
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *hw = rwnx_vif->rwnx_hw;
	unsigned int period;
	struct mm_get_cca_cfm get_cfm;
	char p_buf[64];

	if (!cmd_string) {
		goto inv_arg;
	}

	memcpy(p_buf, cmd_string, sizeof(p_buf));

	if (strncmp(p_buf, "enable", 6) == 0) {
		if (kstrtouint(&p_buf[7], 10, &period)) {
			goto inv_arg;
		}
		rwnx_send_cca_config_set(hw, rwnx_vif->vif_index, 1, (u16)period);
	} else if (strncmp(p_buf, "disable", 7) == 0) {
		rwnx_send_cca_config_set(hw, rwnx_vif->vif_index, 0, 0);
	} else if (strncmp(p_buf, "capture", 7) == 0) {
		rwnx_send_cca_data_get(hw, rwnx_vif->vif_index, &get_cfm);
		(void)get_cfm;
	} else {
		goto inv_arg;
	}

	return 0;

inv_arg:

	WQ_DBG(DM_GENERIC, DL_ERR, "%s invalid command, should be like:\n  iwpriv wlan0 cca enable:1000\n  iwpriv wlan0 cca disable\n  iwpriv wlan0 cca capture\n", __func__);
	return -EINVAL;
}

#define wq_2_iwpriv(wq)	(SIOCIWFIRSTPRIV + (wq))
static const struct iw_priv_args wq_iwpriv_args[] = {
    {
        .cmd = wq_2_iwpriv(WQ_IWPRIV_CMD_CCA),
        .set_args = IW_PRIV_TYPE_CHAR | 128,
        .get_args = IW_PRIV_TYPE_CHAR | 128,
        .name = "cca"
    }
};

static const iw_handler wq_iwpriv_handler_table[] = {
    [WQ_IWPRIV_CMD_CCA] = DEF_WQ_IWPRIV_FUNC_NAME(cca),
};

struct iw_handler_def wq_iwpriv_handler_def = {
 #ifdef CONFIG_WEXT_PRIV
     .num_private = ARRAY_SIZE(wq_iwpriv_handler_table),
     .num_private_args = ARRAY_SIZE(wq_iwpriv_args),
     .private = wq_iwpriv_handler_table,
     .private_args = wq_iwpriv_args,
 #endif
};

#endif
