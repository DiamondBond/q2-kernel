/**
 ******************************************************************************
 *
 * @file rwnx_debugfs.c
 *
 * @brief Definition of debugfs entries
 *
 * Copyright (C) RivieraWaves 2012-2020
 *
 ******************************************************************************
 */

#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/debugfs.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/sort.h>
#include <linux/rtc.h>

#include "rwnx_debugfs.h"
#include "rwnx_msg_tx.h"
#include "rwnx_radar.h"
#include "rwnx_tx.h"
#include "wq_wifi_dbg.h"
#include "rwnx_events.h"
#include "wq_log.h"

static ssize_t rwnx_dbgfs_stats_read(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char *buf;
	int ret;
	int i, skipped, per;
	ssize_t read;
	int bufsz =
		(NX_TXQ_CNT)*20 + (ARRAY_SIZE(priv->stats.amsdus_rx) + 1) * 40 +
		(ARRAY_SIZE(priv->stats.ampdus_tx) * 30) + 30; //non-amsdu rx

	if (*ppos)
		return 0;

	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL)
		return 0;

	ret = scnprintf(buf, bufsz, "TXQs CFM balances ");
	for (i = 0; i < NX_TXQ_CNT; i++)
		ret += scnprintf(&buf[ret], bufsz - ret, "  [%1d]:%3d", i,
				 priv->stats.cfm_balance[i]);

	ret += scnprintf(&buf[ret], bufsz - ret, "\n");

#ifdef CONFIG_RWNX_SPLIT_TX_BUF
	ret += scnprintf(&buf[ret], bufsz - ret,
			 "\nAMSDU[len]       done         failed   received\n");
	for (i = skipped = 0; i < NX_TX_PAYLOAD_MAX; i++) {
		if (priv->stats.amsdus[i].done) {
			per = DIV_ROUND_UP((priv->stats.amsdus[i].failed) * 100,
					   priv->stats.amsdus[i].done);
		} else if (priv->stats.amsdus_rx[i]) {
			per = 0;
		} else {
			per = 0;
			skipped = 1;
			continue;
		}
		if (skipped) {
			ret += scnprintf(&buf[ret], bufsz - ret, "   ...\n");
			skipped = 0;
		}

		ret += scnprintf(&buf[ret], bufsz - ret,
				 "   [%2d]    %10d %8d(%3d%%) %10d\n",
				 i ? i + 1 : i, priv->stats.amsdus[i].done,
				 priv->stats.amsdus[i].failed, per,
				 priv->stats.amsdus_rx[i]);
	}

	for (; i < ARRAY_SIZE(priv->stats.amsdus_rx); i++) {
		if (!priv->stats.amsdus_rx[i]) {
			skipped = 1;
			continue;
		}
		if (skipped) {
			ret += scnprintf(&buf[ret], bufsz - ret, "   ...\n");
			skipped = 0;
		}

		ret += scnprintf(&buf[ret], bufsz - ret,
				 "   [%2d]                              %10d\n",
				 i + 1, priv->stats.amsdus_rx[i]);
	}
#else
	ret += scnprintf(&buf[ret], bufsz - ret, "\nAMSDU[len]   received\n");
	for (i = skipped = 0; i < ARRAY_SIZE(priv->stats.amsdus_rx); i++) {
		if (!priv->stats.amsdus_rx[i]) {
			skipped = 1;
			continue;
		}
		if (skipped) {
			ret += scnprintf(&buf[ret], bufsz - ret, "   ...\n");
			skipped = 0;
		}

		ret += scnprintf(&buf[ret], bufsz - ret, "   [%2d]    %10d\n",
				 i + 1, priv->stats.amsdus_rx[i]);
	}

#endif /* CONFIG_RWNX_SPLIT_TX_BUF */

	ret += scnprintf(&buf[ret], bufsz - ret,
			 "\nAMPDU[len]     done  received\n");
	for (i = skipped = 0; i < ARRAY_SIZE(priv->stats.ampdus_tx); i++) {
		if (!priv->stats.ampdus_tx[i] && !priv->stats.ampdus_rx[i]) {
			skipped = 1;
			continue;
		}
		if (skipped) {
			ret += scnprintf(&buf[ret], bufsz - ret, "    ...\n");
			skipped = 0;
		}

		ret += scnprintf(&buf[ret], bufsz - ret, "   [%2d]   %9d %9d\n",
				 i ? i + 1 : i, priv->stats.ampdus_tx[i],
				 priv->stats.ampdus_rx[i]);
	}

	ret += scnprintf(&buf[ret], bufsz - ret, "#mpdu missed        %9d\n",
			 priv->stats.ampdus_rx_miss);

	ret += scnprintf(&buf[ret], bufsz - ret, "#non-amsdu rx       %9d\n",
			 priv->stats.non_amsdu_rx);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);

	return read;
}

static ssize_t rwnx_dbgfs_stats_write(struct file *file,
				      const char __user *user_buf, size_t count,
				      loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;

	/* Prevent from interrupt preemption as these statistics are updated under
     * interrupt */
	spin_lock_bh(&priv->tx_lock);

	memset(&priv->stats, 0, sizeof(priv->stats));

	spin_unlock_bh(&priv->tx_lock);

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(stats);

#define TXQ_STA_PREF "tid|"
#define TXQ_STA_PREF_FMT "%3d|"

#define TXQ_VIF_PREF "type|"
#define TXQ_VIF_PREF_FMT "%4s|"

#define TXQ_HDR "idx|  status|credit|ready|retry|pushed"
#define TXQ_HDR_FMT "%3d|%s%s%s%s%s%s%s%s|%6d|%5d|%5d|%6d"

#ifdef CONFIG_RWNX_AMSDUS_TX
#define TXQ_HDR_SUFF "|amsdu"
#define TXQ_HDR_SUFF_FMT "|%5d"
#else
#define TXQ_HDR_SUFF ""
#define TXQ_HDR_SUF_FMT ""
#endif /* CONFIG_RWNX_AMSDUS_TX */

#define TXQ_HDR_MAX_LEN                                                        \
	(sizeof(TXQ_STA_PREF) + sizeof(TXQ_HDR) + sizeof(TXQ_HDR_SUFF) + 1)

#define PS_HDR "Legacy PS: ready=%d, sp=%d / UAPSD: ready=%d, sp=%d"
#define PS_HDR_LEGACY "Legacy PS: ready=%d, sp=%d"
#define PS_HDR_UAPSD "UAPSD: ready=%d, sp=%d"
#define PS_HDR_MAX_LEN                                                         \
	sizeof("Legacy PS: ready=xxx, sp=xxx / UAPSD: ready=xxx, sp=xxx\n")

#define STA_HDR "** STA %d (%pM)\n"
#define STA_HDR_MAX_LEN                                                        \
	sizeof("- STA xx (xx:xx:xx:xx:xx:xx)\n") + PS_HDR_MAX_LEN

#define VIF_HDR "* VIF [%d] %s\n"
#define VIF_HDR_MAX_LEN sizeof(VIF_HDR) + IFNAMSIZ

#ifdef CONFIG_RWNX_AMSDUS_TX
#define VIF_SEP "---------------------------------------\n"
#else /* ! CONFIG_RWNX_AMSDUS_TX */
#define VIF_SEP "---------------------------------\n"
#endif /* CONFIG_RWNX_AMSDUS_TX*/

#define VIF_SEP_LEN sizeof(VIF_SEP)

#define CAPTION                                                                \
	"status: L=in hwq list, F=stop full, P=stop sta PS, V=stop vif PS,\
 C=stop channel, S=stop CSA, M=stop MU, N=Ndev queue stopped"
#define CAPTION_LEN sizeof(CAPTION)

#define STA_TXQ 0
#define VIF_TXQ 1

/// Index of the HE statistics element in the table
#define RC_HE_STATS_IDX RC_MAX_N_SAMPLE

static int rwnx_dbgfs_txq(char *buf, size_t size, struct rwnx_txq *txq,
			  int type, int tid, char *name)
{
	int res, idx = 0;
	int i, pushed = 0;

	if (type == STA_TXQ) {
		res = scnprintf(&buf[idx], size, TXQ_STA_PREF_FMT, tid);
		idx += res;
		size -= res;
	} else {
		res = scnprintf(&buf[idx], size, TXQ_VIF_PREF_FMT, name);
		idx += res;
		size -= res;
	}

	for (i = 0; i < CONFIG_USER_MAX; i++) {
		pushed += txq->pkt_pushed[i];
	}

	res = scnprintf(&buf[idx], size, TXQ_HDR_FMT, txq->idx,
			(txq->status & RWNX_TXQ_IN_HWQ_LIST) ? "L" : " ",
			(txq->status & RWNX_TXQ_STOP_FULL) ? "F" : " ",
			(txq->status & RWNX_TXQ_STOP_STA_PS) ? "P" : " ",
			(txq->status & RWNX_TXQ_STOP_VIF_PS) ? "V" : " ",
			(txq->status & RWNX_TXQ_STOP_CHAN) ? "C" : " ",
			(txq->status & RWNX_TXQ_STOP_CSA) ? "S" : " ",
			(txq->status & RWNX_TXQ_STOP_MU_POS) ? "M" : " ",
			(txq->status & RWNX_TXQ_NDEV_FLOW_CTRL) ? "N" : " ",
			txq->credits,
			(skb_queue_len(&txq->sk_list) +
			 skb_queue_len(&txq->sk_ack_list)),
			txq->nb_retry, pushed);
	idx += res;
	size -= res;

#ifdef CONFIG_RWNX_AMSDUS_TX
	if (type == STA_TXQ) {
		res = scnprintf(&buf[idx], size, TXQ_HDR_SUFF_FMT,
				txq->amsdu_len);
		idx += res;
		size -= res;
	}
#endif

	res = scnprintf(&buf[idx], size, "\n");
	idx += res;
	size -= res;

	return idx;
}

static int rwnx_dbgfs_txq_sta(char *buf, size_t size, struct rwnx_sta *rwnx_sta,
			      struct rwnx_hw *rwnx_hw)
{
	int tid, res, idx = 0;
	struct rwnx_txq *txq;

	res = scnprintf(&buf[idx], size, "\n" STA_HDR, rwnx_sta->sta_idx,
			rwnx_sta->mac_addr);
	idx += res;
	size -= res;

	if (rwnx_sta->ps.active) {
		if (rwnx_sta->uapsd_tids &&
		    (rwnx_sta->uapsd_tids == ((1 << NX_NB_TXQ_PER_STA) - 1)))
			res = scnprintf(&buf[idx], size, PS_HDR_UAPSD "\n",
					rwnx_sta->ps.pkt_ready[UAPSD_ID],
					rwnx_sta->ps.sp_cnt[UAPSD_ID]);
		else if (rwnx_sta->uapsd_tids)
			res = scnprintf(&buf[idx], size, PS_HDR "\n",
					rwnx_sta->ps.pkt_ready[LEGACY_PS_ID],
					rwnx_sta->ps.sp_cnt[LEGACY_PS_ID],
					rwnx_sta->ps.pkt_ready[UAPSD_ID],
					rwnx_sta->ps.sp_cnt[UAPSD_ID]);
		else
			res = scnprintf(&buf[idx], size, PS_HDR_LEGACY "\n",
					rwnx_sta->ps.pkt_ready[LEGACY_PS_ID],
					rwnx_sta->ps.sp_cnt[LEGACY_PS_ID]);
		idx += res;
		size -= res;
	} else {
		res = scnprintf(&buf[idx], size, "\n");
		idx += res;
		size -= res;
	}

	res = scnprintf(&buf[idx], size,
			TXQ_STA_PREF TXQ_HDR TXQ_HDR_SUFF "\n");
	idx += res;
	size -= res;

	foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw)
	{
		res = rwnx_dbgfs_txq(&buf[idx], size, txq, STA_TXQ, tid, NULL);
		idx += res;
		size -= res;
	}

	return idx;
}

static int rwnx_dbgfs_txq_vif(char *buf, size_t size, struct rwnx_vif *rwnx_vif,
			      struct rwnx_hw *rwnx_hw)
{
	int res, idx = 0;
	struct rwnx_txq *txq;
	struct rwnx_sta *rwnx_sta;

	res = scnprintf(&buf[idx], size, VIF_HDR, rwnx_vif->vif_index,
			rwnx_vif->ndev->name);
	idx += res;
	size -= res;
	if (!rwnx_vif->up || rwnx_vif->ndev == NULL)
		return idx;

	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP ||
	    RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO ||
	    RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MESH_POINT) {
		res = scnprintf(&buf[idx], size, TXQ_VIF_PREF TXQ_HDR "\n");
		idx += res;
		size -= res;
		txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
		res = rwnx_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, "UNK");
		idx += res;
		size -= res;
		txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
		res = rwnx_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, "BCMC");
		idx += res;
		size -= res;
		rwnx_sta = &rwnx_hw->sta_table[rwnx_vif->ap.bcmc_index];
		if (rwnx_sta->ps.active) {
			res = scnprintf(&buf[idx], size, PS_HDR_LEGACY "\n",
					rwnx_sta->ps.sp_cnt[LEGACY_PS_ID],
					rwnx_sta->ps.sp_cnt[LEGACY_PS_ID]);
			idx += res;
			size -= res;
		} else {
			res = scnprintf(&buf[idx], size, "\n");
			idx += res;
			size -= res;
		}

		list_for_each_entry (rwnx_sta, &rwnx_vif->ap.sta_list, list) {
			res = rwnx_dbgfs_txq_sta(&buf[idx], size, rwnx_sta,
						 rwnx_hw);
			idx += res;
			size -= res;
		}
	} else if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||
		   RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
		if (rwnx_vif->sta.ap) {
			res = rwnx_dbgfs_txq_sta(&buf[idx], size,
						 rwnx_vif->sta.ap, rwnx_hw);
			idx += res;
			size -= res;
		}
	}

	return idx;
}

