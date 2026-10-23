#define WQ_LOG_DM DM_TRBUS

#include <linux/kthread.h>

#include "pcie.h"

#include "fw_api/wifi/mac/dp_tx.h"
#include "fw_api/wifi/htc/htc_v1.h"

#include "host_reg_base.h"
#include "wifi_ahb_reg.h"
#include "mail_box_reg.h"

#include "wq_ipc.h"
#include "wq_log.h"

/********************************************************
	Unit Test MACROs
********************************************************/
#undef MUTLI_INBOUD_TEST
#undef OUTBOUND_TEST
#undef MAP_SIGNLE_TEST
#undef CE_UNIT_TEST
#undef TX_CE_UNIT_TEST

#define MAP_TEST_SIZE 4096

static u8 *outbound_buf;
static dma_addr_t outbound_buf_pa;

u32 send_count = 0;

enum { WOAL_STATUS_FAILURE = 0xffffffff,
       WOAL_STATUS_SUCCESS = 0,
       WOAL_STATUS_PENDING,
};

#ifdef MUTLI_INBOUD_TEST
/********************************************************
			Local Functions
********************************************************/
__maybe_unused static void woal_bar_io_wr_test(struct wq_pcie *wq_pcie)
{
	int ret = 0;
	u32 reg_data;
	u32 reg_offset = 0;
	u8 i = 0;
	struct timespec64 ts_start, ts_end;
	struct timespec64 ts_delta;
	u64 diff = 0;

	/*test io write and read to reg*/
	while (i < 20) {
		//reg_offset = get_random_u32();
		//reg_offset = ((reg_offset&0x00ff)+3)&~3;
		reg_data = wq_pcie_read32(wq_pcie, reg_offset);
		WQ_DBG(DM_TRBUS, DL_INF,
		       "PCIe Read reg offset(0x%x) success,data = 0x%x\n",
		       reg_offset, reg_data);

#ifdef IO_WR_TIME
		ktime_get_boottime_ts64(&ts_start);
#endif
		wq_pcie_write32(wq_pcie, reg_offset, reg_offset + 1);
#ifdef IO_WR_TIME
		ktime_get_boottime_ts64(&ts_end);
		ts_delta = timespec64_sub(ts_end, ts_start);
		diff = timespec64_to_ns(&ts_delta);
		WQ_DBG(DM_TRBUS, DL_INF, "PCIe Write reg cose time %lldns\n",
		       diff);
#endif
		WQ_DBG(DM_TRBUS, DL_INF,
		       "PCIe Write reg offset(0x%x) success,data = 0x%x\n",
		       reg_offset, reg_offset + 1);
		usleep_range(200, 500);
#ifdef IO_WR_TIME
		ktime_get_boottime_ts64(&ts_start);
#endif
		reg_data = wq_pcie_read32(wq_pcie, reg_offset);
#ifdef IO_WR_TIME
		ktime_get_boottime_ts64(&ts_end);
		ts_delta = timespec64_sub(ts_end, ts_start);
		diff = timespec64_to_ns(&ts_delta);
		WQ_DBG(DM_TRBUS, DL_INF, "PCIe Read reg cose time %lldns\n",
		       diff);
#endif
		WQ_DBG(DM_TRBUS, DL_INF,
		       "PCIe Read reg offset(0x%x) success,data = 0x%x\n",
		       reg_offset, reg_data);
		reg_offset += sizeof(u32);
		i++;
		WQ_DBG(DM_TRBUS, DL_INF, "\n");
	}
}

/**
 *  @brief This function test the inbound proccess
 *
 *  @param wq_pcie   A pointer to struct wq_pcie structure
 *
 *  @return
 */
static int woal_multi_inbound_test(struct wq_pcie *wq_pcie)
{
	WQ_DBG(DM_TRBUS, DL_INF,
	       "pcie write region 0 - 6 address data: 0x12345600-6\n");
	if (wq_pcie_write32(wq_pcie, HOST_W_IRAM_BASEADDR + 0x04, 0x12345600)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "Failed to write buffer length to region0 reg0x04\n");
		return WOAL_STATUS_FAILURE;
	}
	if (wq_pcie_write32(wq_pcie, HOST_W_AHB_REG_BASEADDR + 0x04,
			    0x12345601)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "Failed to write buffer length to region1 reg0x04\n");
		return WOAL_STATUS_FAILURE;
	}
	if (wq_pcie_write32(wq_pcie, HOST_PCIE_INTC_REG_BASEADDR + 0x04,
			    0x12345602)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "Failed to write buffer length to region2 reg0x04\n");
		return WOAL_STATUS_FAILURE;
	}
	if (wq_pcie_write32(wq_pcie, HOST_W_CE_BASEADDR + 0x04, 0x12345603)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "Failed to write buffer length to region3 reg0x04\n");
		return WOAL_STATUS_FAILURE;
	}
	if (wq_pcie_write32(wq_pcie, HOST_PCIE_EXT_CTRL_BASEADDR + 0x04,
			    0x12345604)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "Failed to write buffer length to region4 reg0x04\n");
		return WOAL_STATUS_FAILURE;
	}
	if (wq_pcie_write32(wq_pcie, HOST_W_APB_REG_BASEADDR + 0x04,
			    0x12345605)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "Failed to write buffer length to region5 reg0x04\n");
		return WOAL_STATUS_FAILURE;
	}
	if (wq_pcie_write32(wq_pcie, HOST_W_MAILBOX_BASEADDR + 0x00,
			    0x16345606)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "Failed to write buffer length to region6 reg0x04\n");
		return WOAL_STATUS_FAILURE;
	}
	return WOAL_STATUS_SUCCESS;
}
#endif

static int wq_pcie_outbound_set(struct wq_pcie *wq_pcie, dma_addr_t addr_pa,
				u32 len)
{
	u32 reg_data;
	/* Write the lower 32bits of the physical address to REG_CMD_ADDR_LO */
	wq_pcie_write32(wq_pcie,
			HOST_W_AHB_REG_BASEADDR + CFG_WIFI_AHB_BAK_REG0_ADDR,
			(u32)addr_pa);

	/* Write the command length to REG_CMD_SIZE */
	wq_pcie_write32(wq_pcie,
			HOST_W_AHB_REG_BASEADDR + CFG_WIFI_AHB_BAK_REG1_ADDR,
			len);

	reg_data = wq_pcie_read32(wq_pcie,
				  HOST_W_MAILBOX_BASEADDR + CFG_MSG_STS_ADDR);

	if (!((reg_data >> MAILBOX_AUTO_FILL_ENA_OFFSET) & 0x01)) {
		wq_pcie_write32(wq_pcie,
				HOST_W_MAILBOX_BASEADDR + CFG_MSG_WDATA_ADDR,
				0x01);
	} else {
		WQ_DBG(DM_TRBUS, DL_INF, "mailbox status=0x%x,bit12=%d\n",
		       reg_data,
		       (reg_data >> MAILBOX_AUTO_FILL_ENA_OFFSET) & 0x01);
	}
	return WOAL_STATUS_SUCCESS;
}

#ifdef OUTBOUND_TEST
/**
 *  @brief This function downloads command to the wq_pcie.
 *
 *  @param wq_pcie A pointer to struct wq_pcie structure
 *  @param pmbuf     A pointer to buffer (data_len should include PCIE header)
 *
 *  @return 	     WOAL_STATUS_SUCCESS or WOAL_STATUS_FAILURE
 */
