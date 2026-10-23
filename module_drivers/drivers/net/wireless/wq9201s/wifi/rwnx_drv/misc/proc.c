#include <linux/version.h>
#include <linux/proc_fs.h>

#include "rwnx_defs.h"
#include "rwnx_compat.h"
#include "rwnx_msg_tx.h"

#include "proc.h"
#include "hif_api.h"
#include "wq_log.h"
#include "wq_wifi_dbg.h"
#include "wq_wifi_priv.h"
#include "rwnx_rx_ll.h"
#include "ieee80211_extap.h"
#include "wq_fw.h"

#define MAX_PRARAM_CNT 5
#define MAX_CMD_BUF_SIZE 120

#ifndef PROC_DIR
#define PROC_DIR "driver/wifi_usb" /* "driver/kiwi_usb" ? */
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0))
#define wq_proc_ops file_operations
#else
#define wq_proc_ops proc_ops
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

#define WQ_FEATURE_NSTATE(x) (rwnx_hw->feature.x) ? "OFF" : "ON"

#ifdef DEBUG_WQ_DFX
struct proc_dir_entry *dfx_dir_entry = NULL;
struct kiwi_proc_dfx_hdl {
	char *name;
	int (*show)(struct seq_file *seq, void *v);
	ssize_t (*write)(struct file *file, const char __user *buffer,
			 size_t count, loff_t *pos, void *data);
};

#define KIWI_PROC_DFX_HDL(_name, _show, _write)                                \
	{                                                                      \
		.name = _name, .show = _show, .write = _write                  \
	}

static int proc_get_vif_info(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct wq_dbg_vif dbg_vif;
	int i;
	char credit_ac_to_name[WQ_CREDIT_TYPE_NUM][3] = { "BK", "BE", "VI",
							  "VO" };

	wq_get_vif_info(vif, &dbg_vif);
	seq_printf(seq, "iface=%s\n", dbg_vif.name);
	seq_printf(seq, "iftype=%s\n", wq_nl80211_iftype_str(dbg_vif.iftype));
	seq_printf(seq, "addr=%pM\n", dbg_vif.mac_addr);

	if (rwnx_chanctx_valid(vif->rwnx_hw, vif->ch_index)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
		seq_printf(seq, "ssid=%s\n", vif->wdev.u.client.ssid);
#else
		seq_printf(seq, "ssid=%s\n", vif->wdev.ssid);
#endif
		seq_printf(seq, "bssid=%pM\n", dbg_vif.bssid);
		seq_printf(seq, "band_width=%s\n",
			   wq_nl80211_chan_width_str(dbg_vif.width));
		if (dbg_vif.width == NL80211_CHAN_WIDTH_80P80) {
			seq_printf(seq, "chan1=%d (%d MHz)\n",
				   ieee80211_frequency_to_channel(
					   dbg_vif.center_freq1),
				   dbg_vif.center_freq1);
			seq_printf(seq, "chan2=%d (%d MHz)\n",
				   ieee80211_frequency_to_channel(
					   dbg_vif.center_freq2),
				   dbg_vif.center_freq2);
		} else {
			seq_printf(seq, "chan=%d (%d MHz)\n",
				   ieee80211_frequency_to_channel(
					   dbg_vif.center_freq),
				   dbg_vif.center_freq);
		}
		if (dbg_vif.width < NL80211_CHAN_WIDTH_80) {
			seq_printf(seq, "chan_type=%s\n",
				   wq_nl80211_chan_type_str(dbg_vif.chan_type));
		}
		seq_printf(seq, "wlan version: %s\n",
			   wq_wlan_ver_str(dbg_vif.wlan_version));

		seq_printf(seq, "tkip_mic_failure_count: %d\n",
			   dbg_vif.tkip_mic_failure_count);

		if (vif->rwnx_hw != NULL) {
			for (i = 0; i < WQ_CREDIT_TYPE_NUM; i++) {
				seq_printf(
					seq,
					"credit total[%s]: total=%d available=%d lend=%d\n",
					credit_ac_to_name[i],
					dbg_vif.credit_total[i],
					dbg_vif.credit_avail[i],
					dbg_vif.credit_lend[i]);
			}
		}

	} else {
		seq_printf(seq,
			   "device is not connected or ap is not ready!\n");
	}

	return 0;
}

static int proc_get_skb_stats(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct wq_skb_stats skb_stats;

	wq_get_skb_stats(vif->rwnx_hw, &skb_stats);
	seq_printf(seq, "pktout_freecnt=%d/%d\n", skb_stats.pkt_out_freecnt,
		   skb_stats.pkt_out_max);
	seq_printf(seq, "pktin_freecnt=%d/%d\n", skb_stats.pkt_in_freecnt,
		   skb_stats.pkt_in_max);
	seq_printf(seq, "msgout_freecnt=%d/%d\n", skb_stats.msg_out_freecnt,
		   skb_stats.msg_out_max);
	seq_printf(seq, "msgin_freecnt=%d/%d\n", skb_stats.msg_in_freecnt,
		   skb_stats.msg_in_max);

	return 0;
}

static int proc_get_security_info(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);

	if (rwnx_chanctx_valid(vif->rwnx_hw, vif->ch_index)) {
		seq_printf(seq, "auth_type=%s\n",
			   wq_auth_type_str(vif->security.auth_type));
		seq_printf(seq, "unicast_cipher=%s\n",
			   wq_cipher_str(vif->security.unicast_cipher));
		seq_printf(seq, "group_cipher=%s\n",
			   wq_cipher_str(vif->security.group_cipher));
		seq_printf(seq, "mgmt_cipher=%s\n",
			   wq_cipher_str(vif->security.mgmt_cipher));
		seq_printf(seq, "mfp_on=%d\n", vif->security.mfp_on);
		seq_printf(seq, "wpa_version=%s\n",
			   wq_wpa_ver_str(vif->security.wpa_version));
	} else {
		seq_printf(seq,
			   "device is not connected or ap is not ready!\n");
	}

	return 0;
}

inline static int
proc_dbg_print_sta_tx_stats(struct seq_file *seq,
			    struct wq_dbg_fw_sta_tx_stats *tx_stats)
{
	seq_printf(seq, "single_tx_succ_cnt  : %u\n",
		   tx_stats->single_success_cnt);
	seq_printf(seq, "single_tx_fail_cnt  : %u\n",
		   tx_stats->single_fail_cnt);
	seq_printf(seq, "single_tx_retry_cnt : %u\n",
		   tx_stats->single_retry_cnt);
	seq_printf(seq, "ampdu_tx_succ_cnt   : %u\n",
		   tx_stats->ampdu_success_cnt);
	seq_printf(seq, "ampdu_tx_fail_cnt   : %u\n", tx_stats->ampdu_fail_cnt);
	seq_printf(seq, "ampdu_tx_retry_cnt  : %u\n",
		   tx_stats->ampdu_retry_cnt);
	seq_printf(seq, "mac_total_tx_cnt    : %u\n",
		   tx_stats->mac_total_tx_cnt);
	seq_printf(seq, "mac_total_tx_len    : %llu\n",
		   tx_stats->mac_total_tx_len);

	if (tx_stats->mac_total_tx_cnt != 0)
#ifdef CONFIG_64BIT
		seq_printf(seq, "Tx PER: %llu\n",
			   (((u64)tx_stats->single_fail_cnt +
			     (u64)tx_stats->ampdu_fail_cnt) *
			    100) / ((u64)tx_stats->mac_total_tx_cnt));
#else
		seq_printf(
			seq, "Tx PER: %u\n",
			(tx_stats->single_fail_cnt + tx_stats->ampdu_fail_cnt) *
				100 / tx_stats->mac_total_tx_cnt);
#endif

	return 0;
}

inline static int
proc_dbg_print_sta_rx_stats(struct seq_file *seq,
			    struct wq_dbg_fw_sta_rx_stats *rx_stats)
{
	/* mac_id is useless */
	/*seq_printf(seq, "mac_id: %hhu\n", rx_stats->mac_id);*/
	seq_printf(seq, "single_rx_succ_cnt : %u\n",
		   rx_stats->single_success_cnt);
	seq_printf(seq, "ampdu_rx_succ_cnt  : %u\n",
		   rx_stats->ampdu_success_cnt);
	seq_printf(seq, "mgmt_frame_cnt     : %u\n",
		   rx_stats->mac_mgmt_frame_cnt);
	seq_printf(seq, "ctrl_frame_cnt     : %u\n",
		   rx_stats->mac_ctrl_frame_cnt);
	seq_printf(seq, "data_frame_cnt     : %u\n",
		   rx_stats->mac_data_frame_cnt);
	seq_printf(seq, "other_frame_cnt    : %u\n",
		   rx_stats->mac_other_frame_cnt);
	seq_printf(seq, "mac_total_rx_cnt   : %u\n",
		   rx_stats->mac_mgmt_frame_cnt + rx_stats->mac_ctrl_frame_cnt +
			   rx_stats->mac_data_frame_cnt +
			   rx_stats->mac_other_frame_cnt);
	seq_printf(seq, "mac_total_rx_len   : %llu\n",
		   rx_stats->mac_total_rx_len);

	return 0;
}

