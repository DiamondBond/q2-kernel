#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "pktlog.h"

//global varibales
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
		    (frame_ctrl[29] == 0x9)) {
			//ACTION P2P
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
		if ((frame_ctrl[0] & 0xf0) == 0x80) {
			//QoS frame
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
	return "RESERVED";
}

void wq_packet_dump_debug(WIFI_DBG_PKT *dpkt)
{
	WIFI_DBG_PKT *pkt = dpkt;
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
	int rxrssi = 0;
	int snr = 0;

	uint8_t non_ht_rate_tx[] = { 2,	 4,  11, 22, 12,  18, 24,
				     36, 48, 72, 96, 108, 0 };
	uint8_t non_ht_rate_rx[] = { 2,	 4,  11, 22, 0,	  0,  0,  0,
				     96, 48, 24, 12, 108, 72, 36, 18 };

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
			printf("unknown rx_vec format_mod: %u",
			       rx_vec_1.format_mod);
			break;
		}

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
			printf("cannot get bandwidth");
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

		switch ((uint32_t)rx_vec_1.rssi_leg) {
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
				printf("cannot get bandwidth in non-ht");
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
			printf("cannot get bandwidth");
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
			printf("cannot get format");
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
			printf("cannot get bandwidth");
			break;
		}

		if (txstatus.descriptor_done_hwtx &&
		    txstatus.frm_successful_tx) {
			//printk("tx ok !");
		} else {
			if (txstatus.retry_limit_reached) {
				printf("[auto]tx fail: %s retry limit reached num=%d\n",
				       txstatus.num_rts_retries ? "rts" :
								  "mpdu",
				       txstatus.num_rts_retries ?
					       txstatus.num_rts_retries :
					       txstatus.num_mpdu_retries);
			} else if (txstatus.lifetime_expired) {
				printf("[auto]tx fail: life time expired\n");
			} else if (txstatus.baFrameReceived) {
				printf("[auto]tx fail: BlockAck received indication\n");
			} else {
				printf("[auto]tx fail: fw flush\n");
			}
		};
	}

	if ((uint8_t)(gv_pktdump_sn + 1) != (pkt->sn)) {
		printf("[auto]warning : pktdump loss, last=0x%x now=0x%x\n",
		       gv_pktdump_sn, pkt->sn);
	}

	gv_pktdump_sn = pkt->sn;

	printf("[auto]%s%d %d:%x %s%d%s BW%u(%u) %ddBm %ddB %02x%02x(%s%c%c%c%c) %02x%02x %02x%02x%02x%02x%02x%02x %02x%02x%02x%02x%02x%02x .. %02x%02x %02x%02x\n",
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
	       pkt->pkt_data[24], pkt->pkt_data[25]);
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("Usage: ./pktlog bin_file\n");
		return 0;
	}
	// open the file using "rb" read binary mode
	FILE *p = fopen(argv[1], "rb");

	if (p == NULL) {
		printf("fail to open file\n");
		return 0;
	}

	// buffer used for receiving the read data
	char buffer[sizeof(WIFI_DBG_PKT)] = { 0 };

	// sizeof(char) : basic unit byte lengte to bt read
	// sizeof(buffer) : number of basic units read
	// p : document pointer
	while (!feof(p)) {
		memset(buffer, 0, sizeof(buffer));
		fread(buffer, sizeof(char), sizeof(buffer), p);
		WIFI_DBG_PKT *pkt = (WIFI_DBG_PKT *)buffer;
		wq_packet_dump_debug(pkt);
	}

	fclose(p);
	return 0;
}