static int wq_pcie_outbound_test(struct wq_pcie *wq_pcie)
{
	int ret = WOAL_STATUS_FAILURE;
	u32 mem_flag = GFP_KERNEL;
	u8 i;
	u32 reg_data = 0;
	ENTER();
#ifdef MAP_SIGNLE_TEST
	outbound_buf = kzalloc(MAP_TEST_SIZE, mem_flag);
	if (outbound_buf == NULL) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: allocate memory (%d bytes) failed!\n", __func__,
		       MAP_TEST_SIZE);
		goto done;
	}
	memset(outbound_buf, 0, MAP_TEST_SIZE);
	if (WOAL_STATUS_FAILURE ==
	    wq_map_memory(wq_pcie, outbound_buf, &outbound_buf_pa,
			  MAP_TEST_SIZE, DMA_TO_DEVICE)) {
#else
	if (WOAL_STATUS_FAILURE == woal_map_consistent(wq_pcie, &outbound_buf,
						       &outbound_buf_pa,
						       MAP_TEST_SIZE)) {
#endif
		goto done;
	}
	if (wq_pcie_outbound_set(wq_pcie, outbound_buf_pa, MAP_TEST_SIZE)) {
		goto done;
	}
	ret = WOAL_STATUS_SUCCESS;
done:
	LEAVE();
	WQ_DBG(DM_TRBUS, DL_INF, "BUFFER ADDR: 0x%x\t LENGTH: 0x%x.\n",
	       (u32)outbound_buf_pa, MAP_TEST_SIZE);
	return ret;
}
#endif

#ifdef RAE_TEST
#define RAE_DESC_SIZE (3520 * 16)
#define RAE_BUF_SIZE (3200 * 16)
static u8 *rae_desc_ring = NULL;
static u8 *rae_buf_ring = NULL;
static dma_addr_t rae_desc_ring_pa = 0x0;
static dma_addr_t rae_buf_ring_pa = 0x0;

static int wq_pcie_rae_test(struct wq_pcie *wq_pcie)
{
	int ret = WOAL_STATUS_FAILURE;
	u32 reg_data = 0;

	ENTER();

	if (WOAL_STATUS_FAILURE == woal_map_consistent(wq_pcie, &rae_desc_ring,
						       &rae_desc_ring_pa,
						       RAE_DESC_SIZE)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: DMA map memory fail!!!(0x%p) to dma pa(0x%llx)!\n",
		       __func__, rae_desc_ring, (u64)rae_desc_ring_pa);
		goto done;
	} else {
		WQ_DBG(DM_TRBUS, DL_INF,
		       "%s:DMA map memory(0x%p) to dma pa(0x%llx)!\n", __func__,
		       rae_desc_ring, (u64)rae_desc_ring_pa);
	}

	wq_pcie_write32(wq_pcie,
			HOST_W_AHB_REG_BASEADDR + CFG_WIFI_AHB_BAK_REG0_ADDR,
			(u32)rae_desc_ring_pa);
	wq_pcie_write32(wq_pcie,
			HOST_W_AHB_REG_BASEADDR + CFG_WIFI_AHB_BAK_REG1_ADDR,
			RAE_DESC_SIZE);

	reg_data = wq_pcie_read32(wq_pcie,
				  HOST_W_MAILBOX_BASEADDR + CFG_MSG_STS_ADDR);
	if (!((reg_data >> MAILBOX_AUTO_FILL_ENA_OFFSET) & 0x01)) {
		wq_pcie_write32(wq_pcie,
				HOST_W_MAILBOX_BASEADDR + CFG_MSG_WDATA_ADDR,
				0x31);
	} else {
		WQ_DBG(DM_TRBUS, DL_INF, "mailbox status=0x%x, bit12=%d\n",
		       reg_data,
		       (reg_data >> MAILBOX_AUTO_FILL_ENA_OFFSET) & 0x01);
	}

	if (WOAL_STATUS_FAILURE == woal_map_consistent(wq_pcie, &rae_buf_ring,
						       &rae_buf_ring_pa,
						       RAE_BUF_SIZE)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: DMA map memory fail!!!(0x%p) to dma pa(0x%llx)!\n",
		       __func__, rae_buf_ring, (u64)rae_buf_ring_pa);
		goto done;
	} else {
		WQ_DBG(DM_TRBUS, DL_INF,
		       "%s: DMA map memory(0x%p) to dma pa(0x%llx)!\n",
		       __func__, rae_buf_ring, (u64)rae_buf_ring_pa);
	}

	wq_pcie_write32(wq_pcie,
			HOST_W_AHB_REG_BASEADDR + CFG_WIFI_AHB_BAK_REG0_ADDR,
			(u32)rae_buf_ring_pa);
	wq_pcie_write32(wq_pcie,
			HOST_W_AHB_REG_BASEADDR + CFG_WIFI_AHB_BAK_REG1_ADDR,
			RAE_BUF_SIZE);

	reg_data = wq_pcie_read32(wq_pcie,
				  HOST_W_MAILBOX_BASEADDR + CFG_MSG_STS_ADDR);
	if (!((reg_data >> MAILBOX_AUTO_FILL_ENA_OFFSET) & 0x01)) {
		wq_pcie_write32(wq_pcie,
				HOST_W_MAILBOX_BASEADDR + CFG_MSG_WDATA_ADDR,
				0x32);
	} else {
		WQ_DBG(DM_TRBUS, DL_INF, "mailbox status=0x%x,bit12=%d\n",
		       reg_data,
		       (reg_data >> MAILBOX_AUTO_FILL_ENA_OFFSET) & 0x01);
	}

	ret = WOAL_STATUS_SUCCESS;
done:
	LEAVE();
	//WQ_DBG(DM_TRBUS, DL_INF, "BUFFER ADDR: 0x%x\t LENGTH: 0x%x.\n",
	//			(u32)rae_desc_ring_pa, RAE_BUF_SIZE);
	return ret;
}

/**
 *  @brief This function handles mailbox irq
 *
 *  @param wq_pcie   A Pointer to the struct wq_pcie structure
 *
 */
struct timespec64 ts_start, ts_end, ts_delta;
u64 diff = 0;
u64 total_machdr_len = 0;
u64 total_payload_len = 0;
u64 total_pkt_len = 0;

void woal_rae_remote_buf_prog_done(void)
{
	uint32_t offset = 0x14, machdr_offset = 48;
	uint32_t rxdesc_len = 120, desc_wrap_len = 44 * 4;
	uint32_t machdr_len = 0, payload_len = 0, align_len = 0;
	uint32_t stat_num = 100, tput = 0;
	static uint32_t idx = 0, i = 0;
	u8 *rxdesc, *pos, *machdr, *payload;

	if (idx == 0 || (idx > 1 && ((idx % stat_num) == 1))) {
		ktime_get_boottime_ts64(&ts_start);
		total_pkt_len = 0;
	}

	rxdesc = rae_desc_ring + i * rxdesc_len + total_machdr_len;
	if (((rae_desc_ring + RAE_DESC_SIZE) - rxdesc) <= desc_wrap_len) {
		rxdesc = rae_desc_ring;
		i = 0;
		total_machdr_len = 0;
		total_payload_len = 0;
	}

	/* calc total machdr length */
	pos = rxdesc + offset + machdr_offset;
	machdr_len = pos[0] | pos[1] << 8 | pos[2] << 16 | pos[3] << 24;
	align_len = ((machdr_len + 3) & ~3);
	total_machdr_len += align_len;

	/* calc total payload length */
	pos = rxdesc + offset;
	payload_len = pos[0] << 0 | pos[1] << 8;
	align_len = ((payload_len + 3) & ~3);
	total_payload_len += align_len;

	total_pkt_len += machdr_len;
	total_pkt_len += payload_len;

	if (idx != 0 && (idx % stat_num) == 0) {
		ktime_get_boottime_ts64(&ts_end);
		ts_delta = timespec64_sub(ts_end, ts_start);
		diff = timespec64_to_ns(&ts_delta) / 1000; //us
		tput = (total_pkt_len * 8) / diff;
		WQ_DBG(DM_GENERIC, DL_WRN, "time diff:%llu us, total pkt len:%llu\n", diff,
		       total_pkt_len);
		WQ_DBG(DM_GENERIC, DL_WRN, "Throughput:%u Mbps\n", tput);
	}

#if 0
	printk("-----------------dump packet-------------------\n");
	dump_bytes(DL_ERR, "rae desc ring", rxdesc, rxdesc_len);

	machdr = rxdesc + rxdesc_len;
	dump_bytes(DL_ERR, "rae machdr", machdr, machdr_len);
	printk("\n");
#endif
	idx++;
	i++;
}

