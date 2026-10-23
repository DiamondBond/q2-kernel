#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/if_arp.h>
#include <net/sock.h>
#include "wq_packet_type.h"
#include "wq_pkt_classify.h"
#include "wq_log.h"
#include "rwnx_defs.h"
#include <linux/tcp.h>
#include <linux/icmp.h>

static char *wq_dhcp_pkt_to_str(uint8_t type)
{
	char *str;

	switch (type) {
	case DHCPDISCOVER:
		str = "DHCPDISCOVER";
		break;
	case DHCPOFFER:
		str = "DHCPOFFER";
		break;
	case DHCPREQUEST:
		str = "DHCPREQUEST";
		break;
	case DHCPDECLINE:
		str = "DHCPDECLINE";
		break;
	case DHCPACK:
		str = "DHCPACK";
		break;
	case DHCPNACK:
		str = "DHCPNACK";
		break;
	case DHCPRELEASE:
		str = "DHCPRELEASE";
		break;
	default:
		str = "NONE";
		break;
	}

	return str;
}

static char *wq_wps_msg_to_str(uint8_t type)
{
	char *str = "";

	switch (type) {
	case WPS_M1:
		str = "WPS_M1";
		break;
	case WPS_M2:
		str = "WPS_M2";
		break;
	case WPS_M3:
		str = "WPS_M3";
		break;
	case WPS_M4:
		str = "WPS_M4";
		break;
	case WPS_M5:
		str = "WPS_M5";
		break;
	case WPS_M6:
		str = "WPS_M6";
		break;
	case WPS_M7:
		str = "WPS_M7";
		break;
	case WPS_M8:
		str = "WPS_M8";
		break;
	}

	return str;
}

static char *wq_eap_type_to_str(uint8_t type)
{
	char *str = "";

	switch (type) {
	case EAP_TYPE_NOTIFICATION:
		str = "Notification";
		break;
	case EAP_TYPE_NAK:
		str = "NAK";
		break;
	case EAP_TYPE_MD5:
		str = "EAP-MD5";
		break;
	case EAP_TYPE_OTP:
		str = "EAP-OTP";
		break;
	case EAP_TYPE_GTC:
		str = "EAP-GTC";
		break;
	case EAP_TYPE_TLS:
		str = "EAP-TLS";
		break;
	case EAP_TYPE_LEAP:
		str = "EAP-LEAP";
		break;
	case EAP_TYPE_SIM:
		str = "EAP-SIM";
		break;
	case EAP_TYPE_TTLS:
		str = "EAP-TTLS";
		break;
	case EAP_TYPE_AKA:
		str = "EAP-AKA";
		break;
	case EAP_TYPE_PEAP:
		str = "EAP-PEAP";
		break;
	case EAP_TYPE_MSCHAPV2:
		str = "EAP-MSCHAPV2";
		break;
	case EAP_TYPE_TLV:
		str = "EAP-TLV";
		break;
	case EAP_TYPE_TNC:
		str = "EAP-TNC";
		break;
	case EAP_TYPE_FAST:
		str = "EAP-FAST";
		break;
	case EAP_TYPE_PAX:
		str = "EAP-PAX";
		break;
	case EAP_TYPE_PSK:
		str = "EAP-PSK";
		break;
	case EAP_TYPE_SAKE:
		str = "EAP-SAKE";
		break;
	case EAP_TYPE_IKEV2:
		str = "EAP-IKEV2";
		break;
	case EAP_TYPE_AKA_PRIME:
		str = "EAP-AKA-PRIME";
		break;
	case EAP_TYPE_GPSK:
		str = "EAP-GPSK";
		break;
	case EAP_TYPE_PWD:
		str = "EAP-PWD";
		break;
	case EAP_TYPE_EKE:
		str = "EAP-EKE";
		break;
	}

	return str;
}

static bool wq_check_if_dhcp_pkt(struct iphdr *ip_hdr)
{
	struct udphdr *udp;
	uint8_t *udp_hdr_ptr = (uint8_t *)ip_hdr + (ip_hdr->ihl << 2);

	udp = (struct udphdr *)udp_hdr_ptr;

	if ((htons(udp->source) == DHCP_SERVER_PORT &&
	     htons(udp->dest) == DHCP_CLIENT_PORT) ||
	    (htons(udp->source) == DHCP_CLIENT_PORT &&
	     htons(udp->dest) == DHCP_SERVER_PORT)) {
		return true;
	} else {
		return false;
	}
}