static int proc_get_sta_trx_stats(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct rwnx_hw *rwnx_hw = vif->rwnx_hw;
	struct wq_dbg_fw_sta_trx_stats sta_stats;
	struct wq_dbg_vif_ext_trx_stats vif_stats;
	int ret = -1;
	int i;

	/* TODO: only get info from stas connEcting to current vif */
	memset(&sta_stats, 0x00, sizeof(struct wq_dbg_fw_sta_trx_stats));
	for (i = 0; i < NX_REMOTE_STA_MAX; i++) {
		if (rwnx_hw->sta_table[i].valid) {
			ret = wq_get_sta_trx_stats(rwnx_hw, i, &sta_stats);
			if (ret == 0) {
				seq_printf(
					seq,
					"\nTRX stats for sta(%pM, idx: %u)\n",
					rwnx_hw->sta_table[i].mac_addr, i);
				seq_printf(seq, "TX STATS:\n");
				proc_dbg_print_sta_tx_stats(
					seq, &sta_stats.tx_stats);

				seq_printf(seq, "RX STATS:\n");
				proc_dbg_print_sta_rx_stats(
					seq, &sta_stats.rx_stats);
			} else {
				seq_printf(
					seq,
					"get sta trx stats error, sta_idx: %d\n",
					i);
			}
		}
	}
	/* get vif extra trx stats */

	memset(&vif_stats, 0x00, sizeof(struct wq_dbg_vif_ext_trx_stats));
	ret = wq_get_vif_ext_trx_stats(vif, &vif_stats);
	if (ret == 0) {
		seq_printf(seq, "\nvif extra trx stats (vif_idx: %u)\n",
			   vif->vif_index);
		seq_printf(seq, "RX stats for unkonwn sta(mac_id: %u):\n",
			   vif_stats.unkonwn_rx_stats.mac_id);
		proc_dbg_print_sta_rx_stats(seq, &vif_stats.unkonwn_rx_stats);
		seq_printf(seq, "\nbeacon success count  : %u\n",
			   vif_stats.bcn_succ_cnt);
		seq_printf(seq, "beacon fail count     : %u\n",
			   vif_stats.bcn_fail_cnt);

		seq_printf(seq, "\narp_req_drop : %u\n",
			   vif_stats.arp_req_drop);
		seq_printf(seq, "802_3_drop : %u\n", vif_stats.eth_802_3_drop);
		seq_printf(seq, "arp_rsp_drop : %u\n", vif_stats.arp_rsp_drop);
		seq_printf(seq, "802_1q_drop : %u\n",
			   vif_stats.eth_802_1q_drop);
		seq_printf(seq, "ipv4_192_1_681_255_drop : %u\n",
			   vif_stats.ipv4_192_1_681_255_drop);
		seq_printf(seq, "ipv4_224_239_drop : %u\n",
			   vif_stats.ipv4_224_239_drop);
		seq_printf(seq, "dhcp_from_client_drop : %u\n",
			   vif_stats.dhcp_from_client_drop);
		seq_printf(seq, "dhcp_for_other_drop : %u\n",
			   vif_stats.dhcp_for_other_drop);
		seq_printf(seq, "ipv6_rs_drop : %u\n", vif_stats.ipv6_rs_drop);
		seq_printf(seq, "ipv6_na_drop : %u\n", vif_stats.ipv6_na_drop);
		seq_printf(seq, "ipv6_multicast_drop : %u\n\n",
			   vif_stats.ipv6_multicast_drop);

		for (i = 0; i < 32; i++)
			seq_printf(seq,
				   "count of aggregation w. %d sub-mpdu : %u\n",
				   i + 1, vif_stats.ampdu_size_record[i]);
		seq_printf(seq, "latest aggregation sub-mpdu number : %u\n",
			   vif_stats.latest_ampdu_size);

		seq_printf(seq, "Tx CCK     : %d %d %d %d\n",
			   vif_stats.tx_rate_cnt.rate_1M,
			   vif_stats.tx_rate_cnt.rate_2M,
			   vif_stats.tx_rate_cnt.rate_5_5M,
			   vif_stats.tx_rate_cnt.rate_11M);
		seq_printf(seq, "Tx OFDM    : %d %d %d %d - %d %d %d %d\n",
			   vif_stats.tx_rate_cnt.rate_6M,
			   vif_stats.tx_rate_cnt.rate_9M,
			   vif_stats.tx_rate_cnt.rate_12M,
			   vif_stats.tx_rate_cnt.rate_18M,
			   vif_stats.tx_rate_cnt.rate_24M,
			   vif_stats.tx_rate_cnt.rate_36M,
			   vif_stats.tx_rate_cnt.rate_48M,
			   vif_stats.tx_rate_cnt.rate_54M);
		seq_printf(seq, "Tx HT MCS  : %d %d %d %d - %d %d %d %d\n",
			   vif_stats.tx_rate_cnt.ht_mcs[0],
			   vif_stats.tx_rate_cnt.ht_mcs[1],
			   vif_stats.tx_rate_cnt.ht_mcs[2],
			   vif_stats.tx_rate_cnt.ht_mcs[3],
			   vif_stats.tx_rate_cnt.ht_mcs[4],
			   vif_stats.tx_rate_cnt.ht_mcs[5],
			   vif_stats.tx_rate_cnt.ht_mcs[6],
			   vif_stats.tx_rate_cnt.ht_mcs[7]);
		seq_printf(seq,
			   "Tx VHT MCS : %d %d %d %d - %d %d %d %d - %d %d\n",
			   vif_stats.tx_rate_cnt.vht_mcs[0],
			   vif_stats.tx_rate_cnt.vht_mcs[1],
			   vif_stats.tx_rate_cnt.vht_mcs[2],
			   vif_stats.tx_rate_cnt.vht_mcs[3],
			   vif_stats.tx_rate_cnt.vht_mcs[4],
			   vif_stats.tx_rate_cnt.vht_mcs[5],
			   vif_stats.tx_rate_cnt.vht_mcs[6],
			   vif_stats.tx_rate_cnt.vht_mcs[7],
			   vif_stats.tx_rate_cnt.vht_mcs[8],
			   vif_stats.tx_rate_cnt.vht_mcs[9]);
		seq_printf(
			seq,
			"Tx HESU MCS: %d %d %d %d - %d %d %d %d - %d %d %d %d\n",
			vif_stats.tx_rate_cnt.he_su_mcs[0],
			vif_stats.tx_rate_cnt.he_su_mcs[1],
			vif_stats.tx_rate_cnt.he_su_mcs[2],
			vif_stats.tx_rate_cnt.he_su_mcs[3],
			vif_stats.tx_rate_cnt.he_su_mcs[4],
			vif_stats.tx_rate_cnt.he_su_mcs[5],
			vif_stats.tx_rate_cnt.he_su_mcs[6],
			vif_stats.tx_rate_cnt.he_su_mcs[7],
			vif_stats.tx_rate_cnt.he_su_mcs[8],
			vif_stats.tx_rate_cnt.he_su_mcs[9],
			vif_stats.tx_rate_cnt.he_su_mcs[10],
			vif_stats.tx_rate_cnt.he_su_mcs[11]);
		seq_printf(
			seq,
			"Tx HEMU MCS: %d %d %d %d - %d %d %d %d - %d %d %d %d\n",
			vif_stats.tx_rate_cnt.he_mu_mcs[0],
			vif_stats.tx_rate_cnt.he_mu_mcs[1],
			vif_stats.tx_rate_cnt.he_mu_mcs[2],
			vif_stats.tx_rate_cnt.he_mu_mcs[3],
			vif_stats.tx_rate_cnt.he_mu_mcs[4],
			vif_stats.tx_rate_cnt.he_mu_mcs[5],
			vif_stats.tx_rate_cnt.he_mu_mcs[6],
			vif_stats.tx_rate_cnt.he_mu_mcs[7],
			vif_stats.tx_rate_cnt.he_mu_mcs[8],
			vif_stats.tx_rate_cnt.he_mu_mcs[9],
			vif_stats.tx_rate_cnt.he_mu_mcs[10],
			vif_stats.tx_rate_cnt.he_mu_mcs[11]);
		seq_printf(
			seq,
			"Tx HEER MCS: %d %d %d %d - %d %d %d %d - %d %d %d %d\n",
			vif_stats.tx_rate_cnt.he_er_mcs[0],
			vif_stats.tx_rate_cnt.he_er_mcs[1],
			vif_stats.tx_rate_cnt.he_er_mcs[2],
			vif_stats.tx_rate_cnt.he_er_mcs[3],
			vif_stats.tx_rate_cnt.he_er_mcs[4],
			vif_stats.tx_rate_cnt.he_er_mcs[5],
			vif_stats.tx_rate_cnt.he_er_mcs[6],
			vif_stats.tx_rate_cnt.he_er_mcs[7],
			vif_stats.tx_rate_cnt.he_er_mcs[8],
			vif_stats.tx_rate_cnt.he_er_mcs[9],
			vif_stats.tx_rate_cnt.he_er_mcs[10],
			vif_stats.tx_rate_cnt.he_er_mcs[11]);
		seq_printf(seq, "Tx MCS32   : %d\n",
			   vif_stats.tx_rate_cnt.ht_mcs32);
		seq_printf(seq, "Tx MCS?    : %d %d %d \n",
			   vif_stats.tx_rate_cnt.ht_mcs_unknown,
			   vif_stats.tx_rate_cnt.vht_mcs_unknown,
			   vif_stats.tx_rate_cnt.he_mcs_unknown);
		seq_printf(seq, "Tx STBC    : %d\n",
			   vif_stats.tx_rate_cnt.stbc_cnt);

		seq_printf(seq, "Rx CCK     : %d %d %d %d\n",
			   vif_stats.rx_rate_cnt.rate_1M,
			   vif_stats.rx_rate_cnt.rate_2M,
			   vif_stats.rx_rate_cnt.rate_5_5M,
			   vif_stats.rx_rate_cnt.rate_11M);
		seq_printf(seq, "Rx OFDM    : %d %d %d %d - %d %d %d %d\n",
			   vif_stats.rx_rate_cnt.rate_6M,
			   vif_stats.rx_rate_cnt.rate_9M,
			   vif_stats.rx_rate_cnt.rate_12M,
			   vif_stats.rx_rate_cnt.rate_18M,
			   vif_stats.rx_rate_cnt.rate_24M,
			   vif_stats.rx_rate_cnt.rate_36M,
			   vif_stats.rx_rate_cnt.rate_48M,
			   vif_stats.rx_rate_cnt.rate_54M);
		seq_printf(seq, "Rx HT MCS  : %d %d %d %d - %d %d %d %d\n",
			   vif_stats.rx_rate_cnt.ht_mcs[0],
			   vif_stats.rx_rate_cnt.ht_mcs[1],
			   vif_stats.rx_rate_cnt.ht_mcs[2],
			   vif_stats.rx_rate_cnt.ht_mcs[3],
			   vif_stats.rx_rate_cnt.ht_mcs[4],
			   vif_stats.rx_rate_cnt.ht_mcs[5],
			   vif_stats.rx_rate_cnt.ht_mcs[6],
			   vif_stats.rx_rate_cnt.ht_mcs[7]);
		seq_printf(seq,
			   "Rx VHT MCS : %d %d %d %d - %d %d %d %d - %d %d\n",
			   vif_stats.rx_rate_cnt.vht_mcs[0],
			   vif_stats.rx_rate_cnt.vht_mcs[1],
			   vif_stats.rx_rate_cnt.vht_mcs[2],
			   vif_stats.rx_rate_cnt.vht_mcs[3],
			   vif_stats.rx_rate_cnt.vht_mcs[4],
			   vif_stats.rx_rate_cnt.vht_mcs[5],
			   vif_stats.rx_rate_cnt.vht_mcs[6],
			   vif_stats.rx_rate_cnt.vht_mcs[7],
			   vif_stats.rx_rate_cnt.vht_mcs[8],
			   vif_stats.rx_rate_cnt.vht_mcs[9]);
		seq_printf(
			seq,
			"Rx HESU MCS: %d %d %d %d - %d %d %d %d - %d %d %d %d\n",
			vif_stats.rx_rate_cnt.he_su_mcs[0],
			vif_stats.rx_rate_cnt.he_su_mcs[1],
			vif_stats.rx_rate_cnt.he_su_mcs[2],
			vif_stats.rx_rate_cnt.he_su_mcs[3],
			vif_stats.rx_rate_cnt.he_su_mcs[4],
			vif_stats.rx_rate_cnt.he_su_mcs[5],
			vif_stats.rx_rate_cnt.he_su_mcs[6],
			vif_stats.rx_rate_cnt.he_su_mcs[7],
			vif_stats.rx_rate_cnt.he_su_mcs[8],
			vif_stats.rx_rate_cnt.he_su_mcs[9],
			vif_stats.rx_rate_cnt.he_su_mcs[10],
			vif_stats.rx_rate_cnt.he_su_mcs[11]);
		seq_printf(
			seq,
			"Rx HEMU MCS: %d %d %d %d - %d %d %d %d - %d %d %d %d\n",
			vif_stats.rx_rate_cnt.he_mu_mcs[0],
			vif_stats.rx_rate_cnt.he_mu_mcs[1],
			vif_stats.rx_rate_cnt.he_mu_mcs[2],
			vif_stats.rx_rate_cnt.he_mu_mcs[3],
			vif_stats.rx_rate_cnt.he_mu_mcs[4],
			vif_stats.rx_rate_cnt.he_mu_mcs[5],
			vif_stats.rx_rate_cnt.he_mu_mcs[6],
			vif_stats.rx_rate_cnt.he_mu_mcs[7],
			vif_stats.rx_rate_cnt.he_mu_mcs[8],
			vif_stats.rx_rate_cnt.he_mu_mcs[9],
			vif_stats.rx_rate_cnt.he_mu_mcs[10],
			vif_stats.rx_rate_cnt.he_mu_mcs[11]);
		seq_printf(
			seq,
			"Rx HEER MCS: %d %d %d %d - %d %d %d %d - %d %d %d %d\n",
			vif_stats.rx_rate_cnt.he_er_mcs[0],
			vif_stats.rx_rate_cnt.he_er_mcs[1],
			vif_stats.rx_rate_cnt.he_er_mcs[2],
			vif_stats.rx_rate_cnt.he_er_mcs[3],
			vif_stats.rx_rate_cnt.he_er_mcs[4],
			vif_stats.rx_rate_cnt.he_er_mcs[5],
			vif_stats.rx_rate_cnt.he_er_mcs[6],
			vif_stats.rx_rate_cnt.he_er_mcs[7],
			vif_stats.rx_rate_cnt.he_er_mcs[8],
			vif_stats.rx_rate_cnt.he_er_mcs[9],
			vif_stats.rx_rate_cnt.he_er_mcs[10],
			vif_stats.rx_rate_cnt.he_er_mcs[11]);
		seq_printf(seq, "Rx MCS32   : %d\n",
			   vif_stats.rx_rate_cnt.ht_mcs32);
		seq_printf(seq, "Rx MCS?    : %d %d %d \n",
			   vif_stats.rx_rate_cnt.ht_mcs_unknown,
			   vif_stats.rx_rate_cnt.vht_mcs_unknown,
			   vif_stats.rx_rate_cnt.he_mcs_unknown);
		seq_printf(seq, "Rx STBC    : %d\n",
			   vif_stats.rx_rate_cnt.stbc_cnt);

		seq_printf(seq, "\ntx reach retry limit failed count: \n");
		for (i = 0; i < 8; i++) {
			seq_printf(seq, "tid[%d] count: %u\n", i,
				   vif_stats.mib_qos_fail_cnt[i]);
		}
	} else {
		seq_printf(seq, "get unknown sta tx stats error, vif_idx: %d\n",
			   vif->vif_index);
	}

	return 0;
}

ssize_t proc_set_edca_param(struct file *file, const char __user *buffer,
			    size_t count, loff_t *pos, void *data)
{
	struct net_device *ndev = data;
	struct rwnx_vif *vif = netdev_priv(ndev);
	char tmp[64];
	u8 ac;
	u16 cwmin;
	u16 cwmax;
	u8 aifs;
	u16 txop;

	if (count > sizeof(tmp)) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "usage:\n echo [ac] [aifs] [cwmin] [cwmax] [txop] > edca_param\n");
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int num = sscanf(tmp, "%hhu %hhu %hu %hu %hu", &ac, &aifs,
				 &cwmin, &cwmax, &txop);

		if (num < 5)
			return -EINVAL;

		WQ_DBG(DM_GENERIC, DL_ERR,
		       "set_edca_param ac:%d aifs:%hhu cwmin:%hu cwmax:%hu txop:%hu\n",
		       ac, aifs, cwmin, cwmax, txop);

		wq_set_edca_params(vif, ac, aifs, cwmin, cwmax, txop);
	}

	return count;
}

static int inline proc_dbg_print_edca_param(struct seq_file *seq, u32 param[4])
{
	int i;
	u8 aifs;
	u16 cwmin;
	u16 cwmax;
	u16 txop;
	const char ac_str[][32] = { "edca_param_bk", "edca_param_be",
				    "edca_param_vi", "edca_param_vo" };

	if (seq) {
		for (i = 0; i < 4; i++) {
			aifs = param[i] & 0x000F;
			cwmin = (param[i] >> 4) & 0x000F;
			cwmax = (param[i] >> 8) & 0x000F;
			txop = (param[i] >> 12) & 0x000F;

			seq_printf(
				seq,
				"%s[%d] aifs:%hhu cwmin:%hu cwmax:%hu txop:%hu\n",
				ac_str[i], i, aifs, cwmin, cwmax, txop);
		}
	}

	return 0;
}

static int proc_get_edca_param(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct dbg_dfx_edca_param dfx_edca;
	int macid = 0;

	wq_get_edca_param(vif->rwnx_hw, macid, &dfx_edca);

	proc_dbg_print_edca_param(seq, dfx_edca.ac_param);

	return 0;
}

static int proc_get_agc_gain(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct dbg_dfx_agc_code_param agc_code;
	int macid = 0;

	wq_get_agc_code_get(vif->rwnx_hw, macid, &agc_code);

	seq_printf(seq, "dig_gain80m: %u\n", agc_code.dig_gain80m);
	seq_printf(seq, "dig_gain40m: %u\n", agc_code.dig_gain40m);
	seq_printf(seq, "dig_gain20m: %u\n", agc_code.dig_gain20m);
	seq_printf(seq, "rf_rx_gain_db: %u\n", agc_code.rf_rx_gain_db);
	seq_printf(seq, "rf_rx_gain_code: %x\n", agc_code.rf_rx_gain_code);
	seq_printf(seq, "rf_tx_gain_code: %x\n", agc_code.rf_tx_gain_code);

	return 0;
}

static u32 chan_noise_duration = 100;
ssize_t proc_set_chan_noise_info(struct file *file, const char __user *buffer,
				 size_t count, loff_t *pos, void *data)
{
	char tmp[32];
	u32 val;

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int num = sscanf(tmp, "%u", &val);

		if (num < 1)
			return -EINVAL;

		WQ_DBG(DM_GENERIC, DL_ERR, "set chan noise duration, val=%u\n",
		       val);

		chan_noise_duration = val;
	}

	return count;
}

static int proc_get_chan_noise_info(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct wq_dbg_chan_noise_info info;
	int ret;

	ret = wq_get_chan_noise_info(vif, chan_noise_duration, &info);

	if (ret == 0) {
		seq_printf(seq, "statis duration: %u us\n", info.duration);

		seq_printf(seq, "rssi_nonwifi: %d dBm\n", info.rssi_nonwifi);
		seq_printf(seq, "nonwifi_busy_time: %u us (%d %%)\n",
			   info.nonwifi_busy_time,
			   info.nonwifi_busy_time / info.duration);

		seq_printf(seq, "groud_noise_pri20: %d dBm\n",
			   info.groud_noise_pri20);
		seq_printf(seq, "groud_noise_pri40: %d dBm\n",
			   info.groud_noise_pri40);
		seq_printf(seq, "groud_noise_pri80: %d dBm\n",
			   info.groud_noise_pri80);

	} else {
		seq_printf(seq, "get channel noise info failed!\n");
	}

	return 0;
}

static u32 chan_util_duration = 100;
ssize_t proc_set_chan_util_info(struct file *file, const char __user *buffer,
				size_t count, loff_t *pos, void *data)
{
	char tmp[32];
	u32 val;

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int num = sscanf(tmp, "%u", &val);

		if (num < 1)
			return -EINVAL;

		WQ_DBG(DM_GENERIC, DL_ERR, "set chan util duration, val=%u\n",
		       val);

		chan_util_duration = val;
	}

	return count;
}

static int proc_get_chan_util_info(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct wq_dbg_chan_util_info info;
	int ret;

	ret = wq_get_chan_util_info(vif, chan_noise_duration, &info);

	if (ret == 0) {
		seq_printf(seq, "statis duration: %u us\n", info.duration);
		seq_printf(seq, "total_busy_time: %u us (%d %%)\n",
			   info.total_busy_time,
			   info.total_busy_time * 100 / info.duration);

		seq_printf(seq, "tx_time_total: %u us (%d %%)\n",
			   info.tx_time_total,
			   info.tx_time_total * 100 / info.duration);
		seq_printf(seq, "rx_time_self: %u us (%d %%)\n",
			   info.rx_time_self,
			   info.rx_time_self * 100 / info.duration);
		seq_printf(seq, "rx_time_other: %u us (%d %%)\n",
			   info.rx_time_other,
			   info.rx_time_other * 100 / info.duration);

		seq_printf(seq, "cca_idle_pri_20: %u us (%d %%)\n",
			   info.cca_idle_pri_20,
			   info.cca_idle_pri_20 * 100 / info.duration);
		seq_printf(seq, "cca_idle_pri_40: %u us (%d %%)\n",
			   info.cca_idle_pri_40,
			   info.cca_idle_pri_40 * 100 / info.duration);
		seq_printf(seq, "cca_idle_pri_80: %u us (%d %%)\n",
			   info.cca_idle_pri_80,
			   info.cca_idle_pri_80 * 100 / info.duration);

	} else {
		seq_printf(seq, "get channel util info failed!\n");
	}

	return 0;
}