#endif

#if defined(TX_CE_UNIT_TEST)
#define WOAL_CE_WLAN_TX_SKB_RANDOM_LEN 1
#define WOAL_CE_WLAN_TX_SKB_COUNT 100000

#define WOAL_CE_WLAN_TX_AMSDU_NUM 5
#define WOAL_CE_WLAN_TX_SKB_PAYLOAD_LEN 1500 //1344
#define WOAL_CE_WLAN_TX_SKB_POOL_NUM 64
#define WOAL_CE_WLAN_TX_SKB_ENCAP_TYPE 0x1 //ethernet v2

#define CE_TEST_LOG(fmt, args...)                                              \
	(void)printk(KERN_ALERT "%s, %d, " fmt, __func__, __LINE__, ##args)
#define CE_ASSERT(x)                                                           \
	do {                                                                   \
		if (!(x)) {                                                    \
			CE_TEST_LOG("assert");                                 \
			*(int *)0 = 0;                                         \
		}                                                              \
	} while (0)
#define RING_IDX_INC_ONCE(idx, mask) ((((idx) + 1)) & (mask))
#define RING_AVAIL_SPACE(hidx, tidx, mask) (((hidx) - (tidx)) & (mask))

typedef struct {
	struct sk_buff *skb;
	dma_addr_t pa_addr;
} woal_ce_wlan_tx_test_pool_t;

static woal_ce_wlan_tx_test_pool_t
	skb_phy_addr_pool[WOAL_CE_WLAN_TX_SKB_POOL_NUM] = { 0 };
static volatile u32 skb_pool_r_idx = 0;
static volatile u32 skb_pool_w_idx = 0;

static struct task_struct *task_id = NULL;
static void woal_ce_wlan_tx_replenish_rx(struct wq_pcie *wq_pcie,
					 CE_CHN_UUID chn, uint32_t num);

static u8 woal_ce_wlan_tx_eth_dest_addr[] = {
	0xBB, 0xAA, 0x34, 0x12, 0x88, 0x56
};
static u8 woal_ce_wlan_tx_eth_src_addr[] = {
	0x45, 0x60, 0x55, 0x4D, 0x38, 0x56
};

static void woal_ce_wlan_tx_build_skb_payload(u16 encap_type, u8 *buf, u16 len)
{
	u32 off = 0;
	u32 i = 0;
	// buld eth header
	switch (encap_type) {
	case 0x1: //Ethernet V2
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		ether_addr_copy(&buf[off], woal_ce_wlan_tx_eth_dest_addr);
#else
		(void)memcpy(&buf[off], woal_ce_wlan_tx_eth_dest_addr, ETH_ALEN);
#endif
		off += ETH_ALEN;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		ether_addr_copy(&buf[off], woal_ce_wlan_tx_eth_src_addr);
#else
		(void)memcpy(&buf[off], woal_ce_wlan_tx_eth_src_addr, ETH_ALEN);
#endif
		off += ETH_ALEN;
		buf[off] = 0x8;
		off++;
		buf[off] = 0x0;
		off++;
		break;

	default:
		break;
	}

	for (; off < len - 4; off++, i++)
		buf[off] = (i & 0xF) | 0xF0;

	buf[off++] = 0xDD;
	buf[off++] = 0xCC;
	buf[off++] = 0xBB;
	buf[off++] = 0xAA;
}

static int woal_ce_wlan_tx_send_ring_overflow_cb(struct wq_pcie *wq_pcie,
						 CE_CHN_UUID chn)
{
	CE_TEST_LOG("chn = %d overflow \n", (int)chn);
	return WOAL_STATUS_SUCCESS;
}

static int woal_ce_wlan_tx_send_buf_len_invalid_cb(struct wq_pcie *wq_pcie,
						   CE_CHN_UUID chn)
{
	CE_TEST_LOG("chn = %d send invlid len\n", chn);
	return WOAL_STATUS_SUCCESS;
}

static int woal_ce_wlan_tx_recv_curr_desc_cb(struct wq_pcie *wq_pcie,
					     CE_CHN_UUID chn)
{
	int status;
	dma_addr_t phys_addr;
	u32 nbytes = 0;
	struct sk_buff *skb;
	u32 i;
	u32 recv_count = 0;
	u32 recv_addr_cnt = 0;
	u32 *phy_addr;

	CE_TEST_LOG("CE RX irq\n");

	while ((status = woal_ce_recv_completed_next(
			wq_pcie, chn, (void **)&skb, &phys_addr, &nbytes)) ==
	       WOAL_STATUS_SUCCESS) {
		++recv_count;

		wq_sync_memory_for_cpu(wq_pcie, phys_addr, nbytes,
				       DMA_FROM_DEVICE);

		if (nbytes == 0 || (nbytes % sizeof(uint32_t)) != 0)
			CE_TEST_LOG("recv size not 4-byte align:%d\n", nbytes);

		recv_addr_cnt = nbytes / sizeof(uint32_t);
		phy_addr = (u32 *)skb->data;

		while (recv_addr_cnt--) {
			if (RING_AVAIL_SPACE(skb_pool_r_idx - 1, skb_pool_w_idx,
					     WOAL_CE_WLAN_TX_SKB_POOL_NUM -
						     1) <= 0)
				BUG_ON(0);
			skb_phy_addr_pool[skb_pool_w_idx].skb = skb;
			skb_phy_addr_pool[skb_pool_w_idx].pa_addr = *phy_addr;
			phy_addr++;
			skb_pool_w_idx = RING_IDX_INC_ONCE(
				skb_pool_w_idx,
				WOAL_CE_WLAN_TX_SKB_POOL_NUM - 1);
		}
		wq_unmap_memory(wq_pcie, &phys_addr, nbytes, DMA_FROM_DEVICE);

		dev_kfree_skb_any(skb);
	}

	woal_ce_wlan_tx_replenish_rx(wq_pcie, chn, recv_count);

	if (task_id != NULL) {
		wake_up_process(task_id);
	}

	return WOAL_STATUS_SUCCESS;
}

static int woal_ce_wlan_tx_send_curr_desc_cb(struct wq_pcie *wq_pcie,
					     CE_CHN_UUID chn)
{
	int status;
	struct sk_buff *skb;
	dma_addr_t phys_addr = 0;
	u32 nbytes = 0;
	u32 send_count = 0;

	CE_TEST_LOG("CE TX irq\n");

	while ((status = woal_ce_send_completed_next(
			wq_pcie, chn, (void **)&skb, &phys_addr, &nbytes,
			NULL)) == WOAL_STATUS_SUCCESS) {
		++send_count;

		wq_unmap_memory(wq_pcie, &phys_addr, nbytes, DMA_TO_DEVICE);

		dev_kfree_skb_any(skb);
	}

	return WOAL_STATUS_SUCCESS;
}

#define WOAL_CE_WLAN_TX_SRC_SZ_MAX 256
#define WOAL_CE_WLAN_TX_DST_SZ_MAX 256

#define WOAL_CE_WLAN_TX_SRC_RING_DEPTH_LOG2 5
#define WOAL_CE_WLAN_TX_DST_RING_DEPTH_LOG2 5

#define WOAL_CE_WLAN_TX_SRC_RING_DEPTH                                         \
	(1 << WOAL_CE_WLAN_TX_SRC_RING_DEPTH_LOG2)
#define WOAL_CE_WLAN_TX_DST_RING_DEPTH                                         \
	(1 << WOAL_CE_WLAN_TX_DST_RING_DEPTH_LOG2)

#define WOAL_CE_WLAN_TX_SRC_ATTR                                               \
	{                                                                      \
		.src_sz_max = WOAL_CE_WLAN_TX_SRC_SZ_MAX, \
        .src = { \
            .depth_log2 = WOAL_CE_WLAN_TX_SRC_RING_DEPTH_LOG2, \
            .low_water_thrs = 0, \
            .int_cbs = { \
                [CE_SRC_INT_CURR_DESC] = woal_ce_wlan_tx_send_curr_desc_cb, \
            }, \
        .int_enas = CE_INT_ON(CE_SRC_INT_CURR_DESC) \
        }, \
        .dst = {.depth_log2 = CE_RING_DEPTH_LOG2_INVALID},                    \
	}

#define WOAL_CE_WLAN_TX_DST_ATTR                                               \
	{                                                                      \
		.src_sz_max = WOAL_CE_WLAN_TX_DST_SZ_MAX, \
        .src = {.depth_log2 = CE_RING_DEPTH_LOG2_INVALID}, \
        .dst = { \
            .depth_log2 = WOAL_CE_WLAN_TX_DST_RING_DEPTH_LOG2, \
            .int_cbs = { \
                [CE_DST_INT_CURR_DESC] = woal_ce_wlan_tx_recv_curr_desc_cb, \
            }, \
            .int_enas = CE_INT_ON(CE_DST_INT_CURR_DESC) \
        },                    \
	}

static const ce_attr_t woal_wlan_tx_ce_table[WQ_PCIE_CE_CH_LAST] = {
	[WQ_PCIE_CE_CH_CMD_TX] = WOAL_CE_WLAN_TX_SRC_ATTR,
	[WQ_PCIE_CE_CH_EVT_RX] = WOAL_CE_WLAN_TX_DST_ATTR,
	[WQ_PCIE_CE_CH_PKT_TX] = WOAL_CE_WLAN_TX_SRC_ATTR,
	[WQ_PCIE_CE_CH_PKT_RX] = WOAL_CE_WLAN_TX_DST_ATTR,
	[WQ_PCIE_CE_CH_RAW_TX] = WOAL_CE_WLAN_TX_SRC_ATTR,
	[WQ_PCIE_CE_CH_RAW_RX] = WOAL_CE_WLAN_TX_DST_ATTR,
	[WQ_PCIE_CE_CH_LOG_RX] = WOAL_CE_WLAN_TX_DST_ATTR,
	[WQ_PCIE_CE_CH_MEMCPY] = { 0 },
};

static void woal_ce_wlan_tx_replenish_rx(struct wq_pcie *wq_pcie,
					 CE_CHN_UUID chn, uint32_t num)
{
	int status;
	dma_addr_t phys_addr;
	u32 nbytes = 0;
	struct sk_buff *skb;

	CE_TEST_LOG("fill chn(%d) buffer:%d\n", chn, num);

	while (num-- > 0) {
		nbytes = WOAL_CE_WLAN_TX_DST_SZ_MAX;
		skb = dev_alloc_skb(nbytes);
		if (!skb) {
			BUG_ON(0);
		}
		status = wq_map_memory(wq_pcie, skb->data, &phys_addr, nbytes,
				       DMA_FROM_DEVICE);
		if (status != WOAL_STATUS_SUCCESS) {
			BUG_ON(0);
		}

		status = woal_ce_recv(wq_pcie, chn, skb, phys_addr, nbytes,
				      CE_DESC_FLAG_INT);
		CE_TEST_LOG("desc status: %d\n", status);
	}
}

static int woal_ce_wlan_tx_test_init(struct wq_pcie *wq_pcie)
{
	int status = WOAL_STATUS_SUCCESS;
	CE_CHN_UUID chn;

	status = woal_ce_chn_init(
		wq_pcie, WQ_PCIE_CE_CH_PKT_TX,
		&(woal_wlan_tx_ce_table[WQ_PCIE_CE_CH_PKT_TX]));
	if (status != WOAL_STATUS_SUCCESS) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: Init CE PKT_TX channel failure!\n", __func__);
		return WOAL_STATUS_FAILURE;
	}

	CE_ASSERT(WOAL_STATUS_SUCCESS == status);
	status = woal_ce_chn_init(
		wq_pcie, WQ_PCIE_CE_CH_PKT_RX,
		&(woal_wlan_tx_ce_table[WQ_PCIE_CE_CH_PKT_RX]));
	if (status != WOAL_STATUS_SUCCESS) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: Init CE PKT_RX channel failure!\n", __func__);
		return WOAL_STATUS_FAILURE;
	}
	CE_ASSERT(WOAL_STATUS_SUCCESS == status);

	WQ_DBG(DM_TRBUS, DL_INF, "ce_chn_table[%d].src_sz_max = %u\n",
	       WQ_PCIE_CE_CH_PKT_TX,
	       woal_wlan_tx_ce_table[WQ_PCIE_CE_CH_PKT_TX].src_sz_max);
	WQ_DBG(DM_TRBUS, DL_INF, "ce_chn_table[%d].src_sz_max = %u\n",
	       WQ_PCIE_CE_CH_PKT_RX,
	       woal_wlan_tx_ce_table[WQ_PCIE_CE_CH_PKT_RX].src_sz_max);

	woal_ce_wlan_tx_replenish_rx(
		wq_pcie, WQ_PCIE_CE_CH_PKT_RX,
		(1
		 << woal_wlan_tx_ce_table[WQ_PCIE_CE_CH_PKT_RX].dst.depth_log2));

	return 0;
}

