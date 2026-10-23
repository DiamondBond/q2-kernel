/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2008 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: stable/12/sys/net80211/ieee80211_ht.h 326272 2017-11-27 15:23:17Z pfg $
 */
#ifndef _NET80211_IEEE80211_HT_H_
#define _NET80211_IEEE80211_HT_H_

/*
 * 802.11n protocol implementation definitions.
 */

#define RXA_OFF_INVALID     0xFFFF

#define IEEE80211_AGGR_BAWMAX 256 /* max block ack window size */
/* threshold for aging overlapping non-HT bss */
#define IEEE80211_NONHT_PRESENT_AGE msecs_to_ticks(60 * 1000)
typedef uint16_t ieee80211_seq;

#define IEEE80211_AGGR_IMMEDIATE 0x0001 /* BA policy */
#define IEEE80211_AGGR_XCHGPEND 0x0002 /* ADDBA response pending */
#define IEEE80211_AGGR_RUNNING 0x0004 /* ADDBA response received */
#define IEEE80211_AGGR_SETUP 0x0008 /* deferred state setup */
#define IEEE80211_AGGR_NAK 0x0010 /* peer NAK'd ADDBA request */
#define IEEE80211_AGGR_BARPEND 0x0020 /* BAR response pending */
#define IEEE80211_AGGR_FIRST_RXED 0x0040 /* first rx ampdu received */

#define IEEE80211_AGGR_BITS "\20\1IMMEDIATE\2XCHGPEND\3RUNNING\4SETUP\5NAK"

struct ieee80211_rx_ampdu {
	struct rwnx_hw *rwnx_hw;
	int rxa_flags;
	int rxa_qbytes; /* data queued (bytes) */
	short rxa_qframes; /* data queued (frames) */
	ieee80211_seq rxa_seqstart;
	ieee80211_seq rxa_start; /* start of current BA window */
	uint16_t rxa_wnd; /* BA window size */
	unsigned long rxa_age; /* age of oldest frame in window */
	int rxa_nframes; /* frames since ADDBA */
	struct sk_buff *rxa_m[IEEE80211_AGGR_BAWMAX];
	uint32_t rxa_m_msdu_seq[IEEE80211_AGGR_BAWMAX]; /* msdu seq in a-msdu*/
	uint8_t rxa_msdu_qframe[IEEE80211_AGGR_BAWMAX]; /* data queued (frames) in a-msdu */
	uint16_t rxa_off;

	struct rwnx_sta *ni;
	uint8_t tid;

	struct timer_list rx_reorder_timer;
	ieee80211_seq rx_reorder_pending_seq;
	//void		*rxa_private;
	//uint64_t	rxa_pad[3];
	uint16_t baseqctl;
	spinlock_t rxa_qframes_lock;
	struct work_struct flush_task_work;
	u32 seq_s_cnt;
	u32 seq_f_null_cnt;
	u32 seq_f_null_flush_cnt;
	u32 seq_f_noend_cnt;
	u32 seq_f_noend_flush_cnt;
	u32 amsdu_s_cnt;
	u32 amsdu_f_diff_cnt;
	u32 amsdu_f_end_cnt;
	u32 amsdu_f_noend_cnt;
	u32 amsdu_f_noend_flush_cnt;
	u32 timer_cnt;
	u32 timeout_flush_cnt;
	u32 in_range_cnt;
	u32 out_range_cnt;
	u32 timeout_cnt;
	u32 bar_cnt;
	u32 bar_seq_cnt;
};

enum bam_ba_type { BA_RESPONDER = 0, BA_ORIGINATOR, BA_DEV_NONE };

struct bam_evt_addba_parm {
	uint8_t dev_type;
	uint8_t sta_idx;
	uint8_t tid;
	uint8_t amsdu;
	uint16_t buffer_size;
	uint16_t ssn;
} __attribute__((packed));

struct bam_evt_delba_parm {
	uint8_t dev_type;
	uint8_t sta_idx;
	uint8_t tid;
} __attribute__((packed));

struct bam_evt_bar_parm {
	uint8_t sta_idx;
	uint8_t tid;
	uint16_t ssn;
	uint8_t fctrl_retry;
} __attribute__((packed));

void ieee80211_ht_init(void);
void ieee80211_ampdu_reorder_dump_info(struct rwnx_hw *rwnx_hw,
				       struct ieee80211_rx_ampdu *rap,
				       bool need_lock_flag, bool reset_flag,
				       uint16_t sta_idx, uint8_t tid);
void ieee80211_ampdu_age_msecs_set(unsigned int ampdu_age_msecs);
unsigned int ieee80211_ampdu_age_msecs_get(void);
int ieee80211_ampdu_reorder(struct rwnx_hw *rwnx_hw, uint16_t sta_idx,
			    uint8_t tid, uint16_t rxseq, uint8_t msdu_seq,
			    uint8_t msdu_seq_end, struct sk_buff *m);
void ht_handle_bam_event(struct rwnx_hw *rwnx_hw, uint8_t evt_id,
			 void *bam_evt_parm);
void ieee80211_recv_bar(struct rwnx_hw *rwnx_hw, uint8_t sta_idx, uint8_t tid,
			uint16_t rxseq, uint8_t fctrl_retry);
#endif /* _NET80211_IEEE80211_HT_H_ */