static inline char *wq_dhcp_pkt_classify(uint8_t *dhcp_hdr_ptr, int udp_len)
{
	int i = 0;
	dhcp_packet_t *dhcp = (dhcp_packet_t *)dhcp_hdr_ptr;
	char *option = (char *)dhcp->options;
	int option_length =
		udp_len - sizeof(struct udphdr) - sizeof(dhcp_packet_t);

	/* Validate the option length */
	if (option_length < 0 || option_length > DHCP_OPTIONS_LENGTH)
		return NULL;

	while (i < option_length) {
		uint8_t type = option[i];
		uint8_t length = option[i + 1];

		if (type == DHCP_OPTION_MESSAGE_TYPE && length == 1)
			return wq_dhcp_pkt_to_str(option[i + 2]);

		i += (2 + length);
	}

	return NULL;
}

static char *wq_parse_extended_type(uint8_t *data, int eap_len)
{
	int exp_vendor;
	uint32_t exp_type;

	exp_vendor = WQ_GET_BE24(data);
	data += 3;
	exp_type = WQ_GET_BE32(data);
	data += 4;

	if (exp_vendor != EAP_VENDOR_WFA || exp_type != 1)
		return NULL;

	if (*data == WSC_Start)
		return "Start";
	else if (*data == WSC_Done)
		return "Done";
	else if (*data == WSC_ACK)
		return "ACK";
	else if (*data == WSC_NACK)
		return "NACK";
	else if (*data == WSC_MSG) {
		int i = 0;

		/* skip the opcode and flags offset */
		data += 2;
		/* skip the EAP Vendor, EXP Type, opcode size and flags */
		eap_len -= 9;

		while (i < eap_len) {
			uint16_t type, length;

			type = WQ_GET_BE16(&data[i]);
			i += sizeof(uint16_t);
			length = WQ_GET_BE16(&data[i]);
			i += sizeof(uint16_t);

			if (type == ATTR_MSG_TYPE) {
				return wq_wps_msg_to_str(data[i]);
			}

			/* skip the TLV size */
			i += length;
		}
	}

	return NULL;
}

