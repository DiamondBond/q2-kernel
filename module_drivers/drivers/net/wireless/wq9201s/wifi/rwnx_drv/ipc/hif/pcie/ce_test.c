/** @file ce_test.c
 *
 *  @brief This file contains CopyEngine Test related functions.
 *
 * Copyright (C) 2016-2023, WuQi Ltd.
 *
 * This software file (the "File") is distributed by WuQi Ltd.
 * Under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 *
 */

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/kthread.h>
#include <linux/pci.h>
#include <linux/module.h>

#include "pcie.h"

#include "wq_crc.h" /* FIXME: use kernel APIs */

#include "wifi_ahb_reg.h"
#include "host_reg_base.h"

char *test_run_dir = NULL;
module_param(test_run_dir, charp, 0);
MODULE_PARM_DESC(test_run_dir, "Targ2Host/Host2Targ");

#define TEST_CE_DEMO TEST_CE_TPUT

#define TEST_CE_SINGLE 0
#define TEST_CE_TX 1
#define TEST_CE_RX 2
#define TEST_CE_TPUT 3
#define TEST_CE_STRESS 4

#if (TEST_CE_DEMO == TEST_CE_SINGLE) || (TEST_CE_DEMO == TEST_CE_TX) ||        \
	(TEST_CE_DEMO == TEST_CE_RX)

#define TEST_CE_DMA_ATTR_FLAGS 0 // 0, CE_ATTR_FLAG_DATA_WORD_ORDER
#define TEST_CE_DMA_SRC_SZ_MAX 2048 // 1~1048575
#define TEST_CE_DMA_FIFO_DEPTH 1024 // 2~8192
#define TEST_CE_DMA_DESC_INT_ON_OFF                                            \
	CE_DESC_FLAG_INT_EB // 0, CE_DESC_FLAG_INT_EB
#define TEST_CE_DMA_DESC_GATHER_ON_OFF 0 // 0, CE_DESC_FLAG_GATHER_EB
#define TEST_CE_DMA_DESC_CRC_ON_OFF 0 // 0, CE_DESC_FLAG_CRC_EB
#define TEST_CE_DMA_DESC_CRC_MODE                                              \
	CE_DESC_FLAG_CRC_MODE_8 // CE_DESC_FLAG_CRC_MODE_(8, 16, 24, 32)
#define TEST_CE_DMA_ALIGN_OFFSET 0 // 0, 1, 2, 3

#endif

#ifndef STR
#define STR(x) XSTR(x)
#endif
#ifndef XSTR
#define XSTR(x) #x
#endif

