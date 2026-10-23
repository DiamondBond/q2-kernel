/****************************
 * Include
 ****************************/
#include <net/cfg80211.h>
#include "wq_wifi_dbg.h"
#include "rwnx_defs.h"
#include "rwnx_events.h"
#include "hal_desc.h"
#include "wq_log.h"

#define IEEE80211_QOS_CTL_LEN 2
#define IEEE80211_WEP_IVLEN 3 /* 24bit */
#define IEEE80211_WEP_KIDLEN 1 /* 1 octet */
#define IEEE80211_WEP_EXTIV 0x20
#define IEEE80211_WEP_EXTIVLEN 4 /* extended IV length */
#define IEEE80211_HTC_LEN 4

WIFI_MGMT_PKT mgmt_hist[HIST_CNT];
WIFI_MGMT_PKT pktdump_hist[HIST_CNT];
struct wq_dbg_dfx_pkt_info dfx_pkt_info;
uint8_t mgmt_idx = 0;
uint8_t dump_idx = 0;
uint8_t gv_pktdump_sn = 0xff;

const char p2p_action_to_str[9][20] = {
	"P2P_GO_NEG_REQ", /* GO Negociation Request */
	"P2P_GO_NEG_RSP", /* GO Negociation Response */
	"P2P_GO_NEG_CFM", /* GO Negociation Confirmation */
	"P2P_INVIT_REQ", /* P2P Invitation Request */
	"P2P_INVIT_RSP", /* P2P Invitation Response */
	"P2P_DEV_DISC_REQ", /* Device Discoverability Request */
	"P2P_DEV_DISC_RSP", /* Device Discoverability Response */
	"P2P_PROV_DISC_REQ", /* Provision Discovery Request */
	"P2P_PROV_DISC_RSP", /* Provision Discovery Response */
};

const char mgmt_subtype_to_str[16][10] = {
	"ASOCREQ", //00 0000
	"ASOCRSP", //00 0001
	"REASOCREQ", //00 0010
	"REASOCRSP", //00 0011
	"PROBEREQ", //00 0100
	"PROBERSP", //00 0101
	"TA", //00 0110
	"RSVD", //00 0111
	"BEACON", //00 1000
	"ATIM", //00 1001
	"DISASSOC", //00 1010
	"AUTH", //00 1011
	"DEAUTH", //00 1100
	"ACTION", //00 1101
	"ACTIONNA", //00 1110
	"RSVD", //00 1111
};

const char *frame_type_to_str(uint8_t *frame_ctrl, int is_ampdu)
{
	int amsdu_bit;
	int qos_ctrl_offset;

	if ((frame_ctrl[0] & 0xc) == 0) {
		if (((frame_ctrl[0] >> 4) == 0xD) && (frame_ctrl[24] == 0x4) &&
		    (frame_ctrl[29] == 0x9)) //ACTION P2P
		{
			if (frame_ctrl[30] < 9)
				return &p2p_action_to_str[frame_ctrl[30]][0];
			else
				return "P2P_ACTION_UNKNOWN";

		} else
			return &mgmt_subtype_to_str[(frame_ctrl[0] >> 4)][0];
	} else if ((frame_ctrl[0] & 0xc) == 0x4) {
		if ((frame_ctrl[0] & 0xf0) == 0x80)
			return "BAR";
		else if ((frame_ctrl[0] & 0xf0) == 0x90)
			return "BA";
		else if ((frame_ctrl[0] & 0xf0) == 0xa0)
			return "PSPOL";
		else if ((frame_ctrl[0] & 0xf0) == 0x20)
			return "TRIG";
		else if ((frame_ctrl[0] & 0xf0) == 0x40)
			return "BRP";
		else if ((frame_ctrl[0] & 0xf0) == 0x50)
			return "NDPA";
		else if ((frame_ctrl[0] & 0xf0) == 0x60)
			return "C-EXT";
		else
			return "CTRL";
	} else if ((frame_ctrl[0] & 0xc) == 0x8) {
		if ((frame_ctrl[0] & 0xf0) == 0x80) //QoS frame
		{
			qos_ctrl_offset = 24;
			if ((frame_ctrl[1] & 0x3) ==
			    0x3) //toDS and frDS both 1 : 4-address frame
				qos_ctrl_offset = 30;

			amsdu_bit = frame_ctrl[qos_ctrl_offset] >>
				    7; //qos-ctrl bit7 : A-MSDU bit

			if (amsdu_bit)
				return "AMSDU";
			else if (is_ampdu)
				return "AMPDU";
			else
				return "QDATA";
		} else if ((frame_ctrl[0] & 0xf0) == 0xc0) //QoS NULL frame
			return "QNULL";
		else if ((frame_ctrl[0] & 0xf0) == 0x00) //data frame
			return "DATA";
		else if ((frame_ctrl[0] & 0xf0) == 0x40) //NULL frame
			return "NULL";
		else
			return "D-TBD";
	}
	//else if ((frame_ctrl & 0xc) == 0x8)
	return "RESERVED";
}