static void woal_ce_wlan_tx_skb_pool_init(struct wq_pcie *wq_pcie)
{
	int status;
	dma_addr_t phys_addr;
	struct sk_buff *skb;
	u32 nbytes = 0;
	u32 i;

	nbytes = WOAL_CE_WLAN_TX_SKB_PAYLOAD_LEN;

	for (i = 0; i < WOAL_CE_WLAN_TX_SKB_POOL_NUM; i++) {
		skb = dev_alloc_skb(nbytes);
		if (!skb) {
			BUG_ON(0);
		}

		skb_trim(skb, 0);
		skb_put(skb, nbytes);

		woal_ce_wlan_tx_build_skb_payload(
			WOAL_CE_WLAN_TX_SKB_ENCAP_TYPE, skb->data, nbytes);

		status = wq_map_memory(wq_pcie, skb->data, &phys_addr, nbytes,
				       DMA_FROM_DEVICE);
		if (status != WOAL_STATUS_SUCCESS) {
			BUG_ON(0);
		}

		skb_phy_addr_pool[skb_pool_w_idx].skb = skb;
		skb_phy_addr_pool[skb_pool_w_idx].pa_addr = phys_addr;
		skb_pool_w_idx = RING_IDX_INC_ONCE(
			skb_pool_w_idx, WOAL_CE_WLAN_TX_SKB_POOL_NUM - 1);
	}
}

struct {
	uint32_t sn : 12;
	uint32_t rsvd : 20;
} woal_ce_wlan_tx_sn;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 90)
static volatile u32 g_tx_amsdu_cnt = 0;
static volatile u32 g_pre_tx_amsdu_cnt = 0;
static struct timespec64 pre_ts;
#endif

