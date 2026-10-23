#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/bsearch.h>
#include <linux/cdev.h>

#include "ingenic_can.h"

#define MODNAME "ingenic_can"
#define TX_ECHO_SKB_MAX 1

#define DMA_RX_DESC_NUM 8
#define DMA_RX_DESC_DTC 8
#define DMA_BUFF_SIZE 16 /*A frame of data is 16 bytes*/
#define DMA_RX_BUFF_SIZE (DMA_BUFF_SIZE * DMA_RX_DESC_DTC)
#define INGENIC_CAN_FILTER_ALL_ACCEPT 0x1FFFFFFF /*Filter Settings all pass, SocketCan have software filters*/
#define PDMA_IOBASE (0xb3420000)
#define DMA_CH_DTC (0x08)
#define DMA_CHA_DTC(ch) ((void __iomem *)(PDMA_IOBASE + ch * 0x20 + DMA_CH_DTC))
#define INGENIC_CAN_DEBUG


static struct workqueue_struct *can_auto_recovery_wq;

#define SIOCSBUSOFFSWITCH 0x89F1 // 自定义命令号
#define SIOCSSSPSWITCH    0x89F2
#define SIOCSMULFRAME     0x89F6
#define SIOCSMULFRAMETEST 0x89F7


static const struct can_bittiming_const ingenic_can_bittiming_const = {
	.name = MODNAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 2,        //ISO-11898
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 64,
	.brp_inc = 1,
};

/*
static struct baudregs_t
{
	unsigned int baud;
	unsigned int tseg2 : 3;
	unsigned int tseg1 : 4;
	unsigned int sjw : 2;
	unsigned int cancs : 6;
} baudregs[] = {
#if (CONFIG_CAN_CLOCK == 24)
	{125000, 7, 14, 3, 7},
	{250000, 7, 14, 3, 3},
	{500000, 7, 14, 3, 1},
	{1000000, 7, 14, 3, 0},
#endif
#if (CONFIG_CAN_CLOCK == 12)
	{100000, 7, 14, 3, 4},
	{125000, 7, 14, 3, 3},
	{250000, 7, 14, 3, 1},
	{500000, 7, 14, 3, 0},
	{1000000, 1, 2, 3, 1},
#endif
#if (CONFIG_CAN_CLOCK == 8)
	{5000, 7, 15, 3, 63},
	{10000, 7, 15, 3, 31},
	{20000, 7, 15, 3, 15},
	{50000, 7, 10, 3, 7},
	{100000, 7, 10, 3, 3},
	{125000, 6, 7, 3, 3},
	{250000, 2, 3, 3, 3},
	{500000, 6, 7, 3, 0},
	{1000000, 2, 3, 3, 0},
#endif
};
*/

static uint32_t can_readl(struct ingenic_can_priv *priv, uint32_t reg_off)
{
	return readl(priv->reg_base + reg_off);
}

static void can_writel(struct ingenic_can_priv *priv, uint32_t val, uint32_t reg_off)
{
	writel(val, priv->reg_base + reg_off);
}