ssize_t proc_set_crc_stats(struct file *file, const char __user *buffer,
			   size_t count, loff_t *pos, void *data)
{
	struct net_device *ndev = data;
	struct rwnx_vif *vif = netdev_priv(ndev);
	char tmp[32];
	u32 val;

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int num = sscanf(tmp, "%u", &val);

		if (num < 1)
			return -EINVAL;

		if (val == 1 || val == 0) {
			wq_crc_stats_enable(vif, val);
		} else {
			WQ_DBG(DM_GENERIC, DL_ERR, "invalid cmd type, val=%u\n",
			       val);
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "example: echo 0 > fw_crc_stats\n");
			return count;
		}
	}

	return count;
}

static int proc_get_crc_stats(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct wq_dbg_crc_stats stats;
	int ret;
	u32 total_cnt, pass_cnt, fail_cnt;

	ret = wq_get_crc_stats(vif, &stats);

	if (ret == 0) {
		seq_printf(seq, "crc_pass_stat_dsss     : %hu\n",
			   stats.crc_pass_stat_dsss);
		seq_printf(seq, "crc_pass_stat_nonht    : %hu\n",
			   stats.crc_pass_stat_nonht);
		seq_printf(seq, "crc_pass_stat_nonht_dup: %hu\n",
			   stats.crc_pass_stat_nonht_dup);
		seq_printf(seq, "crc_pass_stat_ht_mm    : %hu\n",
			   stats.crc_pass_stat_ht_mm);
		seq_printf(seq, "crc_pass_stat_ht_gf    : %hu\n",
			   stats.crc_pass_stat_ht_gf);
		seq_printf(seq, "crc_pass_stat_vht      : %hu\n",
			   stats.crc_pass_stat_vht);
		seq_printf(seq, "crc_pass_stat_he_su    : %hu\n",
			   stats.crc_pass_stat_he_su);
		seq_printf(seq, "crc_pass_stat_he_mu    : %hu\n",
			   stats.crc_pass_stat_he_mu);
		seq_printf(seq, "crc_pass_stat_he_ext_su: %hu\n",
			   stats.crc_pass_stat_he_ext_su);
		seq_printf(seq, "crc_pass_stat_he_tb    : %hu\n",
			   stats.crc_pass_stat_he_tb);

		seq_printf(seq, "crc_fail_stat_dsss     : %hu\n",
			   stats.crc_fail_stat_dsss);
		seq_printf(seq, "crc_fail_stat_nonht    : %hu\n",
			   stats.crc_fail_stat_nonht);
		seq_printf(seq, "crc_fail_stat_nonht_dup: %hu\n",
			   stats.crc_fail_stat_nonht_dup);
		seq_printf(seq, "crc_fail_stat_ht_mm    : %hu\n",
			   stats.crc_fail_stat_ht_mm);
		seq_printf(seq, "crc_fail_stat_ht_gf    : %hu\n",
			   stats.crc_fail_stat_ht_gf);
		seq_printf(seq, "crc_fail_stat_vht      : %hu\n",
			   stats.crc_fail_stat_vht);
		seq_printf(seq, "crc_fail_stat_he_su    : %hu\n",
			   stats.crc_fail_stat_he_su);
		seq_printf(seq, "crc_fail_stat_he_mu    : %hu\n",
			   stats.crc_fail_stat_he_mu);
		seq_printf(seq, "crc_fail_stat_he_ext_su: %hu\n",
			   stats.crc_fail_stat_he_ext_su);
		seq_printf(seq, "crc_fail_stat_he_tb    : %hu\n",
			   stats.crc_fail_stat_he_tb);

		seq_printf(seq, "rx_overrun : %u\n", stats.rx_overrun);

		fail_cnt = (u32)stats.crc_fail_stat_dsss +
			   stats.crc_fail_stat_nonht +
			   stats.crc_fail_stat_nonht_dup +
			   stats.crc_fail_stat_ht_mm +
			   stats.crc_fail_stat_ht_gf + stats.crc_fail_stat_vht +
			   stats.crc_fail_stat_he_su +
			   stats.crc_fail_stat_he_mu +
			   stats.crc_fail_stat_he_ext_su +
			   stats.crc_fail_stat_he_tb;
		pass_cnt = (u32)stats.crc_pass_stat_dsss +
			   stats.crc_pass_stat_nonht +
			   stats.crc_pass_stat_nonht_dup +
			   stats.crc_pass_stat_ht_mm +
			   stats.crc_pass_stat_ht_gf + stats.crc_pass_stat_vht +
			   stats.crc_pass_stat_he_su +
			   stats.crc_pass_stat_he_mu +
			   stats.crc_pass_stat_he_ext_su +
			   stats.crc_pass_stat_he_tb;
		total_cnt = fail_cnt + pass_cnt;
		if (total_cnt != 0)
			seq_printf(seq, "Rx PER : %u\n",
				   (fail_cnt * 100 + 50) / total_cnt);

	} else {
		seq_printf(seq, "get crc stats failed!\n");
	}

	return 0;
}

ssize_t proc_set_agc_lock_stats(struct file *file, const char __user *buffer,
				size_t count, loff_t *pos, void *data)
{
	struct net_device *ndev = data;
	struct rwnx_vif *vif = netdev_priv(ndev);
	char tmp[32];
	u32 val;

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int num = sscanf(tmp, "%u", &val);

		if (num < 1)
			return -EINVAL;

		if (val == 1 || val == 0) {
			wq_agc_lock_stats_enable(vif, val);
		} else {
			WQ_DBG(DM_GENERIC, DL_ERR, "invalid cmd type, val=%u\n",
			       val);
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "example: echo 0 > fw_agc_lock_stats\n");
			return count;
		}
	}

	return count;
}

static int proc_get_agc_lock_stats(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct wq_dbg_agc_lock_stats stats;
	int ret;

	ret = wq_get_agc_lock_stats(vif, &stats);

	if (ret == 0) {
		seq_printf(seq, "agc lock count:\n");
		seq_printf(seq, "agc_lock_time_cnt   : %u\n",
			   stats.agc_lock_time_cnt);
		seq_printf(seq, "agc_lock_timeout_thr: %hu\n",
			   stats.agc_lock_timeout_thr);
		seq_printf(seq, "agc_lock_cnt        : %hu\n",
			   stats.agc_lock_cnt);
		seq_printf(seq, "agc_lock_timeout_cnt: %hu\n",
			   stats.agc_lock_timeout_cnt);
	} else {
		seq_printf(seq, "get agc lock stats failed!\n");
	}

	return 0;
}

ssize_t proc_set_phy_sig_stats(struct file *file, const char __user *buffer,
			       size_t count, loff_t *pos, void *data)
{
	struct net_device *ndev = data;
	struct rwnx_vif *vif = netdev_priv(ndev);
	char tmp[32];
	u32 val;

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int num = sscanf(tmp, "%u", &val);

		if (num < 1)
			return -EINVAL;

		if (val == 1 || val == 0) {
			wq_phy_signal_stats_enable(vif, val);
		} else {
			WQ_DBG(DM_GENERIC, DL_ERR, "invalid cmd type, val=%u\n",
			       val);
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "example: echo 0 > fw_phy_signal_stats\n");
			return count;
		}
	}

	return count;
}

static int proc_get_phy_sig_stats(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct wq_dbg_phy_signal_stats stats;
	int ret;

	ret = wq_get_phy_signal_stats(vif, &stats);

	if (ret == 0) {
		seq_printf(seq, "phy sync count:\n");
		seq_printf(seq, "coare sync count: %hu\n",
			   stats.sync_stats.coarse_sync_cnt);
		seq_printf(seq, "fine sync count : %hu\n",
			   stats.sync_stats.fine_sync_cnt);

		seq_printf(seq, "\n");
		seq_printf(seq, "signal crc cnt:\n");
		seq_printf(seq, "lsig_fail_cnt    : %hu\n",
			   stats.sig_crc_stats.lsig_fail_cnt);
		seq_printf(seq, "lsig_ok_cnt      : %hu\n",
			   stats.sig_crc_stats.lsig_ok_cnt);
		seq_printf(seq, "herlsig_fail_cnt : %hu\n",
			   stats.sig_crc_stats.herlsig_fail_cnt);
		seq_printf(seq, "herlsig_ok_cnt   : %hu\n",
			   stats.sig_crc_stats.herlsig_ok_cnt);
		seq_printf(seq, "htsig_fail_cnt   : %hu\n",
			   stats.sig_crc_stats.htsig_fail_cnt);
		seq_printf(seq, "htsig_ok_cnt     : %hu\n",
			   stats.sig_crc_stats.htsig_ok_cnt);
		seq_printf(seq, "vhtsiga_fail_cnt : %hu\n",
			   stats.sig_crc_stats.vhtsiga_fail_cnt);
		seq_printf(seq, "vhtsiga_ok_cnt   : %hu\n",
			   stats.sig_crc_stats.vhtsiga_ok_cnt);
		seq_printf(seq, "vhtsigb_fail_cnt : %hu\n",
			   stats.sig_crc_stats.vhtsigb_fail_cnt);
		seq_printf(seq, "vhtsigb_ok_cnt   : %hu\n",
			   stats.sig_crc_stats.vhtsigb_ok_cnt);
		seq_printf(seq, "hesiga_fail_cnt  : %hu\n",
			   stats.sig_crc_stats.hesiga_fail_cnt);
		seq_printf(seq, "hesiga_ok_cnt    : %hu\n",
			   stats.sig_crc_stats.hesiga_ok_cnt);
		seq_printf(seq, "hesigb_fail_cnt  : %hu\n",
			   stats.sig_crc_stats.hesigb_fail_cnt);
		seq_printf(seq, "hesigb_ok_cnt    : %hu\n",
			   stats.sig_crc_stats.hesigb_ok_cnt);
		seq_printf(seq, "data_crc_fail_cnt: %hu\n",
			   stats.sig_crc_stats.data_crc_fail_cnt);
		seq_printf(seq, "data_crc_ok_cnt  : %hu\n",
			   stats.sig_crc_stats.data_crc_ok_cnt);
	} else {
		seq_printf(seq, "get phy signal stats failed!\n");
	}

	return 0;
}

static int proc_get_ac_delay_time(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct wq_dbg_dfx_ac_delay ac_delay;
	int macid = 0;
	int ret = 0;

	ret = wq_get_ac_delay_time(vif->rwnx_hw, macid, &ac_delay);

	if (ret == 0) {
		seq_printf(seq, "bk_delay_time: %uus\n",
			   ac_delay.bk_delay_time);
		seq_printf(seq, "be_delay_time: %uus\n",
			   ac_delay.be_delay_time);
		seq_printf(seq, "vi_delay_time: %uus\n",
			   ac_delay.vi_delay_time);
		seq_printf(seq, "vo_delay_time: %uus\n",
			   ac_delay.vo_delay_time);
	}

	return 0;
}

static int proc_get_trx_pkt_info(struct seq_file *seq, void *v)
{
	struct wq_dbg_dfx_pkt_info pkt_info;
	int macid = 0;
	int ret = 0;

	ret = wq_get_trx_pkt_info(&pkt_info);

	if (ret == 0) {
		seq_printf(seq, "snr: %d\n", pkt_info.snr);
		seq_printf(seq, "rxrssi: %d\n", pkt_info.rssi);
		for (macid = 0; macid < 2; macid++) {
			seq_printf(seq, "mac%d  tx_mcs = %d\n", macid,
				   pkt_info.mcs_tx[macid]);
			seq_printf(seq, "mac%d  rx_mcs = %d\n", macid,
				   pkt_info.mcs_rx[macid]);
			seq_printf(seq, "mac%d  tx_bw = %d\n", macid,
				   pkt_info.bw_tx[macid]);
			seq_printf(seq, "mac%d  rx_bw = %d\n", macid,
				   pkt_info.bw_rx[macid]);
		}
	}

	return 0;
}

static int proc_get_phy_rf_trx_state(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct wq_dbg_phy_rf_trx_state phy_rf_trx_state;
	int ret = 0;
	int macid = 0;

	ret = wq_get_phy_rf_trx_state(vif->rwnx_hw, macid, &phy_rf_trx_state);

	if (ret == 0) {
		seq_printf(seq, "phy mpif_tx_state :%d\n",
			   phy_rf_trx_state.phy_trx_state.mpif_tx_state);
		seq_printf(seq, "phy mpif_rx_state :%d\n",
			   phy_rf_trx_state.phy_trx_state.mpif_rx_state);
		seq_printf(seq, "phy mfsm_tx_state :%d\n",
			   phy_rf_trx_state.phy_trx_state.mfsm_tx_state);
		seq_printf(seq, "phy mfsm_rx_state :%d\n",
			   phy_rf_trx_state.phy_trx_state.mfsm_rx_state);

		seq_printf(seq, "rf rf_tx_fsm :%d\n",
			   phy_rf_trx_state.rf_trx_state.rf_tx_fsm);
		seq_printf(seq, "rf rf_rx_fsm :%d\n",
			   phy_rf_trx_state.rf_trx_state.rf_rx_fsm);
	}
	return 0;
}

static int proc_get_freq_dc_state(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct dbg_freq_dc_state freq_rf_state;
	int ret = 0;
	int macid = 0;

	ret = wq_get_freq_dc_state(vif->rwnx_hw, macid, &freq_rf_state);

	if (ret == 0) {
		seq_printf(seq, "freq_offset_pha :%d\n",
			   freq_rf_state.freq_offset.freq_offset_pha);
		seq_printf(seq, "freq_offset_hz :%d(Hz)\n",
			   freq_rf_state.freq_offset.freq_offset_hz);
		seq_printf(seq, "dc_i_af_comp :%d\n",
			   freq_rf_state.rx_dc_af.dc_i_af_comp);
		seq_printf(seq, "dc_q_af_comp :%d\n",
			   freq_rf_state.rx_dc_af.dc_q_af_comp);
	}
	return 0;
}

static int proc_test_get(struct seq_file *seq, void *v)
{
	seq_printf(seq, "proc_test_get\n");

	return 0;
}

ssize_t proc_test_set(struct file *file, const char __user *buffer,
		      size_t count, loff_t *pos, void *data)
{
	char tmp[32];
	u8 val;

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int num = sscanf(tmp, "%hhd", &val);

		if (num < 1)
			return -EINVAL;

		WQ_DBG(DM_GENERIC, DL_ERR, "proc_test_set, val=%d\n", val);
	}

	return count;
}

static int proc_get_ampdu_stats(struct seq_file *seq, void *v)
{
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	struct rwnx_stats *stats;
	int i;
	int ret;

	stats = kmalloc(sizeof(struct rwnx_stats), GFP_KERNEL);
	ret = wq_get_ampdu_stats(vif->rwnx_hw, stats);
	if (ret == 0) {
		seq_printf(seq, "ampdu[len]    tx     rx\n");
		for (i = 0; i < IEEE80211_MAX_AMPDU_BUF; i++) {
			if (stats->ampdus_tx[i] || stats->ampdus_rx[i]) {
				seq_printf(seq, "    [%d]       %d     %d\n",
					   i + 1, stats->ampdus_tx[i],
					   stats->ampdus_rx[i]);
			}
		}
	} else {
		seq_printf(seq, "get ampdu stats failed!\n");
	}
	kfree(stats);

	return 0;
}