static ssize_t rwnx_dbgfs_txq_read(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = file->private_data;
	struct rwnx_vif *vif;
	char *buf;
	int idx, res;
	ssize_t read;
	size_t bufsz =
		((NX_VIRT_DEV_MAX * (VIF_HDR_MAX_LEN + 2 * VIF_SEP_LEN)) +
		 (NX_REMOTE_STA_MAX * STA_HDR_MAX_LEN) +
		 ((NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX + NX_NB_TXQ) *
		  TXQ_HDR_MAX_LEN) +
		 CAPTION_LEN);

	/* everything is read in one go */
	if (*ppos)
		return 0;

	bufsz = min_t(size_t, bufsz, count);
	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL)
		return 0;

	bufsz--;
	idx = 0;

	res = scnprintf(&buf[idx], bufsz, CAPTION);
	idx += res;
	bufsz -= res;

	//spin_lock_bh(&rwnx_hw->tx_lock);
	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		res = scnprintf(&buf[idx], bufsz, "\n" VIF_SEP);
		idx += res;
		bufsz -= res;
		res = rwnx_dbgfs_txq_vif(&buf[idx], bufsz, vif, rwnx_hw);
		idx += res;
		bufsz -= res;
		res = scnprintf(&buf[idx], bufsz, VIF_SEP);
		idx += res;
		bufsz -= res;
	}
	//spin_unlock_bh(&rwnx_hw->tx_lock);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, idx);
	kfree(buf);

	return read;
}
DEBUGFS_READ_FILE_OPS(txq);

static ssize_t rwnx_dbgfs_acsinfo_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	struct wiphy *wiphy = priv->wiphy;
	size_t bufsz = (SCAN_CHANNEL_MAX + 1) * 43;
	char *buf = NULL;
	int survey_cnt = 0;
	int len = 0;
	int band, support_band, chan_cnt;
	ssize_t res;

#ifdef SUPPORT_5G_BAND
	support_band = NL80211_BAND_5GHZ;
#else
	support_band = NL80211_BAND_2GHZ;
#endif

	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL)
		return 0;
	memset(buf, 0, bufsz);

	mutex_lock(&priv->dbgdump_elem.mutex);
	len += scnprintf(buf, min_t(size_t, bufsz - 1, count),
			 "FREQ    TIME(ms)    BUSY(ms)    NOISE(dBm)\n");

	for (band = NL80211_BAND_2GHZ; band <= support_band; band++) {
		for (chan_cnt = 0; chan_cnt < wiphy->bands[band]->n_channels;
		     chan_cnt++) {
			struct rwnx_survey_info *p_survey_info =
				&priv->survey[survey_cnt];
			struct ieee80211_channel *p_chan =
				&wiphy->bands[band]->channels[chan_cnt];

			if (p_survey_info->filled) {
				len += scnprintf(
					&buf[len],
					min_t(size_t, bufsz - len - 1, count),
					"%d    %03d         %03d         %d\n",
					p_chan->center_freq,
					p_survey_info->chan_time_ms,
					p_survey_info->chan_time_busy_ms,
					p_survey_info->noise_dbm);
			} else {
				len += scnprintf(&buf[len],
						 min_t(size_t, bufsz - len - 1,
						       count),
						 "%d    NOT AVAILABLE\n",
						 p_chan->center_freq);
			}

			survey_cnt++;
		}
	}

	mutex_unlock(&priv->dbgdump_elem.mutex);

	res = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	if (buf != NULL) {
		kfree(buf);
		buf = NULL;
	}

	return res;
}

DEBUGFS_READ_FILE_OPS(acsinfo);

static ssize_t rwnx_dbgfs_tx_credit_read(struct file *file,
					 char __user *user_buf, size_t count,
					 loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = file->private_data;
	char *buf;
	int bufsz, len = 0;
	ssize_t read;
	int i;
	char *ac[] = { "BK", "BE", "VO", "VI" };

	struct credit_mgmt *crdt_mgmt = &rwnx_hw->crdt_mgmt;
	struct credit_grp *crdt_grp = &(crdt_mgmt->credit_grp[0]);

	/* Currently, we fixed the buffer size to 512 */
	bufsz = 512;
	buf = kmalloc(bufsz + 1, GFP_KERNEL);
	if (!buf)
		return 0;

	len = scnprintf(buf, bufsz,
			"Device total credit size: %d, Active Group: %02x\n",
			crdt_mgmt->dev_credit_sz, crdt_mgmt->active_group);

	spin_lock_bh(&crdt_mgmt->credit_mgmt_lock);
	for (i = 0; i < WQ_CREDIT_GROUP_NUM; i++) {
		int j;

		len += scnprintf(&buf[len], bufsz - len,
				 "Group %d|  credit |  lend |    tick\n", i);

		for (j = 0; j < WQ_CREDIT_TYPE_NUM; j++) {
			len += scnprintf(&buf[len], bufsz - len,
					 "     %s     %02x       %02x    %lu\n",
					 ac[j], crdt_grp[i].credit[j],
					 crdt_grp[i].lend[j],
					 crdt_grp[i].tick[j]);
		}
	}
	spin_unlock_bh(&crdt_mgmt->credit_mgmt_lock);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return read;
}

DEBUGFS_READ_FILE_OPS(tx_credit);