static int woal_ce_wlan_tx_build_one_entry(struct wq_pcie *wq_pcie, u8 *buf,
					   u16 len)
{
	struct wq_hif_hdr *hif_hdr = (struct wq_hif_hdr *)buf;
	struct wq_htc_v1 *htc_v1 = (struct wq_htc_v1 *)(hif_hdr + 1);

	struct txdesc_host *desc;
	u8 i;
	u32 j;
#if WOAL_CE_WLAN_TX_SKB_RANDOM_LEN
	u32 skb_len = 0;
#endif

	/* build hif hdr and htc hdr */
	hif_hdr->ptn = WQ_HIF_HDR_MAGIC;
	hif_hdr->ver = WQ_HIF_HDR_VER_1;
	hif_hdr->qid = WQ_QID_AC_BE;
	hif_hdr->dw_len = len / 4;

	htc_v1->flags = 0x2;
	htc_v1->channel = WQ_PCIE_CE_CH_PKT_TX;
	htc_v1->buf_len = sizeof(struct txdesc_host);

	/* build struct txdesc_host */
	desc = (struct txdesc_host *)(buf + HEADROOM_HIF_HTC);

	memset(desc, 0, sizeof(*desc));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	ether_addr_copy(desc->api.host.ethhdr.h_dest,
			woal_ce_wlan_tx_eth_dest_addr);
	ether_addr_copy(desc->api.host.ethhdr.h_source,
			woal_ce_wlan_tx_eth_src_addr);
#else
	(void)memcpy(desc->api.host.ethhdr.h_dest,
			woal_ce_wlan_tx_eth_dest_addr, ETH_ALEN);
	(void)memcpy(desc->api.host.ethhdr.h_source,
			woal_ce_wlan_tx_eth_src_addr, ETH_ALEN);
#endif
	desc->api.host.ethhdr.h_proto = 0x8; //ethernet v2
	desc->api.host.sn = woal_ce_wlan_tx_sn.sn++;
	desc->api.host.tid = 0x1;
	desc->api.host.flags = 0x1;
	desc->api.host.encap_type = MSDU_ENCAP_ETH_V2;
	desc->api.host.end_marker = HOST_DESC_END_MARKER;

	for (i = 0; i < WOAL_CE_WLAN_TX_AMSDU_NUM; i++) {
		if (i >= NX_TX_PAYLOAD_MAX)
			BUG_ON(0);

		desc->api.host.packet_addr[i] =
			skb_phy_addr_pool[skb_pool_r_idx].pa_addr;
#if WOAL_CE_WLAN_TX_SKB_RANDOM_LEN
		skb_len = get_random_u32() % WOAL_CE_WLAN_TX_SKB_PAYLOAD_LEN;
		if (skb_len < 64)
			skb_len = 64;
		desc->api.host.packet_len[i] = skb_len;
#else
		desc->api.host.packet_len[i] = WOAL_CE_WLAN_TX_SKB_PAYLOAD_LEN;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 90)
		if (g_tx_amsdu_cnt == 0) {
			g_pre_tx_amsdu_cnt = 0;
			ktime_get_raw_ts64(&pre_ts);
		}
		g_tx_amsdu_cnt++;
		if (g_tx_amsdu_cnt - g_pre_tx_amsdu_cnt >=
		    WOAL_CE_WLAN_TX_SKB_COUNT) {
			struct timespec64 cur_ts;
			ktime_get_raw_ts64(&cur_ts);
			g_pre_tx_amsdu_cnt = g_tx_amsdu_cnt;
			WQ_DBG(DM_TRBUS, DL_INF,
			       "\nSend >= %d len:%d pkts during %lld seconds, %ld nseconds\n",
			       WOAL_CE_WLAN_TX_SKB_COUNT,
			       WOAL_CE_WLAN_TX_SKB_PAYLOAD_LEN,
			       cur_ts.tv_sec - pre_ts.tv_sec,
			       cur_ts.tv_nsec - pre_ts.tv_nsec);

			pre_ts = cur_ts;
		}
#endif

#if 1
		WQ_DBG(DM_TRBUS, DL_INF, "----DUMP AMSDU(%d):0x%x----\n", i,
		       desc->api.host.packet_addr[i]);
		for (j = 0; j < 256;
		     j++) //WOAL_CE_WLAN_TX_SKB_PAYLOAD_LEN; j++)
			WQ_DUMP_DBG(
				DM_TRBUS, DL_INF, "0x%x ",
				*(skb_phy_addr_pool[skb_pool_r_idx].skb->data +
				  j));
#endif

		skb_pool_r_idx = RING_IDX_INC_ONCE(
			skb_pool_r_idx, WOAL_CE_WLAN_TX_SKB_POOL_NUM - 1);
	}
	desc->api.host.packet_cnt = WOAL_CE_WLAN_TX_AMSDU_NUM;
	if (desc->api.host.packet_cnt > 1)
		desc->api.host.flags |= (1 << 6);

	WQ_DBG(DM_TRBUS, DL_INF, "%s: last skb pool r idx:%d\n", __func__,
	       skb_pool_r_idx);

	return WOAL_STATUS_SUCCESS;
}

static void woal_ce_wlan_tx_test_handler(struct wq_pcie *wq_pcie)
{
	int status;
	dma_addr_t phys_addr;
	u32 nbytes = 0;
	struct sk_buff *skb;
	u32 i;

	//if (RING_AVAIL_SPACE(skb_pool_w_idx - 1, skb_pool_r_idx, WOAL_CE_WLAN_TX_SKB_POOL_NUM - 1) < WOAL_CE_WLAN_TX_AMSDU_NUM)
	//	return;

	//if (woal_ce_get_src_sw_lfo(WQ_PCIE_CE_CH_PKT_TX)) {
	while (woal_ce_get_src_sw_lfo(WQ_PCIE_CE_CH_PKT_TX)) {
		nbytes = WOAL_CE_WLAN_TX_SRC_SZ_MAX;
		skb = dev_alloc_skb(nbytes);
		if (!skb)
			BUG_ON(0);

		skb_trim(skb, 0);
		skb_put(skb, sizeof(struct wq_hif_hdr) +
				     sizeof(struct wq_htc_v0) +
				     sizeof(struct txdesc_host));

		if ((skb->len % 4) != 0)
			skb_put(skb, (4 - (skb->len % 4)));

		skb_put(skb, 4); //CRC32
		memset(skb->data, 0, skb->len);
		woal_ce_wlan_tx_build_one_entry(wq_pcie, skb->data, skb->len);

		status = wq_map_memory(wq_pcie, skb->data, &phys_addr, nbytes,
				       DMA_TO_DEVICE);
		if (status != WOAL_STATUS_SUCCESS) {
			WQ_DBG(DM_TRBUS, DL_INF, "%s: wq_map_memory fails!\n",
			       __func__);
			BUG_ON(0);
		}

		wq_sync_memory_for_device(wq_pcie, phys_addr, nbytes,
					  DMA_TO_DEVICE);

		status = woal_ce_send(wq_pcie, WQ_PCIE_CE_CH_PKT_TX, skb,
				      phys_addr, skb->len, CE_DESC_FLAG_INT);
		if (status != WOAL_STATUS_SUCCESS) {
			WQ_DBG(DM_TRBUS, DL_INF, "%s: woal_ce_send fails!\n",
			       __func__);
			BUG_ON(0);
		}
	};
}

static int woal_ce_wlan_tx_test_func(void *data)
{
	struct wq_pcie *wq_pcie = (struct wq_pcie *)data;

	if (!wq_pcie) {
		WQ_DBG(DM_TRBUS, DL_ERR, "wq_pcie is NULL \n");
		return WOAL_STATUS_FAILURE;
	}

	WQ_DBG(DM_TRBUS, DL_INF, "%s->woal_ce_wlan_tx_test_init\n", __func__);

	woal_ce_wlan_tx_test_init(wq_pcie);

	WQ_DBG(DM_TRBUS, DL_INF, "%s->woal_ce_wlan_tx_skb_pool_init\n",
	       __func__);

	woal_ce_wlan_tx_skb_pool_init(wq_pcie);

	while (!kthread_should_stop()) {
		//WQ_DBG(DM_TRBUS, DL_INF, "thread sleep\n");
		//msleep_interruptible(100);
		//WQ_DBG(DM_TRBUS, DL_INF, "thread wakeup\n");

		woal_ce_wlan_tx_test_handler(wq_pcie);
	}

	return 0;
}

