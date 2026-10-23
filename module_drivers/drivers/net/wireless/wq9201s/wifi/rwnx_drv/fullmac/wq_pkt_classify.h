#ifndef WQ_PACKET_CLASSIFY_H
#define WQ_PACKET_CLASSIFY_H

#include <linux/skbuff.h>

/* define the TCP ACK protocol resv len. unit Byte */
#define TCP_ACK_PROTO_LEN_RESV 20

u16 wq_pkt_classify(struct sk_buff *skb, int tx, bool has_eth_hdr);

#endif