static ssize_t rwnx_dbgfs_reg_addr_read(struct file *file,
					char __user *user_buf, size_t count,
					loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	uint8_t buf[50];
	unsigned int len = 0;

	if (priv->dbgfs_diag_reg)
		len += scnprintf(buf + len, sizeof(buf) - len, "0x%x\n",
				 priv->dbgfs_diag_reg);
	else
		len += scnprintf(buf + len, sizeof(buf) - len, "No address\n");

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static bool rwnx_is_diag_reg_valid(uint32_t reg_addr)
{
	return true;
}

static ssize_t rwnx_dbgfs_reg_addr_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	unsigned long reg_addr;

	if (kstrtoul_from_user(user_buf, count, 0, &reg_addr))
		return -EINVAL;

	if ((reg_addr % 4) != 0)
		return -EINVAL;

	if (reg_addr && !rwnx_is_diag_reg_valid(reg_addr))
		return -EINVAL;

	priv->dbgfs_diag_reg = reg_addr;

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(reg_addr);

static int rwnx_dbgfs_reg_dump_open(struct inode *inode, struct file *file)
{
	struct rwnx_hw *priv = inode->i_private;
	uint8_t *buf;
	unsigned long int reg_len;
	unsigned int len = 0;
	uint32_t addr;
	uint32_t reg_val = 0;
	int status;

	reg_len = 25;

	buf = vmalloc(reg_len);
	if (!buf)
		return -ENOMEM;

	addr = priv->dbgfs_diag_reg;
	status =
		rwnx_read_reg(priv, addr, (uint8_t *)&reg_val, sizeof(reg_val));
	if (status)
		goto fail_reg_read;

	len += scnprintf(buf + len, reg_len - len, "0x%06x 0x%08x\n", addr,
			 le32_to_cpu(reg_val));
	file->private_data = buf;
	return 0;

fail_reg_read:
	WQ_DBG(DM_ALL, DL_ERR, "Unable to read memory: %u\n", addr);
	vfree(buf);
	return -EIO;
}

static ssize_t rwnx_dbgfs_reg_dump_read(struct file *file,
					char __user *user_buf, size_t count,
					loff_t *ppos)
{
	uint8_t *buf = file->private_data;
	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static int rwnx_dbgfs_reg_dump_release(struct inode *inode, struct file *file)
{
	vfree(file->private_data);
	return 0;
}

DEBUGFS_READ_OPEN_RELEASE_FILE_OPS(reg_dump);

static ssize_t rwnx_dbgfs_reg_write_read(struct file *file,
					 char __user *user_buf, size_t count,
					 loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	uint8_t buf[32];
	unsigned int len = 0;

	len = scnprintf(buf, sizeof(buf), "Addr: 0x%x Val: 0x%x\n",
			priv->diag_reg_addr_wr, priv->diag_reg_val_wr);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t rwnx_dbgfs_reg_write_write(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	char *sptr, *token;
	unsigned int len = 0;
	uint32_t reg_addr, reg_val;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	sptr = buf;

	token = strsep(&sptr, "=");
	if (!token)
		return -EINVAL;

	if (kstrtou32(token, 0, &reg_addr))
		return -EINVAL;

	if (!rwnx_is_diag_reg_valid(reg_addr))
		return -EINVAL;

	if (kstrtou32(sptr, 0, &reg_val))
		return -EINVAL;

	priv->diag_reg_addr_wr = reg_addr;
	priv->diag_reg_val_wr = reg_val;

	if (rwnx_write_reg(priv, priv->diag_reg_addr_wr,
			   cpu_to_le32(priv->diag_reg_val_wr)))
		return -EIO;

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(reg_write);

static ssize_t rwnx_dbgfs_tx_statics_read(struct file *file,
					  char __user *user_buf, size_t count,
					  loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	struct rwnx_debugfs *debugfs = &priv->debugfs;
	uint8_t buf[32];
	unsigned int len = 0;

	len = scnprintf(buf, sizeof(buf), "txq_num: %d, mac_id: %d\n",
			debugfs->txq_num, debugfs->mac_id);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t rwnx_dbgfs_tx_statics_write(struct file *file,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	struct rwnx_debugfs *debugfs = &priv->debugfs;
	char buf[32];
	char *sptr, *token;
	unsigned int len = 0;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	sptr = buf;

	token = strsep(&sptr, "=");
	if (!token)
		return -EINVAL;

	if (!strcmp(token, "txq_num")) {
		u8 txq_num;

		if (kstrtou8(sptr, 0, &txq_num))
			return -EINVAL;

		if (txq_num < 0 || txq_num >= 5) {
			printk(KERN_ERR "txq_num %d is out of range\n",
			       txq_num);
			return -EINVAL;
		}

		debugfs->txq_num = txq_num;
	} else if (!strcmp(token, "mac_id")) {
		u8 mac_id;

		if (kstrtou8(sptr, 0, &mac_id))
			return -EINVAL;

		if (mac_id != 0 && mac_id != 1) {
			printk(KERN_ERR "mac_id can be only 0 or 1\n");
			return -EINVAL;
		}

		debugfs->mac_id = mac_id;
	} else {
		printk(KERN_ERR "cmd %s not support\n", token);
		return -EINVAL;
	}

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(tx_statics);

/*
 * example to access this API:
 *      cd /sys/kernel/debug/ieee80211/phy0/rwnx
 *      echo mac_id=0 >tx_statics
 *      echo txq_num=1 >tx_statics
 *      cat tx_stats_dump
 */
static ssize_t rwnx_dbgfs_tx_stats_dump_read(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	struct rwnx_debugfs *debugfs = &priv->debugfs;
	struct dbg_tx_statics_req req = {
		.txq_num = debugfs->txq_num,
		.mac_id = debugfs->mac_id,
	};
	struct {
		struct txl_txstats tx_stats;
		struct txl_txstats_external_v1 v1;
	} tx_stats_buf = {};
	int ret;

	/* everything should fit in one call */
	if (*ppos)
		return 0;

	ret = RWNX_INFO_NOTIFY_GET_NO_CHK(priv, MSG_TYPE_TX_STATICS, req,
					  &tx_stats_buf);
	if (ret >= sizeof(tx_stats_buf.tx_stats)) {
		struct txl_txstats *tx_stats = &tx_stats_buf.tx_stats;
		ssize_t read = 0;
		int len = 0;
		int bufsz = 1024;
		char *buf = kmalloc(bufsz + 1, GFP_KERNEL);

		if (buf == NULL)
			return 0;

		len += scnprintf(buf, bufsz, "==== mac: %d, txq: %d ====\n",
				 debugfs->mac_id, debugfs->txq_num);
		len += scnprintf(&buf[len], bufsz - len, "txl_cntrl_cnt: %u\n",
				 tx_stats->txl_cntrl_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "payload_alloc_cnt: %u\n",
				 tx_stats->payload_alloc_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "payload_transfer_cnt: %u\n",
				 tx_stats->payload_transfer_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "buffer_alloc_cnt: %u\n",
				 tx_stats->buffer_alloc_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "buffer_alloc_fail_cnt: %u\n",
				 tx_stats->buffer_alloc_fail_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "buffer_free_cnt: %u\n",
				 tx_stats->buffer_free_cnt);
		len += scnprintf(&buf[len], bufsz - len, "dma_push_cnt: %u\n",
				 tx_stats->dma_push_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "frame_queue_cnt: %u\n",
				 tx_stats->frame_queue_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "frame_direct_xmit_cnt: %u\n",
				 tx_stats->frame_direct_xmit_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "frame_sn_in_baw_cnt: %u\n",
				 tx_stats->frame_sn_in_baw_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "frame_sn_out_of_baw_cnt: %u\n",
				 tx_stats->frame_sn_out_of_baw_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "sn_out_of_baw_when_xmit_cnt: %u\n",
				 tx_stats->sn_out_of_baw_when_xmit_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "mpdu_in_aggr_cnt: %u\n",
				 tx_stats->mpdu_in_aggr_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "mpdu_in_single_cnt: %u\n",
				 tx_stats->mpdu_in_single_cnt);
		len += scnprintf(&buf[len], bufsz - len, "xmit_aggr_cnt: %u\n",
				 tx_stats->xmit_aggr_cnt);
		len += scnprintf(&buf[len], bufsz - len, "pkt_free_cnt: %u\n",
				 tx_stats->pkt_free_cnt);
		len += scnprintf(&buf[len], bufsz - len, "mpdu_retry_cnt: %u\n",
				 tx_stats->mpdu_retry_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "txu_discard_cnt: %u\n",
				 tx_stats->txu_discard_cnt);
		len += scnprintf(&buf[len], bufsz - len,
				 "queue_pause_cnt: %u\n",
				 tx_stats->queue_pause_cnt);
		len += scnprintf(&buf[len], bufsz - len, "tid_queue_cnt: %u\n",
				 tx_stats->tid_queue_cnt);
		len += scnprintf(&buf[len], bufsz - len, "tid_sched_cnt: %u\n",
				 tx_stats->tid_sched_cnt);

		if (ret >= sizeof(tx_stats_buf)) {
			len += scnprintf(&buf[len], bufsz - len,
					 "txl_discard_cnt: %u\n",
					 tx_stats_buf.v1.txl_discard_cnt);
		}

		read = simple_read_from_buffer(user_buf, count, ppos, buf, len);
		kfree(buf);
		return read;
	}

	return 0;
}

DEBUGFS_READ_FILE_OPS(tx_stats_dump);

static ssize_t rwnx_dbgfs_hml_dfx_edca_read(struct file *file,
					    char __user *user_buf, size_t count,
					    loff_t *ppos)
{
	char *buf;
	int len = 0;
	int error = 0;
	int bufsz;
	ssize_t read = 0;
	struct rwnx_hw *priv = file->private_data;
	struct rwnx_debugfs *debugfs = &priv->debugfs;
	struct dbg_dfx_edca_param dfx_edca;

	if (*ppos)
		return 0;

	if ((error = wq_get_edca_param(priv, debugfs->mac_id, &dfx_edca))) {
		goto done;
	} else {
		bufsz = 1024;
		buf = kmalloc(bufsz + 1, GFP_KERNEL);
		if (buf == NULL)
			goto done;

		len = scnprintf(buf, bufsz, "==== mac: %d ====\n",
				debugfs->mac_id);
		len += scnprintf(&buf[len], bufsz - len, "ac_param: %x\n",
				 dfx_edca.ac_param[0]);
		len += scnprintf(&buf[len], bufsz - len, "ac_param: %x\n",
				 dfx_edca.ac_param[1]);
		len += scnprintf(&buf[len], bufsz - len, "ac_param: %x\n",
				 dfx_edca.ac_param[2]);
		len += scnprintf(&buf[len], bufsz - len, "ac_param: %x\n",
				 dfx_edca.ac_param[3]);

		read = simple_read_from_buffer(user_buf, count, ppos, buf, len);
		kfree(buf);
	}

done:
	return read;
}

DEBUGFS_READ_FILE_OPS(hml_dfx_edca);

static ssize_t rwnx_dbgfs_fw_dbg_read(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	char help[] = "usage: [MOD:<ALL|KE|DBG|IPC|DMA|MM|TX|RX|PHY>]* "
		      "[DBG:<NONE|CRT|ERR|WRN|INF|VRB>]\n";

	return simple_read_from_buffer(user_buf, count, ppos, help,
				       sizeof(help));
}

static ssize_t rwnx_dbgfs_fw_dbg_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
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
			rwnx_send_dbg_set_sev_filter_req(priv, dbg);
		} else {
			idx++;
		}
	}

	if (mod) {
		rwnx_send_dbg_set_mod_filter_req(priv, mod);
	}

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(fw_dbg);

static ssize_t rwnx_dbgfs_sys_stats_read(struct file *file,
					 char __user *user_buf, size_t count,
					 loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[3 * 64];
	int len = 0;
	ssize_t read;
	int error = 0;
	struct dbg_get_sys_stat_cfm cfm;
	u32 sleep_int, sleep_frac, doze_int, doze_frac;

	ENTER();

	/* Get the information from the FW */
	if ((error = rwnx_send_dbg_get_sys_stat_req(priv, &cfm)))
		return error;

	if (cfm.stats_time == 0)
		return 0;

	sleep_int = ((cfm.cpu_sleep_time * 100) / cfm.stats_time);
	sleep_frac = (((cfm.cpu_sleep_time * 100) % cfm.stats_time) * 10) /
		     cfm.stats_time;
	doze_int = ((cfm.doze_time * 100) / cfm.stats_time);
	doze_frac = (((cfm.doze_time * 100) % cfm.stats_time) * 10) /
		    cfm.stats_time;

	len += scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
			 "\nSystem statistics:\n");
	len += scnprintf(&buf[len], min_t(size_t, sizeof(buf) - 1, count),
			 "  CPU sleep [%%]: %d.%d\n", sleep_int, sleep_frac);
	len += scnprintf(&buf[len], min_t(size_t, sizeof(buf) - 1, count),
			 "  Doze      [%%]: %d.%d\n", doze_int, doze_frac);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	return read;
}

DEBUGFS_READ_FILE_OPS(sys_stats);

#ifdef CONFIG_RWNX_MUMIMO_TX
static ssize_t rwnx_dbgfs_mu_group_read(struct file *file,
					char __user *user_buf, size_t count,
					loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = file->private_data;
	struct rwnx_mu_info *mu = &rwnx_hw->mu;
	struct rwnx_mu_group *group;
	size_t bufsz =
		NX_MU_GROUP_MAX * sizeof("xx = (xx - xx - xx - xx)\n") + 50;
	char *buf;
	int j, res, idx = 0;

	if (*ppos)
		return 0;

	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL)
		return 0;

	res = scnprintf(&buf[idx], bufsz,
			"MU Group list (%d groups, %d users max)\n",
			NX_MU_GROUP_MAX, CONFIG_USER_MAX);
	idx += res;
	bufsz -= res;

	list_for_each_entry (group, &mu->active_groups, list) {
		if (group->user_cnt) {
			res = scnprintf(&buf[idx], bufsz, "%2d = (",
					group->group_id);
			idx += res;
			bufsz -= res;
			for (j = 0; j < (CONFIG_USER_MAX - 1); j++) {
				if (group->users[j])
					res = scnprintf(
						&buf[idx], bufsz, "%2d - ",
						group->users[j]->sta_idx);
				else
					res = scnprintf(&buf[idx], bufsz,
							".. - ");

				idx += res;
				bufsz -= res;
			}

			if (group->users[j])
				res = scnprintf(&buf[idx], bufsz, "%2d)\n",
						group->users[j]->sta_idx);
			else
				res = scnprintf(&buf[idx], bufsz, "..)\n");

			idx += res;
			bufsz -= res;
		}
	}

	res = simple_read_from_buffer(user_buf, count, ppos, buf, idx);
	kfree(buf);

	return res;
}

DEBUGFS_READ_FILE_OPS(mu_group);
#endif

#ifdef CONFIG_RWNX_P2P_DEBUGFS
static ssize_t rwnx_dbgfs_oppps_write(struct file *file,
				      const char __user *user_buf, size_t count,
				      loff_t *ppos)
{
	struct rwnx_hw *rw_hw = file->private_data;
	struct rwnx_vif *rw_vif;
	char buf[32];
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	int ctw;

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	/* Read the written CT Window (provided in ms) value */
	if (sscanf(buf, "ctw=%d", &ctw) > 0) {
		/* Check if at least one VIF is configured as P2P GO */
		list_for_each_entry (rw_vif, &rw_hw->vifs, list) {
			if (RWNX_VIF_TYPE(rw_vif) == NL80211_IFTYPE_P2P_GO) {
				struct mm_set_p2p_oppps_cfm cfm;

				/* Forward request to the embedded and wait for confirmation */
				rwnx_send_p2p_oppps_req(rw_hw, rw_vif, (u8)ctw,
							&cfm);

				break;
			}
		}
	}

	return count;
}

DEBUGFS_WRITE_FILE_OPS(oppps);

static ssize_t rwnx_dbgfs_noa_write(struct file *file,
				    const char __user *user_buf, size_t count,
				    loff_t *ppos)
{
	struct rwnx_hw *rw_hw = file->private_data;
	struct rwnx_vif *rw_vif;
	char buf[64];
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	int noa_count, interval, duration, dyn_noa;

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	/* Read the written NOA information */
	if (sscanf(buf, "count=%d interval=%d duration=%d dyn=%d", &noa_count,
		   &interval, &duration, &dyn_noa) > 0) {
		/* Check if at least one VIF is configured as P2P GO */
		list_for_each_entry (rw_vif, &rw_hw->vifs, list) {
			if (RWNX_VIF_TYPE(rw_vif) == NL80211_IFTYPE_P2P_GO) {
				struct mm_set_p2p_noa_cfm cfm;

				/* Forward request to the embedded and wait for confirmation */
				rwnx_send_p2p_noa_req(rw_hw, rw_vif, noa_count,
						      interval, duration,
						      (dyn_noa > 0), &cfm);

				break;
			}
		}
	}

	return count;
}

DEBUGFS_WRITE_FILE_OPS(noa);
#endif /* CONFIG_RWNX_P2P_DEBUGFS */

struct rwnx_dbgfs_fw_trace {
	struct rwnx_fw_trace_local_buf lbuf;
	struct rwnx_fw_trace *trace;
	struct rwnx_hw *rwnx_hw;
};

static int rwnx_dbgfs_fw_trace_open(struct inode *inode, struct file *file)
{
	struct rwnx_dbgfs_fw_trace *ltrace =
		kmalloc(sizeof(*ltrace), GFP_KERNEL);
	struct rwnx_hw *priv = inode->i_private;

	if (!ltrace)
		return -ENOMEM;

	if (rwnx_fw_trace_alloc_local(&ltrace->lbuf, 5120)) {
		kfree(ltrace);
	}

	ltrace->trace = &priv->debugfs.fw_trace;
	ltrace->rwnx_hw = priv;
	file->private_data = ltrace;
	return 0;
}

static int rwnx_dbgfs_fw_trace_release(struct inode *inode, struct file *file)
{
	struct rwnx_dbgfs_fw_trace *ltrace = file->private_data;

	if (ltrace) {
		rwnx_fw_trace_free_local(&ltrace->lbuf);
		kfree(ltrace);
	}

	return 0;
}

static ssize_t rwnx_dbgfs_fw_trace_read(struct file *file,
					char __user *user_buf, size_t count,
					loff_t *ppos)
{
	struct rwnx_dbgfs_fw_trace *ltrace = file->private_data;
	bool dont_wait = ((file->f_flags & O_NONBLOCK) ||
			  ltrace->rwnx_hw->debugfs.unregistering);

	return rwnx_fw_trace_read(ltrace->trace, &ltrace->lbuf, dont_wait,
				  user_buf, count);
}

static ssize_t rwnx_dbgfs_fw_trace_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct rwnx_dbgfs_fw_trace *ltrace = file->private_data;
	int ret;

	ret = _rwnx_fw_trace_reset(ltrace->trace, true);
	if (ret)
		return ret;

	return count;
}

DEBUGFS_READ_WRITE_OPEN_RELEASE_FILE_OPS(fw_trace);

static ssize_t rwnx_dbgfs_fw_trace_level_read(struct file *file,
					      char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	return rwnx_fw_trace_level_read(&priv->debugfs.fw_trace, user_buf,
					count, ppos);
}

static ssize_t rwnx_dbgfs_fw_trace_level_write(struct file *file,
					       const char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	return rwnx_fw_trace_level_write(&priv->debugfs.fw_trace, user_buf,
					 count);
}
DEBUGFS_READ_WRITE_FILE_OPS(fw_trace_level);

void seq_print_mgmt_info(struct seq_file *file, u16 frame_control, u8 cat,
			 u8 type, u8 p2p)
{
	seq_printf(file, "(");
	switch (frame_control & IEEE80211_FCTL_STYPE) {
	case (IEEE80211_STYPE_ASSOC_REQ):
		seq_printf(file, "Association Request");
		break;
	case (IEEE80211_STYPE_ASSOC_RESP):
		seq_printf(file, "Association Response");
		break;
	case (IEEE80211_STYPE_REASSOC_REQ):
		seq_printf(file, "Reassociation Request");
		break;
	case (IEEE80211_STYPE_REASSOC_RESP):
		seq_printf(file, "Reassociation Response");
		break;
	case (IEEE80211_STYPE_PROBE_REQ):
		seq_printf(file, "Probe Request");
		break;
	case (IEEE80211_STYPE_PROBE_RESP):
		seq_printf(file, "Probe Response");
		break;
	case (IEEE80211_STYPE_BEACON):
		seq_printf(file, "Beacon");
		break;
	case (IEEE80211_STYPE_ATIM):
		seq_printf(file, "ATIM");
		break;
	case (IEEE80211_STYPE_DISASSOC):
		seq_printf(file, "Disassociation");
		break;
	case (IEEE80211_STYPE_AUTH):
		seq_printf(file, "Authentication");
		break;
	case (IEEE80211_STYPE_DEAUTH):
		seq_printf(file, "Deauthentication");
		break;
	case (IEEE80211_STYPE_ACTION):
		seq_printf(file, "Action");
		if (cat == MGMT_ACTION_PUBLIC_CAT && type == 0x9)
			switch (p2p) {
			case (P2P_ACTION_GO_NEG_REQ):
				seq_printf(file, ": GO Negociation Request");
				break;
			case (P2P_ACTION_GO_NEG_RSP):
				seq_printf(file, ": GO Negociation Response");
				break;
			case (P2P_ACTION_GO_NEG_CFM):
				seq_printf(file,
					   ": GO Negociation Confirmation");
				break;
			case (P2P_ACTION_INVIT_REQ):
				seq_printf(file, ": P2P Invitation Request");
				break;
			case (P2P_ACTION_INVIT_RSP):
				seq_printf(file, ": P2P Invitation Response");
				break;
			case (P2P_ACTION_DEV_DISC_REQ):
				seq_printf(file,
					   ": Device Discoverability Request");
				break;
			case (P2P_ACTION_DEV_DISC_RSP):
				seq_printf(file,
					   ": Device Discoverability Response");
				break;
			case (P2P_ACTION_PROV_DISC_REQ):
				seq_printf(file,
					   ": Provision Discovery Request");
				break;
			case (P2P_ACTION_PROV_DISC_RSP):
				seq_printf(file,
					   ": Provision Discovery Response");
				break;
			default:
				seq_printf(file, "Unknown p2p %d", p2p);
				break;
			}
		else {
			switch (cat) {
			case 0:
				seq_printf(file, ":Spectrum %d", type);
				break;
			case 1:
				seq_printf(file, ":QOS %d", type);
				break;
			case 2:
				seq_printf(file, ":DLS %d", type);
				break;
			case 3:
				seq_printf(file, ":BA %d", type);
				break;
			case 4:
				seq_printf(file, ":Public %d", type);
				break;
			case 5:
				seq_printf(file, ":Radio Measure %d", type);
				break;
			case 6:
				seq_printf(file, ":Fast BSS %d", type);
				break;
			case 7:
				seq_printf(file, ":HT Action %d", type);
				break;
			case 8:
				seq_printf(file, ":SA Query %d", type);
				break;
			case 9:
				seq_printf(file, ":Protected Public %d", type);
				break;
			case 10:
				seq_printf(file, ":WNM %d", type);
				break;
			case 11:
				seq_printf(file, ":Unprotected WNM %d", type);
				break;
			case 12:
				seq_printf(file, ":TDLS %d", type);
				break;
			case 13:
				seq_printf(file, ":Mesh %d", type);
				break;
			case 14:
				seq_printf(file, ":MultiHop %d", type);
				break;
			case 15:
				seq_printf(file, ":Self Protected %d", type);
				break;
			case 126:
				seq_printf(file, ":Vendor protected");
				break;
			case 127:
				seq_printf(file, ":Vendor");
				break;
			default:
				seq_printf(file, ":Unknown category %d", cat);
				break;
			}
		}
		break;
	default:
		seq_printf(file, "Unknown subtype %d",
			   frame_control & IEEE80211_FCTL_STYPE);
		break;
	}
	seq_printf(file, ") ");
	return;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
static int read_file_dump_hist(struct seq_file *file, void *data)
{
	uint8_t i, cnt;
	struct rtc_time rtc_tv;

	for (cnt = 0; cnt < HIST_CNT; cnt++) {
		i = (dump_idx + cnt) % HIST_CNT;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
		rtc_time64_to_tm(
			(pktdump_hist[i].ts - sys_tz.tz_minuteswest * 60),
			&rtc_tv);
#else
		rtc_time_to_tm(
			(pktdump_hist[i].ts - sys_tz.tz_minuteswest * 60),
			&rtc_tv);
#endif
		seq_printf(file, "%04d-%02d-%02d %02d:%02d:%02d ",
			   rtc_tv.tm_year + 1900, rtc_tv.tm_mon + 1,
			   rtc_tv.tm_mday, rtc_tv.tm_hour, rtc_tv.tm_min,
			   rtc_tv.tm_sec);

		seq_printf(file, "sn:%03d [%s] macid:%d stat=%08x %pM | %pM ",
			   pktdump_hist[i].sn,
			   pktdump_hist[i].dir == WIFI_DBG_PKT_TX ? "Tx" : "Rx",
			   pktdump_hist[i].mac_id, pktdump_hist[i].tx_status,
			   pktdump_hist[i].da, pktdump_hist[i].sa);

		seq_print_mgmt_info(file, pktdump_hist[i].frame_ctrl,
				    pktdump_hist[i].category,
				    pktdump_hist[i].action_type,
				    pktdump_hist[i].p2p);
		seq_printf(file, "\n");
	}
	return 0;
}

static int read_file_mgmt_hist(struct seq_file *file, void *data)
{
	uint8_t i, cnt;
	struct rtc_time rtc_tv;
	struct wq_core *core = dev_get_drvdata(file->private);
	struct rwnx_hw *priv = core ? core->hw : NULL;

	if (!priv)
		return 0;

	spin_lock_bh(&priv->mgmt_hist_lock);
	for (cnt = 0; cnt < HIST_CNT; cnt++) {
		i = (mgmt_idx + cnt) % HIST_CNT;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
		rtc_time64_to_tm((mgmt_hist[i].ts - sys_tz.tz_minuteswest * 60),
				 &rtc_tv);
#else
		rtc_time_to_tm((mgmt_hist[i].ts - sys_tz.tz_minuteswest * 60),
				 &rtc_tv);
#endif
		seq_printf(file, "%04d-%02d-%02d %02d:%02d:%02d [%s] ",
			   rtc_tv.tm_year + 1900, rtc_tv.tm_mon + 1,
			   rtc_tv.tm_mday, rtc_tv.tm_hour, rtc_tv.tm_min,
			   rtc_tv.tm_sec,
			   mgmt_hist[i].dir == WIFI_DBG_PKT_TX ? "Tx" : "Rx");

		seq_printf(file, "%pM | %pM ", mgmt_hist[i].da,
			   mgmt_hist[i].sa);

		seq_print_mgmt_info(file, mgmt_hist[i].frame_ctrl,
				    mgmt_hist[i].category,
				    mgmt_hist[i].action_type, mgmt_hist[i].p2p);

		if (mgmt_hist[i].dir == WIFI_DBG_PKT_TX) {
			seq_printf(file, "ack=%x", mgmt_hist[i].ack);
		}
		seq_printf(file, "\n");
	}
	spin_unlock_bh(&priv->mgmt_hist_lock);
	return 0;
}
#endif

#ifdef CONFIG_RWNX_RADAR
static ssize_t rwnx_dbgfs_pulses_read(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos, int rd_idx)
{
	struct rwnx_hw *priv = file->private_data;
	char *buf;
	int len = 0;
	int bufsz;
	int i;
	int index;
	struct rwnx_radar_pulses *p = &priv->radar.pulses[rd_idx];
	ssize_t read;

	if (*ppos != 0)
		return 0;

	/* Prevent from interrupt preemption */
	spin_lock_bh(&priv->radar.lock);
	bufsz = p->count * 34 + 51;
	bufsz +=
		rwnx_radar_dump_pattern_detector(NULL, 0, &priv->radar, rd_idx);
	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL) {
		spin_unlock_bh(&priv->radar.lock);
		return 0;
	}

	if (p->count) {
		len += scnprintf(&buf[len], bufsz - len,
				 " PRI     WIDTH     FOM     FREQ\n");
		index = p->index;
		for (i = 0; i < p->count; i++) {
			struct radar_pulse *pulse;

			if (index > 0)
				index--;
			else
				index = RWNX_RADAR_PULSE_MAX - 1;

			pulse = (struct radar_pulse *)&p->buffer[index];

			len += scnprintf(
				&buf[len], bufsz - len,
				"%05dus  %03dus     %2d%%    %+3dMHz\n",
				pulse->rep, 2 * pulse->len, 6 * pulse->fom,
				2 * pulse->freq);
		}
	}

	len += rwnx_radar_dump_pattern_detector(&buf[len], bufsz - len,
						&priv->radar, rd_idx);

	spin_unlock_bh(&priv->radar.lock);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);

	return read;
}