static inline int wq_eapol_pkt_classify(struct rwnx_hw *rwnx_hw, uint8_t *eapol,
					int eapol_len, uint8_t *str,
					int str_size)
{
	struct ieee802_1x_hdr *ieee80211_1x = (struct ieee802_1x_hdr *)eapol;
	int eapol_m4_flag = 0;
	int len = 0;

	/* Currently, we only focus on EAPOL start and WPA handshake */
	if (ieee80211_1x->type == IEEE802_1X_TYPE_EAPOL_START)
		scnprintf(str, str_size, "EAPOL-START");
	else if (ieee80211_1x->type == IEEE802_1X_TYPE_EAP_PACKET) {
		struct eap_hdr *eap = (struct eap_hdr *)(ieee80211_1x + 1);
		uint8_t *data = (uint8_t *)(eap + 1);
		uint8_t type = *data++;
		int eap_len = eapol_len - sizeof(*eap);

		if (eap->code == EAP_CODE_REQUEST) {
			len = scnprintf(str, str_size, "EAP Request");
			if (type == EAP_TYPE_IDENTITY)
				len += scnprintf(&str[len], str_size - len,
						 " Identity");
			else if (type == EAP_TYPE_EXPANDED) {
				char *wps_str;

				len += scnprintf(&str[len], str_size - len,
						 " Extended Type");
				eap_len--;
				wps_str = wq_parse_extended_type(data, eap_len);
				if (wps_str) {
					len += scnprintf(&str[len],
							 str_size - len,
							 " - %s", wps_str);
				}
			} else {
				char *eap_type = wq_eap_type_to_str(type);

				if (eap_type) {
					len += scnprintf(&str[len],
							 str_size - len,
							 " - %s", eap_type);
				}
			}
		} else if (eap->code == EAP_CODE_RESPONSE) {
			len = scnprintf(str, str_size, "EAP Response");
			if (type == EAP_TYPE_IDENTITY)
				len += scnprintf(&str[len], str_size - len,
						 " Identity");
			else if (type == EAP_TYPE_EXPANDED) {
				char *wps_str;

				len += scnprintf(&str[len], str_size - len,
						 " Extended Type");
				eap_len--;
				wps_str = wq_parse_extended_type(data, eap_len);
				if (wps_str) {
					len += scnprintf(&str[len],
							 str_size - len,
							 " - %s", wps_str);
				}
			} else {
				char *eap_type = wq_eap_type_to_str(type);

				if (eap_type) {
					len += scnprintf(&str[len],
							 str_size - len,
							 " - %s", eap_type);
				}
			}
		} else if (eap->code == EAP_CODE_SUCCESS)
			len = scnprintf(str, str_size, "EAP Success");
		else if (eap->code == EAP_CODE_FAILURE)
			len = scnprintf(str, str_size, "EAP Failure");
		else if (eap->code == EAP_CODE_INITIATE)
			len = scnprintf(str, str_size, "EAP Initiate");
		else if (eap->code == EAP_CODE_FINISH)
			len = scnprintf(str, str_size, "EAP Finish");
	} else if (ieee80211_1x->type == IEEE802_1X_TYPE_EAPOL_KEY) {
		struct wpa_eapol_key *key =
			(struct wpa_eapol_key *)(ieee80211_1x + 1);
		uint16_t key_info;

		if (key->type == EAPOL_KEY_TYPE_WPA)
			len = scnprintf(str, str_size, "WPA ");
		else if (key->type == EAPOL_KEY_TYPE_RSN)
			len = scnprintf(str, str_size, "WPA2/RSN ");

		key_info = be16_to_cpu(*((uint16_t *)&key->key_info));

		/* pairwise key */
		if (key_info & WPA_KEY_INFO_KEY_TYPE) {
			if ((key_info & WPA_KEY_INFO_ACK) &&
			    !(key_info & WPA_KEY_INFO_MIC) &&
			    !(key_info & WPA_KEY_INFO_INSTALL)) {
				len += scnprintf(&str[len], str_size - len,
						 "EAPOL-Key msg 1/4");
			} else if ((key_info & WPA_KEY_INFO_MIC) &&
				   !(key_info & WPA_KEY_INFO_ACK) &&
				   !(key_info & WPA_KEY_INFO_INSTALL)) {
				if (be16_to_cpu(ieee80211_1x->length) > 100)
					len += scnprintf(&str[len],
							 str_size - len,
							 "EAPOL-Key msg 2/4");
				else {
					len += scnprintf(&str[len],
							 str_size - len,
							 "EAPOL-Key msg 4/4");
					eapol_m4_flag = 1;
				}
			} else if ((key_info & WPA_KEY_INFO_MIC) &&
				   (key_info & WPA_KEY_INFO_ACK) &&
				   (key_info & WPA_KEY_INFO_INSTALL)) {
				len += scnprintf(&str[len], str_size - len,
						 "EAPOL-Key msg 3/4");
			}
		} else {
			if (key_info & WPA_KEY_INFO_ACK)
				len += scnprintf(&str[len], str_size - len,
						 "EAPOL-Key msg 1/2");
			else
				len += scnprintf(&str[len], str_size - len,
						 "EAPOL-Key msg 2/2");
		}
	}

	str[len] = '\0';
	return eapol_m4_flag;
}