static int proc_get_fw_custom_cmd(struct seq_file *seq, void *v)
{
	seq_printf(seq, "usage:\n");
	seq_printf(seq, "echo cmd info > fw_custom_cmd\n");
	seq_printf(seq, "cmd list:\n");
	seq_printf(seq, "%d, DBG_CMD_PRINT_ERR, print err msg\n",
		   DBG_CMD_PRINT_ERR);
	seq_printf(seq, "%d, DBG_CMD_SEND_NULL_PKT, send null data\n",
		   DBG_CMD_SEND_NULL_PKT);
	seq_printf(seq, "%d, DBG_CMD_MAX, cmd max value\n", DBG_CMD_MAX);

	return 0;
}

ssize_t proc_set_fw_custom_cmd(struct file *file, const char __user *buffer,
			       size_t count, loff_t *pos, void *data)
{
	struct net_device *ndev = data;
	struct rwnx_vif *vif = netdev_priv(ndev);
	char tmp[32];
	u8 cmd;
	u8 info;

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int num = sscanf(tmp, "%hhu %hhu", &cmd, &info);

		if (num < 2)
			return -EINVAL;

		if (cmd < DBG_CMD_MAX) {
			wq_send_fw_cmd(vif, cmd, info);
		} else {
			WQ_DBG(DM_GENERIC, DL_ERR, "invalid cmd=%u\n", cmd);
			return count;
		}
	}

	return count;
}

struct kiwi_proc_dfx_hdl wq_dfx_hdls[] = {
	KIWI_PROC_DFX_HDL("test", proc_test_get, proc_test_set),
	KIWI_PROC_DFX_HDL("vif_info", proc_get_vif_info, NULL),
	KIWI_PROC_DFX_HDL("skb_info", proc_get_skb_stats, NULL),
	KIWI_PROC_DFX_HDL("sec_info", proc_get_security_info, NULL),
	KIWI_PROC_DFX_HDL("fw_sta_trx_stats", proc_get_sta_trx_stats, NULL),
	KIWI_PROC_DFX_HDL("edca_param", proc_get_edca_param,
			  proc_set_edca_param),
	KIWI_PROC_DFX_HDL("agc_gain", proc_get_agc_gain, NULL),
	KIWI_PROC_DFX_HDL("fw_chan_noise_info", proc_get_chan_noise_info,
			  proc_set_chan_noise_info),
	KIWI_PROC_DFX_HDL("fw_chan_util_info", proc_get_chan_util_info,
			  proc_set_chan_util_info),
	KIWI_PROC_DFX_HDL("ampdu_stats", proc_get_ampdu_stats, NULL),
	KIWI_PROC_DFX_HDL("ac_delay", proc_get_ac_delay_time, NULL),
	KIWI_PROC_DFX_HDL("trx_pkt_info", proc_get_trx_pkt_info, NULL),
	KIWI_PROC_DFX_HDL("phy_rf_trx_state", proc_get_phy_rf_trx_state, NULL),
	KIWI_PROC_DFX_HDL("fw_crc_stats", proc_get_crc_stats,
			  proc_set_crc_stats),
	KIWI_PROC_DFX_HDL("fw_phy_signal_stats", proc_get_phy_sig_stats,
			  proc_set_phy_sig_stats),
	KIWI_PROC_DFX_HDL("fw_agc_lock_stats", proc_get_agc_lock_stats,
			  proc_set_agc_lock_stats),
	KIWI_PROC_DFX_HDL("freq_dc_state", proc_get_freq_dc_state, NULL),
	KIWI_PROC_DFX_HDL("fw_custom_cmd", proc_get_fw_custom_cmd,
			  proc_set_fw_custom_cmd),
};

static int kiwi_proc_dfx_open(struct inode *inode, struct file *file)
{
	ssize_t index = (ssize_t)PDE_DATA(inode);
	void *private = proc_get_parent_data(inode);
	struct kiwi_proc_dfx_hdl *hdl = wq_dfx_hdls + index;
	int (*show)(struct seq_file *, void *) = hdl->show;

	if (show) {
		return single_open(file, show, private);
	}

	return 0;
}

static ssize_t kiwi_proc_dfx_write(struct file *file, const char __user *buffer,
				   size_t count, loff_t *pos)
{
	struct inode *inode = file_inode(file);
	ssize_t index = (ssize_t)PDE_DATA(inode);
	void *private = proc_get_parent_data(inode);
	struct kiwi_proc_dfx_hdl *hdl = wq_dfx_hdls + index;
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *,
			 void *) = hdl->write;

	if (write) {
		return write(file, buffer, count, pos, private);
	}

	return -EROFS;
}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_dfx_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_dfx_open,
	.write = kiwi_proc_dfx_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_dfx_fops = {
	.proc_open = kiwi_proc_dfx_open,
	.proc_write = kiwi_proc_dfx_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

static int wq_proc_dfx_create(struct wireless_dev *wdev)
{
	struct net_device *ndev = wdev->netdev;
	ssize_t i;

	dfx_dir_entry =
		proc_mkdir_data(PROC_DIR "/dfx", 0555, NULL, (void *)ndev);
	for (i = 0; i < sizeof(wq_dfx_hdls) / sizeof(struct kiwi_proc_dfx_hdl);
	     i++) {
		proc_create_data(wq_dfx_hdls[i].name, 0666, dfx_dir_entry,
				 &kiwi_proc_dfx_fops, (void *)i);
	}
	return 0;
}

static void wq_proc_dfx_remove(void)
{
	ssize_t i;

	for (i = 0; i < sizeof(wq_dfx_hdls) / sizeof(struct kiwi_proc_dfx_hdl);
	     i++) {
		remove_proc_entry(wq_dfx_hdls[i].name, dfx_dir_entry);
	}
	remove_proc_entry(PROC_DIR "/dfx", NULL);
	dfx_dir_entry = NULL;
}

#else

static inline int wq_proc_dfx_create(struct wireless_dev *wdev)
{
	return 0;
}

static void wq_proc_dfx_remove(void)
{
}

#endif /* DEBUG_WQ_DFX */

static ssize_t kiwi_proc_write(struct file *file, const char __user *buffer,
			       size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *priv = seq->private;
	char tmp_buf[8] = { 0 };
	long kiwi_proc_dbg = 0;
	int ret, pktlog_cfg_flags;

	if (!count)
		return 0;

	if (count != 7) {
		kiwi_proc_dbg = (wq_conf.wq_dbg_mod << 8) + wq_conf.wq_dbg_lv;
		printk("ex: echo \"0x%04x\" > /proc/driver/kiwi_usb/dbg\n",
		       (unsigned int)kiwi_proc_dbg);
		return -EINVAL;
	}

	if (copy_from_user(tmp_buf, buffer, count))
		return -EFAULT;

	tmp_buf[count - 1] = '\0';
	if ((ret = kstrtol(tmp_buf, 0, &kiwi_proc_dbg)) == 0) {
		wq_conf.wq_dbg_mod = ((kiwi_proc_dbg & 0xFF00) >> 8);
		wq_conf.wq_dbg_lv = kiwi_proc_dbg & 0xFF;
		printk("wq_dbg_flag = 0x%02x, wq_dbg_level = 0x%02x\n",
		       (unsigned int)wq_conf.wq_dbg_mod,
		       (unsigned int)wq_conf.wq_dbg_lv);
	}

	pktlog_cfg_flags = ((wq_conf.wq_dbg_mod & DM_PKTDUMP) ? 1 : 0) +
			   ((wq_conf.wq_dbg_mod & DM_CRDT) ? 2 : 0);
	if (priv)
		rwnx_send_dbg_pktlog_cfg_req(priv, pktlog_cfg_flags);

	return count;
}

static ssize_t kiwi_proc_read(struct file *file, char __user *buffer,
			      size_t count, loff_t *pos)
{
	char info[256];
	int offset = 0;

	if (*pos > 0) {
		return 0;
	}

	offset += snprintf(info + offset, sizeof(info) - offset, "DM_IPC: %s\n",
			   (wq_conf.wq_dbg_mod & DM_IPC) ? "ON" : "OFF");
	offset +=
		snprintf(info + offset, sizeof(info) - offset, "DM_TRBUS: %s\n",
			 (wq_conf.wq_dbg_mod & DM_TRBUS) ? "ON" : "OFF");
	offset += snprintf(info + offset, sizeof(info) - offset, "DM_RX:  %s\n",
			   (wq_conf.wq_dbg_mod & DM_RX) ? "ON" : "OFF");
	offset += snprintf(info + offset, sizeof(info) - offset, "DM_TX:  %s\n",
			   (wq_conf.wq_dbg_mod & DM_TX) ? "ON" : "OFF");
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "DM_IEEE80211: %s\n",
			   (wq_conf.wq_dbg_mod & DM_IEEE80211) ? "ON" : "OFF");
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "DM_GENERIC: %s\n",
			   (wq_conf.wq_dbg_mod & DM_GENERIC) ? "ON" : "OFF");
	offset +=
		snprintf(info + offset, sizeof(info) - offset, "DM_CRDT: %s\n",
			 (wq_conf.wq_dbg_mod & DM_CRDT) ? "ON" : "OFF");
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "DM_PKTDUMP: %s\n",
			   (wq_conf.wq_dbg_mod & DM_PKTDUMP) ? "ON" : "OFF");
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "dbg_mod:0x%02x,dbg_lv:0x%02x\n", wq_conf.wq_dbg_mod,
			   wq_conf.wq_dbg_lv);

	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

static int kiwi_proc_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int kiwi_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, kiwi_proc_show, PDE_DATA(inode));
}

static int kiwi_proc_fwdbg_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int kiwi_fwdbg_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, kiwi_proc_fwdbg_show, PDE_DATA(inode));
}

static ssize_t proc_rwnx_dbgfs_fw_dbg_write(struct rwnx_hw *priv,
					    const char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	char buf[32];
	int idx = 0;
	u32 mod = 0;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

#define RWNX_MOD_TOKEN(str, val)                                               \
	if (strncmp(&buf[idx], str, sizeof(str) - 1) == 0) {                   \
		idx += sizeof(str) - 1;                                        \
		mod |= val;                                                    \
		continue;                                                      \
	}

#define RWNX_DBG_TOKEN(str, val)                                               \
	if (strncmp(&buf[idx], str, sizeof(str) - 1) == 0) {                   \
		idx += sizeof(str) - 1;                                        \
		dbg = val;                                                     \
		goto dbg_done;                                                 \
	}

	while ((idx + 4) < len) {
		if (strncmp(&buf[idx], "MOD:", 4) == 0) {
			idx += 4;
			RWNX_MOD_TOKEN("ALL", 0xffffffff);
			RWNX_MOD_TOKEN("KE", BIT(0));
			RWNX_MOD_TOKEN("DBG", BIT(1));
			RWNX_MOD_TOKEN("IPC", BIT(2));
			RWNX_MOD_TOKEN("DMA", BIT(3));
			RWNX_MOD_TOKEN("MM", BIT(4));
			RWNX_MOD_TOKEN("TX", BIT(5));
			RWNX_MOD_TOKEN("RX", BIT(6));
			RWNX_MOD_TOKEN("PHY", BIT(7));
			RWNX_MOD_TOKEN("CRDT", BIT(9));
			idx++;
		} else if (strncmp(&buf[idx], "DBG:", 4) == 0) {
			u32 dbg = 0;
			idx += 4;
			RWNX_DBG_TOKEN("NONE", 0);
			RWNX_DBG_TOKEN("CRT", 1);
			RWNX_DBG_TOKEN("ERR", 2);
			RWNX_DBG_TOKEN("WRN", 3);
			RWNX_DBG_TOKEN("INF", 4);
			RWNX_DBG_TOKEN("VRB", 5);
			idx++;
			continue;
		dbg_done:
			if (priv) {
				WQ_DBG(DM_GENERIC, DL_WRN, "kiwi_sev_flag:%d\n",
				       dbg);
				rwnx_send_dbg_set_sev_filter_req(priv, dbg);
			}
		} else {
			idx++;
		}
	}

	if (mod) {
		if (priv) {
			WQ_DBG(DM_GENERIC, DL_WRN, "kiwi_mod_flag:%02x\n", mod);
			rwnx_send_dbg_set_mod_filter_req(priv, mod);
		}
	}

	return count;
}

static ssize_t kiwi_fwdbg_proc_write(struct file *file,
				     const char __user *buffer, size_t count,
				     loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *priv = seq->private;

	return proc_rwnx_dbgfs_fw_dbg_write(priv, buffer, count, pos);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.write = kiwi_proc_write,
	.read = kiwi_proc_read,
	.llseek = noop_llseek,
	.release = single_release,
};

static const struct wq_proc_ops kiwi_proc_fwdbg_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_fwdbg_proc_open,
	.write = kiwi_fwdbg_proc_write,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_fops = {
	.proc_open = kiwi_proc_open,
	.proc_write = kiwi_proc_write,
	.proc_read = kiwi_proc_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};

static const struct wq_proc_ops kiwi_proc_fwdbg_fops = {
	.proc_open = kiwi_fwdbg_proc_open,
	.proc_write = kiwi_fwdbg_proc_write,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

/**
 * rwnx_rx_ctrl
 */
static int kiwi_proc_rx_ctrl_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int kiwi_proc_rx_ctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, kiwi_proc_rx_ctrl_show, PDE_DATA(inode));
}

static ssize_t kiwi_proc_rx_ctrl_write(struct file *file,
				       const char __user *buffer, size_t count,
				       loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	char tmp_buf[8] = { 0 };
	int ret;
	long rx_ctrl_l = 0;
	u8 rx_ampdu_disable_t = 0;

	if (!count)
		return 0;

	if (count != 7) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "ex: echo \"0x0001\" > /proc/driver/kiwi_usb/rx_ctrl\n");
		return -EINVAL;
	}

	if (copy_from_user(tmp_buf, buffer, count))
		return -EFAULT;

	if ((ret = kstrtol(tmp_buf, 0, &rx_ctrl_l)) == 0) {
		rwnx_hw->feature.tcp_drop_disable =
			!!(rx_ctrl_l & TCP_DROP_DISABLE);
		rwnx_hw->feature.timeout_disable =
			!!(rx_ctrl_l & TIMEOUT_DISABLE);
		rwnx_hw->feature.reorder_disable =
			!!(rx_ctrl_l & REORDER_DISABLE);
		rwnx_hw->feature.rx_ampdu_disable =
			!!(rx_ctrl_l & RX_AMPDU_DISABLE);
		rwnx_hw->feature.rx_rate_log_disable =
			!!(rx_ctrl_l & RX_RATE_LOG_DISABLE);
		rwnx_hw->feature.rx_amsdu_disable =
			!!(rx_ctrl_l & RX_AMSDU_DISABLE);

		WQ_DBG(DM_GENERIC, DL_WRN, "rx_ctrl:\n");
		WQ_DBG(DM_GENERIC, DL_WRN, "tcp_drop is %s\n",
		       WQ_FEATURE_NSTATE(tcp_drop_disable));
		WQ_DBG(DM_GENERIC, DL_WRN, "timeout is %s\n",
		       WQ_FEATURE_NSTATE(timeout_disable));
		WQ_DBG(DM_GENERIC, DL_WRN, "reorder is %s\n",
		       WQ_FEATURE_NSTATE(reorder_disable));
		WQ_DBG(DM_GENERIC, DL_WRN, "rx_ampdu is %s\n",
		       WQ_FEATURE_NSTATE(rx_ampdu_disable));
		WQ_DBG(DM_GENERIC, DL_WRN, "rx_rate_log is %s\n",
		       WQ_FEATURE_NSTATE(rx_rate_log_disable));
		WQ_DBG(DM_GENERIC, DL_WRN, "rx_amsdu is %s\n",
		       WQ_FEATURE_NSTATE(rx_amsdu_disable));

		if (rx_ctrl_l & RX_AMPDU_DISABLE) {
			rx_ampdu_disable_t = 1;
		}
		if (rx_ctrl_l & RX_AMSDU_DISABLE) {
			rx_ampdu_disable_t |= 0x1 << 1;
		}

		if (RWNX_INFO_NOTIFY_SET(rwnx_hw, MSG_TYPE_RX_AMPDU_DISABLE,
					 rx_ampdu_disable_t)) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "set rx_ampdu_disable (%x) fail\n",
			       rx_ampdu_disable_t);
		} else {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "set rx_ampdu_disable (%x) success\n",
			       rx_ampdu_disable_t);
		}
	}

	return count;
}