static ssize_t rwnx_dbgfs_pulses_prim_read(struct file *file,
					   char __user *user_buf, size_t count,
					   loff_t *ppos)
{
	return rwnx_dbgfs_pulses_read(file, user_buf, count, ppos, 0);
}

DEBUGFS_READ_FILE_OPS(pulses_prim);

static ssize_t rwnx_dbgfs_pulses_sec_read(struct file *file,
					  char __user *user_buf, size_t count,
					  loff_t *ppos)
{
	return rwnx_dbgfs_pulses_read(file, user_buf, count, ppos, 1);
}

DEBUGFS_READ_FILE_OPS(pulses_sec);

static ssize_t rwnx_dbgfs_detected_read(struct file *file,
					char __user *user_buf, size_t count,
					loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char *buf;
	int bufsz, len = 0;
	ssize_t read;

	if (*ppos != 0)
		return 0;

	bufsz = 5; // RIU:\n
	bufsz += rwnx_radar_dump_radar_detected(NULL, 0, &priv->radar,
						RWNX_RADAR_RIU);

	if (priv->phy.cnt > 1) {
		bufsz += 5; // FCU:\n
		bufsz += rwnx_radar_dump_radar_detected(NULL, 0, &priv->radar,
							RWNX_RADAR_FCU);
	}

	buf = kmalloc(bufsz, GFP_KERNEL);
	if (buf == NULL) {
		return 0;
	}

	len = scnprintf(&buf[len], bufsz, "RIU:\n");
	len += rwnx_radar_dump_radar_detected(&buf[len], bufsz - len,
					      &priv->radar, RWNX_RADAR_RIU);

	if (priv->phy.cnt > 1) {
		len += scnprintf(&buf[len], bufsz - len, "FCU:\n");
		len += rwnx_radar_dump_radar_detected(
			&buf[len], bufsz - len, &priv->radar, RWNX_RADAR_FCU);
	}

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);

	return read;
}

DEBUGFS_READ_FILE_OPS(detected);

static ssize_t rwnx_dbgfs_enable_read(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
			"RIU=%d FCU=%d\n",
			priv->radar.dpd[RWNX_RADAR_RIU]->enabled,
			priv->radar.dpd[RWNX_RADAR_FCU]->enabled);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_enable_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int val;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (sscanf(buf, "RIU=%d", &val) > 0)
		rwnx_radar_detection_enable(&priv->radar, val, RWNX_RADAR_RIU);

	if (sscanf(buf, "FCU=%d", &val) > 0)
		rwnx_radar_detection_enable(&priv->radar, val, RWNX_RADAR_FCU);

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(enable);

static ssize_t rwnx_dbgfs_band_read(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count), "BAND=%d\n",
			priv->phy.sec_chan.band);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_band_write(struct file *file,
				     const char __user *user_buf, size_t count,
				     loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int val;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if ((sscanf(buf, "%d", &val) > 0) && (val >= 0) &&
	    (val <= NL80211_BAND_5GHZ))
		priv->phy.sec_chan.band = val;

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(band);

static ssize_t rwnx_dbgfs_type_read(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count), "TYPE=%d\n",
			priv->phy.sec_chan.type);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_type_write(struct file *file,
				     const char __user *user_buf, size_t count,
				     loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int val;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if ((sscanf(buf, "%d", &val) > 0) && (val >= PHY_CHNL_BW_20) &&
	    (val <= PHY_CHNL_BW_80P80))
		priv->phy.sec_chan.type = val;

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(type);

static ssize_t rwnx_dbgfs_prim20_read(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
			"PRIM20=%dMHz\n", priv->phy.sec_chan.prim20_freq);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_prim20_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int val;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (sscanf(buf, "%d", &val) > 0)
		priv->phy.sec_chan.prim20_freq = val;

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(prim20);

static ssize_t rwnx_dbgfs_center1_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
			"CENTER1=%dMHz\n", priv->phy.sec_chan.center1_freq);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_center1_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int val;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (sscanf(buf, "%d", &val) > 0)
		priv->phy.sec_chan.center1_freq = val;

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(center1);

static ssize_t rwnx_dbgfs_center2_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
			"CENTER2=%dMHz\n", priv->phy.sec_chan.center2_freq);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_center2_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int val;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (sscanf(buf, "%d", &val) > 0)
		priv->phy.sec_chan.center2_freq = val;

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(center2);