u16 wq_pkt_classify(struct sk_buff *skb, int tx, bool has_eth_hdr)
{
	const char *dir = tx ? "TX" : "RX";
	struct ethhdr *eth;
	int packet_len = skb->len;
	struct rwnx_vif *rwnx_vif = netdev_priv(skb->dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	u16 wq_pkt_cls = 0;

	eth = (struct ethhdr *)skb->data;
	if (!has_eth_hdr) {
		--eth;
		packet_len += sizeof(*eth);
	}

	if (ntohs(eth->h_proto) == ETH_P_IP) {
		struct iphdr *ip_hdr = (struct iphdr *)(eth + 1);

		wq_pkt_cls |= BIT(WQ_PKT_CLS_IP);
		if (ip_hdr->protocol == IPPROTO_TCP) {
			struct tcphdr *tcp_header =
				(struct tcphdr *)(ip_hdr + 1);
			wq_pkt_cls |= BIT(WQ_PKT_CLS_TCP);
			if (tx && tcp_header->ack == 1 &&
			    (packet_len <= sizeof(struct tcphdr) +
						   sizeof(struct iphdr) +
						   sizeof(struct ethhdr) +
						   TCP_ACK_PROTO_LEN_RESV)) {
				wq_pkt_cls |= BIT(WQ_PKT_CLS_TCP_ACK);
			}
		} else if (ip_hdr->protocol == IPPROTO_UDP) {
			struct udphdr *udp;
			uint16_t udp_len;
			char *str = NULL;
			uint8_t *udp_hdr_ptr =
				(uint8_t *)ip_hdr + (ip_hdr->ihl << 2);
			udp = (struct udphdr *)udp_hdr_ptr;
			udp_len = ntohs(udp->len);

			// iperf UDP connection packet size is 4
			if (udp_len == 12) {
				uint32_t msg = *(uint32_t *)(udp_hdr_ptr + sizeof(struct udphdr));

				if (msg == 123456789) {
					str = "connect req";
				}
				else if (msg == 987654321) {
					str = "connect reply";
				}

				wq_pkt_cls |= BIT(WQ_PKT_CLS_IPERF_UDP_SETUP);

				WQ_DBG(DM_TX, DL_WRN,
					"%s iperf UDP [%s][D %pM][S %pM], len: %d\n",
					dir, ((str != NULL) ? str : "NONE"),
					eth->h_dest, eth->h_source, packet_len);
			}

			wq_pkt_cls |= BIT(WQ_PKT_CLS_UDP);
			if (wq_check_if_dhcp_pkt(ip_hdr)) {
				uint8_t *dhcp_hdr_ptr;

				dhcp_hdr_ptr =
					udp_hdr_ptr + sizeof(struct udphdr);

				wq_pkt_cls |= BIT(WQ_PKT_CLS_DHCP);
				str = wq_dhcp_pkt_classify(dhcp_hdr_ptr,
							   udp_len);
				WQ_DBG(DM_TX, DL_WRN,
				       "%s DHCP packet [%s][D %pM][S %pM], len: %d\n",
				       dir, ((str != NULL) ? str : "NONE"),
				       eth->h_dest, eth->h_source, packet_len);
			}
		} else if (ip_hdr->protocol == IPPROTO_ICMP) {
			struct icmphdr *icmph = (struct icmphdr *)(ip_hdr + 1);
			wq_pkt_cls |= BIT(WQ_PKT_CLS_ICMP);
			if (tx) {
				WQ_DBG(DM_TX, DL_WRN,
				       "%s ICMP packet [D %pM][S %pM], len: %d, seq: 0x%04x\n",
				       dir, eth->h_dest, eth->h_source,
				       packet_len,
				       htons(icmph->un.echo.sequence));
			} else {
				if (icmph->type == 0) {
					WQ_DBG(DM_RX, DL_WRN,
					       "%s ICMP reply packet [D %pM][S %pM], len: %d, seq: 0x%04x\n",
					       dir, eth->h_dest, eth->h_source,
					       packet_len,
					       htons(icmph->un.echo.sequence));
				} else if (icmph->type == 8) {
					WQ_DBG(DM_RX, DL_WRN,
					       "%s ICMP request packet [D %pM][S %pM], len: %d, seq: 0x%04x\n",
					       dir, eth->h_dest, eth->h_source,
					       packet_len,
					       htons(icmph->un.echo.sequence));
				}
			}
		}
	} else if (ntohs(eth->h_proto) == ETH_P_PAE) {
		char str[64];
		uint8_t *eapol = (uint8_t *)(eth + 1);
		int eapol_len = packet_len - sizeof(struct ethhdr);

		wq_pkt_cls |= BIT(WQ_PKT_CLS_EAPOL);
		if (wq_eapol_pkt_classify(rwnx_hw, eapol, eapol_len, str,
					  sizeof(str))) {
			wq_pkt_cls |= BIT(WQ_PKT_CLS_EAPOL_M4);
			if (tx) {
#ifdef CONFIG_HML
				rwnx_hw->key_add_params.m4_sended = 1;
#endif
				rwnx_hw->key_add_params.m4_ack_done = 0;
			}
		}

		WQ_DBG(DM_TX, DL_WRN,
		       "%s EAPOL packet [%s] [D %pM][S %pM], len: %d\n", dir,
		       str, eth->h_dest, eth->h_source, packet_len);
	} else if (ntohs(eth->h_proto) == ETH_P_ARP) {
		wq_pkt_cls |= BIT(WQ_PKT_CLS_ARP);
//		if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION) {
		{
			struct arphdr *arphdr = (struct arphdr *)(eth + 1);
			char *str = "";

			if (be16_to_cpu(arphdr->ar_op) == ARPOP_REQUEST) {
				str = "REQUEST";
				wq_pkt_cls |= BIT(WQ_PKT_CLS_ARP_REQ);
			} else if (be16_to_cpu(arphdr->ar_op) == ARPOP_REPLY) {
				str = "REPLY";
				wq_pkt_cls |= BIT(WQ_PKT_CLS_ARP_REPLY);
			}

			WQ_DBG(DM_TX, DL_WRN,
			       "%s ARP %s [D %pM][S %pM], len: %d\n", dir, str,
			       eth->h_dest, eth->h_source, packet_len);
		}
	}
	return wq_pkt_cls;
}