static int woal_ce_wlan_tx_test(struct wq_pcie *wq_pcie)
{
	task_id = kthread_run(woal_ce_wlan_tx_test_func, wq_pcie,
			      "woal ce wlan tx test thread");

	if (task_id == NULL) {
		WQ_DBG(DM_TRBUS, DL_INF,
		       "woal ce wlan tx test thread creation fails!\n");
		return WOAL_STATUS_FAILURE;
	}
	WQ_DBG(DM_TRBUS, DL_INF, "%s return SUCCESS\n", __func__);

	return WOAL_STATUS_SUCCESS;
}

#else
//Iterate tx_skb_testcase_t->array test cases mode.
//WQ_TX_ITER_TEST_MODE_NEXT: get next
//WQ_TX_ITER_TEST_MODE_RESET: get from first
#define WQ_TX_ITER_TEST_MODE_RESET 0
#define WQ_TX_ITER_TEST_MODE_NEXT 1

typedef struct {
	u8 chn;
	u8 amsdu_num;
	u8 encap_type;
	u8 tid;
	u16 ethtype;
	u16 skb_len;
} tx_skb_info_t;

typedef struct {
	u32 skb_num;
	tx_skb_info_t skb_info;
} tx_skb_bulk_t;

/* each tx skb bulks sent one time*/
typedef struct {
	u32 skb_bulk_num;
	tx_skb_bulk_t *array;
} tx_skb_testcase_t;

struct {
	uint32_t sn : 12;
	uint32_t rsvd : 20;
} g_pkt_sn;

//Example test case
static tx_skb_bulk_t tx_skb_ethv2_array[] = {
	{ 1, { WQ_PCIE_CE_CH_RAW_TX, 0x3, 0x1, 0x1, 0x8, 100 } },
	{ 2, { WQ_PCIE_CE_CH_RAW_TX, 0x3, 0x1, 0x1, 0x8, 200 } },
	{ 3, { WQ_PCIE_CE_CH_RAW_TX, 0x3, 0x1, 0x1, 0x8, 300 } },
};

static tx_skb_testcase_t tx_skb_test1 = {
	sizeof(tx_skb_ethv2_array) / sizeof(tx_skb_bulk_t),
	tx_skb_ethv2_array,
};

//return next, or NULL
static tx_skb_bulk_t wq_tx_iter_test_bulk(tx_skb_testcase_t *tc, u8 mode)
{
	static tx_skb_testcase_t *op = NULL;
	static u32 iter_idx = 0;
	tx_skb_bulk_t bulk_ret = { 0 };

	if (tc != op) {
		op = tc;
		iter_idx = 0;
	}

	if (mode == WQ_TX_ITER_TEST_MODE_RESET)
		iter_idx = 0;

	if (op && (iter_idx < op->skb_bulk_num)) {
		bulk_ret = op->array[iter_idx];
		iter_idx++;
	}

	return bulk_ret;
}

static u8 eth_dest_addr[] = { 0xBB, 0xAA, 0x34, 0x12, 0x78, 0x56 };
static u8 eth_src_addr[] = { 0x45, 0x60, 0x55, 0x4D, 0x38, 0x56 };

static void wq_tx_build_skb_payload(u16 encap_type, u8 *buf, u16 len)
{
	u32 off = 0;
	u32 i = 0;
	// buld eth header
	switch (encap_type) {
	case 0x1: //Ethernet V2
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		ether_addr_copy(&buf[off], eth_dest_addr);
#else
		(void)memcpy(&buf[off], eth_dest_addr, ETH_ALEN);
#endif
		off += ETH_ALEN;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		ether_addr_copy(&buf[off], eth_src_addr);
#else
		(void)memcpy(&buf[off], eth_src_addr, ETH_ALEN);
#endif
		off += ETH_ALEN;
		buf[off] = 0x8;
		off++;
		buf[off] = 0x0;
		off++;
		break;

	default:
		break;
	}

	for (; off < len - 4; off++, i++)
		buf[off] = (i & 0xF) | 0xF0;

	buf[off++] = 0xDD;
	buf[off++] = 0xCC;
	buf[off++] = 0xBB;
	buf[off++] = 0xAA;
}

#define ADDITIONAL_PLD_LEN 42 //eth_max_len+ip_info

static int wq_if_ipc_tx_pkt_test(u8 hw_txq, u32 pkt_flags, struct sk_buff *skb,
				 u8 pkt_channel, u32 send_count)
{
	struct wq_hif_hdr *hif_hdr = NULL;
	struct wq_htc_v1 *htc_v1 = NULL;
	int ret = 0;
	u32 *crc_ptr = NULL;

	if (!skb->data) {
		WQ_DBG(DM_IPC, DL_ERR, "%s: skb->data is null\n", __func__);
		return -ENODATA;
	}
	if (skb->len < sizeof(struct txdesc_host)) {
		WQ_DBG(DM_IPC, DL_ERR, "%s: skb->len = %d, is not right\n",
		       __func__, skb->len);
		return -ENODATA;
	}

	skb_push(skb, HEADROOM_HIF_HTC);
	hif_hdr = (struct wq_hif_hdr *)skb->data;
	htc_v1 = (struct wq_htc_v1 *)(hif_hdr + 1);
	memset(hif_hdr, 0, HEADROOM_HIF_HTC);

	hif_hdr->ptn = WQ_HIF_HDR_MAGIC;
	hif_hdr->ver = WQ_HIF_HDR_VER_1;
	hif_hdr->qid = WQ_QID_AC_BE;
	hif_hdr->crc = 0;

	//unit test
	htc_v1->flags =
		0x2; //bit[0]=tx;bit[1]=inlcude htc_v1_hdr;bit[2]=no credit
	htc_v1->channel = pkt_channel;

	if (pkt_channel == WQ_PCIE_CE_CH_PKT_TX) {
		htc_v1->buf_len =
			sizeof(struct txdesc_host) + ADDITIONAL_PLD_LEN;
	} else {
		htc_v1->buf_len = cpu_to_le32(
			skb->len - HEADROOM_HIF_HTC); //txdesc_host sz + pkt_len
	}

	if ((skb->len % 4) != 0)
		skb_put(skb, (4 - (skb->len % 4)));

	skb_put(skb, 4); //crc32
	crc_ptr = (u32 *)(skb->data + skb->len - 4);
	*crc_ptr = send_count;

	hif_hdr->dw_len = skb->len >> 2;

	WQ_DBG(DM_IPC, DL_INF,
	       "%s: wq_ll_if_head sz = %d, txdesc_host sz = %d, channel = %d, flags=0x%x, buf_len=%d\n",
	       __func__, (uint16_t)sizeof(struct wq_htc_v1),
	       (uint16_t)sizeof(struct txdesc_host), pkt_channel, htc_v1->flags,
	       htc_v1->buf_len);

#if 0
	WQ_DUMP_DBG(DM_TRBUS, DL_INF, "hif and htc hdr dump,size %d: %*ph\n",
		    HEADROOM_HIF_HTC, HEADROOM_HIF_HTC, skb->data);
#endif
	return ret;
}