static ssize_t rwnx_dbgfs_set_read(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t rwnx_dbgfs_set_write(struct file *file,
				    const char __user *user_buf, size_t count,
				    loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;

	rwnx_send_set_channel(priv, 1, NULL);
	rwnx_radar_detection_enable(&priv->radar, RWNX_RADAR_DETECT_ENABLE,
				    RWNX_RADAR_FCU);

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(set);
#endif /* CONFIG_RWNX_RADAR */

#define LINE_MAX_SZ 150

struct st {
	char line[LINE_MAX_SZ + 1];
	unsigned int r_idx;
};

static int compare_idx(const void *st1, const void *st2)
{
	int index1 = ((struct st *)st1)->r_idx;
	int index2 = ((struct st *)st2)->r_idx;

	if (index1 > index2)
		return 1;
	if (index1 < index2)
		return -1;

	return 0;
}

static const int ru_size[] = { 26, 52, 106, 242, 484, 996 };

static int print_rate(char *buf, int size, int format, int nss, int mcs, int bw,
		      int sgi, int pre, int dcm, int *r_idx)
{
	int res = 0;
	int bitrates_cck[4] = { 10, 20, 55, 110 };
	int bitrates_ofdm[8] = { 6, 9, 12, 18, 24, 36, 48, 54 };
	char he_gi[3][4] = { "0.8", "1.6", "3.2" };

	if (format < FORMATMOD_HT_MF) {
		if (mcs < 4) {
			if (r_idx) {
				*r_idx = (mcs * 2) + pre;
				res = scnprintf(buf, size - res, "%3d ",
						*r_idx);
			}
			res += scnprintf(&buf[res], size - res,
					 "L-CCK/%cP          %2u.%1uM    ",
					 pre > 0 ? 'L' : 'S',
					 bitrates_cck[mcs] / 10,
					 bitrates_cck[mcs] % 10);
		} else {
			mcs -= 4;
			if (r_idx) {
				*r_idx = N_CCK + mcs;
				res = scnprintf(buf, size - res, "%3d ",
						*r_idx);
			}
			res += scnprintf(&buf[res], size - res,
					 "L-OFDM            %2u.0M    ",
					 bitrates_ofdm[mcs]);
		}
	} else if (format < FORMATMOD_VHT) {
		if (r_idx) {
			*r_idx = N_CCK + N_OFDM + nss * 32 + mcs * 4 + bw * 2 +
				 sgi;
			res = scnprintf(buf, size - res, "%3d ", *r_idx);
		}
		mcs += nss * 8;
		res += scnprintf(&buf[res], size - res,
				 "HT%d/%cGI           MCS%-2d   ",
				 20 * (1 << bw), sgi ? 'S' : 'L', mcs);
	} else if (format == FORMATMOD_VHT) {
		if (r_idx) {
			*r_idx = N_CCK + N_OFDM + N_HT + nss * 80 + mcs * 8 +
				 bw * 2 + sgi;
			res = scnprintf(buf, size - res, "%3d ", *r_idx);
		}
		res += scnprintf(&buf[res], size - res,
				 "VHT%d/%cGI%*cMCS%d/%1d  ", 20 * (1 << bw),
				 sgi ? 'S' : 'L', bw > 2 ? 9 : 10, ' ', mcs,
				 nss + 1);
	} else if (format == FORMATMOD_HE_SU) {
		if (r_idx) {
			*r_idx = N_CCK + N_OFDM + N_HT + N_VHT + nss * 144 +
				 mcs * 12 + bw * 3 + sgi;
			res = scnprintf(buf, size - res, "%3d ", *r_idx);
		}
		res += scnprintf(&buf[res], size - res,
				 "HE%d/GI%s%4s%*cMCS%d/%1d%*c", 20 * (1 << bw),
				 he_gi[sgi], dcm ? "/DCM" : "", bw > 2 ? 4 : 5,
				 ' ', mcs, nss + 1, mcs > 9 ? 1 : 2, ' ');
	} else {
		if (r_idx) {
			*r_idx = N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU +
				 nss * 216 + mcs * 18 + bw * 3 + sgi;
			res = scnprintf(buf, size - res, "%3d ", *r_idx);
		}
		res += scnprintf(&buf[res], size - res,
				 "HEMU-%d/GI%s%*cMCS%d/%1d%*c", ru_size[bw],
				 he_gi[sgi], bw > 1 ? 1 : 2, ' ', mcs, nss + 1,
				 mcs > 9 ? 1 : 2, ' ');
	}

	return res;
}

int print_rate_from_cfg(char *buf, int size, u32 rate_config, int *r_idx,
			       int ru_size)
{
	union rwnx_rate_ctrl_info *r_cfg =
		(union rwnx_rate_ctrl_info *)&rate_config;
	union rwnx_mcs_index *mcs_index = (union rwnx_mcs_index *)&rate_config;
	unsigned int ft, pre, gi, bw, nss, mcs, dcm, len;

	ft = r_cfg->formatModTx;
	pre = r_cfg->giAndPreTypeTx >> 1;
	gi = r_cfg->giAndPreTypeTx;
	bw = r_cfg->bwTx;
	dcm = 0;
	if (ft == FORMATMOD_HE_MU) {
		mcs = mcs_index->he.mcs;
		nss = mcs_index->he.nss;
		bw = ru_size;
		dcm = r_cfg->dcmTx;
	} else if (ft == FORMATMOD_HE_SU) {
		mcs = mcs_index->he.mcs;
		nss = mcs_index->he.nss;
		dcm = r_cfg->dcmTx;
	} else if (ft == FORMATMOD_VHT) {
		mcs = mcs_index->vht.mcs;
		nss = mcs_index->vht.nss;
	} else if (ft >= FORMATMOD_HT_MF) {
		mcs = mcs_index->ht.mcs;
		nss = mcs_index->ht.nss;
	} else {
		mcs = mcs_index->legacy;
		nss = 0;
	}

	len = print_rate(buf, size, ft, nss, mcs, bw, gi, pre, dcm, r_idx);
	return len;
}

static void idx_to_rate_cfg(int idx, union rwnx_rate_ctrl_info *r_cfg,
			    int *ru_size)
{
	r_cfg->value = 0;
	if (idx < N_CCK) {
		r_cfg->formatModTx = FORMATMOD_NON_HT;
		r_cfg->giAndPreTypeTx = (idx & 1) << 1;
		r_cfg->mcsIndexTx = idx / 2;
	} else if (idx < (N_CCK + N_OFDM)) {
		r_cfg->formatModTx = FORMATMOD_NON_HT;
		r_cfg->mcsIndexTx = idx - N_CCK + 4;
	} else if (idx < (N_CCK + N_OFDM + N_HT)) {
		union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

		idx -= (N_CCK + N_OFDM);
		r_cfg->formatModTx = FORMATMOD_HT_MF;
		r->ht.nss = idx / (8 * 2 * 2);
		r->ht.mcs = (idx % (8 * 2 * 2)) / (2 * 2);
		r_cfg->bwTx = ((idx % (8 * 2 * 2)) % (2 * 2)) / 2;
		r_cfg->giAndPreTypeTx = idx & 1;
	} else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT)) {
		union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

		idx -= (N_CCK + N_OFDM + N_HT);
		r_cfg->formatModTx = FORMATMOD_VHT;
		r->vht.nss = idx / (10 * 4 * 2);
		r->vht.mcs = (idx % (10 * 4 * 2)) / (4 * 2);
		r_cfg->bwTx = ((idx % (10 * 4 * 2)) % (4 * 2)) / 2;
		r_cfg->giAndPreTypeTx = idx & 1;
	} else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU)) {
		union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

		idx -= (N_CCK + N_OFDM + N_HT + N_VHT);
		r_cfg->formatModTx = FORMATMOD_HE_SU;
		r->vht.nss = idx / (12 * 4 * 3);
		r->vht.mcs = (idx % (12 * 4 * 3)) / (4 * 3);
		r_cfg->bwTx = ((idx % (12 * 4 * 3)) % (4 * 3)) / 3;
		r_cfg->giAndPreTypeTx = idx % 3;
	} else {
		union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

		BUG_ON(ru_size == NULL);

		idx -= (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU);
		r_cfg->formatModTx = FORMATMOD_HE_MU;
		r->vht.nss = idx / (12 * 6 * 3);
		r->vht.mcs = (idx % (12 * 6 * 3)) / (6 * 3);
		*ru_size = ((idx % (12 * 6 * 3)) % (6 * 3)) / 3;
		r_cfg->giAndPreTypeTx = idx % 3;
		r_cfg->bwTx = 0;
	}
}

static struct rwnx_sta *rwnx_dbgfs_get_sta(struct rwnx_hw *rwnx_hw,
					   char *mac_addr)
{
	u8 mac[6];

	if (sscanf(mac_addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1],
		   &mac[2], &mac[3], &mac[4], &mac[5]) != 6)
		return NULL;
	return rwnx_get_sta(rwnx_hw, mac);
}

static ssize_t rwnx_dbgfs_twt_request_read(struct file *file,
					   char __user *user_buf, size_t count,
					   loff_t *ppos)
{
	char buf[750];
	ssize_t read;
	struct rwnx_hw *priv = file->private_data;
	struct rwnx_sta *sta = NULL;
	int len;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(
		priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;
	if (sta->twt_ind.sta_idx != RWNX_INVALID_STA) {
		struct twt_conf_tag *conf = &sta->twt_ind.conf;
		if (sta->twt_ind.resp_type == MAC_TWT_SETUP_ACCEPT)
			len = scnprintf(buf, sizeof(buf) - 1,
					"Accepted configuration");
		else if (sta->twt_ind.resp_type == MAC_TWT_SETUP_ALTERNATE)
			len = scnprintf(
				buf, sizeof(buf) - 1,
				"Alternate configuration proposed by AP");
		else if (sta->twt_ind.resp_type == MAC_TWT_SETUP_DICTATE)
			len = scnprintf(
				buf, sizeof(buf) - 1,
				"AP dictates the following configuration");
		else if (sta->twt_ind.resp_type == MAC_TWT_SETUP_REJECT)
			len = scnprintf(
				buf, sizeof(buf) - 1,
				"AP rejects the following configuration");
		else {
			len = scnprintf(buf, sizeof(buf) - 1,
					"Invalid response from the peer");
			goto end;
		}
		len += scnprintf(&buf[len], sizeof(buf) - 1 - len,
				 ":\n"
				 "flow_type = %d\n"
				 "wake interval mantissa = %d\n"
				 "wake interval exponent = %d\n"
				 "wake interval = %d us\n"
				 "nominal minimum wake duration = %d us\n",
				 conf->flow_type, conf->wake_int_mantissa,
				 conf->wake_int_exp,
				 conf->wake_int_mantissa << conf->wake_int_exp,
				 conf->wake_dur_unit ?
					 conf->min_twt_wake_dur * 1024 :
					 conf->min_twt_wake_dur * 256);
	} else {
		len = scnprintf(
			buf, min_t(size_t, sizeof(buf) - 1, count),
			"setup_command = <0: request, 1: suggest, 2: demand>,"
			"flow_type = <0: announced, 1: unannounced>,"
			"wake_interval_mantissa = <0 if setup request and no constraints>,"
			"wake_interval_exp = <0 if setup request and no constraints>,"
			"nominal_min_wake_dur = <0 if setup request and no constraints>,"
			"wake_dur_unit = <0: 256us, 1: tu>");
	}
end:
	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	return read;
}

static ssize_t rwnx_dbgfs_twt_request_write(struct file *file,
					    const char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	char *accepted_params[] = { "setup_command",
				    "flow_type",
				    "wake_interval_mantissa",
				    "wake_interval_exp",
				    "nominal_min_wake_dur",
				    "wake_dur_unit",
				    0 };
	struct twt_conf_tag twt_conf;
	struct twt_setup_cfm twt_setup_cfm;
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	char param[30];
	char *line, *buf;
	int error = 1, i, val, setup_command = -1;
	bool found;
	size_t bufsz = 1024;
	size_t len = bufsz - 1;
	ssize_t res;

	ENTER();

	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL) {
		res = -EINVAL;
		goto FUNC_EXIT;
	}
	memset(buf, 0, bufsz);

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(
		priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL) {
		res = -EINVAL;
		goto FUNC_EXIT;
	}

	/* Get the content of the file */
	if (copy_from_user(buf, user_buf, len)) {
		res = -EFAULT;
		goto FUNC_EXIT;
	}

	buf[len] = '\0';
	memset(&twt_conf, 0, sizeof(twt_conf));

	line = buf;
	/* Get the content of the file */
	while (line != NULL) {
		if (sscanf(line, "%s = %d", param, &val) == 2) {
			i = 0;
			found = false;
			// Check if parameter is valid
			while (accepted_params[i]) {
				if (strcmp(accepted_params[i], param) == 0) {
					found = true;
					break;
				}
				i++;
			}

			if (!found) {
				dev_err(priv->dev,
					"%s: parameter %s is not valid\n",
					__func__, param);
				res = -EINVAL;
				goto FUNC_EXIT;
			}

			if (!strcmp(param, "setup_command")) {
				setup_command = val;
			} else if (!strcmp(param, "flow_type")) {
				twt_conf.flow_type = val;
			} else if (!strcmp(param, "wake_interval_mantissa")) {
				twt_conf.wake_int_mantissa = val;
			} else if (!strcmp(param, "wake_interval_exp")) {
				twt_conf.wake_int_exp = val;
			} else if (!strcmp(param, "nominal_min_wake_dur")) {
				twt_conf.min_twt_wake_dur = val;
			} else if (!strcmp(param, "wake_dur_unit")) {
				twt_conf.wake_dur_unit = val;
			}
		} else {
			dev_err(priv->dev,
				"%s: Impossible to read TWT configuration option\n",
				__func__);
			res = -EFAULT;
			goto FUNC_EXIT;
		}
		line = strchr(line, ',');
		if (line == NULL)
			break;
		line++;
	}

	if (setup_command == -1) {
		dev_err(priv->dev, "%s: TWT missing setup command\n", __func__);
		res = -EFAULT;
		goto FUNC_EXIT;
	}

	// Forward the request to the LMAC
	if ((error = rwnx_send_twt_request(priv, setup_command, sta->vif_idx,
					   &twt_conf, &twt_setup_cfm)) != 0) {
		res = error;
		goto FUNC_EXIT;
	}

	// Check the status
	if (twt_setup_cfm.status != CO_OK) {
		res = -EIO;
		goto FUNC_EXIT;
	}

	res = count;
FUNC_EXIT:
	if (buf != NULL) {
		kfree(buf);
		buf = NULL;
	}

	return res;
}
DEBUGFS_READ_WRITE_FILE_OPS(twt_request);