void wq_packet_dump_evt_handler(WIFI_DBG_PKTDUMP *dpkt)
{
	WIFI_DBG_PKTDUMP *pkt = dpkt;
	struct ieee80211_hdr_3addr *hdr;
	struct ieee80211_mgmt *mgmt;
	unsigned int ether_offset = sizeof(struct ieee80211_hdr_3addr);
	unsigned char eth_str[12] = { 0 };
	struct rx_vec_detail_1 rx_vec_1;
	struct rx_vec_detail_1 *rxvect = NULL;
	struct rx_leg_vect rx_leg_vec;
	struct rx_ht_vect rx_ht_vec;
	struct rx_he_vect *rx_he_mu_vec = NULL;
	union rwnx_rate_ctrl_info ratecntrl;
	union rwnx_hw_txstatus_um txstatus;

	uint8_t bw = 0, rate = 0, txstatusbw = 0;
	char *str_format = NULL, *str_l_rssi = NULL, *str_pregitype = NULL;
	uint8_t l_length_l = 0, l_length_h = 0;
	uint8_t mcs = 0, nss = 0, sgi = 0;

	uint8_t non_ht_rate_tx[] = { 2,	 4,  11, 22, 12,  18, 24,
				     36, 48, 72, 96, 108, 0 };
	uint8_t non_ht_rate_rx[] = { 2,	 4,  11, 22, 0,	  0,  0,  0,
				     96, 48, 24, 12, 108, 72, 36, 18 };
	s32 rxrssi = 0;
	s32 snr = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
	struct timeval now = { .tv_sec = 0, .tv_usec = 0 };
#else
	struct timespec64 now = { .tv_sec = 0, .tv_nsec = 0 };
#endif

	hdr = (struct ieee80211_hdr_3addr *)pkt->pkt_data;
	//dump_bytes("pkt_dump", pkt->pkt_data, PKT_COPY_LEN);

	if (ieee80211_is_data(hdr->frame_control) &&
	    (pkt->len > ether_offset)) {
		if (ieee80211_is_data_qos(hdr->frame_control))
			ether_offset += IEEE80211_QOS_CTL_LEN;
		if (ieee80211_has_order(hdr->frame_control))
			ether_offset += IEEE80211_HTC_LEN;
		if (ieee80211_has_protected(hdr->frame_control)) {
			ether_offset +=
				IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN;
			if (pkt->pkt_data[ether_offset - 1] &
			    IEEE80211_WEP_EXTIV)
				ether_offset += IEEE80211_WEP_EXTIVLEN;
		}
		ether_offset += 6; /* Ignore SNAP/LLC header */
		if (pkt->len >= ether_offset)
			snprintf(eth_str, 12, "eth: %02x%02x",
				 pkt->pkt_data[ether_offset],
				 pkt->pkt_data[ether_offset + 1]);
	}
	//rx vec1 status show
	if (pkt->dir == WIFI_DBG_PKT_RX) {
		rxvect = (struct rx_vec_detail_1 *)&(pkt->rx_vec[0]);
		rxrssi = rxvect->rssi_leg;
		snr = rxvect->snr;

		//format
		rx_vec_1.format_mod =
			(pkt->rx_vec[0] & RX_VEC_COMMON_FORMAT_MASK) >>
			RX_VEC_COMMON_FORMAT_OFFSET;
		switch (rx_vec_1.format_mod) //todo VHT HE
		{
		case FORMATMOD_NON_HT:
			str_format = "";
			break;
		case FORMATMOD_NON_HT_DUP_OFDM:
			str_format = "HT DUP";
			break;
		case FORMATMOD_HT_MF:
			str_format = "HT MCS";
			break;
		case FORMATMOD_HT_GF:
			str_format = "GF MCS";
			break;
		case FORMATMOD_VHT:
			str_format = "VHT MCS";
			break;
		case FORMATMOD_HE_SU:
			str_format = "HE-SU MCS";
			break;
		case FORMATMOD_HE_MU:
			str_format = "HE-MU MCS";
			break;
		case FORMATMOD_HE_ER:
			str_format = "HE-ER MCS";
			break;
		case FORMATMOD_HE_TB:
			str_format = "HE-TB MCS";
			break;
		default:
			str_format = "TBD";
			WQ_DBG(DM_PKTDUMP, DL_WRN,
			       "unknown rx_vec format_mod: %u",
			       rx_vec_1.format_mod);
			break;
		}
		//bandwidth
		rx_vec_1.ch_bw =
			(pkt->rx_vec[0] & RX_VEC_COMMON_CH_BANDWIDTH_MASK) >>
			RX_VEC_COMMON_CH_BANDWIDTH_OFFSET;
		switch (rx_vec_1.ch_bw) {
		case BW_20MHZ:
			bw = 20;
			break;
		case BW_40MHZ:
			bw = 40;
			break;
		case BW_80MHZ:
			bw = 80;
			break;
		case BW_160MHZ:
			bw = 160;
			break;
		default:
			WQ_DBG(DM_PKTDUMP, DL_WRN, "cannot get bandwidth");
			break;
		}
		//preamble type
		rx_vec_1.pre_type =
			(pkt->rx_vec[0] & RX_VEC_COMMON_PREAMBLE_TYPE_MASK) >>
			RX_VEC_COMMON_PREAMBLE_TYPE_OFFSET;
		//antenna_set
		rx_vec_1.antenna_set =
			(pkt->rx_vec[0] & RX_VEC_COMMON_ANTENNA_SET_MASK) >>
			RX_VEC_COMMON_ANTENNA_SET_OFFSET;
		//rssi_legacy
		rx_vec_1.rssi_leg =
			(pkt->rx_vec[0] & RX_VEC_COMMON_RSSI_LEGACY_MASK) >>
			RX_VEC_COMMON_RSSI_LEGACY_OFFSET;
		switch ((u32)rx_vec_1.rssi_leg) {
		case RSSI_LEGACY_128:
			str_l_rssi = "-128dBm";
			break;
		case RSSI_LEGACY_1:
			str_l_rssi = "-1dBm";
			break;
		case RSSI_LEGACY_0:
			str_l_rssi = "0dBm";
			break;
		default:
			str_l_rssi = "unstable";
			break;
		}
		//legacy length
		l_length_l = (pkt->rx_vec[0] &
			      RX_VEC_COMMON_LEGACY_LENGTH_LOW_MASK) >>
			     RX_VEC_COMMON_LEGACY_LENGTH_LOW_OFFSET;
		l_length_h = (pkt->rx_vec[1] &
			      RX_VEC_COMMON_LEGACY_LENGTH_HIGH_MASK) >>
			     RX_VEC_COMMON_LEGACY_LENGTH_HIGH_OFFSET;
		rx_vec_1.leg_length = (l_length_h << 4) | l_length_l;
		//lagacy rate always 6Mbps(0) when HT,VHT,HE
		rx_vec_1.leg_rate = 0;
		//rssi
		rx_vec_1.rssi_leg =
			(pkt->rx_vec[1] & RX_VEC_COMMON_RSSI_MASK) >>
			RX_VEC_COMMON_RSSI_OFFSET;
		//print common stat
		/*
        printk("%03d Rx || format:%s, bw=%d, preamble type=%s, antenna=%02x,"
        "legacy_rssi=%s,l_length=%d ,rssi=%d ",
        pkt->sn,str_format,bw,rx_vec_1.pre_type==1?"long":"short",rx_vec_1.antenna_set,
        str_l_rssi,rx_vec_1.leg_length,rx_vec_1.rssi1);
        */

		rx_leg_vec = rx_vec_1.leg;
		rx_ht_vec = rx_vec_1.ht;

		if (rx_vec_1.format_mod == FORMATMOD_NON_HT) {
			//DYN_BANDWIDTH_IN_NON_HT : 0 static , 1 dynamic
			rx_leg_vec.dyn_bw_in_non_ht =
				(pkt->rx_vec[1] &
				 RX_VEC_NON_HT_DYN_BANDWIDTH_MASK) >>
				RX_VEC_NON_HT_DYN_BANDWIDTH_OFFSET;
			//CHANNEL_BANDWIDTH_IN_NON_HT : 0 20MHz , 1 40MHz , 2 80MHz , 3 160MHz/80+80MHz
			rx_leg_vec.chn_bw_in_non_ht =
				(pkt->rx_vec[1] &
				 RX_VEC_NON_HT_CH_BANDWIDTH_MASK) >>
				RX_VEC_NON_HT_CH_BANDWIDTH_OFFSET;
			switch (rx_leg_vec.chn_bw_in_non_ht) {
			case BW_20MHZ:
				bw = 20;
				break;
			case BW_40MHZ:
				bw = 40;
				break;
			case BW_80MHZ:
				bw = 80;
				break;
			case BW_160MHZ:
				bw = 160;
				break;
			default:
				WQ_DBG(DM_PKTDUMP, DL_WRN,
				       "cannot get bandwidth in non-ht");
				break;
			}
			//lagacy rate
			rx_vec_1.leg_rate = (pkt->rx_vec[1] &
					     RX_VEC_COMMON_LEGACY_RATE_MASK) >>
					    RX_VEC_COMMON_LEGACY_RATE_OFFSET;
			pkt->rate = rx_vec_1.leg_rate;
			rate = (non_ht_rate_rx[pkt->rate]) / 2;
			//L_SIG_VALID
			rx_leg_vec.lsig_valid =
				(pkt->rx_vec[1] &
				 RX_VEC_NON_HT_L_SIG_VALID_MASK) >>
				RX_VEC_NON_HT_l_SIG_VALID_OFFSET;
			//print non-ht stat
			/*
            printk("%03d Rx NON-HT|| dyn_bandwidth=%s, ch_bandwidth=%dMHz, leagcy_rate=%dMbps, l_sig_valid=%d",
            pkt->sn,rx_leg_vec.dyn_bw_in_non_ht==0?"static":"dynamic",bw,rate,rx_leg_vec.lsig_valid);
            */
		} else if (rx_vec_1.format_mod == FORMATMOD_HT_MF) {
			//SOUNDING : 0 not_sounding , 1 sounding
			rx_ht_vec.sounding =
				(pkt->rx_vec[1] & RX_VEC_HT_MF_SOUNDING_MASK) >>
				RX_VEC_HT_MF_SOUNDING_OFFSET;
			//SMOOTHING : 0 recommended , 1 not recommended
			rx_ht_vec.smoothing = (pkt->rx_vec[1] &
					       RX_VEC_HT_MF_SMOOTHING_MASK) >>
					      RX_VEC_HT_MF_SMOOTHING_OFFSET;
			//GI_TYPE : 0 LONG GI , 1 SHORT GI
			rx_ht_vec.short_gi =
				(pkt->rx_vec[1] & RX_VEC_HT_MF_GI_TYPE_MASK) >>
				RX_VEC_HT_MF_GI_TYPE_OFFSET;
			//AGGREGATION : 0 PPDU is not an AMPDU , 1 PPDU is an AMPDU
			rx_ht_vec.aggregation =
				(pkt->rx_vec[1] &
				 RX_VEC_HT_MF_AGGREGATION_MASK) >>
				RX_VEC_HT_MF_AGGREGATION_OFFSET;
			//STBC
			rx_ht_vec.stbc =
				(pkt->rx_vec[1] & RX_VEC_HT_MF_STBC_MASK) >>
				RX_VEC_HT_MF_STBC_OFFSET;
			//NUM_EXT_SS
			rx_ht_vec.num_extn_ss =
				(pkt->rx_vec[1] &
				 RX_VEC_HT_MF_NUM_EXT_SS_MASK) >>
				RX_VEC_HT_MF_NUM_EXT_SS_OFFSET;
			//L_SIG_VALID
			rx_ht_vec.lsig_valid =
				(pkt->rx_vec[1] &
				 RX_VEC_HT_MF_L_SIG_VALID_MASK) >>
				RX_VEC_HT_MF_l_SIG_VALID_OFFSET;
			//MCS
			rx_ht_vec.mcs =
				(pkt->rx_vec[1] & RX_VEC_HT_MF_MCS_MASK) >>
				RX_VEC_HT_MF_MCS_OFFSET;
			mcs = rx_ht_vec.mcs;
			//FEC : 0 use BCC , 1 use LDPC
			rx_ht_vec.fec =
				(pkt->rx_vec[1] & RX_VEC_HT_MF_FEC_MASK) >>
				RX_VEC_HT_MF_FEC_OFFSET;
			//HT_LENGTH
			rx_ht_vec.length =
				(pkt->rx_vec[2] & RX_VEC_HT_MF_LENGTH_MASK) >>
				RX_VEC_HT_MF_LENGTH_OFFSET;
			//print ht stat
			rate = 0; //rate always 0 in HT,VHT,HE
			/*
            printk("%03d Rx HT_MF|| %s,smoothing recommended = %s,gi_type = %s,"
            "aggregation = %s,ht_stbc = %02x,num_ext_ss = %02x,"
            "l_sig_valid = %x,mcs = %d,fec = %s,ht_length = %d",
            pkt->sn,rx_ht_vec.sounding==0?"not sounding":"sounding",rx_ht_vec.smoothing==0?"yes":"no",rx_ht_vec.short_gi==0?"long":"short",
            rx_ht_vec.aggregation==0?"not ampdu":"ampdu",rx_ht_vec.stbc,rx_ht_vec.num_extn_ss,
            rx_ht_vec.lsig_valid,rx_ht_vec.mcs,rx_ht_vec.fec==0?"BCC":"LDPC",rx_ht_vec.length);
            */
		} else if (rx_vec_1.format_mod == FORMATMOD_VHT) {
			mcs = rxvect->vht.mcs;
			nss = rxvect->vht.nss;
			sgi = rxvect->vht.short_gi;
		} else if (rx_vec_1.format_mod == FORMATMOD_HE_SU) {
			mcs = rxvect->he.mcs;
			nss = rxvect->he.nss;
			sgi = rxvect->he.gi_type;
		} else if (rx_vec_1.format_mod == FORMATMOD_HE_MU) {
			rx_he_mu_vec = (struct rx_he_vect *)&rxvect;
			mcs = rx_he_mu_vec->mcs;
			nss = rx_he_mu_vec->nss;
			sgi = rx_he_mu_vec->gi_type;
			rate = 0; //rate always 0 in HT,VHT,HE
		}

		dfx_pkt_info.snr = snr;
		dfx_pkt_info.rssi = rxrssi;
		dfx_pkt_info.mcs_rx[pkt->mac_id] = mcs;
		dfx_pkt_info.bw_rx[pkt->mac_id] = bw;
	} else {
		int i = 0;
		int total_retries =
			((pkt->tx_status & TX_NUMMPDURETRIES_MASK) >>
			 TX_NUMMPDURETRIES_OFFSET);

		rxrssi = pkt->rssi / 2;

		if (total_retries != 0) {
			/* search for the one which transmits successfully */
			for (i = 0; i < PKT_RXVEC_LEN - 1; i++) {
				int nRetry =
					(pkt->rx_vec[i] & TX_NRETRY_MASK) >>
					TX_NRETRY_OFFSET;

				/* For the special case, frames transmitted by using 24G/5G
                   txl_buffer_contrl are filled only one data rate and the
                   retry count in policy table will be 0. */
				if (nRetry == 0 && i == 0)
					break;

				if (total_retries < nRetry)
					break;

				total_retries -= nRetry;
			}
		}

		//MCS Index of PPDU for Transmission
		ratecntrl.mcsIndexTx = (pkt->rx_vec[i] & TX_MCSINDEX_MASK) >>
				       TX_MCSINDEX_OFFSET;
		//Band Width of PPDU for Transmission
		ratecntrl.bwTx =
			(pkt->rx_vec[i] & TX_BWTX_MASK) >> TX_BWTX_OFFSET;
		//Guard Interval of PPDU and Preamble Type for Transmission
		ratecntrl.giAndPreTypeTx =
			(pkt->rx_vec[i] & TX_GIANDPRETYPE_MASK) >>
			TX_GIANDPRETYPE_OFFSET;
		//Format and Modulation of PPDU for Transmission
		ratecntrl.formatModTx = (pkt->rx_vec[i] & TX_FORMATMOD_MASK) >>
					TX_FORMATMOD_OFFSET;
		ratecntrl.dcmTx =
			(pkt->rx_vec[i]) & TX_DCM_MASK >> TX_DCM_OFFSET;

		switch (ratecntrl.bwTx) {
		case BW_20MHZ:
			bw = 20;
			break;
		case BW_40MHZ:
			bw = 40;
			break;
		case BW_80MHZ:
			bw = 80;
			break;
		case BW_160MHZ:
			bw = 160;
			break;
		default:
			WQ_DBG(DM_PKTDUMP, DL_WRN, "cannot get bandwidth");
			break;
		}
		switch (ratecntrl.formatModTx) {
		case FORMATMOD_NON_HT:
			str_format = "";
			pkt->rate = ratecntrl.mcsIndexTx;
			rate = non_ht_rate_tx[pkt->rate] / 2;
			str_pregitype = (ratecntrl.giAndPreTypeTx) == 0 ?
						"short" :
						"long";
			break;

		case FORMATMOD_HT_MF:
			str_format = "HT MCS";
			mcs = (ratecntrl.mcsIndexTx) & 0x0F;
			str_pregitype = (ratecntrl.giAndPreTypeTx) == 0 ?
						"short" :
						"long";
			break;

		case FORMATMOD_VHT:
			str_format = "VHT MCS";
			nss = (ratecntrl.mcsIndexTx) & 0x30 >> 4;
			mcs = (ratecntrl.mcsIndexTx) & 0x0F;
			str_pregitype = (ratecntrl.giAndPreTypeTx) == 0 ?
						"short" :
						"long";
			break;

		case FORMATMOD_HE_SU:
			str_format = "HE-SU MCS";
			nss = (ratecntrl.mcsIndexTx) & 0x30 >> 4;
			mcs = (ratecntrl.mcsIndexTx) & 0x0F;
			str_pregitype = (ratecntrl.giAndPreTypeTx) == 0 ?
						"short" :
						"long";
			break;

		case FORMATMOD_HE_MU:
			str_format = "HE-MU MCS";
			nss = (ratecntrl.mcsIndexTx) & 0x30 >> 4;
			mcs = (ratecntrl.mcsIndexTx) & 0x0F;
			str_pregitype = (ratecntrl.giAndPreTypeTx) == 0 ?
						"short" :
						"long";
			break;

		default:
			WQ_DBG(DM_PKTDUMP, DL_WRN, "cannot get format");
			str_format = "?";
			break;
		}

		///tx done status print
		//printk("tx status = 0x%08x\n",pkt->tx_status);
		//number of RTS Retries
		txstatus.num_rts_retries =
			(pkt->tx_status & TX_NUMRTSRETRIES_MASK) >>
			TX_NUMRTSRETRIES_OFFSET;
		//number of MPDU Retries
		txstatus.num_mpdu_retries =
			(pkt->tx_status & TX_NUMMPDURETRIES_MASK) >>
			TX_NUMMPDURETRIES_OFFSET;
		//frame unsuccessful - Retry Limit Reached
		txstatus.retry_limit_reached =
			(pkt->tx_status & TX_RETRYLIMITREACHED_MASK) >>
			TX_RETRYLIMITREACHED_OFFSET;
		//frame unsuccessful - life time expired
		txstatus.lifetime_expired =
			(pkt->tx_status & TX_LIFETIMEEXPIRED_MASK) >>
			TX_LIFETIMEEXPIRED_OFFSET;
		//BlockAck received indication
		txstatus.baFrameReceived =
			(pkt->tx_status & TX_BAFRAMERECEIVED_MASK) >>
			TX_BAFRAMERECEIVED_OFFSET;
		//Frame successful for Transmit DMA
		txstatus.frm_successful_tx =
			(pkt->tx_status & TX_FRMSUCCESSFULTX_MASK) >>
			TX_FRMSUCCESSFULTX_OFFSET;
		//TransmissionBW
		txstatus.transmission_bw =
			(pkt->tx_status & TX_TRANSMISSIONBW_MASK) >>
			TX_TRANSMISSIONBW_OFFSET;
		//Which Descriptor for SW
		txstatus.which_descriptor_sw =
			(pkt->tx_status & TX_WHICHDESCRIPTORSW_MASK) >>
			TX_WHICHDESCRIPTORSW_OFFSET;
		//Descriptor Done by SW
		txstatus.descriptor_done_swtx =
			(pkt->tx_status & TX_DESCRIPTORDONESWTX_MASK) >>
			TX_DESCRIPTORDONESWTX_OFFSET;
		//Descriptor Done by HW
		txstatus.descriptor_done_hwtx =
			(pkt->tx_status & TX_DESCRIPTORDONEHWTX_MASK) >>
			TX_DESCRIPTORDONEHWTX_OFFSET;

		switch (txstatus.transmission_bw) {
		case BW_20MHZ:
			txstatusbw = 20;
			break;
		case BW_40MHZ:
			txstatusbw = 40;
			break;
		case BW_80MHZ:
			txstatusbw = 80;
			break;
		case BW_160MHZ:
			txstatusbw = 160;
			break;
		default:
			WQ_DBG(DM_PKTDUMP, DL_WRN, "cannot get bandwidth");
			break;
		}

		if (txstatus.descriptor_done_hwtx &&
		    txstatus.frm_successful_tx) {
			//printk("tx ok !");
		} else {
			if (txstatus.retry_limit_reached) {
				WQ_DBG(DM_PKTDUMP, DL_WRN,
				       "[auto]tx fail: %s retry limit reached num=%d",
				       txstatus.num_rts_retries ? "rts" :
								  "mpdu",
				       txstatus.num_rts_retries ?
					       txstatus.num_rts_retries :
					       txstatus.num_mpdu_retries);
			} else if (txstatus.lifetime_expired) {
				WQ_DBG(DM_PKTDUMP, DL_WRN,
				       "[auto]tx fail: life time expired\n");
			} else if (txstatus.baFrameReceived) {
				WQ_DBG(DM_PKTDUMP, DL_WRN,
				       "[auto]tx fail: BlockAck received indication\n");
			} else {
				WQ_DBG(DM_PKTDUMP, DL_WRN,
				       "[auto]tx fail: fw flush\n");
			}
		};
		/*
        printk("Transmission_bw=%dMHz, Descriptor Done by %s, SW Descriptor para=%04x",
            txstatusbw,txstatus.descriptor_done_swtx?"SW":"HW",txstatus.which_descriptor_sw);
        */

		/*
        printk("%03d Tx rate=%dMbps,mcs=%d,bwTx=%dMHz,giAndPreTypeTx=%s,formatModTx=%s,"
        "dcmTx=%x",
        pkt->sn,rate,mcs,bw,str_pregitype,str_format,
        ratecntrl.dcmTx);
        */
		dfx_pkt_info.mcs_tx[pkt->mac_id] = mcs;
		dfx_pkt_info.bw_tx[pkt->mac_id] = bw;
	}

	if ((uint8_t)(gv_pktdump_sn + 1) != (pkt->sn)) {
		WQ_DBG(DM_PKTDUMP, DL_WRN,
		       "[auto]warning : pktdump loss, last=0x%x now=0x%x",
		       gv_pktdump_sn, pkt->sn);
	}

	gv_pktdump_sn = pkt->sn;

	WQ_DBG(DM_PKTDUMP, DL_WRN,
	       "[auto]%s%d %d:%x %s%d%s BW%u(%u) %ddBm %ddB %02x%02x(%s%c%c%c%c) %02x%02x %02x%02x%02x%02x%02x%02x %02x%02x%02x%02x%02x%02x .. %02x%02x %02x%02x %s\n",
	       pkt->dir == WIFI_DBG_PKT_TX ? "T" : "R", pkt->len, pkt->mac_id,
	       pkt->tx_status, str_format, rate ? rate : mcs,
	       rate ? "Mbps" : "", bw,
	       pkt->dir == WIFI_DBG_PKT_TX ? txstatusbw : bw, rxrssi, snr,
	       pkt->pkt_data[0], pkt->pkt_data[1],
	       frame_type_to_str(pkt->pkt_data, pkt->is_ampdu),
	       pkt->pkt_data[1] & 0x8 ? '+' : ' ',
	       pkt->pkt_data[1] & 0x10 ? 'P' : ' ',
	       pkt->pkt_data[1] & 0x20 ? 'M' : ' ',
	       pkt->pkt_data[1] & 0x40 ? 'W' : ' ', pkt->pkt_data[2],
	       pkt->pkt_data[3], pkt->pkt_data[4], pkt->pkt_data[5],
	       pkt->pkt_data[6], pkt->pkt_data[7], pkt->pkt_data[8],
	       pkt->pkt_data[9], pkt->pkt_data[10], pkt->pkt_data[11],
	       pkt->pkt_data[12], pkt->pkt_data[13], pkt->pkt_data[14],
	       pkt->pkt_data[15], pkt->pkt_data[22], pkt->pkt_data[23],
	       pkt->pkt_data[24], pkt->pkt_data[25], eth_str);

	//only mgmt frame
	mgmt = (struct ieee80211_mgmt *)pkt->pkt_data;
	if (dump_idx < HIST_CNT && (pkt->pkt_data[0] & 0xc) == 0) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
		do_gettimeofday(&now);
#else
		ktime_get_real_ts64(&now);
#endif
		pktdump_hist[dump_idx].ts = now.tv_sec;
		pktdump_hist[dump_idx].sn = pkt->sn;
		pktdump_hist[dump_idx].dir = pkt->dir;
		pktdump_hist[dump_idx].mac_id = pkt->mac_id;
		pktdump_hist[dump_idx].tx_status = pkt->tx_status;
		pktdump_hist[dump_idx].frame_ctrl = mgmt->frame_control;
		pktdump_hist[dump_idx].category = mgmt->u.action.category;
		pktdump_hist[dump_idx].action_type =
			mgmt->u.action.u.wme_action.action_code;
		pktdump_hist[dump_idx].p2p =
			*((uint8_t *)&mgmt->u.action.category +
			  MGMT_ACTION_OUI_SUBTYPE_OFFSET);
		memcpy(pktdump_hist[dump_idx].da, mgmt->da, ETH_ALEN);
		memcpy(pktdump_hist[dump_idx].sa, mgmt->sa, ETH_ALEN);
		dump_idx++;
		if (dump_idx == HIST_CNT)
			dump_idx = 0;
	}
}