static int wq_tx_pkt_data_prepare_test(struct wq_pcie *wq_pcie, u8 chn_type)
{
	int ret = WOAL_STATUS_SUCCESS;
	struct sk_buff *tx_entry_skb = wq_pcie->tx_entry_skb;
	struct sk_buff *skb = NULL;
	struct sk_buff *pld_skb = NULL;
	tx_skb_bulk_t skb_bulk_info;
	u8 *payload_ptr;
	u8 is_ll = 0;
	u8 amsdu_num;
	u8 chn;
	u32 i;
	u32 j;
	u32 skb_cnt = 0;
	u32 *pvalue;

	if (!tx_entry_skb) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: wq_pcie->tx_entry_skb is NULL\n", __func__);
		return WOAL_STATUS_FAILURE;
	}

	memset(tx_entry_skb->data, 0, tx_entry_skb->len);
	skb_trim(tx_entry_skb, 0);
	pvalue = (u32 *)tx_entry_skb->data;

	skb_bulk_info =
		wq_tx_iter_test_bulk(&tx_skb_test1, WQ_TX_ITER_TEST_MODE_RESET);
	for (;;) {
		if (skb_bulk_info.skb_num == 0) {
			// No valid test skb procued. Else reach the last bulk info
			if (skb_cnt == 0)
				ret = WOAL_STATUS_FAILURE;

			break;
		}

		amsdu_num = skb_bulk_info.skb_info.amsdu_num;
		chn = skb_bulk_info.skb_info.chn;

		is_ll = (chn == WQ_PCIE_CE_CH_PKT_TX);
		for (i = 0;
		     i < skb_bulk_info.skb_num && skb_cnt < TX_TEST_MAX_SKB_NUM;
		     i++) {
			struct txdesc_host *desc = NULL;

			if (chn != WQ_PCIE_CE_CH_RAW_TX &&
			    chn != WQ_PCIE_CE_CH_PKT_TX) {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "%s: skb_info.chn:%d is NOT supported\n",
				       __func__, skb_bulk_info.skb_info.chn);
				return WOAL_STATUS_FAILURE;
			}

			if (amsdu_num >= NX_TX_PAYLOAD_MAX || amsdu_num <= 0) {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "%s: skb_info.amsdu_num:%d <= 0 or > MAX:%d\n",
				       __func__,
				       skb_bulk_info.skb_info.amsdu_num,
				       NX_TX_PAYLOAD_MAX);
				return WOAL_STATUS_FAILURE;
			}

			if (chn == WQ_PCIE_CE_CH_PKT_TX)
				skb = wq_pcie->tx_skb[skb_cnt / amsdu_num];
			else
				skb = wq_pcie->tx_skb[skb_cnt];

			if (!skb) {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "%s: wq_pcie->tx_skb[%d] is NULL\n",
				       __func__, skb_cnt);
				return WOAL_STATUS_FAILURE;
			}

			if (!is_ll || (skb_cnt % amsdu_num == 0)) {
				memset(skb->data, 0, skb->len);
				skb_trim(skb, 0);
				skb_reserve(skb,
					    sizeof(struct wq_hif_hdr) +
						    sizeof(struct wq_htc_v0));
				skb_put(skb, sizeof(struct txdesc_host));
				desc = (struct txdesc_host *)skb->data;
				memset(desc, 0, sizeof(*desc));
				desc->api.host.packet_len[0] =
					skb_bulk_info.skb_info.skb_len;
				desc->api.host.packet_cnt = 1;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
				ether_addr_copy(desc->api.host.ethhdr.h_dest,
						eth_dest_addr);
				ether_addr_copy(desc->api.host.ethhdr.h_source,
						eth_src_addr);
#else
				(void)memcpy(desc->api.host.ethhdr.h_dest,
						eth_dest_addr, ETH_ALEN);
				(void)memcpy(desc->api.host.ethhdr.h_source,
						eth_src_addr, ETH_ALEN);
#endif
				desc->api.host.ethhdr.h_proto =
					skb_bulk_info.skb_info.ethtype;
				desc->api.host.sn = g_pkt_sn.sn++;
				desc->api.host.tid = skb_bulk_info.skb_info.tid;
				desc->api.host.flags = 0x1;
				desc->api.host.encap_type =
					skb_bulk_info.skb_info.encap_type;
				desc->api.host.end_marker =
					HOST_DESC_END_MARKER;
			}

			if (is_ll) {
				pld_skb = wq_pcie->tx_pld_skb[skb_cnt];

				if (!pld_skb) {
					WQ_DBG(DM_GENERIC, DL_ERR,
					       "%s: wq_pcie->tx_pld_skb[%d] is NULL\n",
					       __func__, skb_cnt);
					return WOAL_STATUS_FAILURE;
				}
				memset(pld_skb->data, 0, pld_skb->len);
				skb_trim(pld_skb, 0);
				skb_put(pld_skb,
					skb_bulk_info.skb_info.skb_len);
				wq_tx_build_skb_payload(
					skb_bulk_info.skb_info.encap_type,
					pld_skb->data,
					skb_bulk_info.skb_info.skb_len);

				//add additional payload in skb
				skb_put(skb, ADDITIONAL_PLD_LEN);
				payload_ptr =
					(u8 *)(skb->data +
					       sizeof(struct txdesc_host));
				memcpy(payload_ptr, pld_skb->data,
				       ADDITIONAL_PLD_LEN);
				//skb_put(skb, 2);//word align:ADDITIONAL_PLD_LEN(42)+txdesc_host(96) is not word align

				if (wq_pcie->tx_pld_skb_data_pa[skb_cnt]) {
					wq_unmap_memory(
						wq_pcie,
						&wq_pcie->tx_pld_skb_data_pa
							 [skb_cnt],
						TX_TEST_LEN, DMA_TO_DEVICE);
				}
				if (WOAL_STATUS_FAILURE ==
				    wq_map_memory(
					    wq_pcie,
					    wq_pcie->tx_pld_skb[skb_cnt]->data,
					    &wq_pcie->tx_pld_skb_data_pa[skb_cnt],
					    TX_TEST_LEN, DMA_TO_DEVICE)) {
					WQ_DBG(DM_TRBUS, DL_ERR,
					       "%s:DMA map pld memory fail!!!(0x%p) to dma pa(0x%llx),len(%d)!\n",
					       __func__,
					       wq_pcie->tx_pld_skb[skb_cnt]
						       ->data,
					       (u64)wq_pcie->tx_pld_skb_data_pa
						       [skb_cnt],
					       wq_pcie->tx_pld_skb[skb_cnt]
						       ->len);
					return WOAL_STATUS_FAILURE;
				} else if (desc) {
					WQ_DBG(DM_TRBUS, DL_INF,
					       "%s:DMA map pld memory(0x%p) to dma pa(0x%llx),len(%d)!\n",
					       __func__,
					       wq_pcie->tx_pld_skb[skb_cnt]
						       ->data,
					       (u64)wq_pcie->tx_pld_skb_data_pa
						       [skb_cnt],
					       wq_pcie->tx_pld_skb[skb_cnt]
						       ->len);
					desc->api.host.packet_addr[skb_cnt %
								   amsdu_num] =
						(u32)wq_pcie->tx_pld_skb_data_pa
							[skb_cnt];
					desc->api.host.packet_len[skb_cnt %
								  amsdu_num] =
						skb_bulk_info.skb_info.skb_len;
					desc->api.host.packet_cnt =
						(skb_cnt % amsdu_num) + 1;
				}
			} else {
				skb_put(skb, skb_bulk_info.skb_info.skb_len);
				payload_ptr =
					(u8 *)(skb->data +
					       sizeof(struct txdesc_host));
				wq_tx_build_skb_payload(
					skb_bulk_info.skb_info.encap_type,
					payload_ptr,
					skb_bulk_info.skb_info.skb_len);
			}

			if (!is_ll ||
			    (skb_cnt % amsdu_num == (amsdu_num - 1))) {
				if (wq_if_ipc_tx_pkt_test(3, 0, skb, chn,
							  send_count))
					return WOAL_STATUS_FAILURE;

				WQ_DUMP_DBG(DM_TRBUS, DL_INF,
					    "%s:    skb[%d] dump,size %d:",
					    __func__, skb_cnt, skb->len);
				WQ_DUMP_DBG(DM_TRBUS, DL_INF, "\n");
				for (j = 0; j < skb->len; j++) {
					WQ_DUMP_DBG(DM_TRBUS, DL_INF, "0x%x ",
						    *(skb->data + j));
				}

				if (WOAL_STATUS_FAILURE ==
				    wq_map_memory(
					    wq_pcie, skb->data,
					    &wq_pcie->tx_skb_data_pa[skb_cnt],
					    TX_TEST_LEN, DMA_TO_DEVICE)) {
					WQ_DBG(DM_TRBUS, DL_ERR,
					       "%s:DMA map memory fail!!!(0x%p) to dma pa(0x%llx),len(%d)!\n",
					       __func__, skb->data,
					       (u64)wq_pcie
						       ->tx_skb_data_pa[skb_cnt],
					       skb->len);
					return WOAL_STATUS_FAILURE;
				} else {
					WQ_DBG(DM_TRBUS, DL_INF,
					       "%s:DMA map memory(0x%p) to dma pa(0x%llx),len(%d)!\n",
					       __func__, skb->data,
					       (u64)wq_pcie
						       ->tx_skb_data_pa[skb_cnt],
					       skb->len);
				}

				skb_put(tx_entry_skb, sizeof(u32));
				*pvalue = wq_pcie->tx_skb_data_pa[skb_cnt];
				++pvalue;
				skb_put(tx_entry_skb, sizeof(u32));
				*pvalue = skb->len;
				++pvalue;
			}

			++skb_cnt;
		}

		skb_bulk_info = wq_tx_iter_test_bulk(&tx_skb_test1,
						     WQ_TX_ITER_TEST_MODE_NEXT);
	}

	return ret;
}