static ssize_t rwnx_dbgfs_twt_teardown_read(struct file *file,
					    char __user *user_buf, size_t count,
					    loff_t *ppos)
{
	char buf[512];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
			"TWT teardown format:\n\n"
			"flow_id = <ID>\n");
	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_twt_teardown_write(struct file *file,
					     const char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct twt_teardown_req twt_teardown;
	struct twt_teardown_cfm twt_teardown_cfm;
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	char buf[256];
	char *line;
	int error = 1;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	ENTER();
	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(
		priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	/* Get the content of the file */
	if (copy_from_user(buf, user_buf, len))
		return -EINVAL;

	buf[len] = '\0';
	memset(&twt_teardown, 0, sizeof(twt_teardown));

	/* Get the content of the file */
	line = buf;

	if (sscanf(line, "flow_id = %d", (int *)&twt_teardown.id) != 1) {
		dev_err(priv->dev, "%s: Invalid TWT configuration\n", __func__);
		return -EINVAL;
	}

	twt_teardown.neg_type = 0;
	twt_teardown.all_twt = 0;
	twt_teardown.vif_idx = sta->vif_idx;

	// Forward the request to the LMAC
	if ((error = rwnx_send_twt_teardown(priv, &twt_teardown,
					    &twt_teardown_cfm)) != 0)
		return error;

	// Check the status
	if (twt_teardown_cfm.status != CO_OK)
		return -EIO;

	return count;
}
DEBUGFS_READ_WRITE_FILE_OPS(twt_teardown);

static ssize_t rwnx_dbgfs_rc_stats_read(struct file *file,
					char __user *user_buf, size_t count,
					loff_t *ppos)
{
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	char *buf;
	int bufsz, len = 0;
	ssize_t read;
	int i = 0;
	int error = 0;
	struct me_rc_stats_cfm me_rc_stats_cfm;
	unsigned int no_samples;
	struct st *st;

	ENTER();

	/* everything should fit in one call */
	if (*ppos)
		return 0;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(
		priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	/* Forward the information to the LMAC */
	if ((error = rwnx_send_me_rc_stats(priv, sta->sta_idx,
					   &me_rc_stats_cfm)))
		return error;

	no_samples = me_rc_stats_cfm.no_samples;
	if (no_samples == 0)
		return 0;

	bufsz = no_samples * LINE_MAX_SZ + 500;

	buf = kmalloc(bufsz + 1, GFP_ATOMIC);
	if (buf == NULL)
		return 0;

	st = kmalloc(sizeof(struct st) * no_samples, GFP_ATOMIC);
	if (st == NULL) {
		kfree(buf);
		return 0;
	}

	for (i = 0; i < no_samples; i++) {
		unsigned int tp, eprob;
		len = print_rate_from_cfg(
			st[i].line, LINE_MAX_SZ,
			me_rc_stats_cfm.rate_stats[i].rate_config, &st[i].r_idx,
			0);

		if (me_rc_stats_cfm.sw_retry_step != 0) {
			len += scnprintf(
				&st[i].line[len], LINE_MAX_SZ - len, "%c",
				me_rc_stats_cfm.retry_step_idx
							[me_rc_stats_cfm
								 .sw_retry_step] ==
						i ?
					'*' :
					' ');
		} else {
			len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len,
					 " ");
		}
		len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c",
				 me_rc_stats_cfm.retry_step_idx[0] == i ? 'T' :
									  ' ');
		len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c",
				 me_rc_stats_cfm.retry_step_idx[1] == i ? 't' :
									  ' ');
		len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c ",
				 me_rc_stats_cfm.retry_step_idx[2] == i ? 'P' :
									  ' ');

		tp = me_rc_stats_cfm.tp[i] / 10;
		len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len,
				 " %4u.%1u", tp / 10, tp % 10);

		eprob = ((me_rc_stats_cfm.rate_stats[i].probability * 1000) >>
			 16) +
			1;
		len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len,
				 "  %4u.%1u %5u(%6u)  %6u", eprob / 10,
				 eprob % 10,
				 me_rc_stats_cfm.rate_stats[i].success,
				 me_rc_stats_cfm.rate_stats[i].attempts,
				 me_rc_stats_cfm.rate_stats[i].sample_skipped);
	}
	len = scnprintf(buf, bufsz,
			"\nTX rate info for %02X:%02X:%02X:%02X:%02X:%02X:\n",
			sta->mac_addr[0], sta->mac_addr[1], sta->mac_addr[2],
			sta->mac_addr[3], sta->mac_addr[4], sta->mac_addr[5]);

	len += scnprintf(
		&buf[len], bufsz - len,
		" #  type               rate             tpt   eprob    ok(   tot)   skipped\n");

	// add sorted statistics to the buffer
	sort(st, no_samples, sizeof(st[0]), compare_idx, NULL);
	for (i = 0; i < no_samples; i++) {
		len += scnprintf(&buf[len], bufsz - len, "%s\n", st[i].line);
	}

	// display HE TB statistics if any
	if (me_rc_stats_cfm.rate_stats[RC_HE_STATS_IDX].rate_config != 0) {
		unsigned int tp, eprob;
		struct rc_rate_stats *rate_stats =
			&me_rc_stats_cfm.rate_stats[RC_HE_STATS_IDX];
		int ru_index = rate_stats->ru_and_length & 0x07;
		int ul_length = rate_stats->ru_and_length >> 3;

		len += scnprintf(&buf[len], bufsz - len,
				 "\nHE TB rate info:\n");

		len += scnprintf(
			&buf[len], bufsz - len,
			"    type               rate             tpt   eprob    ok(   tot)   ul_length\n    ");
		len += print_rate_from_cfg(&buf[len], bufsz - len,
					   rate_stats->rate_config, NULL,
					   ru_index);

		tp = me_rc_stats_cfm.tp[RC_HE_STATS_IDX] / 10;
		len += scnprintf(&buf[len], bufsz - len, "      %4u.%1u",
				 tp / 10, tp % 10);

		eprob = ((rate_stats->probability * 1000) >> 16) + 1;
		len += scnprintf(&buf[len], bufsz - len,
				 "  %4u.%1u %5u(%6u)  %6u\n", eprob / 10,
				 eprob % 10, rate_stats->success,
				 rate_stats->attempts, ul_length);
	}

	len += scnprintf(&buf[len], bufsz - len,
			 "\n MPDUs AMPDUs AvLen trialP");
	len += scnprintf(&buf[len], bufsz - len, "\n%6u %6u %3d.%1d %6u\n",
			 me_rc_stats_cfm.ampdu_len,
			 me_rc_stats_cfm.ampdu_packets,
			 me_rc_stats_cfm.avg_ampdu_len >> 16,
			 ((me_rc_stats_cfm.avg_ampdu_len * 10) >> 16) % 10,
			 me_rc_stats_cfm.sample_wait);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	kfree(st);

	return read;
}

DEBUGFS_READ_FILE_OPS(rc_stats);