#define PRINT_FW_HW_FEATURE(feature)                                           \
	WQ_DBG(DM_GENERIC, DL_WRN, "%s : %d\n", #feature, mod_params->feature);
#define PRINT_FW_HW_FEATURE_X(feature)                                         \
	WQ_DBG(DM_GENERIC, DL_WRN, "%s : 0x%X\n", #feature,                    \
	       mod_params->feature);

void print_fw_hw_feature(struct wq_conf *mod_params)
{
	PRINT_FW_HW_FEATURE(ht_on);
	PRINT_FW_HW_FEATURE(vht_on);
	PRINT_FW_HW_FEATURE(he_on);
	PRINT_FW_HW_FEATURE_X(mcs_map);
	PRINT_FW_HW_FEATURE_X(he_mcs_map);
	PRINT_FW_HW_FEATURE(he_ul_on);
	PRINT_FW_HW_FEATURE(ldpc_on);
	PRINT_FW_HW_FEATURE(stbc_on);
	PRINT_FW_HW_FEATURE(gf_rx_on);
	PRINT_FW_HW_FEATURE_X(phy_cfg);
	PRINT_FW_HW_FEATURE(uapsd_timeout);
	PRINT_FW_HW_FEATURE(ap_uapsd_on);
	PRINT_FW_HW_FEATURE(sgi);
	PRINT_FW_HW_FEATURE(sgi80);
	PRINT_FW_HW_FEATURE(use_2040);
	PRINT_FW_HW_FEATURE(use_80);
	PRINT_FW_HW_FEATURE(custregd);
	PRINT_FW_HW_FEATURE(custchan);
	PRINT_FW_HW_FEATURE(nss);
	PRINT_FW_HW_FEATURE(amsdu_rx_max);
	PRINT_FW_HW_FEATURE(bfmee);
	PRINT_FW_HW_FEATURE(bfmer);
	PRINT_FW_HW_FEATURE(mesh);
	PRINT_FW_HW_FEATURE(murx);
	PRINT_FW_HW_FEATURE(mutx);
	PRINT_FW_HW_FEATURE(mutx_on);
	PRINT_FW_HW_FEATURE(roc_dur_max);
	PRINT_FW_HW_FEATURE(listen_itv);
	PRINT_FW_HW_FEATURE(listen_bcmc);
	PRINT_FW_HW_FEATURE(lp_clk_ppm);
	PRINT_FW_HW_FEATURE(ps_on);
	PRINT_FW_HW_FEATURE(tx_lft);
	PRINT_FW_HW_FEATURE(amsdu_maxnb);
	PRINT_FW_HW_FEATURE(uapsd_queues);
	PRINT_FW_HW_FEATURE(tdls);
	PRINT_FW_HW_FEATURE(uf);
	//PRINT_FW_HW_FEATURE(ftl);
	PRINT_FW_HW_FEATURE(dpsm);
	PRINT_FW_HW_FEATURE(tx_to_bk);
	PRINT_FW_HW_FEATURE(tx_to_be);
	PRINT_FW_HW_FEATURE(tx_to_vi);
	PRINT_FW_HW_FEATURE(tx_to_vo);
	PRINT_FW_HW_FEATURE(ant_div);
}

void print_mgmt_frame_info(char *note, struct ieee80211_mgmt *mgmt,
			   u16 mgmt_tx_len)
{
	char buf[30];
	u8 cat = mgmt->u.action.category;
	u8 type = mgmt->u.action.u.chan_switch.variable[3];
	u8 subtype = mgmt->u.action.u.chan_switch.variable[4];
	u8 p2p_dump_len = 16;
	unsigned long p2p_action_offset =
		(unsigned long)&mgmt->u.action.u.chan_switch.variable -
		(unsigned long)mgmt;

	switch (mgmt->frame_control & IEEE80211_FCTL_STYPE) {
	case (IEEE80211_STYPE_ASSOC_REQ):
		snprintf(buf, 30, "%s", "Association Request");
		break;
	case (IEEE80211_STYPE_ASSOC_RESP):
		snprintf(buf, 30, "%s", "Association Response");
		break;
	case (IEEE80211_STYPE_REASSOC_REQ):
		snprintf(buf, 30, "%s", "Reassociation Request");
		break;
	case (IEEE80211_STYPE_REASSOC_RESP):
		snprintf(buf, 30, "%s", "Reassociation Response");
		break;
	case (IEEE80211_STYPE_PROBE_REQ):
		snprintf(buf, 30, "%s", "Probe Request");
		break;
	case (IEEE80211_STYPE_PROBE_RESP):
		snprintf(buf, 30, "%s", "Probe Response");
		break;
	case (IEEE80211_STYPE_BEACON):
		snprintf(buf, 30, "%s", "Beacon");
		break;
	case (IEEE80211_STYPE_ATIM):
		snprintf(buf, 30, "%s", "ATIM");
		break;
	case (IEEE80211_STYPE_DISASSOC):
		snprintf(buf, 30, "%s", "Disassociation");
		break;
	case (IEEE80211_STYPE_AUTH):
		snprintf(buf, 30, "%s", "Authentication");
		break;
	case (IEEE80211_STYPE_DEAUTH):
		snprintf(buf, 30, "%s", "Deauthentication");
		break;
	case (IEEE80211_STYPE_ACTION):
		if (cat == MGMT_ACTION_PUBLIC_CAT && type == 0x9)
			switch (subtype) {
			case (P2P_ACTION_GO_NEG_REQ):
				snprintf(buf, 30, "%s",
					 "Action: GO Negociation Request");
				break;
			case (P2P_ACTION_GO_NEG_RSP):
				snprintf(buf, 30, "%s",
					 "Action: GO Negociation Response");
				/* dump P2P status code */
				if (p2p_action_offset + p2p_dump_len <
				    mgmt_tx_len)
					dump_bytes(DL_WRN, "NEGO_RSP_Tx",
						   mgmt->u.action.u.chan_switch
							   .variable,
						   p2p_dump_len);
				break;
			case (P2P_ACTION_GO_NEG_CFM):
				snprintf(buf, 30, "%s",
					 "Action: GO Negociation Confirmation");
				break;
			case (P2P_ACTION_INVIT_REQ):
				snprintf(buf, 30, "%s",
					 "Action: P2P Invitation Request");
				break;
			case (P2P_ACTION_INVIT_RSP):
				snprintf(buf, 30, "%s",
					 "Action: P2P Invitation Response");
				break;
			case (P2P_ACTION_DEV_DISC_REQ):
				snprintf(
					buf, 30, "%s",
					"Action: Device Discoverability Request");
				break;
			case (P2P_ACTION_DEV_DISC_RSP):
				snprintf(
					buf, 30, "%s",
					"Action: Device Discoverability Response");
				break;
			case (P2P_ACTION_PROV_DISC_REQ):
				snprintf(buf, 30, "%s",
					 "Action: Provision Discovery Request");
				break;
			case (P2P_ACTION_PROV_DISC_RSP):
				snprintf(
					buf, 30, "%s",
					"Action: Provision Discovery Response");
				break;
			default:
				snprintf(buf, 30, "%s %d",
					 "Action: Unknown p2p", subtype);
				break;
			}
		else {
			switch (cat) {
			case 0:
				snprintf(buf, 30, "%s %d", "Action:Spectrum",
					 type);
				break;
			case 1:
				snprintf(buf, 30, "%s %d", "Action:QOS", type);
				break;
			case 2:
				snprintf(buf, 30, "%s %d", "Action:DLS", type);
				break;
			case 3:
				snprintf(buf, 30, "%s %d", "Action:BA", type);
				break;
			case 4:
				snprintf(buf, 30, "%s %d", "Action:Public",
					 type);
				break;
			case 5:
				snprintf(buf, 30, "%s %d",
					 "Action:Radio Measure", type);
				break;
			case 6:
				snprintf(buf, 30, "%s %d", "Action:Fast BSS",
					 type);
				break;
			case 7:
				snprintf(buf, 30, "%s %d", "Action:HT Action",
					 type);
				break;
			case 8:
				snprintf(buf, 30, "%s %d", "Action:SA Query",
					 type);
				break;
			case 9:
				snprintf(buf, 30, "%s %d",
					 "Action:Protected Public", type);
				break;
			case 10:
				snprintf(buf, 30, "%s %d", "Action:WNM %d",
					 type);
				break;
			case 11:
				snprintf(buf, 30, "%s %d",
					 "Action:Unprotected WNM", type);
				break;
			case 12:
				snprintf(buf, 30, "%s %d", "Action:TDLS", type);
				break;
			case 13:
				snprintf(buf, 30, "%s %d", "Action:Mesh", type);
				break;
			case 14:
				snprintf(buf, 30, "%s %d", "Action:MultiHop",
					 type);
				break;
			case 15:
				snprintf(buf, 30, "%s %d",
					 "Action:Self Protected", type);
				break;
			case 126:
				snprintf(buf, 30, "%s",
					 "Action:Vendor protected");
				break;
			case 127:
				snprintf(buf, 30, "%s", "Action:Vendor");
				break;
			default:
				snprintf(buf, 30, "%s %d",
					 "Action:Unknown category", cat);
				break;
			}
		}
		break;
	default:
		snprintf(buf, 30, "%s %d", "Unknown subtype",
			 mgmt->frame_control & IEEE80211_FCTL_STYPE);
		break;
	}

	WQ_DBG(DM_GENERIC, DL_WRN, "%s: %s\n", note, buf);
}

void inline wq_dbg_dump_recovery_stats(struct wq_dbg_recovery_stats *stats)
{
	if (stats) {
		WQ_DBG(DM_PKTDUMP, DL_WRN, "mac recovery stats:\n");
		WQ_DBG(DM_PKTDUMP, DL_WRN, "mac_not_idle_cnt     : %hu\n",
		       stats->mac_not_idle_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "tx_timeout_cnt       : %hu\n",
		       stats->tx_timeout_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "idle_timeout_cnt     : %hu\n",
		       stats->idle_timeout_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "he_tb_timeout_cnt    : %hu\n",
		       stats->he_tb_timeout_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "mm_timeout_cnt       : %hu\n",
		       stats->mm_timeout_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "phy_err_cnt          : %hu\n",
		       stats->phy_err_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "phy_err_tb_basic_cnt : %hu\n",
		       stats->phy_err_tb_basic_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "phy_err_tb_bsrp_cnt  : %hu\n",
		       stats->phy_err_tb_bsrp_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "phyif_underrun_cnt   : %hu\n",
		       stats->phyif_underrun_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "phyif_overflow_cnt   : %hu\n",
		       stats->phyif_overflow_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "rx_fifo_overflow_cnt : %hu\n",
		       stats->rx_fifo_overflow_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "pt_error_cnt         : %hu\n",
		       stats->pt_error_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "tx_dma_dead_cnt      : %hu\n",
		       stats->tx_dma_dead_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "beacon_dma_dead_cnt  : %hu\n",
		       stats->beacon_dma_dead_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "rx_header_dead_cnt   : %hu\n",
		       stats->rx_header_dead_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "rx_payload_dead_cnt  : %hu\n",
		       stats->rx_payload_dead_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "hw_err_cnt           : %hu\n",
		       stats->hw_err_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "rx_desc_err_cnt      : %hu\n",
		       stats->rx_desc_err_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "rx_key_idx_err_cnt   : %hu\n",
		       stats->rx_key_idx_err_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "rx_ndp_desc_err_cnt  : %hu\n",
		       stats->rx_ndp_desc_err_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "rx_length_err_cnt    : %hu\n",
		       stats->rx_length_err_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "tx_desc_err_cnt      : %hu\n",
		       stats->tx_desc_err_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "tx_key_idx_err_cnt   : %hu\n",
		       stats->tx_key_idx_err_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "tx_desc_ampdu_err_cnt: %hu\n",
		       stats->tx_desc_ampdu_err_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "tx_mac_idle_cnt      : %hu\n",
		       stats->tx_mac_idle_cnt);
		WQ_DBG(DM_PKTDUMP, DL_WRN, "tx_beacon_mac_idle_cnt: %hu\n",
		       stats->tx_beacon_mac_idle_cnt);
	}
}

void wq_dbg_update_security_info(struct wq_security_info *security,
				 uint8_t key_idx, bool pairwise, uint8_t cipher)
{
	if (security == NULL) {
		return;
	}

	/* update cipher */
	if (key_idx >= 0 && key_idx < 4) {
		if (pairwise) {
			security->unicast_cipher = cipher;
		} else {
			security->group_cipher = cipher;
		}
	} else if (key_idx < 6) {
		security->mgmt_cipher = cipher;
	} else if (key_idx == 0xff) {
		/* clear cipher info */
		security->unicast_cipher = MAC_CIPHER_INVALID;
		security->group_cipher = MAC_CIPHER_INVALID;
		security->mgmt_cipher = MAC_CIPHER_INVALID;
	}

	/* udpate mfp_on */
	if (security->mgmt_cipher == MAC_CIPHER_BIP_CMAC_128 ||
	    security->mgmt_cipher == MAC_CIPHER_BIP_CMAC_256 ||
	    security->mgmt_cipher == MAC_CIPHER_BIP_GMAC_128 ||
	    security->mgmt_cipher == MAC_CIPHER_BIP_GMAC_256) {
		security->mfp_on = true;
	}

	/* update wpa_version */
	if (security->group_cipher == MAC_CIPHER_WEP40 ||
	    security->group_cipher == MAC_CIPHER_WEP104) {
		security->wpa_version = WPA_VER_WEP;
	} else if (security->group_cipher == MAC_CIPHER_TKIP) {
		security->wpa_version = WPA_VER_WPA1;
	} else if (security->group_cipher == MAC_CIPHER_CCMP ||
		   security->group_cipher == MAC_CIPHER_GCMP_128 ||
		   security->group_cipher == MAC_CIPHER_CCMP_256 ||
		   security->group_cipher == MAC_CIPHER_GCMP_256) {
		if (security->auth_type == NL80211_AUTHTYPE_OPEN_SYSTEM) {
			security->wpa_version = WPA_VER_WPA2;
		} else if (security->auth_type == NL80211_AUTHTYPE_SAE) {
			security->wpa_version = WPA_VER_WPA3;
		}
	} else {
		security->wpa_version = WPA_VER_OPEN;
	}
}

/**
* @brief            get vif info of interface
* @note
* @param[in]        rwnx_vif: VIF information
* @param[out]       dbg_vif: custom vif info for debug
* @return           returns 0 on success or negative error code
*/
int wq_get_vif_info(struct rwnx_vif *vif, struct wq_dbg_vif *dbg_vif)
{
	struct cfg80211_chan_def *chandef;
	int i;

	if (vif == NULL || dbg_vif == NULL) {
		return -EINVAL;
	}

	strcpy(dbg_vif->name, vif->ndev->name);
	memcpy(dbg_vif->mac_addr, vif->ndev->dev_addr, ETH_ALEN);
	dbg_vif->iftype = RWNX_VIF_TYPE(vif);

	if (rwnx_chanctx_valid(vif->rwnx_hw, vif->ch_index)) {
		chandef = &vif->rwnx_hw->chanctx_table[vif->ch_index].chan_def;
		dbg_vif->band = chandef->chan->band;
		dbg_vif->width = chandef->width;
		dbg_vif->center_freq = chandef->chan->center_freq;
		dbg_vif->center_freq1 = chandef->center_freq1;
		dbg_vif->center_freq2 = chandef->center_freq2;
		/* get chan type */
		dbg_vif->chan_type = cfg80211_get_chandef_type(chandef);
	}

	if (dbg_vif->iftype == NL80211_IFTYPE_AP) {
		memcpy(dbg_vif->bssid, vif->ndev->dev_addr, ETH_ALEN);
		/* get wlan version */
		if (vif->rwnx_hw->mod_params.he_on) {
			dbg_vif->wlan_version = WLAN_VER_11AX;
		} else if (vif->rwnx_hw->mod_params.vht_on) {
			dbg_vif->wlan_version = WLAN_VER_11AC;
		} else if (vif->rwnx_hw->mod_params.ht_on) {
			dbg_vif->wlan_version = WLAN_VER_11N;
		} else {
			dbg_vif->wlan_version = WLAN_VER_LEGACY;
		}
	} else if (dbg_vif->iftype == NL80211_IFTYPE_STATION) {
		if (vif->sta.ap != NULL && vif->sta.ap->valid) {
			memcpy(dbg_vif->bssid, vif->sta.ap->mac_addr, ETH_ALEN);
			/* get wlan version */
			if (vif->sta.ap->he_mac_cap_info[0] &&
			    vif->rwnx_hw->mod_params.he_on) {
				dbg_vif->wlan_version = WLAN_VER_11AX;
			} else if (vif->sta.ap->vht_cap_info &&
				   vif->rwnx_hw->mod_params.vht_on) {
				dbg_vif->wlan_version = WLAN_VER_11AC;
			} else if (vif->sta.ap->ht_cap_info &&
				   vif->rwnx_hw->mod_params.ht_on) {
				dbg_vif->wlan_version = WLAN_VER_11N;
			} else {
				dbg_vif->wlan_version = WLAN_VER_LEGACY;
			}
		}
	} else if (dbg_vif->iftype == NL80211_IFTYPE_P2P_CLIENT ||
		   dbg_vif->iftype == NL80211_IFTYPE_P2P_DEVICE ||
		   dbg_vif->iftype == NL80211_IFTYPE_P2P_GO) {
		/* TODO support P2P mode */
	}
	dbg_vif->tkip_mic_failure_count = vif->tkip_mic_failure_count;
	for (i = 0; i < WQ_CREDIT_TYPE_NUM; i++) {
		dbg_vif->credit_total[i] =
			vif->rwnx_hw->crdt_mgmt.credit_grp[vif->crdt_gid]
				.size[i];
		dbg_vif->credit_avail[i] =
			vif->rwnx_hw->crdt_mgmt.credit_grp[vif->crdt_gid]
				.credit[i];
		dbg_vif->credit_lend[i] =
			vif->rwnx_hw->crdt_mgmt.credit_grp[vif->crdt_gid]
				.lend[i];
	}

	return 0;
}

/**
* @brief           get skb statics
* @note            all interfaces share skbs
* @param[in]       rwnx_hw: RWNX driver main data
* @param[out]      dbg_stats: skb statics used for caller
* @return          returns 0 on success or negative error code
*/
int wq_get_skb_stats(struct rwnx_hw *rwnx_hw, struct wq_skb_stats *skb_stats)
{
	if (rwnx_hw == NULL || skb_stats == NULL) {
		return -EINVAL;
	}
	memcpy(skb_stats, &rwnx_hw->skb_stats, sizeof(struct wq_skb_stats));

	return 0;
}

/**
* @brief            get security info of interface
* @note
* @param[in]        rwnx_vif: VIF information
* @param[out]       security_info: security info of the interface
* @return           returns 0 on success or negative error code
*/
int wq_get_security_info(struct rwnx_vif *vif,
			 struct wq_security_info *security_info)
{
	if (vif == NULL || security_info == NULL) {
		return -EINVAL;
	}

	memcpy(security_info, &vif->security, sizeof(struct wq_security_info));

	return 0;
}

/**
* @brief           wq get edca param
* @note
* @param[in]       rwnx_hw: RWNX driver main data
* @param[in]       mac_id: mac core
* @param[out]      dfx_edca: dbg_dfx_edca_param
* @return          returns 0 on success or negative error code
*/
int wq_get_edca_param(struct rwnx_hw *rwnx_hw, uint8_t mac_id,
		      struct dbg_dfx_edca_param *dfx_edca)
{
	if (rwnx_hw == NULL || dfx_edca == NULL) {
		return -EINVAL;
	}

	return RWNX_INFO_NOTIFY_GET(rwnx_hw, MSG_TYPE_DFX_EDCA, mac_id,
				    dfx_edca);
}

/**
* @brief           wq get connect time
* @note
* @param[in]       rwnx_sta
* @param[out]      wq_dbg_connect_time
* @return          returns 0 on success or negative error code
*/
extern struct wq_dbg_connect_time dbg_connect_time;
int wq_get_connect_time(struct rwnx_sta *sta_table,
			struct wq_dbg_connect_time *conn_time_stat)
{
	int band = sta_table->band;
	int width = sta_table->width;

	dbg_connect_time.sub_time[band][width] =
		dbg_connect_time.disconnect_time[band][width] -
		dbg_connect_time.connect_time[band][width];
	dbg_connect_time.all_time[band][width] =
		dbg_connect_time.all_time[band][width] +
		dbg_connect_time.sub_time[band][width];

	memcpy(conn_time_stat, &dbg_connect_time,
	       sizeof(struct wq_dbg_connect_time));

	return 0;
}

/**
* @brief           get station trx statics from firmware
* @note
* @param[in]       rwnx_hw: RWNX driver main data
* @param[in]       sta_idx: index of the station in firmware
* @param[out]      stats: firmware stats of a station
* @return          returns 0 on success or negative error code
*/
int wq_get_sta_trx_stats(struct rwnx_hw *rwnx_hw, uint8_t sta_idx,
			 struct wq_dbg_fw_sta_trx_stats *stats)
{
	if (rwnx_hw == NULL || stats == NULL) {
		return -EINVAL;
	}

	/* check for sta valid */
	if (sta_idx >= (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX) ||
	    !rwnx_hw->sta_table[sta_idx].valid) {
		WQ_DBG(DM_GENERIC, DL_ERR, "invalid station index: %d!\n",
		       sta_idx);
		return -EINVAL;
	}

	return RWNX_INFO_NOTIFY_GET(rwnx_hw, MSG_TYPE_STA_TRX_STATS, sta_idx,
				    stats);
}
/**
* @brief           get station trx statics from firmware
* @note
* @param[in]       rwnx_vif: VIF information
* @param[out]      stats: firmware stats of vif extra trx stats
* @return          returns 0 on success or negative error code
*/
int wq_get_vif_ext_trx_stats(struct rwnx_vif *vif,
			     struct wq_dbg_vif_ext_trx_stats *stats)
{
	uint8_t virt_id;

	if (vif == NULL || stats == NULL) {
		return -EINVAL;
	}

	/* vif_idx + 0x80 to diff from sta_idx */
	virt_id = vif->vif_index | 0x80;
	return RWNX_INFO_NOTIFY_GET(vif->rwnx_hw, MSG_TYPE_STA_TRX_STATS,
				    virt_id, stats);
}

/**
* @brief           wq get agc code get
* @note
* @param[in]       rwnx_hw: RWNX driver main data
* @param[in]       mac_id: mac core
* @param[out]      agc_code: dbg_dfx_agc_code_param
* @return          returns 0 on success or negative error code
*/
int wq_get_agc_code_get(struct rwnx_hw *rwnx_hw, uint8_t mac_id,
			struct dbg_dfx_agc_code_param *agc_code)
{
	if (rwnx_hw == NULL || agc_code == NULL) {
		return -EINVAL;
	}

	return RWNX_INFO_NOTIFY_GET(rwnx_hw, MSG_TYPE_AGC_GAIN, mac_id,
				    agc_code);
}

/*
* @brief           get channel noise information from firmware
* @note
* @param[in]       rwnx_vif: VIF information
* @param[in]       time_ms: statistical duration
* @param[out]      noise_info: store channe noise information
* @return          returns 0 on success or negative error code
*/
int wq_get_chan_noise_info(struct rwnx_vif *vif, uint32_t time_ms,
			   struct wq_dbg_chan_noise_info *noise_info)
{
	struct dbg_chan_noise_info_req req;

	if (vif == NULL || noise_info == NULL) {
		return -EINVAL;
	}

	req = (struct dbg_chan_noise_info_req){
		.vif_idx = vif->vif_index,
		.time_ms = time_ms,
	};
	return RWNX_INFO_NOTIFY_GET(vif->rwnx_hw, MSG_TYPE_CHAN_NOISE_INFO, req,
				    noise_info);
}

/**
* @brief           get channel utilization information from firmware
* @note
* @param[in]       rwnx_vif: VIF information
* @param[in]       time_ms: statistical duration
* @param[out]      util_info: store channe busy information
* @return          returns 0 on success or negative error code
*/
int wq_get_chan_util_info(struct rwnx_vif *vif, uint32_t time_ms,
			  struct wq_dbg_chan_util_info *util_info)
{
	struct dbg_chan_util_info_req req;

	if (vif == NULL || util_info == NULL) {
		return -EINVAL;
	}

	req = (struct dbg_chan_util_info_req){
		.vif_idx = vif->vif_index,
		.time_ms = time_ms,
	};
	return RWNX_INFO_NOTIFY_GET(vif->rwnx_hw, MSG_TYPE_CHAN_UTIL_INFO, req,
				    util_info);
}

/**
* @brief           wq get ampdu statistics
* @note
* @param[in]       rwnx_hw: RWNX driver main data
* @param[out]      stats: ampdu stats information
* @return          returns 0 on success or negative error code
*/
int wq_get_ampdu_stats(struct rwnx_hw *rwnx_hw, struct rwnx_stats *stats)
{
	if (rwnx_hw == NULL || stats == NULL) {
		return -EINVAL;
	}

	memcpy(stats, &rwnx_hw->stats, sizeof(struct rwnx_stats));

	return 0;
}

/**
* @brief           wq get ac delay time
* @note
* @param[in]       rwnx_hw: RWNX driver main data
* @param[in]       mac_id: mac core
* @param[out]      ac_delay: wq_dbg_dfx_ac_delay
* @return          returns 0 on success or negative error code
*/
int wq_get_ac_delay_time(struct rwnx_hw *rwnx_hw, uint8_t mac_id,
			 struct wq_dbg_dfx_ac_delay *ac_delay)
{
	if (rwnx_hw == NULL || ac_delay == NULL) {
		return -EINVAL;
	}

	return RWNX_INFO_NOTIFY_GET(rwnx_hw, MSG_TYPE_AC_DELAY_TIME, mac_id,
				    ac_delay);
}

/**
* @brief           wq get trx pkt info
* @note            get tx/rx mcs & bw,snr
* @param[in]
* @param[in]
* @param[out]      pkt_info: wq_dbg_dfx_pkt_info
* @return          returns 0 on success or negative error code
*/
int wq_get_trx_pkt_info(struct wq_dbg_dfx_pkt_info *pkt_info)
{
	if (pkt_info == NULL) {
		return -EINVAL;
	}

	memcpy(pkt_info, &dfx_pkt_info, sizeof(struct wq_dbg_dfx_pkt_info));

	return 0;
}

/**
* @brief           wq get phy rf trx state
* @note
* @param[in]       rwnx_hw: RWNX driver main data
* @param[in]       mac_id: mac core
* @param[out]      phy_rf_state: wq_dbg_phy_rf_trx_state
* @return          returns 0 on success or negative error code
*/
int wq_get_phy_rf_trx_state(struct rwnx_hw *rwnx_hw, uint8_t mac_id,
			    struct wq_dbg_phy_rf_trx_state *phy_rf_state)
{
	if (rwnx_hw == NULL || phy_rf_state == NULL) {
		return -EINVAL;
	}

	return RWNX_INFO_NOTIFY_GET(rwnx_hw, MSG_TYPE_PHY_RF_TRX_STATE, mac_id,
				    phy_rf_state);
}

/**
* @brief           get crc statistics from firmware
* @note
* @param[in]       rwnx_vif: VIF information
* @param[out]      stats: crc statistics
* @return          returns 0 on success or negative error code
*/
int wq_get_crc_stats(struct rwnx_vif *vif, struct wq_dbg_crc_stats *stats)
{
	struct dbg_crc_stats_req req;

	if (vif == NULL || stats == NULL) {
		return -EINVAL;
	}

	req = (struct dbg_crc_stats_req){
		.vif_idx = vif->vif_index, .type = 2, /* get */
	};
	return RWNX_INFO_NOTIFY_GET(vif->rwnx_hw, MSG_TYPE_CRC_STATS, req,
				    stats);
}

/**
* @brief           get crc statistics from firmware
* @note            stats would be clear when call this function
* @param[in]       rwnx_vif: VIF information
* @param[in]       enable: enable/disable statistics
* @return          returns 0 on success or negative error code
*/
int wq_crc_stats_enable(struct rwnx_vif *vif, u8 enable)
{
	struct dbg_crc_stats_req req;

	if (vif == NULL) {
		return -EINVAL;
	}

	req = (struct dbg_crc_stats_req){
		.vif_idx = vif->vif_index,
		.type = !!enable,
	};

	return RWNX_INFO_NOTIFY_SET(vif->rwnx_hw, MSG_TYPE_CRC_STATS, req);
}

/**
* @brief           get phy signal statistics from firmware
* @note            stats would be clear when call this function
* @param[in]       rwnx_vif: VIF information
* @param[in]       enable: enable/disable statistics
* @return          returns 0 on success or negative error code
*/
int wq_phy_signal_stats_enable(struct rwnx_vif *vif, u8 enable)
{
	struct dbg_phy_signal_stats_req req;

	if (vif == NULL) {
		return -EINVAL;
	}

	req = (struct dbg_phy_signal_stats_req){
		.vif_idx = vif->vif_index,
		.type = !!enable,
	};
	return RWNX_INFO_NOTIFY_SET(vif->rwnx_hw, MSG_TYPE_PHY_SIGNAL_STATS,
				    req);
}

/**
* @brief           get phy signal statistics from firmware
* @note
* @param[in]       rwnx_vif: VIF information
* @param[out]      stats: phy signal statistics
* @return          returns 0 on success or negative error code
*/
int wq_get_phy_signal_stats(struct rwnx_vif *vif,
			    struct wq_dbg_phy_signal_stats *stats)
{
	struct dbg_phy_signal_stats_req req;

	if (vif == NULL || stats == NULL) {
		return -EINVAL;
	}

	req = (struct dbg_phy_signal_stats_req){
		.vif_idx = vif->vif_index, .type = 2, /* get */
	};
	return RWNX_INFO_NOTIFY_GET(vif->rwnx_hw, MSG_TYPE_PHY_SIGNAL_STATS,
				    req, stats);
}

/**
* @brief           get agc lock statistics from firmware
* @note            stats would be clear when call this function
* @param[in]       rwnx_vif: VIF information
* @param[in]       enable: enable/disable statistics
* @return          returns 0 on success or negative error code
*/
int wq_agc_lock_stats_enable(struct rwnx_vif *vif, u8 enable)
{
	struct dbg_agc_lock_stats_req req;

	if (vif == NULL) {
		return -EINVAL;
	}

	req = (struct dbg_agc_lock_stats_req){
		.vif_idx = vif->vif_index,
		.type = !!enable,
	};
	return RWNX_INFO_NOTIFY_SET(vif->rwnx_hw, MSG_TYPE_AGC_LOCK_STATS, req);
}

/**
* @brief           get agc lock statistics from firmware
* @note
* @param[in]       rwnx_vif: VIF information
* @param[out]      stats: agc lock statistics
* @return          returns 0 on success or negative error code
*/
int wq_get_agc_lock_stats(struct rwnx_vif *vif,
			  struct wq_dbg_agc_lock_stats *stats)
{
	struct dbg_agc_lock_stats_req req;

	if (vif == NULL || stats == NULL) {
		return -EINVAL;
	}

	req = (struct dbg_agc_lock_stats_req){
		.vif_idx = vif->vif_index, .type = 2, /* get */
	};
	return RWNX_INFO_NOTIFY_GET(vif->rwnx_hw, MSG_TYPE_AGC_LOCK_STATS, req,
				    stats);
}

/**
* @brief           wq get freq dc state
* @note
* @param[in]       rwnx_hw: RWNX driver main data
* @param[in]       mac_id: mac core
* @param[out]      freq_dc_state: dbg_freq_dc_state
* @return          returns 0 on success or negative error code
*/
int wq_get_freq_dc_state(struct rwnx_hw *rwnx_hw, uint8_t mac_id,
			 struct dbg_freq_dc_state *freq_dc_state)
{
	if (rwnx_hw == NULL || freq_dc_state == NULL) {
		return -EINVAL;
	}

	return RWNX_INFO_NOTIFY_GET(rwnx_hw, MSG_TYPE_FREQ_DC_STATE, mac_id,
				    freq_dc_state);
}

/**
* @brief           set edca paramas
* @note
* @param[in]       rwnx_vif: VIF information
* @param[in]       ac: AC identifier
* @param[in]       txop: Maximum burst time in units of 32 usecs, 0 meaning disabled
* @param[in]       cwmin: Minimum contention window [a value of the form 2^n-1 in the range [1..32767]
* @param[in]       cwmax: Maxinum contention window [a value of the form 2^n-1 in the range [1..32767]
* @param[in]       aifs: Arbitration interframe space [0..255]
* @return          returns 0 on success or negative error code
*/
int wq_set_edca_params(struct rwnx_vif *vif, u8 ac, u8 aifs, u16 cwmin,
		       u16 cwmax, u16 txop)
{
	u32 param;
	int ret;

	/* Store queue information in general structure */
	param = (u32)(aifs << 0);
	param |= (u32)(cwmin << 4);
	param |= (u32)(cwmax << 8);
	param |= (u32)(txop) << 12;

	ret = rwnx_send_set_edca(vif->rwnx_hw, ac, param, false,
				 vif->vif_index);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR, "rwnx_send_set_edca failed!\n");
		return ret;
	}

	return 0;
}

/**
* @brief           send custom cmd to firmware
* @note            there is no cfm info
* @param[in]       rwnx_vif: VIF information
* @param[in]       cmd: cmd sent to fw
* @param[in]       info: info sent to fw
* @return          returns 0 on success or negative error code
*/
int wq_send_fw_cmd(struct rwnx_vif *vif, u8 cmd, u8 info)
{
	struct {
		u8 cmd;
		u8 info;
	} fw_cmd = {
		.cmd = cmd,
		.info = info,
	};

	if (vif == NULL) {
		return -EINVAL;
	}

	return RWNX_INFO_NOTIFY_SET_VIF(vif->rwnx_hw, MSG_TYPE_SEND_CUSTOM_CMD,
					vif->vif_index, fw_cmd);
}