/**
 *  @brief This function downloads command to the wq_pcie.
 *
 *  @param wq_pcie A pointer to struct wq_pcie structure
 *  @param pmbuf     A pointer to buffer (data_len should include PCIE header)
 *
 *  @return 	     WOAL_STATUS_SUCCESS or WOAL_STATUS_FAILURE
 */
int wq_pcie_wlan_tx_outbound_test(struct wq_pcie *wq_pcie)
{
	int ret = WOAL_STATUS_FAILURE;
	u16 i;
	u32 *pvalue;
	ENTER();

	//NOTE: func's 'chan_type'=0xFF param is not used.
	if (wq_tx_pkt_data_prepare_test(wq_pcie, 0xFF)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s:wq_tx_pkt_data_prepare_test fail!\n", __func__);
		goto done;
	}

	WQ_DUMP_DBG(DM_TRBUS, DL_INF, "\n");
	WQ_DUMP_DBG(DM_TRBUS, DL_INF,
		    "%s: tx entry skb dump,size %d:", __func__,
		    wq_pcie->tx_entry_skb->len);
	WQ_DUMP_DBG(DM_TRBUS, DL_INF, "\n");
	pvalue = (u32 *)wq_pcie->tx_entry_skb->data;
	for (i = 0; i < (wq_pcie->tx_entry_skb->len / 8); i++) {
		WQ_DUMP_DBG(DM_TRBUS, DL_INF, "0x%08x:0x%08x        ", *pvalue,
			    *(pvalue + 1));
		pvalue += 2;
	}
	WQ_DUMP_DBG(DM_TRBUS, DL_INF, "\n");

	if (wq_pcie->tx_entry_skb_data_pa) {
		wq_unmap_memory(wq_pcie, &wq_pcie->tx_entry_skb_data_pa,
				TX_TEST_LEN, DMA_TO_DEVICE);
	}

	if (WOAL_STATUS_FAILURE == wq_map_memory(wq_pcie,
						 wq_pcie->tx_entry_skb->data,
						 &wq_pcie->tx_entry_skb_data_pa,
						 TX_TEST_LEN, DMA_TO_DEVICE)) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s:DMA map memory fail!!!(0x%p) to dma pa(0x%llx),len(%d)!\n",
		       __func__, wq_pcie->tx_entry_skb->data,
		       (u64)wq_pcie->tx_entry_skb_data_pa,
		       wq_pcie->tx_entry_skb->len);
		goto done;
	} else {
		WQ_DBG(DM_TRBUS, DL_INF,
		       "%s:DMA map memory(0x%p) to dma pa(0x%llx),len(%d)!\n",
		       __func__, wq_pcie->tx_entry_skb->data,
		       (u64)wq_pcie->tx_entry_skb_data_pa,
		       wq_pcie->tx_entry_skb->len);
	}

	if (wq_pcie_outbound_set(wq_pcie, wq_pcie->tx_entry_skb_data_pa,
				 wq_pcie->tx_entry_skb->len)) {
		goto done;
	}

	wq_pcie->tx_cmd_done = false;
	send_count++;
	ret = WOAL_STATUS_SUCCESS;
done:
	LEAVE();
	return ret;
}
#endif

int wq_pcie_test_start(struct wq_pcie *wq_pcie)
{
	int i;

	wq_pcie->tx_entry_skb = dev_alloc_skb(TX_TEST_LEN);
	if (!wq_pcie->tx_entry_skb) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "%s: tx entry skb alloc_skb failed\n", __func__);
	}

	for (i = 0; i < TX_TEST_MAX_SKB_NUM; i++) {
		wq_pcie->tx_skb[i] = dev_alloc_skb(TX_TEST_LEN);
		if (!wq_pcie->tx_skb[i]) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s: tx skb dev_alloc_skb failed\n", __func__);
		}
	}
	for (i = 0; i < TX_TEST_MAX_SKB_NUM; i++) {
		wq_pcie->tx_pld_skb[i] = dev_alloc_skb(TX_TEST_LEN);
		if (!wq_pcie->tx_pld_skb[i]) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s: tx pld skb dev_alloc_skb failed\n",
			       __func__);
		}
	}

#ifdef MUTLI_INBOUD_TEST
	//woal_bar_io_wr_test(wq_pcie);
	woal_multi_inbound_test(wq_pcie);
#endif
#ifdef OUTBOUND_TEST
	wq_pcie_outbound_test(wq_pcie);
#endif

#if defined(TX_CE_UNIT_TEST)
	WQ_DBG(DM_TRBUS, DL_INF, "%s->woal_ce_wlan_tx_test\n", __func__);

	woal_ce_wlan_tx_test(wq_pcie);
#else
	wq_pcie_wlan_tx_outbound_test(wq_pcie);
#endif

#ifdef CE_UNIT_TEST
	(void)wq_ce_test_start(wq_pcie);
#endif // CE_UNIT_TEST

#ifdef RAE_TEST
	wq_pcie_rae_test(wq_pcie);
#endif
	return 0;
}

void wq_pcie_test_stop(struct wq_pcie *wq_pcie)
{
	int i;

	wq_unmap_memory(wq_pcie, &wq_pcie->tx_entry_skb_data_pa, TX_TEST_LEN,
			DMA_TO_DEVICE);

	for (i = 0; i < TX_TEST_MAX_SKB_NUM; i++)
		wq_unmap_memory(wq_pcie, &wq_pcie->tx_pld_skb_data_pa[i],
				TX_TEST_LEN, DMA_TO_DEVICE);

	for (i = 0; i < TX_TEST_MAX_SKB_NUM; i++)
		wq_unmap_memory(wq_pcie, &wq_pcie->tx_skb_data_pa[i],
				TX_TEST_LEN, DMA_TO_DEVICE);

#ifdef MAP_SIGNLE_TEST
	wq_unmap_memory(wq_pcie, &outbound_buf_pa, MAP_TEST_SIZE,
			DMA_TO_DEVICE);
#else
	wq_unmap_consistent(wq_pcie, &outbound_buf, &outbound_buf_pa,
			    MAP_TEST_SIZE);
#endif

#ifdef CE_UNIT_TEST
	wq_ce_test_stop(wq_pcie);
#endif // CE_UNIT_TEST
}