static ssize_t rwnx_dbgfs_rc_fixed_rate_idx_write(struct file *file,
						  const char __user *user_buf,
						  size_t count, loff_t *ppos)
{
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	char buf[10];
	int fixed_rate_idx = -1;
	union rwnx_rate_ctrl_info rate_config;
	int error = 0;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	ENTER();

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(
		priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	/* Get the content of the file */
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';
	sscanf(buf, "%i\n", &fixed_rate_idx);

	/* Convert rate index into rate configuration */
	if ((fixed_rate_idx < 0) ||
	    (fixed_rate_idx >= (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU))) {
		// disable fixed rate
		rate_config.value = (u32)-1;
	} else {
		idx_to_rate_cfg(fixed_rate_idx, &rate_config, NULL);
	}

	// Forward the request to the LMAC
	if ((error = rwnx_send_me_rc_set_rate(priv, sta->sta_idx,
					      (u16)rate_config.value)) != 0) {
		return error;
	}

	priv->debugfs.rc_config[sta->sta_idx] = (int)rate_config.value;
	return len;
}

DEBUGFS_WRITE_FILE_OPS(rc_fixed_rate_idx);

static ssize_t rwnx_dbgfs_last_rx_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	struct rwnx_rx_rate_stats *rate_stats;
	char *buf;
	int bufsz, i, len = 0;
	ssize_t read;
	unsigned int fmt, pre, bw, nss, mcs, gi, dcm = 0;
	struct rx_vec_detail_1 *last_rx;
	char hist[] = "##################################################";
	int hist_len = sizeof(hist) - 1;
	u8 nrx;

	ENTER();

	/* everything should fit in one call */
	if (*ppos)
		return 0;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(
		priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	rate_stats = &sta->stats.rx_rate;
	bufsz = (rate_stats->rate_cnt * (50 + hist_len) + 200);
	buf = kmalloc(bufsz + 1, GFP_ATOMIC);
	if (buf == NULL)
		return 0;

	// Get number of RX paths
	nrx = (priv->version_cfm.version_phy_1 & MDM_NRX_MASK) >> MDM_NRX_LSB;

	len += scnprintf(buf, bufsz,
			 "\nRX rate info for %02X:%02X:%02X:%02X:%02X:%02X:\n",
			 sta->mac_addr[0], sta->mac_addr[1], sta->mac_addr[2],
			 sta->mac_addr[3], sta->mac_addr[4], sta->mac_addr[5]);

	// Display Statistics
	for (i = 0; i < rate_stats->size; i++) {
		if (rate_stats->table[i]) {
			union rwnx_rate_ctrl_info rate_config;
			int percent;
			int p;
			int ru_size;

			u64 tmp = rate_stats->table[i];
			tmp *= 1000;
			do_div(tmp, rate_stats->cpt);
			percent = (int)tmp;

			idx_to_rate_cfg(i, &rate_config, &ru_size);
			len += print_rate_from_cfg(&buf[len], bufsz - len,
						   rate_config.value, NULL,
						   ru_size);
			p = (percent * hist_len) / 1000;
			len += scnprintf(&buf[len], bufsz - len,
					 ": %9d(%2d.%1d%%)%.*s\n",
					 rate_stats->table[i], percent / 10,
					 percent % 10, p, hist);
		}
	}

	// Display detailed info of the last received rate
	last_rx = &sta->stats.last_rx.rx_vec_1;

	len += scnprintf(
		&buf[len], bufsz - len,
		"\nLast received rate\n"
		"  type         rate    LDPC STBC BEAMFM DCM DOPPLER %s\n",
		(nrx > 1) ? "rssi1(dBm) rssi2(dBm)" : "rssi(dBm)");

	fmt = last_rx->format_mod;
	bw = last_rx->ch_bw;
	pre = last_rx->pre_type;
	if (fmt >= FORMATMOD_HE_SU) {
		mcs = last_rx->he.mcs;
		nss = last_rx->he.nss;
		gi = last_rx->he.gi_type;
		if (fmt == FORMATMOD_HE_MU)
			bw = last_rx->he.ru_size;
		dcm = last_rx->he.dcm;
	} else if (fmt == FORMATMOD_VHT) {
		mcs = last_rx->vht.mcs;
		nss = last_rx->vht.nss;
		gi = last_rx->vht.short_gi;
	} else if (fmt >= FORMATMOD_HT_MF) {
		mcs = last_rx->ht.mcs % 8;
		nss = last_rx->ht.mcs / 8;
		;
		gi = last_rx->ht.short_gi;
	} else {
		BUG_ON((mcs = legrates_lut[last_rx->leg_rate].idx) == -1);
		nss = 0;
		gi = 0;
	}

	len += print_rate(&buf[len], bufsz - len, fmt, nss, mcs, bw, gi, pre,
			  dcm, NULL);

	/* flags for HT/VHT/HE */
	if (fmt >= FORMATMOD_HE_SU) {
		len += scnprintf(&buf[len], bufsz - len,
				 "  %c    %c     %c    %c     %c",
				 last_rx->he.fec ? 'L' : ' ',
				 last_rx->he.stbc ? 'S' : ' ',
				 last_rx->he.beamformed ? 'B' : ' ',
				 last_rx->he.dcm ? 'D' : ' ',
				 last_rx->he.doppler ? 'D' : ' ');
	} else if (fmt == FORMATMOD_VHT) {
		len += scnprintf(&buf[len], bufsz - len,
				 "  %c    %c     %c           ",
				 last_rx->vht.fec ? 'L' : ' ',
				 last_rx->vht.stbc ? 'S' : ' ',
				 last_rx->vht.beamformed ? 'B' : ' ');
	} else if (fmt >= FORMATMOD_HT_MF) {
		len += scnprintf(&buf[len], bufsz - len,
				 "  %c    %c                  ",
				 last_rx->ht.fec ? 'L' : ' ',
				 last_rx->ht.stbc ? 'S' : ' ');
	} else {
		len += scnprintf(&buf[len], bufsz - len,
				 "                         ");
	}
	if (nrx > 1) {
		len += scnprintf(&buf[len], bufsz - len,
				 "       %-4d       %d\n", last_rx->rssi_leg,
				 last_rx->rssi_leg);
	} else {
		len += scnprintf(&buf[len], bufsz - len, "      %d\n",
				 last_rx->rssi_leg);
	}

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	return read;
}

static ssize_t rwnx_dbgfs_last_rx_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(
		priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	/* Prevent from interrupt preemption as these statistics are updated under
     * interrupt */
	spin_lock_bh(&priv->tx_lock);
	memset(sta->stats.rx_rate.table, 0,
	       sta->stats.rx_rate.size * sizeof(sta->stats.rx_rate.table[0]));
	sta->stats.rx_rate.cpt = 0;
	sta->stats.rx_rate.rate_cnt = 0;
	spin_unlock_bh(&priv->tx_lock);

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(last_rx);

static ssize_t rwnx_dbgfs_ps_state_read(struct file *file,
					char __user *user_buf, size_t count,
					loff_t *ppos)
{
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	char *buf;
	int bufsz, len = 0;
	ssize_t read;

	/* everything should fit in one call */
	if (*ppos)
		return 0;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	/* Currently, we fixed the buffer size to 512 */
	bufsz = 512;
	buf = kmalloc(bufsz + 1, GFP_KERNEL);
	if (buf == NULL)
		return 0;

	len = scnprintf(
		buf, bufsz,
		"\nPower saving info for %02X:%02X:%02X:%02X:%02X:%02X:\n",
		sta->mac_addr[0], sta->mac_addr[1], sta->mac_addr[2],
		sta->mac_addr[3], sta->mac_addr[4], sta->mac_addr[5]);

	len += scnprintf(
		&buf[len], bufsz - len,
		"active | pkt_ready[LEGACY_PS] pkt_ready[UAPSD] sp_cnt[LEGACY_PS] sp_cnt[UAPSD]\n");

	len += scnprintf(
		&buf[len], bufsz - len,
		" %s                    %-4d             %-4d              %-4d         %-4d\n",
		(sta->ps.active == true) ? "true" : "false",
		sta->ps.pkt_ready[LEGACY_PS_ID], sta->ps.pkt_ready[UAPSD_ID],
		sta->ps.sp_cnt[LEGACY_PS_ID], sta->ps.sp_cnt[UAPSD_ID]);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return read;
}

DEBUGFS_READ_FILE_OPS(ps_state);

static ssize_t rwnx_dbgfs_sta_info_read(struct file *file,
					char __user *user_buf, size_t count,
					loff_t *ppos)
{
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	char *buf;
	int bufsz, len = 0;
	ssize_t read;
	struct rwnx_chanctx *ctxt;
	enum nl80211_band band;
	u16 center_freq;
	int sta_bw, local_sup_bw, peer_sup_bw = PHY_CHNL_BW_20;

	/* everything should fit in one call */
	if (*ppos)
		return 0;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	/* Currently, we fixed the buffer size to 512 */
	bufsz = 512;
	buf = kmalloc(bufsz + 1, GFP_KERNEL);
	if (buf == NULL)
		return 0;

	{
		BUG_ON(sta->ch_idx == RWNX_CH_NOT_SET);
		ctxt = &priv->chanctx_table[sta->ch_idx];
		band = ctxt->chan_def.chan->band;
		center_freq = ctxt->chan_def.chan->center_freq;
		local_sup_bw = bw2chnl[ctxt->chan_def.width];

		if (sta->vht_cap_info) {
			switch ((sta->vht_cap_info &
				 IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK) >>
				2) {
			case 0:
				peer_sup_bw = PHY_CHNL_BW_80;
				break;
			case 1:
				peer_sup_bw = PHY_CHNL_BW_160;
				break;
			case 2:
				peer_sup_bw = PHY_CHNL_BW_80P80;
				break;
			default:
				/*VHT40 or VHT20*/
				peer_sup_bw =
					(sta->ht_cap_info &
					 IEEE80211_HT_CAP_SUP_WIDTH_20_40) ?
						PHY_CHNL_BW_40 :
						PHY_CHNL_BW_20;
				break;
			}
		} else if (sta->ht_cap_info) {
			if (sta->ht_cap_info & IEEE80211_HT_CAP_SUP_WIDTH_20_40)
				peer_sup_bw = PHY_CHNL_BW_40;
		}
		//printk("%s(%d) ht_cap_info:0x%x, vht_cap_info:0x%x,band:%d,freq:%d (%d,%d)\n",
		//    __func__,__LINE__,sta->ht_cap_info,sta->vht_cap_info,band,center_freq,
		//    local_sup_bw,peer_sup_bw);
	}

	sta_bw = peer_sup_bw > local_sup_bw ? local_sup_bw : peer_sup_bw;

	len = scnprintf(buf, bufsz,
			"\nsta info for %02X:%02X:%02X:%02X:%02X:%02X\n",
			sta->mac_addr[0], sta->mac_addr[1], sta->mac_addr[2],
			sta->mac_addr[3], sta->mac_addr[4], sta->mac_addr[5]);

	len += scnprintf(
		&buf[len], bufsz - len,
		" AID        BAND        FREQ        IS_HT        IS_VHT       BW\n");

	len += scnprintf(
		&buf[len], bufsz - len,
		"%4d        %4s        %4d            %d             %d       %-6s\n",
		sta->aid,
		(band == NL80211_BAND_2GHZ) ?
			"2G" :
			(band == NL80211_BAND_5GHZ) ? "5G" : "unknow",
		center_freq, !!sta->ht_cap_info, !!sta->vht_cap_info,
		(sta_bw == PHY_CHNL_BW_20) ?
			"20M" :
			(sta_bw == PHY_CHNL_BW_40) ?
			"40M" :
			(sta_bw == PHY_CHNL_BW_80) ?
			"80M" :
			(sta_bw == PHY_CHNL_BW_160) ?
			"160M" :
			(sta_bw == PHY_CHNL_BW_80P80) ? "80P80" : "unknow");

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	buf = NULL;

	return read;
}

DEBUGFS_READ_FILE_OPS(sta_info);

/*
 * trace helper
 */
void rwnx_fw_trace_dump(struct rwnx_hw *rwnx_hw)
{
	/* may be called before rwnx_dbgfs_register */
#if 0
    /* as usb implementation, rwnx_hw->plat->enabled never be true */
    if (rwnx_hw->plat->enabled && !rwnx_hw->debugfs.fw_trace.buf.data) {
        rwnx_fw_trace_buf_init(&rwnx_hw->debugfs.fw_trace.buf,
                               rwnx_ipc_fw_trace_desc_get(rwnx_hw));
    }
#endif

	if (!rwnx_hw->debugfs.fw_trace.buf.data)
		return;

	_rwnx_fw_trace_dump(&rwnx_hw->debugfs.fw_trace.buf);
}

void rwnx_fw_trace_reset(struct rwnx_hw *rwnx_hw)
{
	_rwnx_fw_trace_reset(&rwnx_hw->debugfs.fw_trace, true);
}

void rwnx_dbgfs_trigger_fw_dump(struct rwnx_hw *rwnx_hw, char *reason)
{
	rwnx_send_dbg_trigger_req(rwnx_hw, reason);
}

static void _rwnx_dbgfs_register_sta(struct rwnx_debugfs *rwnx_debugfs,
				     struct rwnx_sta *sta)
{
	struct rwnx_hw *rwnx_hw =
		container_of(rwnx_debugfs, struct rwnx_hw, debugfs);
	struct dentry *dir_sta;
	char sta_name[18];
	struct dentry *dir_rc;
	struct dentry *file;
	struct rwnx_rx_rate_stats *rate_stats = &sta->stats.rx_rate;
	int nb_rx_rate = N_CCK + N_OFDM;
	struct rwnx_rc_config_save *rc_cfg, *next;

	if (sta->sta_idx >= NX_REMOTE_STA_MAX) {
		scnprintf(sta_name, sizeof(sta_name), "bc_mc");
	} else {
		scnprintf(sta_name, sizeof(sta_name), "%pM", sta->mac_addr);
	}

	if (!(dir_sta = debugfs_create_dir(sta_name, rwnx_debugfs->dir_stas)))
		goto error;
	rwnx_debugfs->dir_sta[sta->sta_idx] = dir_sta;

	if (!(dir_rc = debugfs_create_dir("rc",
					  rwnx_debugfs->dir_sta[sta->sta_idx])))
		goto error_after_dir;

	rwnx_debugfs->dir_rc_sta[sta->sta_idx] = dir_rc;

	file = debugfs_create_file("stats", S_IRUSR, dir_rc, rwnx_hw,
				   &rwnx_dbgfs_rc_stats_ops);
	if (IS_ERR_OR_NULL(file))
		goto error_after_dir;

	file = debugfs_create_file("fixed_rate_idx", S_IWUSR, dir_rc, rwnx_hw,
				   &rwnx_dbgfs_rc_fixed_rate_idx_ops);
	if (IS_ERR_OR_NULL(file))
		goto error_after_dir;

	file = debugfs_create_file("rx_rate", S_IRUSR | S_IWUSR, dir_rc,
				   rwnx_hw, &rwnx_dbgfs_last_rx_ops);
	if (IS_ERR_OR_NULL(file))
		goto error_after_dir;

	if (rwnx_hw->mod_params.ht_on)
		nb_rx_rate += N_HT;

	if (rwnx_hw->mod_params.vht_on)
		nb_rx_rate += N_VHT;

	if (rwnx_hw->mod_params.he_on)
		nb_rx_rate += N_HE_SU + N_HE_MU;

	rate_stats->table =
		kzalloc(nb_rx_rate * sizeof(rate_stats->table[0]), GFP_KERNEL);
	if (!rate_stats->table)
		goto error_after_dir;

	rate_stats->size = nb_rx_rate;
	rate_stats->cpt = 0;
	rate_stats->rate_cnt = 0;

	/* By default enable rate contoller */
	rwnx_debugfs->rc_config[sta->sta_idx] = -1;

	/* Unless we already fix the rate for this station */
	list_for_each_entry_safe (rc_cfg, next, &rwnx_debugfs->rc_config_save,
				  list) {
		if (jiffies_to_msecs(jiffies - rc_cfg->timestamp) >
		    RC_CONFIG_DUR) {
			list_del(&rc_cfg->list);
			kfree(rc_cfg);
		} else if (!memcmp(rc_cfg->mac_addr, sta->mac_addr, ETH_ALEN)) {
			rwnx_debugfs->rc_config[sta->sta_idx] = rc_cfg->rate;
			list_del(&rc_cfg->list);
			kfree(rc_cfg);
			break;
		}
	}

	if ((rwnx_debugfs->rc_config[sta->sta_idx] >= 0) &&
	    rwnx_send_me_rc_set_rate(rwnx_hw, sta->sta_idx,
				     (u16)rwnx_debugfs->rc_config[sta->sta_idx]))
		rwnx_debugfs->rc_config[sta->sta_idx] = -1;

	WQ_ASSERT((NULL != rwnx_hw->vif_table[sta->vif_idx] && sta->vif_idx < NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX),
		"ERR: vif %p, vif_idx %d(max %d), sta_idx %d.\n",
		rwnx_hw->vif_table[sta->vif_idx], sta->vif_idx, NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX, sta->sta_idx);

	if (RWNX_VIF_TYPE(rwnx_hw->vif_table[sta->vif_idx]) ==
	    NL80211_IFTYPE_STATION) {
		/* register the sta */
		struct dentry *dir_twt;
		struct dentry *file;

		if (!(dir_twt = debugfs_create_dir(
			      "twt", rwnx_debugfs->dir_sta[sta->sta_idx])))
			goto error_after_dir;

		rwnx_debugfs->dir_twt_sta[sta->sta_idx] = dir_twt;

		file = debugfs_create_file("request", S_IRUSR | S_IWUSR,
					   dir_twt, rwnx_hw,
					   &rwnx_dbgfs_twt_request_ops);
		if (IS_ERR_OR_NULL(file))
			goto error_after_dir;

		file = debugfs_create_file("teardown", S_IRUSR | S_IWUSR,
					   dir_twt, rwnx_hw,
					   &rwnx_dbgfs_twt_teardown_ops);
		if (IS_ERR_OR_NULL(file))
			goto error_after_dir;

		sta->twt_ind.sta_idx = RWNX_INVALID_STA;
	} else if (RWNX_VIF_TYPE(rwnx_hw->vif_table[sta->vif_idx]) ==
		   NL80211_IFTYPE_AP) {
		file = debugfs_create_file("ps_state", S_IRUSR,
					   rwnx_debugfs->dir_sta[sta->sta_idx],
					   rwnx_hw, &rwnx_dbgfs_ps_state_ops);

		if (IS_ERR_OR_NULL(file))
			goto error_after_dir;

		file = debugfs_create_file("sta_info", S_IRUSR,
					   rwnx_debugfs->dir_sta[sta->sta_idx],
					   rwnx_hw, &rwnx_dbgfs_sta_info_ops);

		if (IS_ERR_OR_NULL(file))
			goto error_after_dir;
	}
	return;

error_after_dir:
	debugfs_remove_recursive(rwnx_debugfs->dir_sta[sta->sta_idx]);
	rwnx_debugfs->dir_sta[sta->sta_idx] = NULL;
	rwnx_debugfs->dir_rc_sta[sta->sta_idx] = NULL;
	rwnx_debugfs->dir_twt_sta[sta->sta_idx] = NULL;
error:
	dev_err(rwnx_hw->dev,
		"Error while registering debug entry for sta %d\n",
		sta->sta_idx);
}

static void _rwnx_dbgfs_unregister_sta(struct rwnx_debugfs *rwnx_debugfs,
				       struct rwnx_sta *sta)
{
	debugfs_remove_recursive(rwnx_debugfs->dir_sta[sta->sta_idx]);
	/* unregister the sta */
	if (sta->stats.rx_rate.table) {
		kfree(sta->stats.rx_rate.table);
		sta->stats.rx_rate.table = NULL;
	}
	sta->stats.rx_rate.size = 0;
	sta->stats.rx_rate.cpt = 0;
	sta->stats.rx_rate.rate_cnt = 0;

	/* If fix rate was set for this station, save the configuration in case
       we reconnect to this station within RC_CONFIG_DUR msec */
	if (rwnx_debugfs->rc_config[sta->sta_idx] >= 0) {
		struct rwnx_rc_config_save *rc_cfg;
		rc_cfg = kmalloc(sizeof(*rc_cfg), GFP_KERNEL);
		if (rc_cfg) {
			rc_cfg->rate = rwnx_debugfs->rc_config[sta->sta_idx];
			rc_cfg->timestamp = jiffies;
			memcpy(rc_cfg->mac_addr, sta->mac_addr, ETH_ALEN);
			list_add_tail(&rc_cfg->list,
				      &rwnx_debugfs->rc_config_save);
		}
	}

	rwnx_debugfs->dir_sta[sta->sta_idx] = NULL;
	rwnx_debugfs->dir_rc_sta[sta->sta_idx] = NULL;
	rwnx_debugfs->dir_twt_sta[sta->sta_idx] = NULL;
	sta->twt_ind.sta_idx = RWNX_INVALID_STA;
}

static void rwnx_sta_work(struct work_struct *ws)
{
	struct rwnx_sta_ws *sta_ws = container_of(ws, struct rwnx_sta_ws, sta_work);
	struct rwnx_debugfs *rwnx_debugfs = sta_ws->debugfs;
	struct rwnx_hw *rwnx_hw =
		container_of(rwnx_debugfs, struct rwnx_hw, debugfs);
	struct rwnx_sta *sta;
	uint8_t sta_idx;

	sta_idx = sta_ws->sta_idx;

	if (sta_idx > (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)) {
		WARN(1, "Invalid sta index %d", sta_idx);
		kfree(sta_ws);
		return;
	}

	sta = &rwnx_hw->sta_table[sta_idx];
	if (!sta) {
		WARN(1, "Invalid sta %d", sta_idx);
		kfree(sta_ws);
		return;
	}

	if (rwnx_debugfs->dir_sta[sta_idx] == NULL)
		_rwnx_dbgfs_register_sta(rwnx_debugfs, sta);
	else
		_rwnx_dbgfs_unregister_sta(rwnx_debugfs, sta);

	kfree(sta_ws);
	return;
}

void _rwnx_dbgfs_sta_write(struct rwnx_debugfs *rwnx_debugfs, uint8_t sta_idx)
{
	struct rwnx_sta_ws *ws = NULL;

	if (rwnx_debugfs->unregistering)
		return;

	ws = kmalloc(sizeof(struct rwnx_sta_ws), GFP_ATOMIC);

	if (!ws) {
		WQ_DBG(DM_GENERIC, DL_WRN, "rwnx_dbgfs_sta_write ws alloc failed sta_idx:%d\n", sta_idx);
		return;
	}

	INIT_WORK(&ws->sta_work, rwnx_sta_work);
	ws->debugfs = rwnx_debugfs;
	ws->sta_idx = sta_idx;

	schedule_work(&ws->sta_work);
}

void rwnx_dbgfs_unregister_sta(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	WQ_DBG(DM_GENERIC, DL_WRN, "rwnx_dbgfs_unregister_sta %d, vif %d.\n", sta->sta_idx, sta->vif_idx);
	_rwnx_dbgfs_sta_write(&rwnx_hw->debugfs, sta->sta_idx);
}

void rwnx_dbgfs_register_sta(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	WQ_DBG(DM_GENERIC, DL_WRN, "rwnx_dbgfs_register_sta %d, vif %d.\n", sta->sta_idx, sta->vif_idx);
	_rwnx_dbgfs_sta_write(&rwnx_hw->debugfs, sta->sta_idx);
}

int rwnx_debugfs_init(struct rwnx_debugfs *rwnx_debugfs,u16 num)
{
    BUG_ON(rwnx_debugfs == NULL);
    rwnx_debugfs->dir_sta = (struct dentry **)kzalloc(sizeof(struct dentry *) * num, GFP_KERNEL);
    if (rwnx_debugfs->dir_sta == NULL)
        goto err_dir_sta;
    rwnx_debugfs->dir_rc_sta = (struct dentry **)kzalloc(sizeof(struct dentry *) * num, GFP_KERNEL);
    if (rwnx_debugfs->dir_rc_sta == NULL)
        goto err_dir_rc_sta;
    rwnx_debugfs->rc_config = (int *)kzalloc(sizeof(int) * num, GFP_KERNEL);
    if (rwnx_debugfs->rc_config == NULL)
        goto err_rc_config;
    rwnx_debugfs->dir_twt_sta = (struct dentry **)kzalloc(sizeof(struct dentry *) * num, GFP_KERNEL);
    if (rwnx_debugfs->dir_twt_sta == NULL)
        goto err_dir_twt_sta;
    return 0;

err_dir_twt_sta:
    if (rwnx_debugfs->rc_config)
        kfree(rwnx_debugfs->rc_config);
err_rc_config:
    if (rwnx_debugfs->dir_rc_sta)
        kfree(rwnx_debugfs->dir_rc_sta);
err_dir_rc_sta:
    if (rwnx_debugfs->dir_sta)
        kfree(rwnx_debugfs->dir_sta);
err_dir_sta:
    return -ENOMEM;
}

void rwnx_debugfs_deinit(struct rwnx_debugfs *rwnx_debugfs)
{
    BUG_ON(rwnx_debugfs == NULL);
    if (rwnx_debugfs->rc_config) {
        kfree(rwnx_debugfs->rc_config);
        rwnx_debugfs->rc_config = NULL;
    }
    if (rwnx_debugfs->dir_rc_sta) {
        kfree(rwnx_debugfs->dir_rc_sta);
        rwnx_debugfs->dir_rc_sta = NULL;
    }
    if (rwnx_debugfs->dir_sta) {
        kfree(rwnx_debugfs->dir_sta);
        rwnx_debugfs->dir_sta = NULL;
    }
    if (rwnx_debugfs->dir_twt_sta) {
        kfree(rwnx_debugfs->dir_twt_sta);
        rwnx_debugfs->dir_twt_sta = NULL;
    }
}

int rwnx_dbgfs_register(struct rwnx_hw *rwnx_hw, const char *name)
{
	struct dentry *phyd = rwnx_hw->wiphy->debugfsdir;
	struct rwnx_debugfs *rwnx_debugfs = &rwnx_hw->debugfs;
	struct dentry *dir_drv, *dir_diags, *dir_stas;

	if (!(dir_drv = debugfs_create_dir(name, phyd)))
		return -ENOMEM;
	rwnx_debugfs_init(rwnx_debugfs,NX_REMOTE_STA_MAX);

	rwnx_debugfs->dir = dir_drv;

	if (!(dir_stas = debugfs_create_dir("stations", dir_drv)))
		return -ENOMEM;

	rwnx_debugfs->dir_stas = dir_stas;
	rwnx_debugfs->unregistering = false;

	if (!(dir_diags = debugfs_create_dir("diags", dir_drv)))
		goto err;

	INIT_WORK(&rwnx_debugfs->sta_work, rwnx_sta_work);
	INIT_LIST_HEAD(&rwnx_debugfs->rc_config_save);
	rwnx_debugfs->sta_idx = RWNX_INVALID_STA;

	DEBUGFS_ADD_U32(tcp_pacing_shift, dir_drv, &rwnx_hw->tcp_pacing_shift,
			S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(stats, dir_drv, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(sys_stats, dir_drv, S_IRUSR);
	DEBUGFS_ADD_FILE(txq, dir_drv, S_IRUSR);
	DEBUGFS_ADD_FILE(acsinfo, dir_drv, S_IRUSR);
	DEBUGFS_ADD_FILE(tx_credit, dir_drv, S_IRUSR);
	DEBUGFS_ADD_FILE(tx_statics, dir_drv, S_IRUSR | S_IWUSR);
	DEBUGFS_ADD_FILE(tx_stats_dump, dir_drv, S_IRUSR);
	DEBUGFS_ADD_FILE(hml_dfx_edca, dir_drv, S_IRUSR);
	DEBUGFS_ADD_FILE(reg_addr, dir_drv, S_IRUSR | S_IWUSR);
	DEBUGFS_ADD_FILE(reg_dump, dir_drv, S_IRUSR);
	DEBUGFS_ADD_FILE(reg_write, dir_drv, S_IRUSR | S_IWUSR);
#ifdef CONFIG_RWNX_MUMIMO_TX
	DEBUGFS_ADD_FILE(mu_group, dir_drv, S_IRUSR);
#endif

	{
		/* Create a hist directory */
		struct dentry *dir_hist;
		if (!(dir_hist = debugfs_create_dir("hist", dir_drv)))
			goto err;

		if (rwnx_hw->dev) {
			/* FIXME: dev_set_drvdata(rwnx_hw->dev, rwnx_hw); */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
			debugfs_create_devm_seqfile(rwnx_hw->dev, "mgmt_hist",
						    dir_hist,
						    read_file_mgmt_hist);
			debugfs_create_devm_seqfile(rwnx_hw->dev, "dump_hist",
						    dir_hist,
						    read_file_dump_hist);
#endif
		}
	}

#ifdef CONFIG_RWNX_P2P_DEBUGFS
	{
		/* Create a p2p directory */
		struct dentry *dir_p2p;
		if (!(dir_p2p = debugfs_create_dir("p2p", dir_drv)))
			goto err;

		/* Add file allowing to control Opportunistic PS */
		DEBUGFS_ADD_FILE(oppps, dir_p2p, S_IRUSR);
		/* Add file allowing to control Notice of Absence */
		DEBUGFS_ADD_FILE(noa, dir_p2p, S_IRUSR);
	}
#endif /* CONFIG_RWNX_P2P_DEBUGFS */

	if (rwnx_dbgfs_register_fw_dump(rwnx_hw, dir_drv, dir_diags))
		goto err;
	DEBUGFS_ADD_FILE(fw_dbg, dir_diags, S_IWUSR | S_IRUSR);

	if (!rwnx_fw_trace_init(&rwnx_hw->debugfs.fw_trace,
				rwnx_ipc_fw_trace_desc_get(rwnx_hw))) {
		DEBUGFS_ADD_FILE(fw_trace, dir_diags, S_IWUSR | S_IRUSR);
		if (rwnx_hw->debugfs.fw_trace.buf.nb_compo)
			DEBUGFS_ADD_FILE(fw_trace_level, dir_diags,
					 S_IWUSR | S_IRUSR);
	} else {
		rwnx_debugfs->fw_trace.buf.data = NULL;
	}

#ifdef CONFIG_RWNX_RADAR
	{
		struct dentry *dir_radar, *dir_sec;
		if (!(dir_radar = debugfs_create_dir("radar", dir_drv)))
			goto err;

		DEBUGFS_ADD_FILE(pulses_prim, dir_radar, S_IRUSR);
		DEBUGFS_ADD_FILE(detected, dir_radar, S_IRUSR);
		DEBUGFS_ADD_FILE(enable, dir_radar, S_IRUSR);

		if (rwnx_hw->phy.cnt == 2) {
			DEBUGFS_ADD_FILE(pulses_sec, dir_radar, S_IRUSR);

			if (!(dir_sec = debugfs_create_dir("sec", dir_radar)))
				goto err;

			DEBUGFS_ADD_FILE(band, dir_sec, S_IWUSR | S_IRUSR);
			DEBUGFS_ADD_FILE(type, dir_sec, S_IWUSR | S_IRUSR);
			DEBUGFS_ADD_FILE(prim20, dir_sec, S_IWUSR | S_IRUSR);
			DEBUGFS_ADD_FILE(center1, dir_sec, S_IWUSR | S_IRUSR);
			DEBUGFS_ADD_FILE(center2, dir_sec, S_IWUSR | S_IRUSR);
			DEBUGFS_ADD_FILE(set, dir_sec, S_IWUSR | S_IRUSR);
		}
	}
#endif /* CONFIG_RWNX_RADAR */
	return 0;

err:
	rwnx_dbgfs_unregister(rwnx_hw);
	return -ENOMEM;
}

void rwnx_dbgfs_unregister(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_debugfs *rwnx_debugfs = &rwnx_hw->debugfs;

	struct rwnx_rc_config_save *cfg, *next;
	list_for_each_entry_safe (cfg, next, &rwnx_debugfs->rc_config_save,
				  list) {
		list_del(&cfg->list);
		kfree(cfg);
	}

	if (!rwnx_hw->debugfs.dir)
		return;

	spin_lock_bh(&rwnx_debugfs->umh_lock);
	rwnx_debugfs->unregistering = true;
	spin_unlock_bh(&rwnx_debugfs->umh_lock);
	rwnx_wait_um_helper(rwnx_hw);
	rwnx_fw_trace_deinit(&rwnx_hw->debugfs.fw_trace);
	flush_work(&rwnx_debugfs->sta_work);
	flush_work(&rwnx_debugfs->helper_work);
	rwnx_debugfs_deinit(rwnx_debugfs);
	debugfs_remove_recursive(rwnx_hw->debugfs.dir);
	rwnx_hw->debugfs.dir = NULL;
}

void rwnx_dbgfs_flush_sta_work(struct rwnx_hw *rwnx_hw)
{
	bool ret;

	struct rwnx_debugfs *rwnx_debugfs = &rwnx_hw->debugfs;

	ret = flush_work(&rwnx_debugfs->sta_work);

	WQ_DBG(DM_GENERIC, DL_WRN, "rwnx_dbgfs_flush_sta_work ret %d.\n", ret);
}