#ifdef INGENIC_CAN_DEBUG
__attribute__((__unused__)) static void ingenic_can_dump_registers(struct ingenic_can_priv *priv)
{
	void __iomem *ingenic_can_iomem = priv->reg_base;
	struct net_device *dev = priv->dev;

	netdev_info(dev, "---------------------------\n");
	netdev_info(dev, "CANMODE 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_MODE), can_readl(priv, CAN_MODE));
	// write only
	/*netdev_info(dev, "CANCMD 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_CMD), can_readl(priv, CAN_CMD));*/
	netdev_info(dev, "CANSTAT 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_STATUS), can_readl(priv, CAN_STATUS));
	// cleared when read
	//netdev_info(dev, "CANINT 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_INTF), can_readl(priv, CAN_INTF));
	netdev_info(dev, "CANBTR 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_BTR), can_readl(priv, CAN_BTR));
	netdev_info(dev, "CANFLT 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_FTER), can_readl(priv, CAN_FTER));
	//cleared when read ECC(0~4)
	//netdev_info(dev, "CANERR 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_ECC), can_readl(priv, CAN_ECC));
	netdev_info(dev, "CANAFID0 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_AFID0), can_readl(priv, CAN_AFID0));
	netdev_info(dev, "CANAFMK0 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_AFMK0), can_readl(priv, CAN_AFMK0));
	netdev_info(dev, "CANAFID1 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_AFID1), can_readl(priv, CAN_AFID1));
	netdev_info(dev, "CANAFMK1 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_AFMK1), can_readl(priv, CAN_AFMK1));
	netdev_info(dev, "CANAFID2 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_AFID2), can_readl(priv, CAN_AFID2));
	netdev_info(dev, "CANAFMK2 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_AFMK2), can_readl(priv, CAN_AFMK2));
	netdev_info(dev, "CANAFID3 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_AFID3), can_readl(priv, CAN_AFID3));
	netdev_info(dev, "CANAFMK3 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_AFMK3), can_readl(priv, CAN_AFMK3));
	netdev_info(dev, "CANTFIR 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_TFIR), can_readl(priv, CAN_TFIR));
	netdev_info(dev, "CANTXID 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_TXID), can_readl(priv, CAN_TXID));
	netdev_info(dev, "CANTXDATA0 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_TXDATA), can_readl(priv, CAN_TXDATA));
	netdev_info(dev, "CANTXDATA1 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_TXDATA1), can_readl(priv, CAN_TXDATA1));
	netdev_info(dev, "CANRFIR 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_RFIR), can_readl(priv, CAN_RFIR));
	netdev_info(dev, "CANRXID 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_RXID), can_readl(priv, CAN_RXID));
	netdev_info(dev, "CANRXDATA0 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_RXDATA), can_readl(priv, CAN_RXDATA));
	netdev_info(dev, "CANRXDATA1 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_RXDATA1), can_readl(priv, CAN_RXDATA1));
	netdev_info(dev, "CANSL 0x%08x : 0x%08x\n", (uint32_t)(ingenic_can_iomem + CAN_SLTM), can_readl(priv, CAN_SLTM));
}
#endif

static int ingenic_can_close(struct net_device *dev)
{
	unsigned int reg_ecc;
    struct ingenic_can_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);
	can_writel(priv, CAN_MODE_RSTM, CAN_MODE);
	if (priv->can.ctrlmode == CAN_CTRLMODE_LOOPBACK)
	{
		can_writel(priv, CAN_SLTM_UNLOCK_CANSLKEY, CAN_SLTM);
		can_writel(priv, 0, CAN_SLTM);
	}

	can_writel(priv, CAN_CMD_CTB | CAN_CMD_RRB, CAN_CMD);
	can_readl(priv, CAN_INTF);
	reg_ecc = can_readl(priv, CAN_ECC);
	reg_ecc &= ~(CAN_ECC_TEC | CAN_ECC_REC);
	can_writel(priv, reg_ecc, CAN_ECC);      //Clear TEC, REC

#ifdef CONFIG_INGENIC_CAN_USE_PDMA
	dmaengine_terminate_all(priv->rxchan);
	dmaengine_terminate_all(priv->txchan);
#endif
	priv->can.ctrlmode = 0;

	priv->can.state = CAN_STATE_STOPPED;

	close_candev(dev);

	return 0;
}

/*
static int can_brt_compareints(const void *key, const void *elt)
{
	unsigned long *d = (unsigned long *)key;
	struct baudregs_t *b = (struct baudregs_t *)elt;
	if (*d > b->baud)
		return 1;
	else if (*d < b->baud)
		return -1;
	return 0;
}
*/

static int ingenic_can_set_baudrate(struct net_device *dev)
{
	struct ingenic_can_priv *priv = netdev_priv(dev);
	struct can_bittiming brt = priv->can.bittiming;
	struct baudregs_t *b = NULL;
	int reg_btr;
	// CLK FIX AS 24M
/*	if (brt.bitrate < 500000)
	{
		reg_btr |= (1 << 15); // 较低波特率，建议使用三次采样，介于TSEG1和TSEG2
							  // 高于500K的，使用单次采样，采样点在TSEG1和TSEG2之间
	}
*/
	if(priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES){
        reg_btr |= CAN_BTR_SAM;
    }

#if 0
	b = (struct baudregs_t *)bsearch((const void *)&brt.bitrate, (const void *)baudregs, ARRAY_SIZE(baudregs),
									 sizeof(struct baudregs_t), can_brt_compareints);
	// printk("ingenic_can: baud = %d, tseg2= %d tseg1= %d sjw =%d cancs =%d\n", b->baud, b->tseg2, b->tseg1, b->sjw, b->cancs);
	if (b)
	{
		reg_btr |= ((b->tseg2 << 12) | (b->tseg1 << 8) | (b->sjw << 6) | (b->cancs << 0));
	}
	else
	{
		printk("the current baud rate is not supported temporarily\n");
		netdev_err(dev, "set can bitrate:%d failed !\n", brt.bitrate);
		return -1;
	}
	can_writel(priv, reg_btr, CAN_BTR);
	netdev_info(dev, "set can bitrate:%d ok !\n", brt.bitrate);
	return 0;
#else

    printk("%s() : brt.sjw = %d, brt.brp=%d, brt.phase_seg2=%d, brt.phase_seg1=%d, brt.prop_seg=%d\n", __func__, brt.sjw, brt.brp, brt.phase_seg2, brt.phase_seg1, brt.prop_seg);
	reg_btr |= (((brt.sjw-1) << 6)&CAN_BTR_SJW) | ((brt.brp-1)&CAN_BTR_CANCS) | (((brt.phase_seg2-1) << 12)&CAN_BTR_TSEG2) | (((brt.phase_seg1+brt.prop_seg-1) << 8)&CAN_BTR_TSEG1);

    netdev_info(dev, "set can bitrate:%d ok !\n",brt.bitrate);
	can_writel(priv, reg_btr, CAN_BTR);

    return 0;
#endif

}

static int ingenic_can_get_berr_counter(const struct net_device *dev,
										struct can_berr_counter *bec)
{
	struct ingenic_can_priv *priv = netdev_priv(dev);
	unsigned int reg_err;

	reg_err = can_readl(priv, CAN_ECC);
	bec->rxerr = (reg_err >> 16) & 0xFF;
	bec->txerr = (reg_err >> 24) & 0xFF;

	return 0;
}

#ifdef CONFIG_INGENIC_CAN_USE_PDMA
static int ingenic_can_dma_rxtx_config(struct ingenic_can_priv *priv)
{
	struct dma_slave_config tx_config;
	struct dma_slave_config rx_config;

	tx_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	tx_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	tx_config.src_maxburst = 4;
	tx_config.dst_maxburst = 4;
	tx_config.slave_id = 0xffffffff;
	tx_config.direction = DMA_MEM_TO_DEV;
	tx_config.dst_addr = (dma_addr_t)(priv->reg_base_phy + CAN_TFIR);
	dmaengine_slave_config(priv->txchan, &tx_config);

	rx_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	rx_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	rx_config.src_maxburst = 16;
	rx_config.dst_maxburst = 16;
	rx_config.slave_id = 0xffffffff;
	rx_config.direction = DMA_DEV_TO_MEM;
	rx_config.src_addr = (dma_addr_t)(priv->reg_base_phy + CAN_RFIR);
	dmaengine_slave_config(priv->rxchan, &rx_config);

	return 0;
}

static void dma_rx_callback(void *data)
{
	//TODO
}

static void dma_tx_callback(void *data)
{
	//TODO
}

static int ingenic_can_dma_rx_pending(struct ingenic_can_priv *priv)
{
	struct dma_async_tx_descriptor *rxdesc;
	struct dma_chan *rxchan = priv->rxchan;

	if (!rxchan)
	{
		netdev_err(priv->dev, "no dma rx channel\n");
		return -ENODEV;
	}

	memset(priv->buffer, 0, (DMA_BUFF_SIZE * DMA_RX_DESC_NUM));

	rxdesc = dmaengine_prep_dma_cyclic(rxchan,
									   priv->buffer_dma,
									   (DMA_RX_BUFF_SIZE * DMA_RX_DESC_NUM),
									   DMA_RX_BUFF_SIZE,
									   DMA_DEV_TO_MEM,
									   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!rxdesc)
	{
		netdev_err(priv->dev, "rxdesc: device_prep_dma_cyclic error\n");
		dmaengine_terminate_all(rxchan);
		return -1;
	}

	rxdesc->callback = dma_rx_callback;
	rxdesc->callback_param = NULL;

	dmaengine_submit(rxdesc);
	dma_async_issue_pending(rxchan);

	return 0;
}

static int ingenic_can_dma_tx_pending(struct ingenic_can_priv *priv)
{
	struct dma_async_tx_descriptor *txdesc;
	struct dma_chan *txchan = priv->txchan;

	if (!txchan)
	{
		netdev_err(priv->dev, "no dma tx channel\n");
		return -ENODEV;
	}

	sg_init_one(priv->sg_tx, priv->txbuffer, DMA_BUFF_SIZE);

	if (dma_map_sg(priv->txchan->device->dev,
				   priv->sg_tx, 1, DMA_TO_DEVICE) != 1)
	{
		netdev_err(priv->dev, "dma_map_sg tx error\n");
	}

	txdesc = dmaengine_prep_slave_sg(txchan,
									 priv->sg_tx,
									 1,
									 DMA_MEM_TO_DEV,
									 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!txdesc)
	{
		netdev_err(priv->dev, "txdesc: device_prep_slave_sg error\n");
		goto unmap_sg;
	}

	txdesc->callback = dma_tx_callback;
	txdesc->callback_param = NULL;

	dmaengine_submit(txdesc);
	dma_async_issue_pending(txchan);

	return 0;
unmap_sg:
	dma_unmap_sg(txchan->device->dev, priv->sg_tx, 1, DMA_MEM_TO_DEV);
	dmaengine_terminate_all(txchan);

	return -1;
}
#endif

static int ingenic_can_send_data(struct ingenic_can_priv *priv, unsigned type, unsigned id, unsigned data0, unsigned data1)
{
	int timeout = 1000;
	__maybe_unused unsigned int *txdata = (unsigned int *)priv->txbuffer;

	while (priv->flag && timeout--)
	{
		udelay(10);
	}

	if (timeout <= 0)
	{
		netdev_err(priv->dev, "error, tx buffer full\n");
		return -EBUSY;
	}

	priv->flag = CAN_SENDING;

#ifdef CONFIG_INGENIC_CAN_USE_PDMA
	txdata[0] = type & 0xFF;
	txdata[1] = id;
	txdata[2] = data0;
	txdata[3] = data1;

	ingenic_can_dma_tx_pending(priv);

	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
	{
		mdelay(1);
		can_writel(priv, CAN_CMD_SRR, CAN_CMD);
	}
#else
	can_writel(priv, (type & 0xFF), CAN_TFIR);
	can_writel(priv, id, CAN_TXID);
	can_writel(priv, data0, CAN_TXDATA);
	can_writel(priv, data1, CAN_TXDATA1);
	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
	{
		mdelay(1);
		can_writel(priv, CAN_CMD_SRR, CAN_CMD);
	}
	else
	{
		can_writel(priv, CAN_CMD_TR, CAN_CMD);
	}
#endif

	return 0;
}

static int ingenic_can_set_filter(struct ingenic_can_priv *priv)
{
	struct can_filter rf[4];

	rf[0].can_id = 0;
	rf[0].can_mask = INGENIC_CAN_FILTER_ALL_ACCEPT;
	rf[1].can_id = 0;
	rf[1].can_mask = INGENIC_CAN_FILTER_ALL_ACCEPT;
	rf[2].can_id = 0;
	rf[2].can_mask = INGENIC_CAN_FILTER_ALL_ACCEPT;
	rf[3].can_id = 0;
	rf[3].can_mask = INGENIC_CAN_FILTER_ALL_ACCEPT;

	can_writel(priv, rf[0].can_id, CAN_AFID0);
	can_writel(priv, rf[0].can_mask, CAN_AFMK0);
	can_writel(priv, rf[1].can_id, CAN_AFID1);
	can_writel(priv, rf[1].can_mask, CAN_AFMK1);
	can_writel(priv, rf[2].can_id, CAN_AFID2);
	can_writel(priv, rf[2].can_mask, CAN_AFMK2);
	can_writel(priv, rf[3].can_id, CAN_AFID3);
	can_writel(priv, rf[3].can_mask, CAN_AFMK3);

	return 0;
}

static int ingenic_can_set_normal_mode(struct ingenic_can_priv *priv)
{
#ifdef CONFIG_INGENIC_CAN_USE_PDMA
	/*set auto send and enable rx dma */
	can_writel(priv, CAN_MODE_RXCNT4 | CAN_MODE_TXAR | CAN_MODE_DMAEN | CAN_MODE_ERAR, CAN_MODE);
	/*set dma and some error interrupt*/
	can_writel(priv, CAN_ALL_WARNING_INT_EN | CAN_INTE_DMAIE | CAN_INTE_TIE, CAN_INTF);
#else
	/*set auto send and enable rx dma */
	can_writel(priv, CAN_MODE_RXCNT4 | CAN_MODE_ERAR, CAN_MODE);
	/*set dma and some error interrupt*/
	/*can_writel(priv, CAN_ALL_WARNING_INT_EN | CAN_INTE_RIE | CAN_INTE_TIE, CAN_INTF);*/
	can_writel(priv, CAN_INTE_ALL, CAN_INTF);
#endif

	return 0;
}

static int ingenic_can_set_loopback_mode(struct ingenic_can_priv *priv)
{
	/*unlock the CANSLKEY*/
	can_writel(priv, CAN_SLTM_UNLOCK_CANSLKEY, CAN_SLTM);
	/*enable canslen and cantxoff*/
	can_writel(priv, CAN_SLTM_UNLOCK_CANSLKEY | CAN_SLTM_CANSLEN | CAN_SLTM_CANTXOFF, CAN_SLTM);
#ifdef CONFIG_INGENIC_CAN_USE_PDMA
	/*set loopback mode*/
	can_writel(priv, CAN_MODE_RXCNT4 | CAN_MODE_STE | CAN_MODE_DMAEN, CAN_MODE);
	/*set dma and some error interrupt*/
	can_writel(priv, CAN_ALL_WARNING_INT_EN | CAN_INTE_DMAIE | CAN_INTE_TIE, CAN_INTF);
#else
	/*set loopback mode*/
	can_writel(priv, CAN_MODE_RXCNT4 | CAN_MODE_STE, CAN_MODE);
	/*set dma and some error interrupt*/
	can_writel(priv, CAN_ALL_WARNING_INT_EN | CAN_INTE_RIE | CAN_INTE_TIE, CAN_INTF);
#endif

	return 0;
}

static int ingenic_can_set_listenonly_mode(struct ingenic_can_priv *priv)
{
#ifdef CONFIG_INGENIC_CAN_USE_PDMA
	/*set auto send and enable rx dma */
	can_writel(priv, CAN_MODE_LOM | CAN_MODE_RXCNT4 | CAN_MODE_DMAEN, CAN_MODE);
	//	can_writel(priv, CAN_MODE_LOM | CAN_MODE_RXCNT4 | CAN_MODE_TXAR | CAN_MODE_DMAEN, CAN_MODE);
	/*set dma and some error interrupt*/
	can_writel(priv, CAN_ALL_WARNING_INT_EN | CAN_INTE_DMAIE, CAN_INTF);
#else
	/*set auto send and enable rx dma */
	can_writel(priv, CAN_MODE_LOM | CAN_MODE_RXCNT4, CAN_MODE);
	/*set dma and some error interrupt*/
	can_writel(priv, CAN_ALL_WARNING_INT_EN | CAN_INTE_RIE, CAN_INTF);
#endif

	return 0;
}

static int ingenic_can_set_work_mode(struct ingenic_can_priv *priv)
{
	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		ingenic_can_set_loopback_mode(priv);
	else if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		ingenic_can_set_listenonly_mode(priv);
	else
		ingenic_can_set_normal_mode(priv);

	return 0;
}

static int ingenic_can_chip_start(struct ingenic_can_priv *priv)
{
	unsigned int reg_ecc;
//	struct net_device *dev = priv->dev;
	can_writel(priv, CAN_MODE_RSTM, CAN_MODE);
	can_writel(priv, CAN_CMD_CTB | CAN_CMD_RRB, CAN_CMD);

	reg_ecc = can_readl(priv, CAN_ECC);
	reg_ecc &= ~(CAN_ECC_TEC | CAN_ECC_REC);
	can_writel(priv, reg_ecc, CAN_ECC);      //Clear TEC, REC

	priv->record = 0;
	priv->rece_sum = 0;
	priv->trans_sum = 0;
	priv->flag = CAN_SEND_OVER;
//	ingenic_can_set_baudrate(dev);
	ingenic_can_set_filter(priv);
	/*unsigned int reg;*/
	/*reg = can_readl(priv, CAN_BTR);*/
	/*printk("<%s>\n[%s %d]: 0x%x\n", __FILE__, __func__, __LINE__, reg); */
#ifdef CONFIG_INGENIC_CAN_USE_PDMA
	ingenic_can_dma_rxtx_config(priv);
	ingenic_can_set_work_mode(priv);
	ingenic_can_dma_rx_pending(priv);
	priv->can.state = CAN_STATE_ERROR_ACTIVE;
#else
	ingenic_can_set_work_mode(priv);
	priv->can.state = CAN_STATE_ERROR_ACTIVE;
#endif

	return 0;
}

static int ingenic_can_restart_set_mode(struct net_device *dev, enum can_mode mode)
{
	struct ingenic_can_priv *priv = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&priv->txrx_lock, flags);
	switch (mode)
	{
	case CAN_MODE_START:

#ifdef CONFIG_INGENIC_CAN_USE_PDMA
		dmaengine_terminate_all(priv->rxchan);
		dmaengine_terminate_all(priv->txchan);
#endif
		ingenic_can_chip_start(priv);
		netif_wake_queue(dev);
		priv->can.can_stats.restarts++;
		break;

	default:
		spin_unlock_irqrestore(&priv->txrx_lock, flags);
		return -EOPNOTSUPP;
	}

	spin_unlock_irqrestore(&priv->txrx_lock, flags);
	return 0;
}

static DECLARE_COMPLETION(worker_done);

static netdev_tx_t ingenic_can_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct can_frame *cf = (struct can_frame *)skb->data;
	struct net_device_stats *stats = &dev->stats;
	struct ingenic_can_priv *priv = netdev_priv(dev);
	unsigned int send[2];
	unsigned int can_tfir = 0;
	unsigned long flags;
	int ret;

	if (can_dropped_invalid_skb(dev, skb) || (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY))
		return NETDEV_TX_OK;

	memset(send, 0, CAN_MAX_DLEN);

	spin_lock_irqsave(&priv->txrx_lock, flags);
	can_tfir |= cf->can_dlc;
	if (cf->can_id & CAN_EFF_FLAG)
		can_tfir |= CAN_TFIR_FF;
	if (cf->can_id & CAN_RTR_FLAG)
		can_tfir |= CAN_TFIR_RTR;
	memcpy(send, cf->data, cf->can_dlc);

	ret = ingenic_can_send_data(priv, can_tfir, (cf->can_id & CAN_EFF_MASK), send[0], send[1]);
	if (ret)
		goto unlock;

	stats->tx_bytes += cf->can_dlc;
	stats->tx_packets++;
	can_put_echo_skb(skb, dev, 0);
	netif_stop_queue(dev);

unlock:
	spin_unlock_irqrestore(&priv->txrx_lock, flags);

	return NETDEV_TX_OK;
}

static int ingenic_can_chip_stop(struct ingenic_can_priv *priv)
{
	can_writel(priv, 0, CAN_INTF);

#ifdef CONFIG_INGENIC_CAN_USE_PDMA
	dmaengine_terminate_all(priv->rxchan);
	dmaengine_terminate_all(priv->txchan);
#endif

	return 0;
}

static void auto_recovery_work_handler(struct work_struct *work)
{
	struct ingenic_can_priv* priv = container_of(work, struct ingenic_can_priv, can_work);
	struct net_device * dev = priv->dev;
	int tec, rec;
	unsigned int reg_trec, reg_intf;
        struct can_frame *skb_cf;
        struct sk_buff *skb = alloc_can_err_skb(dev, &skb_cf);

	do{
		reg_trec = can_readl(priv, CAN_ECC);
		rec = (reg_trec >> 16) & 0xFF;
		tec = (reg_trec >> 24) & 0xFF;

	}while(rec!=0 || tec!=0);

	reg_intf = can_readl(priv, CAN_INTF);
	reg_intf |= CAN_INTE_BOIE;
	can_writel(priv, reg_intf, CAN_INTF);      //只打开EARA=1时出现busoff时关闭的BOIE

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	skb_cf->data[1] |= CAN_ERR_CRTL_ACTIVE;
	skb_cf->data[6] = tec;
	skb_cf->data[7] = rec;

    if (skb) {
        skb_cf->can_id |= CAN_ERR_CRTL;
        dev->stats.rx_packets++;
        dev->stats.rx_bytes += CAN_ERR_DLC;
	 	netif_rx(skb);
    }
}

static void ingenic_can_irq_overload(struct net_device *dev, unsigned int irq_status, unsigned int reg_err){
	if (irq_status & CAN_INTF_OFI){
		struct ingenic_can_priv *priv = netdev_priv(dev);
		unsigned int txerr, rxerr, errc, ecc_seg, ecc_dir;
        struct can_frame *skb_cf;
        struct sk_buff *skb = alloc_can_err_skb(dev, &skb_cf);

        if (skb) {
			rxerr = (reg_err >> 16) & 0xFF;
			txerr = (reg_err >> 24) & 0xFF;
			errc  = (reg_err >> 6)  & 0x03;
			ecc_seg = reg_err & 0x01F;
			ecc_dir = (reg_err >> 5)&0x01;

			dev->stats.rx_frame_errors++;
			skb_cf->can_id |= CAN_ERR_PROT;
			skb_cf->data[2] = CAN_ERR_PROT_OVERLOAD;

			skb_cf->data[3] = ecc_seg;
			skb_cf->data[5] = ecc_dir;
			skb_cf->data[6] = txerr;
			skb_cf->data[7] = rxerr;

            dev->stats.rx_packets++;
            dev->stats.rx_bytes += CAN_ERR_DLC;
			netif_rx(skb);
        }
	}
}

static void ingenic_can_irq_error(struct net_device *dev, unsigned int irq_status, unsigned int reg_err)
{
	struct ingenic_can_priv *priv = netdev_priv(dev);
	unsigned int txerr, rxerr, errc, ecc_seg, ecc_dir;
	unsigned int reg_intf;
    struct sk_buff *skb;
	struct can_frame cf = { };

	rxerr = (reg_err >> 16) & 0xFF;
	txerr = (reg_err >> 24) & 0xFF;
	errc  = (reg_err >> 6)  & 0x03;
	ecc_seg = reg_err & 0x01F;
	ecc_dir = (reg_err >> 5)&0x01;

	if (irq_status & CAN_INTF_BEI)
	{
//		netdev_err(dev, "%s Bus Error\n", dev->name);
//		netdev_err(dev, "CANERR 0x%02x : 0x%08x\n", CAN_ECC, reg_err);
		priv->can.can_stats.bus_error++;
		dev->stats.rx_errors++;

		cf.can_id |= CAN_ERR_PROT|CAN_ERR_BUSERROR;
        if(errc == CAN_ECC_ERRC_BIT_ERROR){
			cf.data[2] |= CAN_ERR_PROT_BIT;
		}else if(errc == CAN_ECC_ERRC_FORM_ERROR){
			cf.data[2] |= CAN_ERR_PROT_FORM;
		}else if(errc == CAN_ECC_ERRC_STUFF_ERROR){
			cf.data[2] |= CAN_ERR_PROT_STUFF;
		}else if(errc == CAN_ECC_ERRC_OTHER_ERROR){
			if(ecc_seg == CAN_ERR_PROT_LOC_ACK_DEL){
				cf.data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
				ecc_seg =  CAN_ERR_PROT_LOC_CRC_SEQ;
			}
		}

	}
	if (irq_status & CAN_INTF_ALI)
	{
//		netdev_err(dev, "%s Arbitration Lost\n", dev->name);
		priv->can.can_stats.arbitration_lost++;
		dev->stats.rx_errors++;

		cf.can_id |= CAN_ERR_LOSTARB;
		cf.data[0] |= CAN_ERR_LOSTARB_UNSPEC;
	}

	if (irq_status & CAN_INTF_DOI)
	{
//		netdev_err(dev, "%s Data Overrun\n", dev->name);
		dev->stats.rx_over_errors++;

		cf.can_id |= CAN_ERR_CRTL;
		cf.data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
	}

	if (cf.can_id) {
        struct can_frame *skb_cf;
        struct sk_buff *skb = alloc_can_err_skb(dev, &skb_cf);

		cf.data[3] = ecc_seg;
		cf.data[5] = ecc_dir;
		cf.data[6] = txerr;
		cf.data[7] = rxerr;

        if (skb) {
            skb_cf->can_id |= cf.can_id;
            memcpy(skb_cf->data, cf.data, CAN_ERR_DLC);
            dev->stats.rx_packets++;
            dev->stats.rx_bytes += CAN_ERR_DLC;
		 	netif_rx(skb);
        }
    }
}

static void ingenic_can_irq_state(struct net_device *dev, unsigned int irq_status, unsigned int reg_err)
{
	struct ingenic_can_priv *priv = netdev_priv(dev);
	enum can_state new_state = CAN_STATE_ERROR_ACTIVE;
	unsigned int txerr, rxerr, errc, ecc_seg, ecc_dir;
	unsigned int reg_intf;
    struct sk_buff *skb;
	struct can_frame cf = { };

	rxerr = (reg_err >> 16) & 0xFF;
	txerr = (reg_err >> 24) & 0xFF;
	errc  = (reg_err >> 6)  & 0x03;
	ecc_seg = reg_err & 0x01F;
	ecc_dir = (reg_err >> 5)&0x01;

	if (irq_status & CAN_INTF_EI)
	{
//		netdev_err(dev, "%s Error Warning\n", dev->name);

		cf.can_id |= CAN_ERR_CRTL;
		if(txerr >= INGENIC_CAN_ERRCNT_WARNING_LIMIT){
			priv->can.can_stats.error_warning++;
			dev->stats.tx_errors++;
			new_state = CAN_STATE_ERROR_WARNING;
			cf.data[1] |= CAN_ERR_CRTL_TX_WARNING;

		}
		if(rxerr >= INGENIC_CAN_ERRCNT_WARNING_LIMIT){
			priv->can.can_stats.error_warning++;
			dev->stats.rx_errors++;
			new_state = CAN_STATE_ERROR_WARNING;
			cf.data[1] |= CAN_ERR_CRTL_RX_WARNING;
		}

		if(txerr < INGENIC_CAN_ERRCNT_WARNING_LIMIT && rxerr < INGENIC_CAN_ERRCNT_WARNING_LIMIT){
			new_state = CAN_STATE_ERROR_ACTIVE;
			cf.data[1] |= CAN_ERR_CRTL_ACTIVE;
		}

	}
	if (irq_status & CAN_INTF_EPI)
	{
//		netdev_err(dev, "%s Error Passive\n", dev->name);

		cf.can_id |= CAN_ERR_CRTL;
		if(txerr >= INGENIC_CAN_ERRCNT_PASSIVE_LIMIT){
			priv->can.can_stats.error_passive++;
			dev->stats.tx_errors++;
			new_state = CAN_STATE_ERROR_PASSIVE;
			cf.data[1] |= CAN_ERR_CRTL_TX_PASSIVE;

		}
		if(rxerr >= INGENIC_CAN_ERRCNT_PASSIVE_LIMIT){
			priv->can.can_stats.error_passive++;
			dev->stats.rx_errors++;
			new_state = CAN_STATE_ERROR_PASSIVE;
			cf.data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
		}

		if(txerr < INGENIC_CAN_ERRCNT_PASSIVE_LIMIT && rxerr < INGENIC_CAN_ERRCNT_PASSIVE_LIMIT){
			if(txerr > INGENIC_CAN_ERRCNT_WARNING_LIMIT){
				new_state = CAN_STATE_ERROR_WARNING;
				cf.data[1] |= CAN_ERR_CRTL_TX_WARNING;
			}

			if(rxerr > INGENIC_CAN_ERRCNT_WARNING_LIMIT){
				new_state = CAN_STATE_ERROR_WARNING;
				cf.data[1] |= CAN_ERR_CRTL_RX_WARNING;
			}

			if(txerr <INGENIC_CAN_ERRCNT_WARNING_LIMIT && INGENIC_CAN_ERRCNT_WARNING_LIMIT){
				new_state = CAN_STATE_ERROR_ACTIVE;
				cf.data[1] |= CAN_ERR_CRTL_ACTIVE;

			}
		}

	}
	if (irq_status & CAN_INTF_BOI)
	{
//		netdev_err(dev, "%s Bus Off\n", dev->name);
//		netdev_err(dev, "CANECC 0x%02x : 0x%08x\n", CAN_ECC, reg_err);
		priv->can.can_stats.bus_off++;
		dev->stats.tx_errors++;
		new_state = CAN_STATE_BUS_OFF;

		cf.can_id |= CAN_ERR_BUSOFF;
	}

	if (new_state == priv->can.state)
		return;

	if (cf.can_id) {
        struct can_frame *skb_cf;
        struct sk_buff *skb = alloc_can_err_skb(dev, &skb_cf);

		cf.data[3] = ecc_seg;
		cf.data[5] = ecc_dir;
		cf.data[6] = txerr;
		cf.data[7] = rxerr;

        if (skb) {
            skb_cf->can_id |= cf.can_id;
            memcpy(skb_cf->data, cf.data, CAN_ERR_DLC);
            dev->stats.rx_packets++;
            dev->stats.rx_bytes += CAN_ERR_DLC;
            netif_rx(skb);
        }
    }

/*
	if(!netif_carrier_ok(dev) && new_state == CAN_STATE_BUS_OFF) {
        netdev_err(dev, "%s Bus Off but net carrier is not recovered!\n", dev->name);
        return;
    }
*/

	if (new_state == CAN_STATE_BUS_OFF)
	{
		unsigned int mode = can_readl(priv, CAN_MODE);
		unsigned int mode_erar = mode & CAN_MODE_ERAR;

		if(!priv->can.restart_ms){
			if(mode_erar == 0){
				ingenic_can_chip_stop(priv);
			}else if(mode_erar == CAN_MODE_ERAR){
			//	can_writel(priv, CAN_CMD_CTB | CAN_CMD_RRB |CAN_CMD_AT, CAN_CMD);
				reg_intf = can_readl(priv, CAN_INTF);
				reg_intf &= ~(CAN_INTE_BOIE);
				can_writel(priv, reg_intf, CAN_INTF);

			    queue_work(can_auto_recovery_wq, &priv->can_work);
			}
		}else if(priv->can.restart_ms){
			ingenic_can_chip_stop(priv);
			can_bus_off(priv->dev);
		}
	}


	priv->can.state = new_state;
}

static int ingenic_can_read_msg(struct net_device *dev, int *rdata)
{
	struct sk_buff *skb_rx;
	struct can_frame *cf;
	struct net_device_stats *stats = &dev->stats;
//	struct ingenic_can_priv *priv = netdev_priv(dev);

	skb_rx = alloc_can_skb(dev, &cf);

	cf->can_id = rdata[1];
	if (rdata[0] & CAN_RFIR_FF)
		cf->can_id |= CAN_EFF_FLAG;
	if (rdata[0] & CAN_RFIR_RTR)
		cf->can_id |= CAN_RTR_FLAG;
	cf->can_dlc = rdata[0] & 0xF;
	if(cf->can_dlc > CAN_MAX_DLEN && cf->can_dlc <= CAN_MAX_RAW_DLC){
		cf->len8_dlc = cf->can_dlc;
		cf->can_dlc = CAN_MAX_DLEN;
	}
	memcpy(cf->data, rdata + 2, cf->can_dlc);
	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;

	netif_receive_skb(skb_rx);

	return 0;
}

static irqreturn_t ingenic_can_irq_handler(int irq, void *data)
{
	struct net_device *dev = (struct net_device *)data;
	struct ingenic_can_priv *priv = netdev_priv(dev);
	unsigned int *rxdata = (unsigned int *)priv->buffer;
	unsigned int irq_status, reg_err;
	unsigned int *rdata;
	unsigned int can_data[4];
	unsigned long flags;

	spin_lock_irqsave(&priv->txrx_lock, flags);
	irq_status = can_readl(priv, CAN_INTF);
	reg_err = can_readl(priv, CAN_ECC);


	if (irq_status & CAN_INTF_DMAI)
	{
		unsigned int cnt, dtc;
		unsigned int i;

		dtc = readl(DMA_CHA_DTC(priv->rxchan->chan_id));
		cnt = (priv->record >= dtc) ? (priv->record - dtc) : (DMA_RX_DESC_NUM - (dtc - priv->record));
		for (i = 0; i < cnt; i++)
		{
			rdata = rxdata + ((priv->rece_sum % (DMA_RX_DESC_NUM * DMA_RX_DESC_DTC)) * 4);
			priv->rece_sum++;
			ingenic_can_read_msg(dev, rdata);
		}
		priv->record = dtc;
	}

	if (irq_status & CAN_INTF_RI)
	{
		can_data[0] = can_readl(priv, CAN_RFIR);
		can_data[1] = can_readl(priv, CAN_RXID);
		can_data[2] = can_readl(priv, CAN_RXDATA);
		can_data[3] = can_readl(priv, CAN_RXDATA1);
		priv->rece_sum++;
		ingenic_can_read_msg(dev, can_data);
	}

#ifndef CONFIG_INGENIC_CAN_USE_PDMA
	can_writel(priv, CAN_CMD_RRB, CAN_CMD);
#endif

	if (irq_status & CAN_INTF_WKI)
	{
		netdev_info(dev, "enter can sleep interrupt \n");
	}

	if (irq_status & CAN_INTF_TI)
	{
		priv->flag = CAN_SEND_OVER;
		priv->trans_sum++;
		can_get_echo_skb(dev, 0);
		netif_wake_queue(dev);
		complete(&worker_done);
	}

	if(unlikely(irq_status & CAN_INTF_OFI)){
		ingenic_can_irq_overload(dev,irq_status, reg_err);
	}

	if(unlikely(irq_status & CAN_INTF_STA_ALL)){
		ingenic_can_irq_state(dev,irq_status, reg_err);
	}

	if(unlikely(irq_status & CAN_INTF_ERR_ALL)){
		ingenic_can_irq_error(dev,irq_status, reg_err);
	}

	spin_unlock_irqrestore(&priv->txrx_lock, flags);

	return IRQ_HANDLED;
}

static int ingenic_can_open(struct net_device *dev)
{
	struct ingenic_can_priv *priv = netdev_priv(dev);
	int err;

	err = open_candev(dev);
	if (err)
		goto out;

	ingenic_can_chip_start(priv);
	netif_start_queue(dev);

	return 0;
out:
	return err;
}

static int ingenic_can_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd){
	struct ingenic_can_priv *priv = netdev_priv(dev);
	int ret = 0;

    if (!ifr) {
        return -EINVAL;
    }

    switch (cmd) {
	case SIOCSBUSOFFSWITCH:
		{
			int bus_off;
			if (copy_from_user(&bus_off, ifr->ifr_data, sizeof(int))){
           		return -1;
        	}

			int reg = can_readl(priv, CAN_MODE);
			if(bus_off == 1){
				reg |= CAN_MODE_ERAR;

			}else if(bus_off == 0){
				reg &= ~CAN_MODE_ERAR;
			}
			can_writel(priv, CAN_MODE_RSTM, CAN_MODE);
			can_writel(priv, reg, CAN_MODE);

			break;
		}
	case SIOCSSSPSWITCH:   //only can fd use
		{
			break;
		}
	case SIOCSMULFRAME:
		{
			struct can_send_data  send_data;
			memset(&send_data, 0, sizeof(send_data));
            if (copy_from_user(&send_data, ifr->ifr_data, sizeof(struct can_send_data))) {
                return -EFAULT;
            }

			int i = 0;
			int ret;
			unsigned int can_tfir = 0;

			can_tfir |= send_data.can_dlc;
			if(send_data.rtr){
            	can_tfir |= CAN_TFIR_RTR;
        	}
        	if(send_data.eff){
            	can_tfir |= CAN_TFIR_FF;
        	}

			for(; i<send_data.repeat_num; i++){
				ret = ingenic_can_send_data(priv, can_tfir, (send_data.can_id & CAN_EFF_MASK), send_data.data[0], send_data.data[1]);
				wait_for_completion(&worker_done);
			}


			break;
		}
	case SIOCSMULFRAMETEST:
		{
			struct ingenic_can_priv *priv = netdev_priv(dev);
			unsigned int send[2]={0x11223344, 0x55667788};

			unsigned int can_tfir = 0;
    		int ret;
			unsigned int can_id = 0x123;
			unsigned int can_dlc = 8;

			can_tfir |= can_dlc;
//			can_tfir |= CAN_TFIR_FF;

			int i = 0;
			ktime_t start_time, end_time;
			start_time = ktime_get();
			for(; i< 10000; i++){
				ret = ingenic_can_send_data(priv, can_tfir, (can_id & CAN_EFF_MASK), send[0], send[1]);
				wait_for_completion(&worker_done);
			}
			end_time = ktime_get();
			printk("Interrupt handler took %lld ns\n", ktime_to_ns(ktime_sub(end_time, start_time)));

		}
		break;
    default:
        ret = -EOPNOTSUPP;
        break;
    }

    return ret;
}

static const struct net_device_ops ingenic_can_netdev_ops = {
	.ndo_open = ingenic_can_open,
	.ndo_stop = ingenic_can_close,
	.ndo_do_ioctl   = ingenic_can_ioctl,
	.ndo_start_xmit = ingenic_can_start_xmit,
};

static int ingenic_can_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct ingenic_can_priv *priv = NULL;
	struct resource *res;
	void __iomem *addr;
	int err, irq;
	dma_cap_mask_t mask;
	char clkname[16];
	unsigned int can_clk_freq;
	struct clk *clk_cgu;
	struct clk *clk_gate;

	dev_info(&pdev->dev, "ingenic_can probe init\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || irq <= 0)
	{
		err = -ENODEV;
		goto exit;
	}

	if (!request_mem_region(res->start, resource_size(res), pdev->name))
	{
		err = -EBUSY;
		goto exit;
	}

	addr = ioremap(res->start, resource_size(res));
	if (!addr)
	{
		err = -ENOMEM;
		goto exit_release_mem;
	}

	pdev->id = of_alias_get_id(pdev->dev.of_node, "can");

	sprintf(clkname, "div_can%d", pdev->id);
	clk_cgu = devm_clk_get(&pdev->dev, clkname);
	if (IS_ERR(clk_cgu))
	{
		dev_err(&pdev->dev, "%s Cannot get clock: %s\n", __func__, clkname);
		goto exit_iounmap;
	}

	sprintf(clkname, "gate_can%d", pdev->id);
	clk_gate = devm_clk_get(&pdev->dev, clkname);
	if (IS_ERR(clk_gate))
	{
		dev_err(&pdev->dev, "%s Cannot get clock: %s\n", __func__, clkname);
		goto exit_iounmap;
	}

	err = of_property_read_u32(pdev->dev.of_node, "ingenic,clk-freq", &can_clk_freq);
	if (err < 0)
	{
		can_clk_freq = 24000000;
	}

	if ((err = clk_prepare_enable(clk_cgu)) < 0)
	{
		dev_err(&pdev->dev, "Enable can%d cgu clk failed\n", pdev->id);
		goto exit_clk_put;
	}

	if (clk_set_rate(clk_cgu, can_clk_freq))
	{
		dev_err(&pdev->dev, "Set cgu_can%d clk rate faild\n", pdev->id);
		goto exit_clk_put;
	}

	if ((err = clk_prepare_enable(clk_gate)) < 0)
	{
		dev_err(&pdev->dev, "Enable can%d gate clk failed\n", pdev->id);
		goto exit_clk_put;
	}

	dev = alloc_candev(sizeof(struct ingenic_can_priv), TX_ECHO_SKB_MAX);
	if (!dev)
	{
		err = -ENOMEM;
		goto exit_clk_put;
	}

	dev->flags |= IFF_ECHO;
	dev->netdev_ops = &ingenic_can_netdev_ops;
	dev->irq = irq;

	priv = netdev_priv(dev);
	priv->can.clock.freq = can_clk_freq;
	priv->can.bittiming_const = &ingenic_can_bittiming_const;
	priv->can.do_set_mode = ingenic_can_restart_set_mode;
	priv->can.do_set_bittiming = ingenic_can_set_baudrate;
	priv->can.do_get_berr_counter = ingenic_can_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES |
								   CAN_CTRLMODE_LOOPBACK | CAN_CTRLMODE_LISTENONLY;
	priv->dev = dev;
	priv->reg_base = addr;
	priv->reg_base_phy = (void __iomem *)res->start;
	priv->clk_cgu = clk_cgu;
	priv->clk_gate = clk_gate;

	dev_set_drvdata(&pdev->dev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	err = register_candev(dev);
	if (err)
	{
		dev_err(&pdev->dev, "registering netdev failed\n");
		goto exit_free_dev;
	}

	err = devm_request_irq(&pdev->dev, dev->irq, ingenic_can_irq_handler, 0, dev->name, dev);
	if (err)
	{
		dev_err(&pdev->dev, "request irq failed\n");
		goto exit_unregister_dev;
	}

	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->txrx_lock);

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	priv->txchan = dma_request_chan(&pdev->dev, "tx");
	if (IS_ERR(priv->txchan))
	{
		if (PTR_ERR(priv->txchan) == -EPROBE_DEFER)
			err = -EPROBE_DEFER;
		dev_err(&pdev->dev, "ingenic_can request dma tx channel failed\n");
		goto exit_unregister_dev;
	}
	priv->rxchan = dma_request_chan(&pdev->dev, "rx");
	if (IS_ERR(priv->rxchan))
	{
		if (PTR_ERR(priv->rxchan) == -EPROBE_DEFER)
			err = -EPROBE_DEFER;
		dev_err(&pdev->dev, "ingenic_can request dma rx channel failed\n");
		goto exit_free_dma_chan;
	}
	priv->buffer = dma_alloc_coherent(&pdev->dev, (DMA_RX_BUFF_SIZE * DMA_RX_DESC_NUM), &priv->buffer_dma, GFP_KERNEL);
	if (!priv->buffer)
	{
		dev_err(&pdev->dev, "ingenic_can request temp dma buffer failed\n");
		goto exit_free_dma_chan;
	}

	priv->txbuffer = dma_alloc_coherent(&pdev->dev, DMA_BUFF_SIZE, &priv->txbuffer_dma, GFP_KERNEL);
	if (!priv->txbuffer)
	{
		dev_err(&pdev->dev, "ingenic_can request temp dma txbuffer failed\n");
		goto exit_free_request_buffer;
	}
	priv->sg_tx = devm_kmalloc(&pdev->dev, sizeof(struct scatterlist), GFP_KERNEL);
	if (!priv->sg_tx)
	{
		dev_err(&pdev->dev, "Failed to alloc tx scatterlist\n");
	}

    can_auto_recovery_wq = create_workqueue("can_auto_recovery_detect");
    if (!can_auto_recovery_wq) {
        printk(KERN_ERR "Create can auto recovery workqueue failed\n");
        return -ENOMEM;
    }
    INIT_WORK(&priv->can_work, auto_recovery_work_handler);

	return 0;

exit_free_request_buffer:
	if (priv->buffer)
		kfree(priv->buffer);
	if (priv->txbuffer)
		kfree(priv->txbuffer);
exit_free_dma_chan:
	if (priv->rxchan)
		dma_release_channel(priv->rxchan);
	if (priv->txchan)
		dma_release_channel(priv->txchan);
exit_unregister_dev:
	unregister_netdev(dev);
exit_free_dev:
	free_candev(dev);
exit_clk_put:
	if (priv->clk_gate)
		devm_clk_put(&pdev->dev, priv->clk_gate);
	if (priv->clk_cgu)
		devm_clk_put(&pdev->dev, priv->clk_cgu);
exit_iounmap:
	iounmap(priv->reg_base);
exit_release_mem:
	release_mem_region(res->start, resource_size(res));
exit:
	return err;
}

static int ingenic_can_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct ingenic_can_priv *priv = netdev_priv(dev);
	struct resource *res;

	unregister_netdev(dev);
	platform_set_drvdata(pdev, NULL);
	iounmap(priv->reg_base);

	clk_disable_unprepare(priv->clk_cgu);
	clk_disable_unprepare(priv->clk_gate);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	free_candev(dev);

	return 0;
}

#ifdef CONFIG_PM
static int ingenic_can_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct ingenic_can_priv *priv = netdev_priv(dev);

	disable_irq(dev->irq);

	if (netif_running(dev))
	{
		netif_stop_queue(dev);
		netif_device_detach(dev);
	}
	clk_disable_unprepare(priv->clk_gate);
	clk_disable_unprepare(priv->clk_cgu);
	priv->can.state = CAN_STATE_SLEEPING;

	return 0;
}

static int ingenic_can_resume(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct ingenic_can_priv *priv = netdev_priv(dev);

	clk_prepare_enable(priv->clk_cgu);
	clk_prepare_enable(priv->clk_gate);

	priv->can.state = CAN_STATE_ERROR_ACTIVE;
	if (netif_running(dev)) {
		netif_device_attach(dev);
		netif_start_queue(dev);
	}

	enable_irq(dev->irq);

	return 0;
}
#else
#define ingenic_can_suspend NULL
#define ingenic_can_resume NULL
#endif

static const struct of_device_id ingenic_can_dt_ids[] = {
	{
		.compatible = "ingenic,x1600-can",
	},
	{
		.compatible = "ingenic,x2600-can",
	},
	{}};
MODULE_DEVICE_TABLE(of, ingenic_can_dt_ids);

static struct platform_driver ingenic_can_driver = {
	.probe = ingenic_can_probe,
	.remove = ingenic_can_remove,
	.suspend = ingenic_can_suspend,
	.resume = ingenic_can_resume,
	.driver = {
		.name = MODNAME,
		.of_match_table = ingenic_can_dt_ids,
		.owner = THIS_MODULE,
	},
};

module_platform_driver(ingenic_can_driver);

MODULE_AUTHOR("xiaozhihao<zhihao.xiao@ingenic.com>");
MODULE_DESCRIPTION("ingenci can netdevice driver");
MODULE_LICENSE("GPL");