#define TEST_CE_LOG(fmt, args...)                                              \
	(void)printk(KERN_ALERT "%s, %d, " fmt, __func__, __LINE__, ##args)
#define TEST_CE_ASSERT(x)                                                      \
	do {                                                                   \
		if (!__builtin_expect(x, 1)) {                                 \
			TEST_CE_LOG("assert\n");                               \
			__builtin_trap();                                      \
		}                                                              \
	} while (0)

static struct task_struct *test_ce_task_id;

#if TEST_CE_DEMO == TEST_CE_SINGLE
#define TEST_CE_DMA_SEND_CODE
#define TEST_CE_DMA_RECV_CODE
#elif TEST_CE_DEMO == TEST_CE_TX
#define TEST_CE_DMA_SEND_CODE
#elif TEST_CE_DEMO == TEST_CE_RX
#define TEST_CE_DMA_RECV_CODE
#endif

#ifdef TEST_CE_DMA_SEND_CODE

static u32 test_ce_dma_send_nbytes[CE_CHN_MAX] = { 5,  6,  7,  8,  9,  10,
						   11, 12, 13, 14, 15, 16 };

static volatile bool test_ce_dma_send_done[CE_CHN_MAX];

static void test_ce_dma_send_next(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	const wq_ce_attr_t *attr;
	u32 src_sz_max;
	u32 *nbytes_ref;
	u32 nbytes;
	u32 gather_flag;
#if TEST_CE_DMA_DESC_GATHER_ON_OFF == CE_DESC_FLAG_GATHER_EB
	u16 gather_count;
	u32 gather_nbytes;
	u32 remain;
#endif
	u32 data_size;
	struct sk_buff *skb;
	u32 i;
	u8 *data;
	int ret;
	dma_addr_t phys_addr;

	attr = wq_ce_attr_get(wq_pcie, chn);
	src_sz_max = attr->src_sz_max;
	TEST_CE_ASSERT(src_sz_max > sizeof(u32));

	nbytes_ref = &test_ce_dma_send_nbytes[chn];
	nbytes = *nbytes_ref;
	TEST_CE_ASSERT(nbytes > sizeof(u32));

	gather_flag = TEST_CE_DMA_DESC_GATHER_ON_OFF;

#if TEST_CE_DMA_DESC_GATHER_ON_OFF == CE_DESC_FLAG_GATHER_EB
	(void)wq_ce_send_gather_statistics_get(wq_pcie, chn, &gather_count,
					       &gather_nbytes);

	if (gather_count + 1 == attr->src_depth - 1)
		gather_flag = 0;

	remain = src_sz_max - gather_nbytes;
	TEST_CE_ASSERT(remain > sizeof(u32));

	if (remain <= nbytes || remain - nbytes <= sizeof(u32)) {
		nbytes = remain;
		gather_flag = 0;
	}
#endif

	data_size = TEST_CE_DMA_ALIGN_OFFSET + nbytes;

	skb = dev_alloc_skb(data_size);
	TEST_CE_ASSERT(skb != NULL && (uintptr_t)skb->data % 4 == 0);

	data = skb->data + TEST_CE_DMA_ALIGN_OFFSET;
	(void)memcpy(data, &nbytes, sizeof(u32));

	for (i = sizeof(u32); i < nbytes; ++i)
		data[i] = (u8)((nbytes + i) % 256);
	(void)skb_put(skb, data_size);

	phys_addr = dma_map_single(wq_pcie->core.dev, skb->data, skb->len,
				   DMA_TO_DEVICE);
	ret = dma_mapping_error(wq_pcie->core.dev, phys_addr);
	TEST_CE_ASSERT(0 == ret);
	dma_sync_single_for_device(wq_pcie->core.dev, phys_addr, skb->len,
				   DMA_TO_DEVICE);

	ret = wq_ce_send(wq_pcie, chn, skb,
			 phys_addr + TEST_CE_DMA_ALIGN_OFFSET, nbytes,
			 TEST_CE_DMA_DESC_INT_ON_OFF | gather_flag |
				 TEST_CE_DMA_DESC_CRC_ON_OFF |
				 TEST_CE_DMA_DESC_CRC_MODE);
	TEST_CE_ASSERT(0 == ret);

	// src_sz_max=2048, nbytes=5~2048
	*nbytes_ref = (nbytes - sizeof(u32)) % (src_sz_max - sizeof(u32)) +
		      (1 + sizeof(u32));
}

static void test_ce_dma_send_to_use(struct sk_buff *skb)
{
	u8 *data;
	u32 nbytes;
	u32 sub_nbytes;
	u32 i;

	TEST_CE_ASSERT(skb != NULL &&
		       skb->len > TEST_CE_DMA_ALIGN_OFFSET + sizeof(u32));

	data = skb->data + TEST_CE_DMA_ALIGN_OFFSET;
	nbytes = skb->len - TEST_CE_DMA_ALIGN_OFFSET;

	(void)memcpy(&sub_nbytes, data, sizeof(u32));
	TEST_CE_ASSERT(nbytes == sub_nbytes);

	for (i = sizeof(u32); i < nbytes; ++i)
		TEST_CE_ASSERT((u8)((nbytes + i) % 256) == data[i]);

	dev_kfree_skb_any(skb);
}

static void test_ce_dma_send_post(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	int ret;
	struct sk_buff *skb;
	dma_addr_t phys_addr;
	u32 nbytes;

	while (0 ==
	       (ret = wq_ce_send_completed_next(wq_pcie, chn, (void **)&skb,
						&phys_addr, &nbytes, NULL))) {
		TEST_CE_LOG(
			STR(TEST_CE_DEMO) ", chn = %d, send nbytes = %u succeed.\n",
			(int)chn, nbytes);
		dma_unmap_single(wq_pcie->core.dev, phys_addr, skb->len,
				 DMA_TO_DEVICE);
		test_ce_dma_send_to_use(skb);
	}
	TEST_CE_ASSERT(-ENODATA == ret);

	test_ce_dma_send_done[chn] = false;
	(void)wq_ce_irq_unmask(wq_pcie, chn, WQ_CE_CHN_SRC);
}

static void test_ce_dma_send_cb(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	TEST_CE_ASSERT(WQ_CE_SRC_FIFO_DEPTH(wq_pcie, chn) > 0);
	wq_ce_irq_mask(wq_pcie, chn, WQ_CE_CHN_SRC);
	test_ce_dma_send_done[chn] = true;
	(void)wake_up_process(test_ce_task_id);
}

#endif // TEST_CE_DMA_SEND_CODE

#ifdef TEST_CE_DMA_RECV_CODE

static volatile bool test_ce_dma_recv_done[CE_CHN_MAX];

static void test_ce_dma_recv_next(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	u32 nbytes;
	u32 data_size;
	struct sk_buff *skb;
	dma_addr_t phys_addr;
	int ret;

	nbytes = WQ_CE_SRC_SZ_MAX(wq_pcie, chn);

#if TEST_CE_DMA_DESC_CRC_ON_OFF == CE_DESC_FLAG_CRC_EB
	nbytes += ((TEST_CE_DMA_DESC_CRC_MODE & CE_DESC_FLAG_CRC_MODE_MASK) >>
		   CE_DESC_FLAG_CRC_MODE_OFFSET) +
		  1;
#endif

	data_size = TEST_CE_DMA_ALIGN_OFFSET + nbytes;

	skb = dev_alloc_skb(data_size);
	TEST_CE_ASSERT(skb != NULL && (uintptr_t)skb->data % 4 == 0);
	(void)memset(skb->data + TEST_CE_DMA_ALIGN_OFFSET, 0, nbytes);

	phys_addr = dma_map_single(wq_pcie->core.dev, skb->data, data_size,
				   DMA_FROM_DEVICE);
	ret = dma_mapping_error(wq_pcie->core.dev, phys_addr);
	TEST_CE_ASSERT(0 == ret);

	ret = wq_ce_recv(
		wq_pcie, chn, skb, phys_addr + TEST_CE_DMA_ALIGN_OFFSET, nbytes,
		TEST_CE_DMA_DESC_INT_ON_OFF | TEST_CE_DMA_DESC_CRC_ON_OFF |
			TEST_CE_DMA_DESC_CRC_MODE);
	TEST_CE_ASSERT(0 == ret);
}

#if TEST_CE_DMA_DESC_CRC_ON_OFF == CE_DESC_FLAG_CRC_EB
static void test_ce_dma_recv_to_use(struct sk_buff *skb, u32 crc)
#else
static void test_ce_dma_recv_to_use(struct sk_buff *skb)
#endif
{
	u8 *data;
	u32 nbytes;
	u32 sub_nbytes;
	u32 i;

	TEST_CE_ASSERT(skb != NULL &&
		       skb->len > TEST_CE_DMA_ALIGN_OFFSET + (sizeof(u32)));

	data = skb->data + TEST_CE_DMA_ALIGN_OFFSET;
	nbytes = skb->len - TEST_CE_DMA_ALIGN_OFFSET;

#if TEST_CE_DMA_DESC_CRC_ON_OFF == CE_DESC_FLAG_CRC_EB
#if TEST_CE_DMA_DESC_CRC_MODE == CE_DESC_FLAG_CRC_MODE_8
	TEST_CE_ASSERT(getcrc8(data, nbytes) == crc);
#elif TEST_CE_DMA_DESC_CRC_MODE == CE_DESC_FLAG_CRC_MODE_16
	TEST_CE_ASSERT(getcrc16(data, nbytes) == crc);
#elif TEST_CE_DMA_DESC_CRC_MODE == CE_DESC_FLAG_CRC_MODE_24
	TEST_CE_ASSERT(getcrc24(data, nbytes) == crc);
#elif TEST_CE_DMA_DESC_CRC_MODE == CE_DESC_FLAG_CRC_MODE_32
	TEST_CE_ASSERT(getcrc32(data, nbytes) == crc);
#endif
#endif

#if TEST_CE_DMA_DESC_GATHER_ON_OFF == CE_DESC_FLAG_GATHER_EB
	while (nbytes > sizeof(u32) &&
	       ((void)memcpy(&sub_nbytes, data, sizeof(u32)),
		0 < sub_nbytes && sub_nbytes <= nbytes)) {
		for (i = sizeof(u32); i < sub_nbytes; ++i)
			TEST_CE_ASSERT((u8)((sub_nbytes + i) % 256) == data[i]);
		data += sub_nbytes;
		nbytes -= sub_nbytes;
	}
	TEST_CE_ASSERT(0 == nbytes);
#else
	(void)memcpy(&sub_nbytes, data, sizeof(u32));
	TEST_CE_ASSERT(sub_nbytes == nbytes);
	for (i = sizeof(u32); i < nbytes; ++i)
		TEST_CE_ASSERT((u8)((nbytes + i) % 256) == data[i]);
#endif

	dev_kfree_skb_any(skb);
}

#if TEST_CE_DMA_DESC_CRC_ON_OFF == CE_DESC_FLAG_CRC_EB
static u32 test_ce_dma_recv_crc_parse(const u8 *data, u32 *nbytes, u32 flags)
{
	u32 _nbytes;
	u8 crc_byte_width;
	u32 crc;
	u8 *s;
	int i;

	TEST_CE_ASSERT(data != NULL && nbytes != NULL &&
		       (_nbytes = *nbytes) != 0);

	crc_byte_width = ((flags & CE_DESC_FLAG_CRC_MODE_MASK) >>
			  CE_DESC_FLAG_CRC_MODE_OFFSET) +
			 1;
	TEST_CE_ASSERT(_nbytes > crc_byte_width);

	_nbytes -= crc_byte_width;
	data += _nbytes;

	crc = 0;
	s = (u8 *)&crc;

	for (i = 0; i < crc_byte_width; ++i)
		s[i] = data[i];

	*nbytes = _nbytes;
	return crc;
}
#endif

static void test_ce_dma_recv_post(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	int ret;
	struct sk_buff *skb;
	dma_addr_t phys_addr;
	u32 nbytes;
	u16 nentries = 0;
#if TEST_CE_DMA_DESC_CRC_ON_OFF == CE_DESC_FLAG_CRC_EB
	u32 flags;
	u32 crc;
#endif
	u16 i;

#if TEST_CE_DMA_DESC_CRC_ON_OFF == CE_DESC_FLAG_CRC_EB
	while (0 ==
	       (ret = wq_ce_recv_completed_next(wq_pcie, chn, (void **)&skb,
						&phys_addr, &nbytes, &flags))) {
#else
	while (0 ==
	       (ret = wq_ce_recv_completed_next(wq_pcie, chn, (void **)&skb,
						&phys_addr, &nbytes, NULL))) {
#endif
		u32 data_size = TEST_CE_DMA_ALIGN_OFFSET + nbytes;
		TEST_CE_ASSERT(skb != NULL && nbytes > sizeof(u32));

		dma_sync_single_for_cpu(wq_pcie->core.dev, phys_addr, data_size,
					DMA_FROM_DEVICE);
		dma_unmap_single(wq_pcie->core.dev, phys_addr, data_size,
				 DMA_FROM_DEVICE);

#if TEST_CE_DMA_DESC_CRC_ON_OFF == CE_DESC_FLAG_CRC_EB
		crc = test_ce_dma_recv_crc_parse(skb->data, &nbytes, flags);
#endif

		(void)skb_put(skb, TEST_CE_DMA_ALIGN_OFFSET + nbytes);
		TEST_CE_LOG(
			STR(TEST_CE_DEMO) ", chn = %d, recv nbytes = %u succeed.\n",
			(int)chn, nbytes);

#if TEST_CE_DMA_DESC_CRC_ON_OFF == CE_DESC_FLAG_CRC_EB
		test_ce_dma_recv_to_use(skb, crc);
#else
		test_ce_dma_recv_to_use(skb);
#endif
		++nentries;
	}
	TEST_CE_ASSERT(-ENODATA == ret);

	for (i = 0; i < nentries; ++i)
		test_ce_dma_recv_next(wq_pcie, chn);

	test_ce_dma_recv_done[chn] = false;
	(void)wq_ce_irq_unmask(wq_pcie, chn, WQ_CE_CHN_DST);
}

static void test_ce_dma_recv_cb(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	TEST_CE_ASSERT(WQ_CE_DST_FIFO_DEPTH(wq_pcie, chn) > 0);
	(void)wq_ce_irq_mask(wq_pcie, chn, WQ_CE_CHN_DST);
	test_ce_dma_recv_done[chn] = true;
	(void)wake_up_process(test_ce_task_id);
}

#endif // TEST_CE_DMA_RECV_CODE

#if (TEST_CE_DEMO == TEST_CE_SINGLE) || (TEST_CE_DEMO == TEST_CE_TX) ||        \
	(TEST_CE_DEMO == TEST_CE_RX)

#ifdef TEST_CE_DMA_SEND_CODE
static bool test_ce_dma_send_fifo_full[CE_CHN_MAX];
#endif

static void test_ce_dma_init(
	struct wq_pcie *wq_pcie,
	const wq_ce_attr_assign_t test_ce_dma_attr_assign_table[CE_CHN_MAX])
{
	CE_CHN_UUID chn;
	const wq_ce_attr_assign_t *attr_assign;
	int ret;
	const wq_ce_attr_t *attr;
#ifdef TEST_CE_DMA_SEND_CODE
	u16 src_depth;
#endif
#ifdef TEST_CE_DMA_RECV_CODE
	u16 i;
	u16 dst_depth;
#endif

	for (chn = 0; chn < CE_CHN_MAX; ++chn) {
		attr_assign = &test_ce_dma_attr_assign_table[chn];
		if (0 == attr_assign->src_sz_max)
			continue;
		ret = wq_ce_chn_init(wq_pcie, chn, attr_assign);

		TEST_CE_LOG(
			STR(TEST_CE_DEMO) ", wq_ce_chn_init, chn = %d, ret = %d, "
					  "flags = 0x%08x, src_sz_max = %u, src_depth = %d, dst_depth = %d\n",
			(int)chn, ret, attr_assign->flags,
			attr_assign->src_sz_max, (int)attr_assign->src_depth,
			(int)attr_assign->dst_depth);
		TEST_CE_ASSERT(0 == ret);

		attr = wq_ce_attr_get(wq_pcie, chn);

#ifdef TEST_CE_DMA_SEND_CODE
		src_depth = attr->src_depth;
		(void)wq_ce_watermarks_set(wq_pcie, chn, WQ_CE_CHN_SRC,
					   src_depth * 3 / 4, src_depth / 4);
		(void)wq_ce_int_cb_set(wq_pcie, chn, WQ_CE_CHN_SRC,
				       CE_SRC_INT_CURR_DESC,
				       test_ce_dma_send_cb);
		(void)wq_ce_int_ena_set(wq_pcie, chn, WQ_CE_CHN_SRC,
					CE_SRC_INT_CURR_DESC, true);
		(void)wq_ce_irq_unmask(wq_pcie, chn, WQ_CE_CHN_SRC);
#endif

#ifdef TEST_CE_DMA_RECV_CODE
		dst_depth = attr->dst_depth;
		(void)wq_ce_watermarks_set(wq_pcie, chn, WQ_CE_CHN_DST,
					   dst_depth * 3 / 4, dst_depth / 4);
		(void)wq_ce_int_cb_set(wq_pcie, chn, WQ_CE_CHN_DST,
				       CE_DST_INT_CURR_DESC,
				       test_ce_dma_recv_cb);
		(void)wq_ce_int_ena_set(wq_pcie, chn, WQ_CE_CHN_DST,
					CE_DST_INT_CURR_DESC, true);
		for (i = 1; i < dst_depth; ++i)
			test_ce_dma_recv_next(wq_pcie,
					      chn); // loop count: depth - 1
		(void)wq_ce_irq_unmask(wq_pcie, chn, WQ_CE_CHN_DST);
#endif
	}
}

static void test_ce_dma_run(struct wq_pcie *wq_pcie)
{
	CE_CHN_UUID chn;

#ifdef TEST_CE_DMA_SEND_CODE
	for (chn = 0; chn < CE_CHN_MAX; ++chn) {
		if (WQ_CE_SRC_FIFO_DEPTH(wq_pcie, chn) > 0) {
			if (!test_ce_dma_send_fifo_full[chn] &&
			    WQ_CE_FIFO_FULL(wq_pcie, chn, WQ_CE_CHN_SRC))
				TEST_CE_LOG("chn = %d, fifo full\n", (int)chn);
			else if (test_ce_dma_send_fifo_full[chn] &&
				 !WQ_CE_FIFO_FULL(wq_pcie, chn, WQ_CE_CHN_SRC))
				TEST_CE_LOG("chn = %d, fifo available\n",
					    (int)chn);
			test_ce_dma_send_fifo_full[chn] =
				WQ_CE_FIFO_FULL(wq_pcie, chn, WQ_CE_CHN_SRC);
			if (!test_ce_dma_send_fifo_full[chn])
				test_ce_dma_send_next(wq_pcie, chn);
		}
	}
	for (chn = 0; chn < CE_CHN_MAX; ++chn) {
		if (WQ_CE_SRC_FIFO_DEPTH(wq_pcie, chn) > 0 &&
		    test_ce_dma_send_done[chn])
			test_ce_dma_send_post(wq_pcie, chn);
	}
#endif

#ifdef TEST_CE_DMA_RECV_CODE
	for (chn = 0; chn < CE_CHN_MAX; ++chn) {
		if (WQ_CE_DST_FIFO_DEPTH(wq_pcie, chn) > 0 &&
		    test_ce_dma_recv_done[chn])
			test_ce_dma_recv_post(wq_pcie, chn);
	}
#endif
}

static void test_ce_dma_clean(struct wq_pcie *wq_pcie)
{
	CE_CHN_UUID chn;
	for (chn = 0; chn < CE_CHN_MAX; ++chn)
		(void)wq_ce_chn_deinit(wq_pcie, chn);
}

#endif

#if TEST_CE_DEMO == TEST_CE_SINGLE

#define TEST_CE_SINGLE_ATTR                                                    \
	{                                                                      \
		.flags = TEST_CE_DMA_ATTR_FLAGS,                               \
		.src_sz_max = TEST_CE_DMA_SRC_SZ_MAX,                          \
		.src_depth = TEST_CE_DMA_FIFO_DEPTH,                           \
		.dst_depth = TEST_CE_DMA_FIFO_DEPTH,                           \
	}

static const wq_ce_attr_assign_t test_ce_single_attr_assign_table[CE_CHN_MAX] = {
	[0] = TEST_CE_SINGLE_ATTR,
	[1] = TEST_CE_SINGLE_ATTR,
	[2] = TEST_CE_SINGLE_ATTR,
	[3] = TEST_CE_SINGLE_ATTR,
	[4] = TEST_CE_SINGLE_ATTR,
	[5] = TEST_CE_SINGLE_ATTR,
#if TEST_CE_DMA_DESC_CRC_ON_OFF != CE_DESC_FLAG_CRC_EB
	[6] = TEST_CE_SINGLE_ATTR,
	[7] = TEST_CE_SINGLE_ATTR,
	[8] = TEST_CE_SINGLE_ATTR,
	[9] = TEST_CE_SINGLE_ATTR,
	[10] = TEST_CE_SINGLE_ATTR,
	[11] = TEST_CE_SINGLE_ATTR,
#endif
};

#define test_ce_single_init(wq_pcie)                                           \
	test_ce_dma_init(wq_pcie, test_ce_single_attr_assign_table)
#define test_ce_single_run test_ce_dma_run
#define test_ce_single_clean test_ce_dma_clean

#elif TEST_CE_DEMO == TEST_CE_TX

#define TEST_CE_TX_ATTR                                                        \
	{                                                                      \
		.flags = TEST_CE_DMA_ATTR_FLAGS,                               \
		.src_sz_max = TEST_CE_DMA_SRC_SZ_MAX,                          \
		.src_depth = TEST_CE_DMA_FIFO_DEPTH, .dst_depth = 0,           \
	}

static const wq_ce_attr_assign_t test_ce_tx_attr_assign_table[CE_CHN_MAX] = {
	[0] = TEST_CE_TX_ATTR,
	[1] = TEST_CE_TX_ATTR,
	[2] = TEST_CE_TX_ATTR,
	[3] = TEST_CE_TX_ATTR,
	[4] = TEST_CE_TX_ATTR,
	[5] = TEST_CE_TX_ATTR,
#if TEST_CE_DMA_DESC_CRC_ON_OFF != CE_DESC_FLAG_CRC_EB
	[6] = TEST_CE_TX_ATTR,
	[7] = TEST_CE_TX_ATTR,
	[8] = TEST_CE_TX_ATTR,
	[9] = TEST_CE_TX_ATTR,
	[10] = TEST_CE_TX_ATTR,
	[11] = TEST_CE_TX_ATTR,
#endif
};

#define test_ce_tx_init(wq_pcie)                                               \
	test_ce_dma_init(wq_pcie, test_ce_tx_attr_assign_table)
#define test_ce_tx_run test_ce_dma_run
#define test_ce_tx_clean test_ce_dma_clean

#elif TEST_CE_DEMO == TEST_CE_RX

#define TEST_CE_RX_ATTR                                                        \
	{                                                                      \
		.flags = TEST_CE_DMA_ATTR_FLAGS,                               \
		.src_sz_max = TEST_CE_DMA_SRC_SZ_MAX, .src_depth = 0,          \
		.dst_depth = TEST_CE_DMA_FIFO_DEPTH,                           \
	}

static const wq_ce_attr_assign_t test_ce_rx_attr_assign_table[CE_CHN_MAX] = {
	[0] = TEST_CE_RX_ATTR,
	[1] = TEST_CE_RX_ATTR,
	[2] = TEST_CE_RX_ATTR,
	[3] = TEST_CE_RX_ATTR,
	[4] = TEST_CE_RX_ATTR,
	[5] = TEST_CE_RX_ATTR,
#if TEST_CE_DMA_DESC_CRC_ON_OFF != CE_DESC_FLAG_CRC_EB
	[6] = TEST_CE_RX_ATTR,
	[7] = TEST_CE_RX_ATTR,
	[8] = TEST_CE_RX_ATTR,
	[9] = TEST_CE_RX_ATTR,
	[10] = TEST_CE_RX_ATTR,
	[11] = TEST_CE_RX_ATTR,
#endif
};

#define test_ce_rx_init(wq_pcie)                                               \
	test_ce_dma_init(wq_pcie, test_ce_rx_attr_assign_table)
#define test_ce_rx_run test_ce_dma_run
#define test_ce_rx_clean test_ce_dma_clean

#elif TEST_CE_DEMO == TEST_CE_TPUT

#define TEST_CE_TPUT_CHN_MAX 12
#define TEST_CE_TPUT_SRC_SZ_MAX 2048
#define TEST_CE_TPUT_FIFO_DEPTH 1024
#define TEST_CE_TPUT_PKG_COUNT 1000000

#define TEST_CE_TPUT_SRC_ATTR                                                  \
	{                                                                      \
		.flags = 0, .src_sz_max = TEST_CE_TPUT_SRC_SZ_MAX,             \
		.src_depth = TEST_CE_TPUT_FIFO_DEPTH, .dst_depth = 0,          \
	}

#define TEST_CE_TPUT_DST_ATTR                                                  \
	{                                                                      \
		.flags = 0, .src_sz_max = TEST_CE_TPUT_SRC_SZ_MAX,             \
		.src_depth = 0, .dst_depth = TEST_CE_TPUT_FIFO_DEPTH,          \
	}

#define TEST_RUN_TARG_TO_HOST 0
#define TEST_RUN_HOST_TO_TARG 1

#define WIFI_SCRATCH1_ADDR (HOST_W_AHB_REG_BASEADDR + 0x34)

static inline __attribute__((always_inline)) int
test_ce_get_run_dir(struct wq_pcie *wq_pcie)
{
	u32 sync_val;

	sync_val = wq_pcie_read32(wq_pcie, WIFI_SCRATCH1_ADDR);

	sync_val = (sync_val & (1u << SCRATCH_PCIE_TEST_RUN_DIR_BIT)) >>
		   SCRATCH_PCIE_TEST_RUN_DIR_BIT;

	return (int)sync_val;
}

static inline __attribute__((always_inline)) void
test_ce_set_run_dir(struct wq_pcie *wq_pcie, int dir)
{
	u32 sync_val;

	sync_val = wq_pcie_read32(wq_pcie, WIFI_SCRATCH1_ADDR);

	if (dir)
		sync_val |= (1u << SCRATCH_PCIE_TEST_RUN_DIR_BIT);
	else
		sync_val &= ~(1u << SCRATCH_PCIE_TEST_RUN_DIR_BIT);

	wq_pcie_write32(wq_pcie, WIFI_SCRATCH1_ADDR, sync_val);
}

static wq_ce_attr_assign_t test_ce_tput_attr_assign_table[TEST_CE_TPUT_CHN_MAX];

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 3, 0)
#define KTIME_GET_BOOTTIME_NS ktime_get_boot_ns
#else
#define KTIME_GET_BOOTTIME_NS ktime_get_boottime_ns
#endif

static void test_ce_tput_callback(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	(void)chn;

	if (wq_pcie->ce_states[2].src)
		(void)wq_ce_irq_mask(wq_pcie, 2, WQ_CE_CHN_SRC);
	if (wq_pcie->ce_states[2].dst)
		(void)wq_ce_irq_mask(wq_pcie, 2, WQ_CE_CHN_DST);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	reinit_completion(&wq_pcie->test_ce_desc_done);
#else
	wq_pcie->test_ce_desc_done = 0;
#endif
	complete(&wq_pcie->test_ce_desc_done);
}

static void test_ce_tput_init(struct wq_pcie *wq_pcie)
{
	CE_CHN_UUID chn;
	const wq_ce_attr_assign_t *attr_assign;
	int ret;
	const wq_ce_attr_t *attr;
	u32 nbytes;
	struct sk_buff *skb;
	dma_addr_t phys_addr;
	u16 i;
	u32 j;

	if (!strcmp(test_run_dir, "Host2Targ")) {
		const wq_ce_attr_t _attr = TEST_CE_TPUT_SRC_ATTR;
		test_ce_set_run_dir(wq_pcie, TEST_RUN_HOST_TO_TARG);
		for (chn = 2; chn < TEST_CE_TPUT_CHN_MAX; ++chn)
			test_ce_tput_attr_assign_table[chn] = _attr;
	} else if (!strcmp(test_run_dir, "Targ2Host")) {
		const wq_ce_attr_t _attr = TEST_CE_TPUT_DST_ATTR;
		test_ce_set_run_dir(wq_pcie, TEST_RUN_TARG_TO_HOST);
		for (chn = 2; chn < TEST_CE_TPUT_CHN_MAX; ++chn)
			test_ce_tput_attr_assign_table[chn] = _attr;
	} else {
		BUG_ON(1);
	}

	msleep(5000);

	for (chn = 2; chn < TEST_CE_TPUT_CHN_MAX; ++chn) {
		attr_assign = &test_ce_tput_attr_assign_table[chn];
		if (0 == attr_assign->src_sz_max)
			continue;
		ret = wq_ce_chn_init(wq_pcie, chn, attr_assign);

		TEST_CE_LOG(
			STR(TEST_CE_DEMO) ", wq_ce_chn_init, chn = %d, ret = %d, "
					  "flags = 0x%08x, src_sz_max = %u, src_depth = %d, dst_depth = %d\n",
			(int)chn, ret, attr_assign->flags,
			attr_assign->src_sz_max, (int)attr_assign->src_depth,
			(int)attr_assign->dst_depth);
		TEST_CE_ASSERT(0 == ret);

		attr = wq_ce_attr_get(wq_pcie, chn);
		nbytes = attr->src_sz_max;

		if (attr->src_depth > 0) {
			if (2 == chn) {
				(void)wq_ce_int_cb_set(wq_pcie, chn,
						       WQ_CE_CHN_SRC,
						       CE_SRC_INT_CURR_DESC,
						       test_ce_tput_callback);
				(void)wq_ce_int_ena_set(wq_pcie, chn,
							WQ_CE_CHN_SRC,
							CE_SRC_INT_CURR_DESC,
							true);
				(void)wq_ce_irq_unmask(wq_pcie, chn,
						       WQ_CE_CHN_SRC);
			}

			for (i = 1; i < attr->src_depth;
			     ++i) { // loop count: src_depth - 1
				skb = dev_alloc_skb(nbytes);
				TEST_CE_ASSERT(skb != NULL);

				for (j = 0; j < nbytes; ++j)
					skb->data[j] = (u8)((nbytes + j) % 256);
				(void)skb_put(skb, nbytes);

				phys_addr = dma_map_single(wq_pcie->core.dev,
							   skb->data, skb->len,
							   DMA_TO_DEVICE);
				ret = dma_mapping_error(wq_pcie->core.dev,
							phys_addr);
				TEST_CE_ASSERT(0 == ret);
				dma_sync_single_for_device(wq_pcie->core.dev,
							   phys_addr, skb->len,
							   DMA_TO_DEVICE);

				if (i < attr->src_depth - 1)
					ret = wq_ce_send(wq_pcie, chn, skb,
							 phys_addr, skb->len,
							 0);
				else
					ret = wq_ce_send(wq_pcie, chn, skb,
							 phys_addr, skb->len,
							 CE_DESC_FLAG_INT_EB);
				TEST_CE_ASSERT(0 == ret);
			}
		} else {
			if (2 == chn) {
				(void)wq_ce_watermarks_set(wq_pcie, chn,
							   WQ_CE_CHN_DST, 2, 1);
				(void)wq_ce_int_cb_set(
					wq_pcie, chn, WQ_CE_CHN_DST,
					CE_DST_INT_RING_LOW_WATER,
					test_ce_tput_callback);
				(void)wq_ce_int_ena_set(
					wq_pcie, chn, WQ_CE_CHN_DST,
					CE_DST_INT_RING_LOW_WATER, true);
				(void)wq_ce_irq_unmask(wq_pcie, chn,
						       WQ_CE_CHN_DST);
			}

			for (i = 1; i < attr->dst_depth;
			     ++i) { // loop count: dst_depth - 1
				skb = dev_alloc_skb(nbytes);
				TEST_CE_ASSERT(skb != NULL);
				(void)memset(skb->data, 0, nbytes);

				phys_addr = dma_map_single(wq_pcie->core.dev,
							   skb->data, nbytes,
							   DMA_FROM_DEVICE);
				ret = dma_mapping_error(wq_pcie->core.dev,
							phys_addr);
				TEST_CE_ASSERT(0 == ret);

				ret = wq_ce_recv(wq_pcie, chn, skb, phys_addr,
						 nbytes, 0);
				TEST_CE_ASSERT(0 == ret);
			}
		}
	}
}

typedef struct {
	struct sk_buff *skb;
	dma_addr_t phys_addr;
	u32 nbytes;
} skb_phys_nbytes_grp_t;

static skb_phys_nbytes_grp_t test_ce_tput_queue[TEST_CE_TPUT_FIFO_DEPTH];

static u64 test_ce_tput_send_count[TEST_CE_TPUT_CHN_MAX] = { 0 },
	   test_ce_tput_recv_count[TEST_CE_TPUT_CHN_MAX] = { 0 };

static void test_ce_tput_run(struct wq_pcie *wq_pcie)
{
	CE_CHN_UUID chn;

	const wq_ce_attr_t *attr;
	u16 nentries;
	struct sk_buff *skb;
	dma_addr_t phys_addr;
	u32 nbytes;

	skb_phys_nbytes_grp_t *p;
	u16 i;
	u64 send_count_acc = 0, recv_count_acc = 0, total_count_acc = 0;

	u64 tm_srt, tm_end, tm_diff;
	u64 tx_tput, rx_tput;

	struct pci_dev *pdev;
	enum wq_wphy_profile wprofile;
	u8 pci_speed;
	char *pci_seed_str;

	pdev = to_pci_dev(wq_pcie->core.dev);
	wprofile = wq_pcie->core.wphy_profile;
	pci_speed = wq_pcie_get_link_speed(pdev);

	switch (pci_speed) {
	case 1:
		pci_seed_str = "2.5G/s";
		break;
	case 2:
		pci_seed_str = "5.0G/s";
		break;
	default:
		pci_seed_str = "unknown";
		break;
	}

	TEST_CE_LOG("vendor=0x%4.04X device=0x%4.04X rev=%d %s, pciespeed:%s\n",
		    pdev->vendor, pdev->device, pdev->revision,
		    wq_wphy_profile_name(wprofile), pci_seed_str);

	tm_srt = KTIME_GET_BOOTTIME_NS();

	do {
		wait_for_completion(&wq_pcie->test_ce_desc_done);

		for (chn = 2; chn < TEST_CE_TPUT_CHN_MAX; ++chn) {
			attr = wq_ce_attr_get(wq_pcie, chn);

			if (attr->src_depth > 0) {
				nentries = 0;
				while (wq_ce_send_completed_next(
					       wq_pcie, chn, (void **)&skb,
					       &phys_addr, &nbytes,
					       NULL) == 0) {
					test_ce_tput_send_count[chn]++;
					++send_count_acc;
					p = &test_ce_tput_queue[nentries];
					p->skb = skb;
					p->phys_addr = phys_addr;
					p->nbytes = nbytes;
					test_ce_tput_queue[nentries++] = *p;
				}
				for (i = 0; i < nentries; ++i) {
					skb = test_ce_tput_queue[i].skb;
					phys_addr =
						test_ce_tput_queue[i].phys_addr;
					nbytes = test_ce_tput_queue[i].nbytes;
					if (i < nentries - 1)
						(void)wq_ce_send(wq_pcie, chn,
								 skb, phys_addr,
								 skb->len, 0);
					else
						(void)wq_ce_send(
							wq_pcie, chn, skb,
							phys_addr, skb->len,
							CE_DESC_FLAG_INT_EB);
				}
			} else {
				nentries = 0;
				while (wq_ce_recv_completed_next(
					       wq_pcie, chn, (void **)&skb,
					       &phys_addr, &nbytes,
					       NULL) == 0) {
					dma_sync_single_for_cpu(
						wq_pcie->core.dev, phys_addr,
						nbytes, DMA_FROM_DEVICE);
					test_ce_tput_recv_count[chn]++;
					++recv_count_acc;
					p = &test_ce_tput_queue[nentries];
					p->skb = skb;
					p->phys_addr = phys_addr;
					p->nbytes = nbytes;
					test_ce_tput_queue[nentries++] = *p;
				}
				for (i = 0; i < nentries; ++i) {
					skb = test_ce_tput_queue[i].skb;
					phys_addr =
						test_ce_tput_queue[i].phys_addr;
					nbytes = test_ce_tput_queue[i].nbytes;
					(void)wq_ce_recv(wq_pcie, chn, skb,
							 phys_addr, nbytes, 0);
				}
			}
		}

		if (wq_pcie->ce_states[2].src)
			(void)wq_ce_irq_unmask(wq_pcie, 2, WQ_CE_CHN_SRC);
		if (wq_pcie->ce_states[2].dst)
			(void)wq_ce_irq_unmask(wq_pcie, 2, WQ_CE_CHN_DST);

	} while ((total_count_acc = send_count_acc + recv_count_acc) <
		 TEST_CE_TPUT_PKG_COUNT);

	tm_end = KTIME_GET_BOOTTIME_NS();

	for (chn = 0; chn < TEST_CE_TPUT_CHN_MAX; ++chn)
		TEST_CE_LOG("CHN: %d, Send Packet: %llu, Recv Packet: %llu\n",
			    (int)chn,
			    (unsigned long long)test_ce_tput_send_count[chn],
			    (unsigned long long)test_ce_tput_recv_count[chn]);

	(void)memset(test_ce_tput_send_count, 0,
		     sizeof(test_ce_tput_send_count));
	(void)memset(test_ce_tput_recv_count, 0,
		     sizeof(test_ce_tput_recv_count));

	if (tm_srt >= tm_end) {
		TEST_CE_LOG(
			"Time: srt=%llu and end=%llu, Total Packets: %llu\n",
			(unsigned long long)tm_srt, (unsigned long long)tm_end,
			(unsigned long long)total_count_acc);
		return;
	}

	tm_diff = tm_end - tm_srt;
	do_div(tm_diff, 1000); // to_us

	TEST_CE_LOG("Time: %lluus, Total Packets: %llu\n",
		    (unsigned long long)tm_diff,
		    (unsigned long long)total_count_acc);

	tx_tput = send_count_acc * TEST_CE_TPUT_SRC_SZ_MAX * 8;
	do_div(tx_tput, tm_diff);

	rx_tput = recv_count_acc * TEST_CE_TPUT_SRC_SZ_MAX * 8;
	do_div(rx_tput, tm_diff);

	TEST_CE_LOG(
		"TX Throughput: %lluMbps, RX Throughput: %lluMbps, Bidi Throuhgput: %lluMbps\n",
		(unsigned long long)tx_tput, (unsigned long long)rx_tput,
		(unsigned long long)(tx_tput + rx_tput));
}

static void test_ce_tput_clean(struct wq_pcie *wq_pcie)
{
	CE_CHN_UUID chn;
	struct sk_buff *skb;
	dma_addr_t phys_addr;
	uint32_t nbytes;

	for (chn = 2; chn < CE_CHN_MAX; ++chn) {
		if (wq_pcie->ce_states[chn].src) {
			while (!wq_ce_send_cancel_next(
				wq_pcie, chn, (void **)&skb, &phys_addr,
				&nbytes, NULL)) {
				dma_unmap_single(wq_pcie->core.dev, phys_addr,
						 nbytes, DMA_TO_DEVICE);
				dev_kfree_skb_any(skb);
			}
		}
		if (wq_pcie->ce_states[chn].dst) {
			while (!wq_ce_recv_revoke_next(
				wq_pcie, chn, (void **)&skb, &phys_addr,
				&nbytes, NULL)) {
				dma_unmap_single(wq_pcie->core.dev, phys_addr,
						 nbytes, DMA_FROM_DEVICE);
				dev_kfree_skb_any(skb);
			}
		}
		(void)wq_ce_chn_deinit(wq_pcie, chn);
	}
}

#elif TEST_CE_DEMO == TEST_CE_STRESS

static const wq_ce_attr_assign_t test_ce_stress_attr_assign_table[CE_CHN_MAX];

static void test_ce_stress_init(struct wq_pcie *wq_pcie)
{
	CE_CHN_UUID chn;

	for (chn = 0; chn < CE_CHN_MAX; ++chn) {
		const wq_ce_attr_assign_t *attr_assign =
			&test_ce_stress_attr_assign_table[chn];
		if (0 == attr_assign->src_sz_max)
			continue;
		(void)wq_ce_chn_init(wq_pcie, chn, attr_assign);
	}
}

static void test_ce_stress_run(struct wq_pcie *wq_pcie)
{
}

static void test_ce_stress_clean(struct wq_pcie *wq_pcie)
{
	CE_CHN_UUID chn;
	for (chn = 0; chn < CE_CHN_MAX; ++chn)
		(void)wq_ce_chn_deinit(wq_pcie, chn);
}

#endif

static int test_ce_task_do(void *data)
{
	struct wq_pcie *wq_pcie = data;

	init_completion(&wq_pcie->test_ce_desc_done);

#if TEST_CE_DEMO == TEST_CE_SINGLE
	test_ce_single_init(wq_pcie);
#elif TEST_CE_DEMO == TEST_CE_TX
	test_ce_tx_init(wq_pcie);
#elif TEST_CE_DEMO == TEST_CE_RX
	test_ce_rx_init(wq_pcie);
#elif TEST_CE_DEMO == TEST_CE_TPUT
	test_ce_tput_init(wq_pcie);
#elif TEST_CE_DEMO == TEST_CE_STRESS
	test_ce_stress_init(wq_pcie);
#endif

	while (!kthread_should_stop()) {
		// TEST_CE_LOG("thread sleep\n");
		// msleep_interruptible(1);
		// TEST_CE_LOG("thread wakeup\n");
#if TEST_CE_DEMO == TEST_CE_SINGLE
		test_ce_single_run(wq_pcie);
#elif TEST_CE_DEMO == TEST_CE_TX
		test_ce_tx_run(wq_pcie);
#elif TEST_CE_DEMO == TEST_CE_RX
		test_ce_rx_run(wq_pcie);
#elif TEST_CE_DEMO == TEST_CE_TPUT
		test_ce_tput_run(wq_pcie);
#elif TEST_CE_DEMO == TEST_CE_STRESS
		test_ce_stress_run(wq_pcie);
#endif
	}

	return 0;
}

int wq_ce_test_start(struct wq_pcie *wq_pcie)
{
	if (NULL == wq_pcie)
		return -EINVAL;

	test_ce_task_id =
		kthread_run(test_ce_task_do, wq_pcie, "ce test thread");

	if (NULL == test_ce_task_id) {
		TEST_CE_LOG("ce test thread create failed\n");
		return -EINVAL;
	}

	return 0;
}

void wq_ce_test_stop(struct wq_pcie *wq_pcie)
{
	(void)kthread_stop(test_ce_task_id);

#if TEST_CE_DEMO == TEST_CE_SINGLE
	test_ce_single_clean(wq_pcie);
#elif TEST_CE_DEMO == TEST_CE_TX
	test_ce_tx_clean(wq_pcie);
#elif TEST_CE_DEMO == TEST_CE_RX
	test_ce_rx_clean(wq_pcie);
#elif TEST_CE_DEMO == TEST_CE_TPUT
	test_ce_tput_clean(wq_pcie);
#elif TEST_CE_DEMO == TEST_CE_STRESS
	test_ce_stress_clean(wq_pcie);
#endif
}