static ssize_t kiwi_proc_rx_ctrl_read(struct file *file, char __user *buffer,
				      size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	char info[256];
	int offset = 0;

	if (*pos > 0) {
		return 0;
	}

	offset +=
		snprintf(info + offset, sizeof(info) - offset, "tcp_drop: %s\n",
			 WQ_FEATURE_NSTATE(tcp_drop_disable));
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "timeout: %s\n", WQ_FEATURE_NSTATE(timeout_disable));
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "reorder: %s\n", WQ_FEATURE_NSTATE(reorder_disable));
	offset +=
		snprintf(info + offset, sizeof(info) - offset, "rx_ampdu: %s\n",
			 WQ_FEATURE_NSTATE(rx_ampdu_disable));
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "rx_rate_log: %s\n",
			   WQ_FEATURE_NSTATE(rx_rate_log_disable));
	offset +=
		snprintf(info + offset, sizeof(info) - offset, "rx_amsdu: %s\n",
			 WQ_FEATURE_NSTATE(rx_amsdu_disable));

	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_rx_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_rx_ctrl_open,
	.write = kiwi_proc_rx_ctrl_write,
	.read = kiwi_proc_rx_ctrl_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_rx_ctrl_fops = {
	.proc_open = kiwi_proc_rx_ctrl_open,
	.proc_write = kiwi_proc_rx_ctrl_write,
	.proc_read = kiwi_proc_rx_ctrl_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

/**
 * rwnx_misc_ctrl
 */
static ssize_t kiwi_proc_misc_ctrl_write(struct file *file,
					 const char __user *buffer,
					 size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	char tmp_buf[7] = { 0 };
	int ret;
	long misc_ctrl_l = 0;

	if (!count)
		return 0;

	if (count != 7) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "ex: echo \"0x0001\" > /proc/driver/kiwi_usb/misc_ctrl\n");
		return -EINVAL;
	}

	if (copy_from_user(tmp_buf, buffer, count))
		return -EFAULT;

	tmp_buf[count - 1] = '\0';
	if ((ret = kstrtol(tmp_buf, 0, &misc_ctrl_l)) == 0) {
		rwnx_hw->feature.scan_disable = !!(misc_ctrl_l & SCAN_DISABLE);
		rwnx_hw->feature.ps_disable = !!(misc_ctrl_l & PS_DISABLE);
		rwnx_hw->feature.autopm_disable =
			!!(misc_ctrl_l & AUTOPM_DISABLE);

		WQ_DBG(DM_GENERIC, DL_WRN,
		       "misc_ctrl: sw scan is %s, low power is %s, auto pm is %s\n",
		       WQ_FEATURE_NSTATE(scan_disable),
		       WQ_FEATURE_NSTATE(ps_disable),
#ifdef CONFIG_PM
		       WQ_FEATURE_NSTATE(autopm_disable)
#else
		       "OFF"
#endif
		);

		hif_autopm_enable(rwnx_hw->core,
				  !(rwnx_hw->feature.autopm_disable));
	}
	return count;
}

static int kiwi_proc_misc_ctrl_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int kiwi_proc_misc_ctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, kiwi_proc_misc_ctrl_show, PDE_DATA(inode));
}

static ssize_t kiwi_proc_misc_ctrl_read(struct file *file, char __user *buffer,
					size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	char info[256];
	int offset = 0;

	if (*pos > 0) {
		return 0;
	}

	offset += snprintf(info + offset, sizeof(info) - offset,
			   "KIWI scan: %s\n", WQ_FEATURE_NSTATE(scan_disable));
	offset +=
		snprintf(info + offset, sizeof(info) - offset,
			 "KIWI low power: %s\n", WQ_FEATURE_NSTATE(ps_disable));
#ifdef CONFIG_PM
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "KIWI auto pm: %s\n",
			   WQ_FEATURE_NSTATE(autopm_disable));
#endif

	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

static int kiwi_proc_amsdu_param_open(struct inode *inode, struct file *file)
{
	return single_open(file, kiwi_proc_misc_ctrl_show, PDE_DATA(inode));
}

static ssize_t kiwi_proc_amsdu_param_write(struct file *file,
					   const char __user *buffer,
					   size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	unsigned int len = 0;
	char *sptr, *token;
	char buf[64];

	if (!count)
		return 0;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, buffer, len))
		return -EFAULT;

	buf[len] = '\0';
	sptr = buf;

	token = strsep(&sptr, "=");
	if (!token)
		return -EINVAL;

	if (!strcmp(token, "timeout")) {
		u32 timeout;

		if (kstrtou32(sptr, 0, &timeout))
			return -EINVAL;

		if (timeout > 200) {
			WQ_DBG(DM_TX, DL_ERR, "timeout %d is greater than 200ms\n",
				timeout);
		            return -EINVAL;
		}

#if defined(CONFIG_HIGH_RES_TIMERS) && defined(USE_HRT)
		// If HRT is used, we need to translate to ns
		rwnx_hw->amsdu_param.timeout = timeout * 1000000;
#else
		rwnx_hw->amsdu_param.timeout = timeout;
#endif
	} else if (!strcmp(token, "max_len")) {
		u32 len;

		if (kstrtou32(sptr, 0, &len))
			return -EINVAL;

		if (len > 1400 || len < 100) {
			printk(KERN_ERR
			       "amsdu length (%d) should be >= 100 and <= 1400\n",
			       len);
			return -EINVAL;
		}

		rwnx_hw->amsdu_param.max_len = len;
	} else if (!strcmp(token, "max_pkt")) {
		u32 pkt_num;

		if (kstrtou32(sptr, 0, &pkt_num))
			return -EINVAL;

		if (pkt_num > 30 || pkt_num < 0) {
			printk(KERN_ERR
			       "Maximum packet number (%d) should be >= 0 and <= 30\n",
			       pkt_num);
			return -EINVAL;
		}

		rwnx_hw->amsdu_param.max_packets_num = pkt_num;
	} else if (!strcmp(token, "enable")) {
		u32 enable;

		if (kstrtou32(sptr, 0, &enable))
			return -EINVAL;

		if (enable == 0)
			rwnx_hw->amsdu_param.enable = false;
		else
			rwnx_hw->amsdu_param.enable = true;
	} else {
		printk(KERN_ERR
		       "Usage: echo timeout=<value>\n"
		       "       echo max_len=<value> | 100 <= max_len <= 1400\n"
		       "       echo max_pkt=<value> | 0 <= max_pkt <= 30\n"
		       "       echo enable=<value>  | 0: disable, 1: enable\n\n");

		return -EINVAL;
	}

	return count;
}

static ssize_t kiwi_proc_amsdu_param_read(struct file *file,
					  char __user *buffer, size_t count,
					  loff_t *pos)
{
	char info[256];
	int offset = 0;
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;

	if (*pos > 0)
		return 0;

	offset += snprintf(info + offset, sizeof(info) - offset,
			   "Tx AMSDU parameters:\n");
#if defined(CONFIG_HIGH_RES_TIMERS) && defined(USE_HRT)
	// If HRT is used, the value set in timeout is in ns
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "amsdu timeout: %lums\n",
			   (rwnx_hw->amsdu_param.timeout/1000000));
#else
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "amsdu timeout: %lums\n",
			   rwnx_hw->amsdu_param.timeout);
#endif
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "amsdu maximum size: %d\n",
			   rwnx_hw->amsdu_param.max_len);
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "amsdu packet num: %d\n",
			   rwnx_hw->amsdu_param.max_packets_num);
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "amsdu enable: %d\n",
			   rwnx_hw->amsdu_param.enable);

	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_amsdu_param_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_amsdu_param_open,
	.write = kiwi_proc_amsdu_param_write,
	.read = kiwi_proc_amsdu_param_read,
	.release = single_release,
};

static const struct wq_proc_ops kiwi_proc_misc_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_misc_ctrl_open,
	.write = kiwi_proc_misc_ctrl_write,
	.read = kiwi_proc_misc_ctrl_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_amsdu_param_fops = {
	.proc_open = kiwi_proc_amsdu_param_open,
	.proc_write = kiwi_proc_amsdu_param_write,
	.proc_read = kiwi_proc_amsdu_param_read,
	.proc_release = single_release,
};

static const struct wq_proc_ops kiwi_proc_misc_ctrl_fops = {
	.proc_open = kiwi_proc_misc_ctrl_open,
	.proc_write = kiwi_proc_misc_ctrl_write,
	.proc_read = kiwi_proc_misc_ctrl_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

/**
 * rwnx_ampdu_param
 */
static int kiwi_proc_ampdu_param_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int kiwi_proc_ampdu_param_open(struct inode *inode, struct file *file)
{
	return single_open(file, kiwi_proc_ampdu_param_show, PDE_DATA(inode));
}

static ssize_t kiwi_proc_ampdu_param_write(struct file *file,
					   const char __user *buffer,
					   size_t count, loff_t *pos)
{
	unsigned int len = 0;
	char *sptr, *token;
	char buf[64];

	if (!count)
		return 0;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, buffer, len))
		return -EFAULT;

	buf[len] = '\0';
	sptr = buf;

	token = strsep(&sptr, "=");
	if (!token)
		return -EINVAL;

	if (!strcmp(token, "ampdu_age_msecs")) {
		u32 ampdu_age_msecs;

		if (kstrtou32(sptr, 0, &ampdu_age_msecs))
			return -EINVAL;
		printk(KERN_ERR "ampdu_age_msecs=%u\n", ampdu_age_msecs);
		ieee80211_ampdu_age_msecs_set(ampdu_age_msecs);
	} else {
		printk(KERN_ERR "Usage: echo ampdu_age_msecs=<value>\n");
		return -EINVAL;
	}

	return count;
}

static ssize_t kiwi_proc_ampdu_param_read(struct file *file,
					  char __user *buffer, size_t count,
					  loff_t *pos)
{
	char info[256];
	int offset = 0;
	// struct seq_file *seq = file->private_data;
	// struct rwnx_hw *rwnx_hw = seq->private;

	if (*pos > 0)
		return 0;

	offset += snprintf(info + offset, sizeof(info) - offset,
			   "Rx AMPDU parameters:\n");
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "ampdu_age_msecs: %d\n",
			   ieee80211_ampdu_age_msecs_get());

	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_ampdu_param_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_ampdu_param_open,
	.write = kiwi_proc_ampdu_param_write,
	.read = kiwi_proc_ampdu_param_read,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_ampdu_param_fops = {
	.proc_open = kiwi_proc_ampdu_param_open,
	.proc_write = kiwi_proc_ampdu_param_write,
	.proc_read = kiwi_proc_ampdu_param_read,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

static int kiwi_proc_max_aggr_num_open(struct inode *inode, struct file *file)
{
	return single_open(file, kiwi_proc_show, PDE_DATA(inode));
}

static ssize_t kiwi_proc_max_aggr_num_write(struct file *file,
					    const char __user *buffer,
					    size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;

	char tmp_buf[5] = { 0 };
	int len;
	u8 val = 0;

	if (!count)
		return 0;

	if (count > sizeof(tmp_buf)) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "echo \"0x04\" > /proc/driver/kiwi_usb/max_aggr_num\n");
		return -EINVAL;
	}

	len = min(count, sizeof(tmp_buf) - 1);
	if (copy_from_user(tmp_buf, buffer, len))
		return -EFAULT;

	if ((kstrtou8(tmp_buf, 0, &val)) == 0) {
		WQ_DBG(DM_GENERIC, DL_WRN, "max_aggr_num: %d\n", val);
		if (RWNX_INFO_NOTIFY_SET(rwnx_hw, MSG_TYPE_MAX_AGGR_NUM, val)) {
			rwnx_hw->ampdu_parm.max_aggr_num = 0;
			WQ_DBG(DM_GENERIC, DL_ERR, "Set max_aggr_num fail\n");
		} else {
			rwnx_hw->ampdu_parm.max_aggr_num = val;
			WQ_DBG(DM_GENERIC, DL_ERR, "Set max_aggr_num:%d\n",
			       val);
		}
	}

	return count;
}

static ssize_t kiwi_proc_max_aggr_num_read(struct file *file,
					   char __user *buffer, size_t count,
					   loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	char info[32] = { 0 };

	if (*pos > 0)
		return 0;

	snprintf(info, sizeof(info), "FW: max_aggr_num = %d\n",
		 rwnx_hw->ampdu_parm.max_aggr_num);
	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_max_aggr_num_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_max_aggr_num_open,
	.write = kiwi_proc_max_aggr_num_write,
	.read = kiwi_proc_max_aggr_num_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_max_aggr_num_fops = {
	.proc_open = kiwi_proc_max_aggr_num_open,
	.proc_write = kiwi_proc_max_aggr_num_write,
	.proc_read = kiwi_proc_max_aggr_num_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

/**
 * rwnx_tx_monitor_ctrl
 */
static ssize_t kiwi_proc_tx_monitor_ctrl(struct file *file,
					 const char __user *buffer,
					 size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;

	char tmp_buf[5] = { 0 };
	int len;
	u8 enable = 0;

	if (!count)
		return 0;

	if (count > sizeof(tmp_buf)) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "echo \"0x01\" > /proc/driver/kiwi_usb/tx_mon_ctrl\n");
		return -EINVAL;
	}

	len = min(count, sizeof(tmp_buf) - 1);
	if (copy_from_user(tmp_buf, buffer, len))
		return -EFAULT;

	if ((kstrtou8(tmp_buf, 0, &enable)) == 0) {
		if (enable) {
			WQ_DBG(DM_GENERIC, DL_WRN, "tx_mon_ctrl enable\n");
			rwnx_hw->record_stats[0].timestamp = jiffies;
			mod_timer(&rwnx_hw->tx_monitor_timer, jiffies + HZ);
		} else {
			WQ_DBG(DM_GENERIC, DL_WRN, "tx_mon_ctrl disable\n");
			del_timer_sync(&rwnx_hw->tx_monitor_timer);
		}
	}

	return count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_tx_monitor_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.write = kiwi_proc_tx_monitor_ctrl,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_tx_monitor_ctrl_fops = {
	.proc_open = kiwi_proc_open,
	.proc_write = kiwi_proc_tx_monitor_ctrl,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

/**
 * rwnx_amsdu_ctrl
 */
static ssize_t kiwi_proc_amsdu_ctrl(struct file *file,
				    const char __user *buffer, size_t count,
				    loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	unsigned int len = 0;
	char *sptr, *token;
	char buf[64];

	if (!count)
		return 0;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, buffer, len))
		return -EFAULT;

	buf[len] = '\0';
	sptr = buf;

	token = strsep(&sptr, "=");
	if (!token)
		return -EINVAL;

	if (!strcmp(token, "encap_data_to_msdu")) {
		u8 encap_data_to_msdu;

		if (kstrtou8(sptr, 0, &encap_data_to_msdu))
			return -EINVAL;
		printk(KERN_ERR "encap_data_to_msdu=%d\n", encap_data_to_msdu);
		//rwnx_hw->amsdu_ctrl.encap_data_to_msdu = encap_data_to_msdu;
		RWNX_INFO_NOTIFY_SET(rwnx_hw, MSG_TYPE_AMSDU_CNTRL,
				     encap_data_to_msdu);
	} else {
		printk(KERN_ERR "Usage: echo encap_data_to_msdu=<value>\n");
		return -EINVAL;
	}

	return count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_amsdu_cntrl_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.write = kiwi_proc_amsdu_ctrl,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_amsdu_cntrl_fops = {
	.proc_open = kiwi_proc_open,
	.proc_write = kiwi_proc_amsdu_ctrl,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

static ssize_t kiwi_proc_debug_flag_set(struct file *file,
					const char __user *buffer, size_t count,
					loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	unsigned int len = 0;
	char buf[64];
	struct rwnx_dbg_debug_flag_set debug_flag;

	if (!count)
		return 0;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, buffer, len))
		return -EFAULT;

	buf[len] = '\0';

	sscanf(buf, "%u %u", &debug_flag.debug_type, &debug_flag.value1);

	WQ_DBG(DM_GENERIC, DL_ERR,
	       "kiwi_proc_debug_flag_set::debug_type=%d, value1=%d\n",
	       debug_flag.debug_type, debug_flag.value1);

	RWNX_INFO_NOTIFY_SET(rwnx_hw, MSG_TYPE_DEBUG_FLAG_SET, debug_flag);

	return count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_debug_flag_set_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.write = kiwi_proc_debug_flag_set,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_debug_flag_set_fops = {
	.proc_open = kiwi_proc_open,
	.proc_write = kiwi_proc_debug_flag_set,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

/**
 * rwnx_param_write
 */

int get_token(char *buf, char *token)
{
	int i = 0;
	int j = 0;

	while (buf[i] == ' ')
		i++;

	while (buf[i] != 0) {
		token[j++] = buf[i++];
		if (buf[i] == ' ')
			break;
	}
	token[j] = 0;
	return i + 1;
}

static ssize_t kiwi_proc_param_write(struct file *file,
				     const char __user *buffer, size_t count,
				     loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;

	char tmp_buf[64];
	char token[5][64];
	int len = 0;
	int offset = 0;
	int i = 0;
	int token_num = 0;

	if ((count > 64) || (count <= 1))
		return -EINVAL;

	if (copy_from_user(tmp_buf, buffer, count))
		return -EFAULT;

	tmp_buf[count - 1] = 0;
	WQ_DBG(DM_GENERIC, DL_ERR, "cmd: %s (%zu)\n", tmp_buf, count);

	while ((offset < count) && (i < 5)) {
		len = get_token(&tmp_buf[offset], token[i]);
		offset += len;
		token_num++;
		WQ_DBG(DM_GENERIC, DL_ERR, "=>%s - %d\n", token[i++], len);
	}
	WQ_DBG(DM_GENERIC, DL_ERR, "token_num = %d\n", token_num);

	if (!memcmp(token[0], "he_bcc_cntrl", strlen("he_bcc_cntrl"))) {
		u8 he_bcc_cntrl;

		if (kstrtou8(token[1], 10, &he_bcc_cntrl) != 0) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "param_proc: he_bcc_cntrl fail\n");
			return -EINVAL;
		}

		if (he_bcc_cntrl != 0 && he_bcc_cntrl != 1) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "Invalid value: he_bcc_cntrl (%d)\n",
			       he_bcc_cntrl);
			return -EINVAL;
		}

		if (RWNX_INFO_NOTIFY_SET(rwnx_hw, MSG_TYPE_SET_HE_BCC_CNTRL,
					 he_bcc_cntrl))
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "set he_bcc_cntrl (%d) fail\n", he_bcc_cntrl);
		else
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "set he_bcc_cntrl (%d) success\n", he_bcc_cntrl);
	}

	return count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_param_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.write = kiwi_proc_param_write,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_param_fops = {
	.proc_open = kiwi_proc_open,
	.proc_write = kiwi_proc_param_write,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

/**
 * rwnx_he_on
 */
static int kiwi_proc_wifi_ctrl_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int kiwi_proc_wifi_ctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, kiwi_proc_wifi_ctrl_show, PDE_DATA(inode));
}

static ssize_t kiwi_proc_wifi_ctrl_write(struct file *file,
					 const char __user *buffer,
					 size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	struct wiphy *wiphy = rwnx_hw->wiphy;
	struct ieee80211_supported_band *band_5GHz =
		wiphy->bands[NL80211_BAND_5GHZ];
#ifdef WQ_HE_STA
	int i;
	int nss = rwnx_hw->mod_params.nss;
	int mcs_map_max_2ss = IEEE80211_HE_MCS_SUPPORT_0_11;
#endif
	int mcs_map;
	char tmp_buf[8] = { 0 };
	int ret;
	long wifi_ctrl_l = 0;

	if (!count)
		return 0;

	if (count != 7) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "ex: echo \"0x0001\" > /proc/driver/kiwi_usb/wifi_ctrl\n");
		return -EINVAL;
	}

	if (copy_from_user(tmp_buf, buffer, count))
		return -EFAULT;

	if ((ret = kstrtol(tmp_buf, 0, &wifi_ctrl_l)) == 0) {
		rwnx_hw->feature.he_disable = !!(wifi_ctrl_l & HE_DISABLE);
		rwnx_hw->feature.bfmee_disable =
			!!(wifi_ctrl_l & BFMEE_DISABLE);
		rwnx_hw->feature.murx_disable = !!(wifi_ctrl_l & MURX_DISABLE);
		rwnx_hw->feature.he_mcs_map_rx_disable =
			!!(wifi_ctrl_l & HE_MCS_MAP_RX_DISABLE);
		rwnx_hw->feature.he_mcs_map_tx_disable =
			!!(wifi_ctrl_l & HE_MCS_MAP_TX_DISABLE);

		if ((rwnx_hw->feature.bfmee_disable) &&
		    (!(rwnx_hw->feature.murx_disable))) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "bfmee OFF and murx ON is invalid.\n");
			return -EINVAL;
		}

		rwnx_hw->mod_params.he_on = !rwnx_hw->feature.he_disable;

		if (rwnx_hw->feature.bfmee_disable) {
			rwnx_hw->mod_params.bfmee = false;
			band_5GHz->vht_cap.cap &=
				~IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
			band_5GHz->vht_cap.cap &=
				~(3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
			band_5GHz->vht_cap.cap &=
				~(3 << __builtin_ctz(IEEE80211_VHT_CAP_BEAMFORMEE_STS_MAX));
#endif
#ifdef WQ_HE_STA
			rwnx_hw->he_cap.phy_cap_info[4] &=
				~IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE;
			rwnx_hw->he_cap.phy_cap_info[4] &=
				~IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
#endif
		} else {
			rwnx_hw->mod_params.bfmee = true;
			band_5GHz->vht_cap.cap |=
				IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
			band_5GHz->vht_cap.cap |=
				3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
			band_5GHz->vht_cap.cap |=
				3 << __builtin_ctz(IEEE80211_VHT_CAP_BEAMFORMEE_STS_MAX);
#endif
#ifdef WQ_HE_STA
			if (rwnx_hw->mod_params.he_on) {
				rwnx_hw->he_cap.phy_cap_info[4] |=
					IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE;
				rwnx_hw->he_cap.phy_cap_info[4] |=
					IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
			} else {
				rwnx_hw->he_cap.phy_cap_info[4] &=
					~IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE;
				rwnx_hw->he_cap.phy_cap_info[4] &=
					~IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
			}
#endif
		}
		if (rwnx_hw->feature.murx_disable) {
			rwnx_hw->mod_params.murx = false;
			band_5GHz->vht_cap.cap &=
				~IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
		} else {
			rwnx_hw->mod_params.murx = true;
			band_5GHz->vht_cap.cap |=
				IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
		}
		if (rwnx_hw->feature.he_mcs_map_rx_disable) {
			mcs_map = IEEE80211_HE_MCS_SUPPORT_0_9;
		} else {
			mcs_map = IEEE80211_HE_MCS_SUPPORT_0_11;
		}
#ifdef WQ_HE_STA
		memset(&(rwnx_hw->he_cap.mcs_supp), 0,
		       sizeof(rwnx_hw->he_cap.mcs_supp));
		for (i = 0; i < nss; i++) {
			__le16 unsup_for_ss = cpu_to_le16(
				IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
			rwnx_hw->he_cap.mcs_supp.rx_mcs_80 |=
				cpu_to_le16(mcs_map << (i * 2));
			rwnx_hw->he_cap.mcs_supp.rx_mcs_160 |= unsup_for_ss;
			rwnx_hw->he_cap.mcs_supp.rx_mcs_80p80 |= unsup_for_ss;
			mcs_map = min_t(int, rwnx_hw->mod_params.he_mcs_map,
					mcs_map_max_2ss);
		}
		for (; i < 8; i++) {
			__le16 unsup_for_ss = cpu_to_le16(
				IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
			rwnx_hw->he_cap.mcs_supp.rx_mcs_80 |= unsup_for_ss;
			rwnx_hw->he_cap.mcs_supp.rx_mcs_160 |= unsup_for_ss;
			rwnx_hw->he_cap.mcs_supp.rx_mcs_80p80 |= unsup_for_ss;
		}
#endif
		if (rwnx_hw->feature.he_mcs_map_tx_disable) {
			mcs_map = IEEE80211_HE_MCS_SUPPORT_0_9;
		} else {
			mcs_map = IEEE80211_HE_MCS_SUPPORT_0_11;
		}
#ifdef WQ_HE_STA
		for (i = 0; i < nss; i++) {
			__le16 unsup_for_ss = cpu_to_le16(
				IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
			rwnx_hw->he_cap.mcs_supp.tx_mcs_80 |=
				cpu_to_le16(mcs_map << (i * 2));
			rwnx_hw->he_cap.mcs_supp.tx_mcs_160 |= unsup_for_ss;
			rwnx_hw->he_cap.mcs_supp.tx_mcs_80p80 |= unsup_for_ss;
			mcs_map = min_t(int, rwnx_hw->mod_params.he_mcs_map,
					mcs_map_max_2ss);
		}
		for (; i < 8; i++) {
			__le16 unsup_for_ss = cpu_to_le16(
				IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
			rwnx_hw->he_cap.mcs_supp.tx_mcs_80 |= unsup_for_ss;
			rwnx_hw->he_cap.mcs_supp.tx_mcs_160 |= unsup_for_ss;
			rwnx_hw->he_cap.mcs_supp.tx_mcs_80p80 |= unsup_for_ss;
		}
#endif

		WQ_DBG(DM_GENERIC, DL_WRN,
		       "wifi_ctrl: he is %s, bfmee is %s, murx is %s, 1024-QAM RX is %s, 1024-QAM TX is %s\n",
		       WQ_FEATURE_NSTATE(he_disable),
		       WQ_FEATURE_NSTATE(bfmee_disable),
		       WQ_FEATURE_NSTATE(murx_disable),
		       WQ_FEATURE_NSTATE(he_mcs_map_rx_disable),
		       WQ_FEATURE_NSTATE(he_mcs_map_tx_disable));
	}

	if (rwnx_hw) {
		/* Set parameters to firmware */
		rwnx_send_me_config_req(rwnx_hw);
	}

	return count;
}

static ssize_t kiwi_proc_wifi_ctrl_read(struct file *file, char __user *buffer,
					size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	char info[256];
	int offset = 0;

	if (*pos > 0) {
		return 0;
	}

	offset += snprintf(info + offset, sizeof(info) - offset, "he: %s\n",
			   WQ_FEATURE_NSTATE(he_disable));
	offset += snprintf(info + offset, sizeof(info) - offset, "bfmee: %s\n",
			   WQ_FEATURE_NSTATE(bfmee_disable));
	offset += snprintf(info + offset, sizeof(info) - offset, "murx: %s\n",
			   WQ_FEATURE_NSTATE(murx_disable));
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "1024-QAM RX: %s\n",
			   WQ_FEATURE_NSTATE(he_mcs_map_rx_disable));
	offset += snprintf(info + offset, sizeof(info) - offset,
			   "1024-QAM TX: %s\n",
			   WQ_FEATURE_NSTATE(he_mcs_map_tx_disable));

	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_wifi_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_wifi_ctrl_open,
	.write = kiwi_proc_wifi_ctrl_write,
	.read = kiwi_proc_wifi_ctrl_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_wifi_ctrl_fops = {
	.proc_open = kiwi_proc_wifi_ctrl_open,
	.proc_write = kiwi_proc_wifi_ctrl_write,
	.proc_read = kiwi_proc_wifi_ctrl_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

static int kiwi_proc_he_ltf_gi_open(struct inode *inode, struct file *file)
{
	return single_open(file, kiwi_proc_show, PDE_DATA(inode));
}

static ssize_t kiwi_proc_he_ltf_gi_write(struct file *file,
					 const char __user *buffer,
					 size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	uint32_t he_ltf_gi;
	char tmp_buf[11] = { 0 };
	int len;

	if (!count)
		return 0;

	if (count > sizeof(tmp_buf)) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "echo \"0x101\" > /proc/driver/kiwi_usb/he_ltf_gi\n");
		return -EINVAL;
	}

	len = min(count, sizeof(tmp_buf) - 1);
	if (copy_from_user(tmp_buf, buffer, len))
		return -EFAULT;

	if ((kstrtou32(tmp_buf, 0, &he_ltf_gi)) == 0) {
		uint8_t he_ltf = (he_ltf_gi & 0xff);
		uint8_t he_gi = ((he_ltf_gi >> 8) & 0xff);

		if (he_ltf >= 3 || he_gi >= 3) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "Invalid value he_ltf_gi: 0x%08x\n", he_ltf_gi);
			return -EINVAL;
		}

		if (RWNX_INFO_NOTIFY_SET(rwnx_hw, MSG_TYPE_SET_HE_LTF_GI,
					 he_ltf_gi))
			WQ_DBG(DM_GENERIC, DL_ERR, "set he_ltf_gi fail\n");
		else {
			rwnx_hw->he_ltf_gi = he_ltf_gi;
			WQ_DBG(DM_GENERIC, DL_ERR, "set he_ltf_gi: 0x%08x\n",
			       he_ltf_gi);
		}
	}

	return count;
}

const char *ltf_str[] = { "1x 3.2us", "2x 6.4us", "3x 12.8us", "invalid" };

const char *gi_str[] = { "0.8us", "1.6us", "3.2us", "invalid" };

static ssize_t kiwi_proc_he_ltf_gi_read(struct file *file, char __user *buffer,
					size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	int offset = 0;
	char info[256] = { 0 };

	if (*pos > 0)
		return 0;

	offset += snprintf(info, sizeof(info), "he_ltf_gi = 0x%08x\n",
			   rwnx_hw->he_ltf_gi);

	if (rwnx_hw->he_ltf_gi != 0xffffffff) {
		uint8_t ltf = (rwnx_hw->he_ltf_gi & 0x3);
		uint8_t gi = ((rwnx_hw->he_ltf_gi >> 8) & 0x3);

		offset += snprintf(info + offset, sizeof(info) - offset,
				   "LTF: %s\n", ltf_str[ltf]);
		offset += snprintf(info + offset, sizeof(info) - offset,
				   "GI: %s\n", gi_str[gi]);
	}

	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_he_ltf_gi_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_he_ltf_gi_open,
	.write = kiwi_proc_he_ltf_gi_write,
	.read = kiwi_proc_he_ltf_gi_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_he_ltf_gi_fops = {
	.proc_open = kiwi_proc_he_ltf_gi_open,
	.proc_write = kiwi_proc_he_ltf_gi_write,
	.proc_read = kiwi_proc_he_ltf_gi_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

static ssize_t proc_rwnx_wifi_coex_scene_write(struct rwnx_hw *priv,
					       const char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	char tmp_buf[7] = { 0 };
	int len;
	u8 scene = 0;

	if (!count)
		return 0;

	if (count > sizeof(tmp_buf)) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "echo \"0xff\" > /proc/driver/kiwi_usb/wifi_coex_scene\n");
		return -EINVAL;
	}

	len = min(count, sizeof(tmp_buf) - 1);
	if (copy_from_user(tmp_buf, user_buf, len))
		return -EFAULT;

	if ((kstrtou8(tmp_buf, 0, &scene)) == 0) {
		if (RWNX_INFO_NOTIFY_SET(priv, MSG_TYPE_SET_COEX_SCENE, scene))
			WQ_DBG(DM_GENERIC, DL_ERR, "set coex_scene fail\n");
		else {
			priv->coex_scene = scene;
			WQ_DBG(DM_GENERIC, DL_ERR, "set coex_scene: %d\n",
			       scene);
		}
	}

	return count;
}

static ssize_t kiwi_proc_wifi_coex_scene_write(struct file *file,
					       const char __user *buffer,
					       size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;

	return proc_rwnx_wifi_coex_scene_write(rwnx_hw, buffer, count, pos);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops kiwi_proc_wifi_coex_scene_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.write = kiwi_proc_wifi_coex_scene_write,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops kiwi_proc_wifi_coex_scene_fops = {
	.proc_open = kiwi_proc_open,
	.proc_write = kiwi_proc_wifi_coex_scene_write,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

#ifdef NAPI_SUPPORT
static int wq_proc_napi_ctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, kiwi_proc_misc_ctrl_show, PDE_DATA(inode));
}

static ssize_t wq_proc_napi_ctrl_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	unsigned int len = 0;
	char *sptr, *token;
	char buf[64];

	if (!count)
		return 0;

	len = min(count, sizeof(buf)-1);
	if (copy_from_user(buf, buffer, len))
		return -EFAULT;

	buf[len] = '\0';
	sptr = buf;

	token = strsep(&sptr, "=");
	if (!token)
		return -EINVAL;

	if (!strcmp(token, "timeout")) {
		u32 timeout;

		if (kstrtou32(sptr, 0, &timeout))
			return -EINVAL;

		if (timeout > 200) {
			WQ_DBG(DM_GENERIC, DL_WRN,
				"timeout %d is greater than 200ms\n", timeout);
			return -EINVAL;
		}

		rwnx_hw->napi_param.timeout = timeout * 1000000;
	}
	else if (!strcmp(token, "pkt_num")) {
		u32 pkt_num;

		if (kstrtou32(sptr, 0, &pkt_num))
			return -EINVAL;

		if (pkt_num > 100 || pkt_num < 0) {
			WQ_DBG(DM_GENERIC, DL_WRN,
				"Maximum packet number (%d) should be >= 0 and <= 100\n", pkt_num);
			return -EINVAL;
		}

		rwnx_hw->napi_param.packets_num = pkt_num;
	}
	else if (!strcmp(token, "enable")) {
		u32 enable;

		if (kstrtou32(sptr, 0, &enable))
			return -EINVAL;

		if (enable == 0)
			rwnx_hw->napi_param.param_enable = false;
		else
			rwnx_hw->napi_param.param_enable = true;
	}
	else {
		WQ_DBG(DM_GENERIC, DL_WRN,
			"Usage: echo timeout=<value> | value shuold be 0 to 200\n"
			"       echo pkt_num=<value> | 0 <= max_pkt <= 100\n"
			"       echo enable=<value>  | 0: disable, 1: enable\n\n");
		return -EINVAL;
	}

	return count;
}

static ssize_t wq_proc_napi_ctrl_read(struct file *file, char __user *buffer, size_t count, loff_t *pos)
{
	char info[256];
	int offset = 0;
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;

	if (*pos > 0)
		return 0;

	offset += snprintf(info + offset, sizeof(info) - offset, "NAPI parameters:\n");
	offset += snprintf(info + offset, sizeof(info) - offset, "schedule timeout: %lums\n", (rwnx_hw->napi_param.timeout/1000000));
	offset += snprintf(info + offset, sizeof(info) - offset, "packet number: %d\n", rwnx_hw->napi_param.packets_num);
	offset += snprintf(info + offset, sizeof(info) - offset, "napi defer enable: %d\n", rwnx_hw->napi_param.param_enable);

	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
static const struct proc_ops wq_proc_napi_ctrl_fops = {
	.proc_open      = wq_proc_napi_ctrl_open,
	.proc_write     = wq_proc_napi_ctrl_write,
	.proc_read      = wq_proc_napi_ctrl_read,
	.proc_lseek     = noop_llseek,
	.proc_release   = single_release,
};
#else
static const struct file_operations wq_proc_napi_ctrl_fops = {
	.owner    = THIS_MODULE,
	.open     = wq_proc_napi_ctrl_open,
	.write    = wq_proc_napi_ctrl_write,
	.read     = wq_proc_napi_ctrl_read,
	.llseek   = noop_llseek,
	.release  = single_release,
};
#endif
#endif

enum llrx_param_id_s {
	PARAM_LLRX_CKSUM = 0,
	LLRX_SET_MAX,
};

static const char *const llrx_write_param[LLRX_SET_MAX] = {
	[PARAM_LLRX_CKSUM] = "cksum_offload",
};

static ssize_t proc_llrx_write(struct file *file, const char __user *buffer,
			       size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	//char *tmpstr;
	char tmp_buf[120];
	char token[5][120];
	int len = 0;
	int offset = 0;
	int i = 0;
	int token_num = 0;
	int ret;
	uint8_t param_id = 0;

	vif->rwnx_hw = rwnx_hw;
	if ((count > 120) || (count == 0)) {
		return count;
	}

	if (copy_from_user(tmp_buf, buffer, count))
		return -EFAULT;

	tmp_buf[count - 1] = 0;
	printk("cmd:%s (%ld)\n", tmp_buf, (long int)count);

	while ((offset < count) && (i < 5)) {
		len = get_token(&tmp_buf[offset], token[i]);
		offset += len;
		token_num++;
		printk("=>%s - %d\n", token[i++], len);
	}
	//printk("token_num = %d\n", token_num);

	//search cmd
	while (memcmp(token[0], llrx_write_param[param_id],
		      strlen(llrx_write_param[param_id]))) {
		param_id++;
		if (param_id == LLRX_SET_MAX) {
			printk("wrong cmd\n");
			return -1;
		}
	}

	switch (param_id) {
	case PARAM_LLRX_CKSUM: {
		bool cksum_offload_en = false;
		ret = true;
		if (!memcmp(token[1], "on", strlen("on"))) {
			cksum_offload_en = true;
		} else if (!memcmp(token[1], "off", strlen("off"))) {
			cksum_offload_en = false;
		} else {
			printk("echo \"cksum_offload [on/off]\"\n");
			ret = false;
		}
		if (ret) {
			gv_cksum_offload = cksum_offload_en;
			printk("cksum_offload %s, en=%d\n", token[1],
			       gv_cksum_offload);
		}
		break;
	}
	default:
		printk("llrx do nothing\n");
		break;
	}
	return count;
}

static ssize_t proc_llrx_read(struct file *file, char __user *buffer,
			      size_t count, loff_t *pos)
{
	char info[256] = { 0 };

	if (*pos > 0)
		return 0;

	snprintf(info, sizeof(info), "LLRX: cksum_offload = %s\n",
		 gv_cksum_offload ? "on" : "off");

	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops proc_llrx_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.write = proc_llrx_write,
	.read = proc_llrx_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops proc_llrx_fops = {
	.proc_open = kiwi_proc_open,
	.proc_write = proc_llrx_write,
	.proc_read = proc_llrx_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

int wq_wifi_priv_recover_test(struct rwnx_hw *rwnx_hw,
			      struct rwnx_vif *rwnx_vif, void *msgs, int msg_len)
{
	int recover_type[PRIV_TEST_PARAM_COUNT] = {0};
	//WQ_DBG(DM_GENERIC, DL_INF, "wq_wifi_priv_netdev_hml_set_battery::argc=%d, argv[1]=[%s]\n",
	//argc, argv[1]);

	if(msg_len > sizeof(recover_type)) {
		printk("wq_wifi_priv_recover_test::msg len is not valid");
		return 0;
	}
	memcpy(recover_type, msgs, msg_len);
	WQ_DBG(DM_GENERIC, DL_ERR, "wq_wifi_priv_netdev_recover_test:type=%d\n",
	       recover_type[0]);
	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
				       HML_TEST_RECOVER_TEST_MODE_SET,
				       (void *)recover_type,
				       sizeof(recover_type));
	return 0;
}

int wq_wifi_priv_phy_cmd_test(struct rwnx_hw *rwnx_hw,
	              struct rwnx_vif *rwnx_vif, void *msgs, int msg_len)
{
	char msg_param[128] = {0};

	if(msg_len > sizeof(msg_param)) {
	    printk("wq_wifi_priv_phy_cmd_test::msg_len is not valid");
	    return 0;
	}

	memcpy(msg_param, msgs, msg_len);
	WQ_DBG(DM_GENERIC, DL_ERR, "wq_wifi_priv_phy_cmd_test: %s\n", msg_param);
	rwnx_send_dbg_wq_priv_test_req(rwnx_hw, rwnx_vif, DBG_WQ_PRIV_HML_TEST,
		    HML_TEST_PHY_CMD_SET,
			(void *)msg_param,
			strlen(msg_param));

	return 0;
}

static ssize_t proc_test_mode_write(struct file *file,
				    const char __user *buffer, size_t count,
				    loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	struct net_device *ndev = seq->private;
	struct rwnx_vif *vif = netdev_priv(ndev);
	char tmp_buf[MAX_CMD_BUF_SIZE] = { 0 };
	char token[MAX_PRARAM_CNT][MAX_CMD_BUF_SIZE] = {
		{ 0 },
	};
	int len = 0;
	int offset = 0;
	int i = 0;
	int token_num = 0;

	vif->rwnx_hw = rwnx_hw;
	if ((count > 120) || (count == 0)) {
		return count;
	}

	if (copy_from_user(tmp_buf, buffer, count))
		return -EFAULT;

	tmp_buf[count - 1] = 0;
	printk("cmd:%s (%zu)\n", tmp_buf, count);

	while ((offset < count) && (i < 5)) {
		len = get_token(&tmp_buf[offset], token[i]);
		offset += len;
		token_num++;
		printk("=>%s - %d\n", token[i++], len);
	}
	printk("token_num = %d\n", token_num);

	if (!memcmp(token[0], "recover_test", strlen("recover_test"))) {
		uint32_t recover_mode;
		struct mm_info_notify_cfm *cfm;
		cfm = kmalloc(sizeof(*cfm), GFP_KERNEL);

		if (kstrtouint(token[1], 0, &recover_mode) != 0) {
			printk("test mode: recover_test failed");
			kfree(cfm);
			return count;
		}
		rwnx_send_dbg_recover_test_req(rwnx_hw, recover_mode, cfm);
		printk("result_len = %d\n", cfm->result_len);
		kfree(cfm);
		return count;
	}

	if (!memcmp(token[0], "recover_to_assert",
		    strlen("recover_to_assert"))) {
		int recover_to_assert[3] = {0};
		if (kstrtouint(token[1], 0, &recover_to_assert[0]) != 0) {
			printk("test mode: recover_test failed");
			return count;
		}
		if (kstrtouint(token[2], 0, &recover_to_assert[1]) != 0) {
			printk(":kstrtouint run fail");
		}
		if (kstrtouint(token[3], 0, &recover_to_assert[2]) != 0) {
			printk(":kstrtouint run fail");

		}
		wq_wifi_priv_recover_test(rwnx_hw, vif, recover_to_assert, sizeof(recover_to_assert));
		return count;
	}

	if (!memcmp(token[0], "ant_sel", strlen("ant_sel"))) {
		uint32_t ant;

		if (kstrtouint(token[1], 0, &ant) != 0) {
			printk("ant_sel: ant_sels failed");
			return count;
		}
		rwnx_send_ant_req(rwnx_hw, ant);

		return count;
	}

	if (!memcmp(token[0], "phy_dbg", strlen("phy_dbg"))) {

		if (token_num > 1) {
			wq_wifi_priv_phy_cmd_test(rwnx_hw, vif, &tmp_buf[strlen(token[0])],
			     strlen(&tmp_buf[strlen(token[0])]));
		}

		return count;
	}

	return count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops proc_test_mode_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.write = proc_test_mode_write,
	.read = seq_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops proc_test_mode_fops = {
	.proc_open = kiwi_proc_open,
	.proc_write = proc_test_mode_write,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

extern int nbw_type;

static ssize_t proc_monitor_nbw_type_write(struct file *file,
					    const char __user *buffer,
					    size_t count, loff_t *pos)
{
	char tmp_buf[5] = { 0 };
	int len;
	u8 val = 0;
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;
	struct rwnx_vif *rwnx_vif;
	struct me_config_monitor_cfm monitor_cfg_cfm = {0};

	if (!count)
		return 0;

	if (count > sizeof(tmp_buf)) {
		WQ_DBG(DM_GENERIC, DL_ERR,
			"echo [0-4] > /proc/driver/wifi_usb/monitor_nbw_type\n");
		return -EINVAL;
	}

	len = min(count, sizeof(tmp_buf) - 1);
	if (copy_from_user(tmp_buf, buffer, len))
		return -EFAULT;

	if ((kstrtou8(tmp_buf, 0, &val)) == 0) {
		WQ_DBG(DM_GENERIC, DL_WRN, "nbw_type: %d\n", val);
		if (val > 4) {
			WQ_DBG(DM_GENERIC, DL_ERR,
				"echo [0-4] > /proc/driver/kiwi_usb/nbw_type\n");
		} else if ((rwnx_hw->monitor_vif == RWNX_INVALID_VIF) ||
			(rwnx_hw->vif_started != 1)) {
			WQ_DBG(DM_GENERIC, DL_ERR,
				"should set monitor_nbw_type only one monitor interface mode\n");
		} else {
			nbw_type = val;
			rwnx_vif = rwnx_hw->vif_table[rwnx_hw->monitor_vif];
			rwnx_send_config_monitor_req(rwnx_hw,
				&rwnx_hw->chanctx_table[rwnx_vif->ch_index].chan_def, &monitor_cfg_cfm);
			WQ_DBG(DM_GENERIC, DL_ERR, "set nbw_type:%d\n", val);
		}
	}

	return count;
}

static ssize_t proc_monitor_nbw_type_read(struct file *file,
					   char __user *buffer, size_t count,
					   loff_t *pos)
{
	char info[32] = { 0 };

	if (*pos > 0)
		return 0;

	snprintf(info, sizeof(info), "FW: nbw_type = %d\n", nbw_type);
	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

/* /proc/driver/wifi_usb/throughput */
static ssize_t proc_throughput_read(struct file *file,
	char __user *buffer, size_t count, loff_t *pos)
{
	struct rwnx_hw *rwnx_hw;
	char info[64] = { 0 };
	struct seq_file *seq = file->private_data;

	if (*pos > 0 || seq == NULL) {
		return 0;
	}

	rwnx_hw = seq->private;
	if (rwnx_hw == NULL) {
		return 0;
	}

	snprintf(info, sizeof(info), "Tx_Tput: %u Mbps, Rx_Tput:%u Mbps\n",
		rwnx_hw->tx_throughput, rwnx_hw->rx_throughput);
	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return EINVAL;
}

/* /proc/driver/wifi_usb/fw_ver */
static ssize_t proc_fw_ver_read(struct file *file,
	char __user *buffer, size_t count, loff_t *pos)
{
	size_t len;
	struct rwnx_hw *rwnx_hw;
	fw_bin_ver_t *dtop_fwver;
	fw_bin_ver_t *wifi_fwver;
	char info[128] = { 0 };
	struct seq_file *seq = file->private_data;

	if (*pos > 0 || seq == NULL) {
		return 0;
	}

	rwnx_hw = seq->private;
	if (rwnx_hw == NULL) {
		return 0;
	}
	if (rwnx_hw->core == NULL) {
		return 0;
	}

	dtop_fwver = (fw_bin_ver_t *)&rwnx_hw->core->dtop_fwver;
	wifi_fwver = (fw_bin_ver_t *)&rwnx_hw->core->wifi_fwver;

	snprintf(info, sizeof(info), "dtop_fwver:%d.%d.%d.%d wifi_fwver:%d.%d.%d.%d"
		" drv_ver:0.0.0.0\n",
		dtop_fwver->major, dtop_fwver->minor,
		dtop_fwver->rever, dtop_fwver->build,
		wifi_fwver->major, wifi_fwver->minor,
		wifi_fwver->rever, wifi_fwver->build);
	len = strlen(info) + 1;
	if (len > count) {
		len = count;
	}
	if (!copy_to_user(buffer, info, len)) {
		*pos = 1;
		return len;
	} else
		return EINVAL;
}

/* /proc/driver/wifi_usb/mon_param */
static ssize_t proc_monitor_param_read(struct file *file,
	char __user *buffer, size_t count, loff_t *pos)
{
	size_t len;
	struct rwnx_hw *rwnx_hw;
	char info[128] = { 0 };
	struct seq_file *seq = file->private_data;

	if (*pos > 0 || seq == NULL) {
		return 0;
	}

	rwnx_hw = seq->private;
	if (rwnx_hw == NULL) {
		return 0;
	}

	/* WAR: if ch_band not 20MHz, ch_idx incorrect */
	snprintf(info, sizeof(info), "monitor chn:%d chn_band:%dMHz nss:%d "
		"power:%d mcs:%d tx_band:%dMHz rssi:%d\n",
		rwnx_hw->monitor_param.ch_index, rwnx_hw->monitor_param.ch_band,
		rwnx_hw->mod_params.nss, rwnx_hw->monitor_param.tx_power,
		rwnx_hw->monitor_param.tx_mcs_idx,
		(1 << rwnx_hw->monitor_param.tx_bw_idx) * 20,
		rwnx_hw->monitor_param.rx_rssi);

	len = strlen(info) + 1;
	if (len > count) {
		len = count;
	}
	if (!copy_to_user(buffer, info, len)) {
		*pos = 1;
		return len;
	} else
		return EINVAL;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops proc_monitor_nbw_type_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.write = proc_monitor_nbw_type_write,
	.read = proc_monitor_nbw_type_read,
	.llseek = noop_llseek,
	.release = single_release,
};

static const struct wq_proc_ops proc_throughput_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.read = proc_throughput_read,
	.llseek = noop_llseek,
	.release = single_release,
};

static const struct wq_proc_ops proc_read_fw_ver_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.read = proc_fw_ver_read,
	.llseek = noop_llseek,
	.release = single_release,
};
static const struct wq_proc_ops proc_read_mon_param_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.read = proc_monitor_param_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops proc_monitor_nbw_type_fops = {
	.proc_open = kiwi_proc_open,
	.proc_write = proc_monitor_nbw_type_write,
	.proc_read = proc_monitor_nbw_type_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};

static const struct wq_proc_ops proc_throughput_fops = {
	.proc_open = kiwi_proc_open,
	.proc_read = proc_throughput_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};

static const struct wq_proc_ops proc_read_fw_ver_fops = {
	.proc_open = kiwi_proc_open,
	.proc_read = proc_fw_ver_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
static const struct wq_proc_ops proc_read_mon_param_fops = {
	.proc_open = kiwi_proc_open,
	.proc_read = proc_monitor_param_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

static int rwnx_proc_extap_ctrl_show(struct seq_file *seq, void *v)
{
    unsigned int *ptr_var = seq->private;
    seq_printf(seq, "rwnx_proc_extap_ctrl_show %u\n", *ptr_var);
    return 0;
}

static int rwnx_proc_extap_ctrl_open(struct inode *inode, struct file *file)
{
    return single_open(file, rwnx_proc_extap_ctrl_show, PDE_DATA(inode));
}

static ssize_t rwnx_proc_extap_ctrl_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos)
{
    struct seq_file *seq = file->private_data;
    struct wireless_dev *wdev = seq->private;
    struct net_device *ndev = wdev->netdev;
    struct rwnx_vif *vif = netdev_priv(ndev);
    struct rwnx_hw *rwnx_hw = vif->rwnx_hw;

    char tmp_buf[3] = {0};
    int ret;
    int extap_ctrl = 0;

    if (!count)
        return 0;

    //1--enable extap, 0--disable extap
    if (count != 2) {
        WQ_DBG(DM_GENERIC, DL_WRN, "ex: echo 1 > /proc/driver/wifi_usb/extap_ctrl\n");
        return -EINVAL;
    }

    if (copy_from_user(tmp_buf, buffer, count))
        return -EFAULT;

    if ((ret = kstrtoint(tmp_buf, 0, &extap_ctrl)) == 0) {
        vif->extAP_supp = extap_ctrl;

        if (!vif->extAP_supp) {
            extap_tbl_clear();
        }

        rwnx_hw->amsdu_param.enable = !extap_ctrl;
        WQ_DBG(DM_GENERIC, DL_WRN, "vif:%p, extAP_supp:%d\n", vif, vif->extAP_supp);
    }

    return count;
}

static ssize_t rwnx_proc_extap_ctrl_read(struct file *file, char __user *buffer, size_t count, loff_t *pos)
{
    struct seq_file *seq = file->private_data;
    struct wireless_dev *wdev = seq->private;
    struct net_device *ndev = wdev->netdev;
    struct rwnx_vif *vif = netdev_priv(ndev);

    char info[32] = {0};
    int offset = 0;

    if(*pos > 0){
        return 0;
    }

    offset += snprintf(info + offset, sizeof(info) - offset, "extAP enable: %d\n", vif->extAP_supp);

    if(!copy_to_user(buffer, info, strlen(info) + 1)) {
        *pos = 1;
        return strlen(info) + 1;
    } else
        return -1;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
static const struct proc_ops proc_extap_ctrl_fops = {
    .proc_open      = rwnx_proc_extap_ctrl_open,
    .proc_write     = rwnx_proc_extap_ctrl_write,
    .proc_read      = rwnx_proc_extap_ctrl_read,
    .proc_lseek     = noop_llseek,
    .proc_release   = single_release,
};
#else
static const struct file_operations proc_extap_ctrl_fops = {
    .owner    = THIS_MODULE,
    .open     = rwnx_proc_extap_ctrl_open,
    .write    = rwnx_proc_extap_ctrl_write,
    .read     = rwnx_proc_extap_ctrl_read,
    .llseek   = noop_llseek,
    .release  = single_release,
};
#endif

static ssize_t proc_peer_tx_info_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
{
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;

	unsigned int len = 0;
	char *sptr, *token;
	char buf[64];

	if (!count)
		return 0;

	// Currently, only when operating at large AP mode, we support
	// peer tx info event report
	if (!rwnx_hw->large_ap_mode)
		return -EINVAL;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, buffer, len))
		return -EFAULT;

	buf[len] = '\0';
	sptr = buf;

	token = strsep(&sptr, "=");
	if (!token)
		return -EINVAL;

	if (!strcmp(token, "enable")) {
		u32 enable;

		if (kstrtou32(sptr, 0, &enable))
			return -EINVAL;

		if (enable) {
			enable = 1;
			WQ_DBG(DM_GENERIC, DL_WRN, "peer_tx_info enable\n");
		} else {
			enable = 0;
			WQ_DBG(DM_GENERIC, DL_WRN, "peer_tx_info disable\n");
		}

		if (RWNX_INFO_NOTIFY_SET(rwnx_hw, MSG_TYPE_ENABLE_PEER_TX_INFO, enable)) {
			WQ_DBG(DM_GENERIC, DL_ERR, "Set peer tx info fail\n");
			return -EINVAL;
		}
		else
			rwnx_hw->enable_tx_info = enable;
	} else if (!strcmp(token, "debug")) {
		u32 debug;

		if (kstrtou32(sptr, 0, &debug))
			return -EINVAL;

		if (debug != 1)
			debug = 0;

		rwnx_hw->enable_show_tx_info = debug;
	} else {
		WQ_DBG(DM_GENERIC, DL_ERR,
			"Usage: echo enable=<value> | value = 0|1\n"
			"       echo debug=<value>  | value = 0|1\n\n");
		return -EINVAL;
	}

	return count;
}
static ssize_t proc_peer_tx_info_read(struct file *file,
	char __user *buffer, size_t count, loff_t *pos)
{
	char info[256];
	int offset = 0;
	struct seq_file *seq = file->private_data;
	struct rwnx_hw *rwnx_hw = seq->private;

	if (*pos > 0)
		return 0;

	offset += snprintf(info + offset, sizeof(info) - offset,
		"Peer Tx info enable: %d\n",
		rwnx_hw->enable_tx_info);
	offset += snprintf(info + offset, sizeof(info) - offset,
		"Show debug info: %d\n",
		rwnx_hw->enable_show_tx_info);

	if (!copy_to_user(buffer, info, strlen(info) + 1)) {
		*pos = 1;
		return strlen(info) + 1;
	} else
		return -1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops proc_peer_tx_info_fops = {
	.owner = THIS_MODULE,
	.open = kiwi_proc_open,
	.write = proc_peer_tx_info_write,
	.read = proc_peer_tx_info_read,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops proc_peer_tx_info_fops = {
	.proc_open = kiwi_proc_open,
	.proc_write = proc_peer_tx_info_write,
	.proc_read = proc_peer_tx_info_read,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

void wq_proc_init(struct rwnx_hw *rwnx_hw, struct wireless_dev *wdev)
{
	proc_mkdir(PROC_DIR, NULL);

	proc_create_data(PROC_DIR "/dbg", 0666, NULL, &kiwi_proc_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/rx_ctrl", 0666, NULL,
			 &kiwi_proc_rx_ctrl_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/misc_ctrl", 0666, NULL,
			 &kiwi_proc_misc_ctrl_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/fwdbg", 0666, NULL, &kiwi_proc_fwdbg_fops,
			 rwnx_hw);
	proc_create_data(PROC_DIR "/amsdu_param", S_IRUGO | S_IWUGO, NULL,
			 &kiwi_proc_amsdu_param_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/ampdu_param", S_IRUGO | S_IWUGO, NULL,
			 &kiwi_proc_ampdu_param_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/wifi_ctrl", 0666, NULL,
			 &kiwi_proc_wifi_ctrl_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/max_aggr_num", S_IRUGO | S_IWUGO, NULL,
			 &kiwi_proc_max_aggr_num_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/he_ltf_gi", S_IRUGO | S_IWUGO, NULL,
			 &kiwi_proc_he_ltf_gi_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/tx_mon_ctrl", S_IRUGO | S_IWUGO, NULL,
			 &kiwi_proc_tx_monitor_ctrl_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/amsdu_cntrl", S_IRUGO | S_IWUGO, NULL,
			 &kiwi_proc_amsdu_cntrl_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/debug_flag_set", S_IRUGO | S_IWUGO, NULL,
			 &kiwi_proc_debug_flag_set_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/param_proc", S_IRUGO | S_IWUGO, NULL,
			 &kiwi_proc_param_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/wifi_coex_scene", 0666, NULL,
			 &kiwi_proc_wifi_coex_scene_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/llrx", 0666, NULL, &proc_llrx_fops,
			 rwnx_hw);
#ifdef NAPI_SUPPORT
	proc_create_data(PROC_DIR "/napi_ctrl", S_IRUGO | S_IWUGO, NULL,
			 &wq_proc_napi_ctrl_fops, rwnx_hw);
#endif
#ifdef DEBUG_WQ_PRIV
	proc_create_data(PROC_DIR "/wqpriv_cmd", 0666, NULL,
			 &wq_wifi_priv_proc_fops, rwnx_hw);
#endif /* #ifdef DEBUG_WQ_PRIV */
	proc_create_data(PROC_DIR "/test_mode", S_IRUGO | S_IWUGO, NULL,
			 &proc_test_mode_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/monitor_nbw_type", S_IRUGO | S_IWUGO, NULL,
			&proc_monitor_nbw_type_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/throughput", S_IRUGO, NULL,
			&proc_throughput_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/fw_ver", S_IRUGO, NULL,
			&proc_read_fw_ver_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/mon_param", S_IRUGO, NULL,
			&proc_read_mon_param_fops, rwnx_hw);
	proc_create_data(PROC_DIR "/extap_ctrl", S_IRUGO | S_IWUGO, NULL,
			 &proc_extap_ctrl_fops, wdev);
	proc_create_data(PROC_DIR "/peer_tx_info", S_IRUGO | S_IWUGO, NULL,
			 &proc_peer_tx_info_fops, rwnx_hw);

	wq_proc_dfx_create(wdev);
}

void wq_proc_deinit(void)
{
	remove_proc_entry(PROC_DIR "/dbg", NULL);
	remove_proc_entry(PROC_DIR "/rx_ctrl", NULL);
	remove_proc_entry(PROC_DIR "/misc_ctrl", NULL);
	remove_proc_entry(PROC_DIR "/fwdbg", NULL);
	remove_proc_entry(PROC_DIR "/amsdu_param", NULL);
	remove_proc_entry(PROC_DIR "/ampdu_param", NULL);
	remove_proc_entry(PROC_DIR "/wifi_ctrl", NULL);
	remove_proc_entry(PROC_DIR "/max_aggr_num", NULL);
	remove_proc_entry(PROC_DIR "/he_ltf_gi", NULL);
	remove_proc_entry(PROC_DIR "/tx_mon_ctrl", NULL);
	remove_proc_entry(PROC_DIR "/amsdu_cntrl", NULL);
	remove_proc_entry(PROC_DIR "/debug_flag_set", NULL);
	remove_proc_entry(PROC_DIR "/param_proc", NULL);
	remove_proc_entry(PROC_DIR "/wifi_coex_scene", NULL);
	remove_proc_entry(PROC_DIR "/llrx", NULL);
#ifdef NAPI_SUPPORT
	remove_proc_entry(PROC_DIR "/napi_ctrl", NULL);
#endif
#ifdef DEBUG_WQ_PRIV
	remove_proc_entry(PROC_DIR "/wqpriv_cmd", NULL);
#endif
	remove_proc_entry(PROC_DIR "/test_mode", NULL);

	remove_proc_entry(PROC_DIR "/monitor_nbw_type", NULL);
	remove_proc_entry(PROC_DIR "/extap_ctrl", NULL);
	remove_proc_entry(PROC_DIR "/peer_tx_info", NULL);
	remove_proc_entry(PROC_DIR "/throughput", NULL);
	remove_proc_entry(PROC_DIR "/fw_ver", NULL);
	remove_proc_entry(PROC_DIR "/mon_param", NULL);

	wq_proc_dfx_remove();

	remove_proc_entry(PROC_DIR, NULL);
}
