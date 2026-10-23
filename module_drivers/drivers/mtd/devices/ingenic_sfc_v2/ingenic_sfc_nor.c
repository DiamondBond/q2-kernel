/*
 * SFC controller for SPI protocol, use FIFO and DMA;
 *
 * Copyright (c) 2015 Ingenic
 * Author: <xiaoyang.fu@ingenic.com>
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/page.h>
#include "sfc_flash.h"
#include "spinor.h"
#include "ingenic_sfc_common.h"
#include "ingenic_sfc_nor_concat.h"
#include "ingenic_sfc_drv.h"
#ifdef CONFIG_INGENIC_GLOBAL_INFO
#include <asm/global_info.h>
#endif

//#define DEBUG_CLONER_PARAMS

#define STATUS_SUSPND	(1<<0)

#define MULTI_DIE_FLASH_NUM 1
#ifdef CONFIG_MTD_INGENIC_SFC_CONCAT
#define SFC_NOR_WRITE_TIMEOUT_MS	3000
#define SFC_NOR_ERASE_TIMEOUT_MS	15000
#endif

static LIST_HEAD(nor_list);
static int sfc_params_offset = 0;

struct sfc_flash *to_ingenic_spi_norflash(struct mtd_info *mtd_info)
{
	return container_of(mtd_info, struct sfc_flash, mtd);
}

#define ACTIVE_DIE(flash, addr)					\
({								\
	uint8_t die_id = addr >> nor_info->die_shift;		\
	if (die_id != nor_info->current_die_id) {			\
		sfc_active_die(flash, die_id);				\
		nor_info->current_die_id = die_id;			\
	}							\
	if (die_id)						\
		addr = addr & ((1 << nor_info->die_shift) - 1);	\
	addr;							\
})								\

static int sfc_die_select(struct sfc_flash *flash, uint8_t die_id)
{
	struct sfc_cdt_xfer xfer;
	memset(&xfer, 0, sizeof(xfer));

	/* set Index */
	xfer.cmd_index = NOR_DIE_SELECT;

	/* set addr */
	xfer.rowaddr = 0;
	xfer.columnaddr = 0;

	/* set transfer config */
	xfer.dataen = ENABLE;
	xfer.config.datalen = 1;
	xfer.config.data_dir = GLB0_TRAN_DIR_WRITE;
	xfer.config.ops_mode = CPU_OPS;
	xfer.config.buf = &die_id;

	if(sfc_sync_cdt(flash->sfc, &xfer)) {
		dev_err(flash->dev,"sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
		return -EIO;
	}

	return 0;
}

static int sfc_read_active_die_id(struct sfc_flash *flash, uint8_t *value)
{
	struct sfc_cdt_xfer xfer;
	memset(&xfer, 0, sizeof(xfer));

	/* set Index */
	xfer.cmd_index = NOR_READ_ACTIVE_DIE_ID;

	/* set addr */
	xfer.rowaddr = 0;
	xfer.columnaddr = 0;

	/* set transfer config */
	xfer.dataen = ENABLE;
	xfer.config.datalen = 1;
	xfer.config.data_dir = GLB0_TRAN_DIR_READ;
	xfer.config.ops_mode = CPU_OPS;
	xfer.config.buf = value;

	if(sfc_sync_cdt(flash->sfc, &xfer)) {
		dev_err(flash->dev,"sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
		return -EIO;
	}

	return 0;
}

static int sfc_active_die(struct sfc_flash *flash, uint8_t die_id)
{
	uint8_t die_id_read;

	sfc_die_select(flash, die_id);
	do {
		sfc_read_active_die_id(flash, &die_id_read);
	}while(die_id != die_id_read);

	return 0;
}

int ingenic_sfc_nor_clk_set_highspeed(struct sfc_flash *flash)
{
	struct spinor_flashinfo *nor_info;
	unsigned long max_freq;

	if (!flash || !flash->sfc)
		return -EINVAL;

	nor_info = flash->flash_info;
	max_freq = flash->sfc->max_frequency;
	if (max_freq > 200000000 && nor_info && !nor_info->quad_succeed) {
		printk("clk freq too hight! set to 200M\n");
		max_freq = 200000000;
	}

	return sfc_clk_set_highspeed(flash->sfc, max_freq);
}

#ifdef CONFIG_MTD_INGENIC_SFC_CONCAT
static int sfc_nor_read_status_locked(struct sfc_flash *flash, uint8_t *status)
{
	struct sfc_cdt_xfer xfer;

	memset(&xfer, 0, sizeof(xfer));

	xfer.cmd_index = NOR_GET_STATUS;
	xfer.dataen = ENABLE;
	xfer.config.datalen = 1;
	xfer.config.data_dir = GLB0_TRAN_DIR_READ;
	xfer.config.ops_mode = CPU_OPS;
	xfer.config.buf = status;

	if (sfc_sync_cdt(flash->sfc, &xfer)) {
		dev_err(flash->dev, "read nor status failed\n");
		return -EIO;
	}

	return 0;
}

static int sfc_nor_wait_ready_locked(struct sfc_flash *flash,
		unsigned int timeout_ms)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(timeout_ms);
	uint8_t status = SR_WIP;
	int ret;

	do {
		ret = sfc_nor_read_status_locked(flash, &status);
		if (ret)
			return ret;

		if (!(status & SR_WIP))
			return 0;

		usleep_range(1000, 2000);
	} while (time_before(jiffies, deadline));

	ret = sfc_nor_read_status_locked(flash, &status);
	if (!ret && !(status & SR_WIP))
		return 0;

	dev_err(flash->dev, "wait nor ready timeout, status=0x%02x\n",
			status);
	return -ETIMEDOUT;
}
#endif

static void add_set_status_to_cdt(struct sfc_flash *flash, uint32_t cmd, uint32_t val)
{
	struct spinor_flashinfo *nor_info = flash->flash_info;
	struct spi_nor_info *params = nor_info->nor_flash_info;
	struct sfc *sfc = flash->sfc;
	struct sfc_cdt cdt[3];

	MK_CMD(cdt[NOR_SET_STATUS_ENABLE], params->wr_en, 1, DEFAULT_ADDRMODE, DISABLE);
	cdt[NOR_SET_STATUS].link = CMD_LINK(1, DEFAULT_ADDRMODE, TM_STD_SPI);
	cdt[NOR_SET_STATUS].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, cmd);
	cdt[NOR_SET_STATUS].staExp = val;
	cdt[NOR_SET_STATUS].staMsk = 0xFFFFFFFF;
	MK_ST(cdt[NOR_SET_STATUS_FINISH], params->busy, 0, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);

	write_cdt(sfc, cdt, NOR_READ_ACTIVE_DIE_ID, NOR_MAX_INDEX);
}

static struct spi_nor_cmd_info cmd_dpb_lock = {
	.cmd           = SPINOR_OP_DPB_LOCK,
	.dummy_byte    = 0,
	.addr_nbyte    = 3,
	.transfer_mode = 0,
};

static struct spi_nor_cmd_info cmd_dpb_unlock = {
	.cmd           = SPINOR_OP_DPB_UNLOCK,
	.dummy_byte    = 0,
	.addr_nbyte    = 3,
	.transfer_mode = 0,
};

static struct spi_nor_cmd_info cmd_dpb_status = {
	.cmd           = SPINOR_OP_DPB_STATUS,
	.dummy_byte    = 0,
	.addr_nbyte    = 3,
	.transfer_mode = 0,
};

static int ingenic_spi_norflash_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct sfc_flash *flash = to_ingenic_spi_norflash(mtd);
	struct sfc_cdt_xfer xfer;
	uint32_t adr = (uint32_t)ofs;
	uint32_t bs = 65536;

	while (len) {
		memset(&xfer, 0, sizeof(xfer));

		xfer.cmd_index = NOR_DPB_LOCK_WRITE_ENABLE;

		xfer.rowaddr = adr;
		xfer.columnaddr = 0;
		xfer.dataen = DISABLE;

		if(sfc_sync_cdt(flash->sfc, &xfer)) {
			dev_err(flash->dev,"sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
			return -EIO;
		}

		if (ofs < 65536 || ofs > (mtd->size - 65536))
			bs = 4096;
		else
			bs = 65536;

		adr += bs;
		len -= bs;
	}

	return 0;
}

static int ingenic_spi_norflash_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct sfc_flash *flash = to_ingenic_spi_norflash(mtd);
	struct sfc_cdt_xfer xfer;
	uint32_t adr = (uint32_t)ofs;
	uint32_t bs = 65536;

	while (len) {
		memset(&xfer, 0, sizeof(xfer));

		xfer.cmd_index = NOR_DPB_UNLOCK_WRITE_ENABLE;

		xfer.rowaddr = adr;
		xfer.columnaddr = 0;
		xfer.dataen = DISABLE;

		if(sfc_sync_cdt(flash->sfc, &xfer)) {
			dev_err(flash->dev,"sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
			return -EIO;
		}

		if (ofs < 65536 || ofs > (mtd->size - 65536))
			bs = 4096;
		else
			bs = 65536;

		adr += bs;
		len -= bs;
	}

	return 0;
}

static int ingenic_spi_norflash_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct sfc_flash *flash = to_ingenic_spi_norflash(mtd);
	uint32_t status = 0;
	uint32_t adr = (uint32_t)ofs;
	uint32_t bs = 65536;
	struct sfc_cdt_xfer xfer;

	while (len) {
		memset(&xfer, 0, sizeof(xfer));
		status = 0;

		xfer.cmd_index = NOR_DPB_STATUS;

		xfer.rowaddr = adr;
		xfer.columnaddr = 0;

		xfer.dataen = ENABLE;
		xfer.config.datalen = 1;
		xfer.config.data_dir = GLB0_TRAN_DIR_READ;
		xfer.config.ops_mode = CPU_OPS;
		xfer.config.buf = (uint8_t *)&status;

		if(sfc_sync_cdt(flash->sfc, &xfer)) {
			dev_err(flash->dev,"sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
			return -EIO;
		}

		if (status & NOR_DPB_STA_LOCK) {
			return NOR_DPB_STA_LOCK;
		}

		if (ofs < 65536 || ofs > (mtd->size - 65536))
			bs = 4096;
		else
			bs = 65536;

		adr += bs;
		len -= bs;
	}

	return NOR_DPB_STA_UNLOCK;
}

int32_t sfc_nor_reset(struct sfc_flash *flash)
{
	struct sfc_cdt_xfer xfer;
	memset(&xfer, 0, sizeof(xfer));

	/* set Index */
	xfer.cmd_index = NOR_RESET_ENABLE;

	/* set addr */
	xfer.rowaddr = 0;
	xfer.columnaddr = 0;

	/* set transfer config */
	xfer.dataen = DISABLE;

	if(sfc_sync_cdt(flash->sfc, &xfer)) {
		dev_err(flash->dev,"sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
		return -EIO;
	}

	udelay(1000);
	return 0;
}

int sfc_nor_read_id(struct sfc_flash *flash)
{
	struct sfc_cdt_xfer xfer;
	unsigned char buf[3];
	unsigned int chip_id = 0;
	memset(&xfer, 0, sizeof(xfer));

	/* set Index */
	xfer.cmd_index = NOR_READ_ID;

	/* set addr */
	xfer.rowaddr = 0;
	xfer.columnaddr = 0;

	/* set transfer config */
	xfer.dataen = ENABLE;
	xfer.config.datalen = 3;
	xfer.config.data_dir = GLB0_TRAN_DIR_READ;
	xfer.config.ops_mode = CPU_OPS;
	xfer.config.buf = buf;

	if(sfc_sync_cdt(flash->sfc, &xfer)) {
		dev_err(flash->dev,"sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
		return -EIO;
	}

	chip_id = ((buf[0] & 0xff) << 16) | ((buf[1] & 0xff) << 8) | (buf[2] & 0xff);

	return chip_id;
}

static unsigned int sfc_do_read(struct sfc_flash *flash, unsigned int addr, unsigned char *buf, size_t len)
{
	struct spinor_flashinfo *nor_info = flash->flash_info;
	struct sfc_cdt_xfer xfer;
	memset(&xfer, 0, sizeof(xfer));

	/* set Index */
	if (nor_info->quad_succeed) {
		xfer.cmd_index = NOR_READ_QUAD;
	} else {
		xfer.cmd_index = NOR_READ_STANDARD;
	}

	/* active die */
	if (nor_info->die_num > 1)
		addr = ACTIVE_DIE(flash, addr);

	/* set addr */
	xfer.columnaddr = 0;
	xfer.rowaddr = addr;

	/* set transfer config */
	xfer.dataen = ENABLE;
	xfer.config.datalen = len;
	xfer.config.data_dir = GLB0_TRAN_DIR_READ;
	xfer.config.ops_mode = DMA_OPS;
	xfer.config.buf = buf;

retry:
	if(sfc_sync_cdt(flash->sfc, &xfer)) {
		dev_err(flash->dev,"sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
		return -EIO;
	}

	/* if underrun, retry read */
	if (flash->sfc->retry_count > 0) {
		dev_warn(flash->dev,"SFC retry transfer! %s %s %d\n",__FILE__,__func__,__LINE__);
		goto retry;
	}

	if(xfer.config.ops_mode == DMA_OPS) {
		sfc_sync_desc_for_cpu(flash->sfc, DMA_FROM_DEVICE);
	}

	return len;
}

static unsigned  int sfc_do_write(struct sfc_flash *flash, unsigned int addr, const unsigned char *buf, size_t len)
{
	struct spinor_flashinfo *nor_info = flash->flash_info;
	struct sfc_cdt_xfer xfer;
	memset(&xfer, 0, sizeof(xfer));

	/* set Index */
	if (nor_info->quad_succeed) {
		xfer.cmd_index = NOR_WRITE_QUAD_ENABLE;
	} else {
		xfer.cmd_index = NOR_WRITE_STANDARD_ENABLE;
	}

	/* active die */
	if (nor_info->die_num > 1)
		addr = ACTIVE_DIE(flash, addr);

	/* set addr */
	xfer.columnaddr = 0;
	xfer.rowaddr = addr;

	/* set transfer config */
	xfer.dataen = ENABLE;
	xfer.config.datalen = len;
	xfer.config.data_dir = GLB0_TRAN_DIR_WRITE;
	xfer.config.ops_mode = DMA_OPS;
	xfer.config.buf = (uint8_t *)buf;

retry:
	if(sfc_sync_cdt(flash->sfc, &xfer)) {
		dev_err(flash->dev,"sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
		return -EIO;
	}

#ifdef CONFIG_MTD_INGENIC_SFC_CONCAT
	if (flash->cs_type == SFC_FLASH_CS_GPIO &&
	    sfc_nor_wait_ready_locked(flash, SFC_NOR_WRITE_TIMEOUT_MS))
		return -EIO;
#endif

	/* if overrun, retry write */
	if (flash->sfc->retry_count > 0) {
		dev_warn(flash->dev,"SFC retry transfer! %s %s %d\n",__FILE__,__func__,__LINE__);
		goto retry;
	}

	return len;
}

static int sfc_do_erase(struct sfc_flash *flash, uint32_t addr)
{
	struct spinor_flashinfo *nor_info = flash->flash_info;
	struct sfc_cdt_xfer xfer;

	memset(&xfer, 0, sizeof(xfer));

	/* set Index */
	xfer.cmd_index = NOR_ERASE_WRITE_ENABLE;

	/* active die */
	if (nor_info->die_num > 1)
		addr = ACTIVE_DIE(flash, addr);

	/* set addr */
	xfer.rowaddr = addr;

	/* set transfer config */
	if(sfc_sync_cdt(flash->sfc, &xfer)) {
		dev_err(flash->dev,"sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
		return -EIO;
	}

#ifdef CONFIG_MTD_INGENIC_SFC_CONCAT
	if (flash->cs_type == SFC_FLASH_CS_GPIO &&
	    sfc_nor_wait_ready_locked(flash, SFC_NOR_ERASE_TIMEOUT_MS))
		return -EIO;
#endif

	return 0;
}

static int sfc_read(struct sfc_flash *flash, loff_t from, size_t len, unsigned char *buf)
{
	int32_t ret;

	sfc_flash_bus_lock(flash);

	/* create DMA Descriptors */
	ret = create_sfc_desc(flash->sfc, buf, len);
	if(ret < 0){
		dev_err(flash->dev, "%s create descriptors error. -%d\n", __func__, ret);
		sfc_flash_bus_unlock(flash);
		return ret;
	}

	//dump_desc(flash->sfc, ret);

	/* DMA Descriptors read */
	ret = sfc_do_read(flash, (unsigned int)from, buf, len);

	sfc_flash_bus_unlock(flash);

	return ret;
}

static int sfc_write(struct sfc_flash *flash, loff_t to, size_t len, const unsigned char *buf)
{
	int32_t ret;

	sfc_flash_bus_lock(flash);

	/* create DMA Descriptors */
	ret = create_sfc_desc(flash->sfc, (unsigned char *)buf, len);
	if(ret < 0){
		dev_err(flash->dev, "%s create descriptors error. -%d\n", __func__, ret);
		sfc_flash_bus_unlock(flash);
		return ret;
	}

	//dump_desc(flash->sfc, ret);

	/* DMA Descriptors write */
	ret = sfc_do_write(flash, (unsigned int)to, buf, len);

	sfc_flash_bus_unlock(flash);

	return ret;
}

static int ingenic_spi_norflash_read(struct mtd_info *mtd, loff_t from, size_t len,size_t *retlen, unsigned char *buf)
{
	struct sfc_flash *flash = to_ingenic_spi_norflash(mtd);

	mutex_lock(&flash->lock);
	*retlen = sfc_read(flash, from, len, buf);
	mutex_unlock(&flash->lock);

	return 0;
}

static int ingenic_spi_norflash_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const unsigned char *buf)
{
	u32 page_offset, actual_len;
	struct sfc_flash *flash = to_ingenic_spi_norflash(mtd);
	struct spinor_flashinfo *nor_info = flash->flash_info;
	struct spi_nor_info *spi_nor_info = nor_info->nor_flash_info;
	int ret;

	mutex_lock(&flash->lock);

	page_offset = to & (spi_nor_info->page_size - 1);
	/* do all the bytes fit onto one page? */
	if (page_offset + len <= spi_nor_info->page_size) {
		ret = sfc_write(flash, (unsigned int)to, len, buf);
		*retlen = ret;
	} else {
		u32 i;

		/* the size of data remaining on the first page */
		actual_len = spi_nor_info->page_size - page_offset;
		ret = sfc_write(flash, (unsigned int)to, actual_len, buf);
		*retlen += ret;

		/* write everything in real NOR page program chunks */
		for (i = actual_len; i < len; i += mtd->writebufsize) {
			actual_len = len - i;
			if (actual_len >= mtd->writebufsize)
				actual_len = mtd->writebufsize;

			ret = sfc_write(flash, (unsigned int)to + i, actual_len, buf + i);
			*retlen += ret;
		}
	}
	mutex_unlock(&flash->lock);
	return 0;
}

static int ingenic_spi_norflash_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct sfc_flash *flash = to_ingenic_spi_norflash(mtd);
	struct spinor_flashinfo *nor_info = flash->flash_info;
	struct spi_nor_info *spi_nor_info = nor_info->nor_flash_info;
	uint32_t addr, end;
	int ret;

	mutex_lock(&flash->lock);

	addr = (instr->addr & (mtd->erasesize - 1));
	if (addr) {
		dev_err(flash->dev, "%s eraseaddr no align\n", __func__);
		mutex_unlock(&flash->lock);
		return -EINVAL;
	}
	end = (instr->len & (mtd->erasesize - 1));
	if (end) {
		dev_err(flash->dev,"%s erasesize no align\n", __func__);
		mutex_unlock(&flash->lock);
		return -EINVAL;
	}
	addr = (uint32_t)instr->addr;
	end = addr + (uint32_t)instr->len;

	sfc_flash_bus_lock(flash);

	while (addr < end) {
		ret = sfc_do_erase(flash, addr);
		if (ret) {
			dev_err(flash->dev,"erase error !\n");
			sfc_flash_bus_unlock(flash);
			mutex_unlock(&flash->lock);
			return ret;
		}
		addr += spi_nor_info->erase_size;
	}

	sfc_flash_bus_unlock(flash);

	mutex_unlock(&flash->lock);

	return 0;
}

#ifndef CONFIG_INGENIC_BUILTIN_PARAMS
static int32_t ingenic_spi_norflash_read_params(struct sfc_flash *flash, loff_t from, size_t len, uint8_t *buf)
{
	struct sfc_cdt_xfer xfer;
	memset(&xfer, 0, sizeof(xfer));

	xfer.cmd_index = NOR_READ_STANDARD;

	xfer.columnaddr = 0;
	xfer.rowaddr = from;

	xfer.dataen =  ENABLE;
	xfer.config.datalen = len;
	xfer.config.data_dir = GLB0_TRAN_DIR_READ;
	xfer.config.ops_mode = CPU_OPS;
	xfer.config.buf = buf;

	if(sfc_sync_cdt(flash->sfc, &xfer)) {
		dev_err(flash->dev,"sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
		return -EIO;
	}

	return 0;
}

struct mtd_partition *ingenic_sfc_nor_alloc_burner_parts(struct sfc_flash *flash,
		int *num_partition_info)
{
	struct spinor_flashinfo *nor_info = flash->flash_info;
	struct mtd_partition *ingenic_mtd_partition;
	int i;

	*num_partition_info = nor_info->norflash_partitions->num_partition_info;
	ingenic_mtd_partition = kzalloc(sizeof(struct mtd_partition) *
			(*num_partition_info), GFP_KERNEL);
	if (!ingenic_mtd_partition) {
		dev_err(flash->dev, "Failed to alloc mem for ingenic_mtd_partition\n");
		return NULL;
	}

	for (i = 0; i < *num_partition_info; i++) {
		ingenic_mtd_partition[i].name =
			nor_info->norflash_partitions->nor_partition[i].name;
		ingenic_mtd_partition[i].offset =
			nor_info->norflash_partitions->nor_partition[i].offset;

		if (nor_info->norflash_partitions->nor_partition[i].size == -1)
			ingenic_mtd_partition[i].size = MTDPART_SIZ_FULL;
		else
			ingenic_mtd_partition[i].size =
				nor_info->norflash_partitions->nor_partition[i].size;

		if (nor_info->norflash_partitions->nor_partition[i].mask_flags &
		    PART_RO)
			ingenic_mtd_partition[i].mask_flags = MTD_CAP_RAM;
		else
			ingenic_mtd_partition[i].mask_flags = MTD_CAP_ROM;
	}

	return ingenic_mtd_partition;
}
#endif

#ifdef DEBUG_CLONER_PARAMS
static void dump_cloner_params(struct burner_params *params)
{
	struct spi_nor_info *spi_nor_info;

	spi_nor_info = &params->spi_nor_info;

	printk("name=%s\n", spi_nor_info->name);
	printk("id=0x%x\n", spi_nor_info->id);

	printk("read_standard->cmd=0x%x\n",		spi_nor_info->read_standard.cmd);
	printk("read_standard->dummy=0x%x\n",		spi_nor_info->read_standard.dummy_byte);
	printk("read_standard->addr_nbyte=0x%x\n",	spi_nor_info->read_standard.addr_nbyte);
	printk("read_standard->transfer_mode=0x%x\n",	spi_nor_info->read_standard.transfer_mode);

	printk("read_quad->cmd=0x%x\n",			spi_nor_info->read_quad.cmd);
	printk("read_quad->dummy=0x%x\n",		spi_nor_info->read_quad.dummy_byte);
	printk("read_quad->addr_nbyte=0x%x\n",		spi_nor_info->read_quad.addr_nbyte);
	printk("read_quad->transfer_mode=0x%x\n",	spi_nor_info->read_quad.transfer_mode);

	printk("write_standard->cmd=0x%x\n",		spi_nor_info->write_standard.cmd);
	printk("write_standard->dummy=0x%x\n",		spi_nor_info->write_standard.dummy_byte);
	printk("write_standard->addr_nbyte=0x%x\n",	spi_nor_info->write_standard.addr_nbyte);
	printk("write_standard->transfer_mode=0x%x\n",	spi_nor_info->write_standard.transfer_mode);

	printk("write_quad->cmd=0x%x\n",		spi_nor_info->write_quad.cmd);
	printk("write_quad->dummy=0x%x\n",		spi_nor_info->write_quad.dummy_byte);
	printk("write_quad->addr_nbyte=0x%x\n",		spi_nor_info->write_quad.addr_nbyte);
	printk("write_quad->transfer_mode=0x%x\n",	spi_nor_info->write_quad.transfer_mode);

	printk("sector_erase->cmd=0x%x\n",		spi_nor_info->sector_erase.cmd);
	printk("sector_erase->dummy=0x%x\n",		spi_nor_info->sector_erase.dummy_byte);
	printk("sector_erase->addr_nbyte=0x%x\n",	spi_nor_info->sector_erase.addr_nbyte);
	printk("sector_erase->transfer_mode=0x%x\n",	spi_nor_info->sector_erase.transfer_mode);

	printk("wr_en->cmd=0x%x\n",		spi_nor_info->wr_en.cmd);
	printk("wr_en->dummy=0x%x\n",		spi_nor_info->wr_en.dummy_byte);
	printk("wr_en->addr_nbyte=0x%x\n",	spi_nor_info->wr_en.addr_nbyte);
	printk("wr_en->transfer_mode=0x%x\n",	spi_nor_info->wr_en.transfer_mode);

	printk("en4byte->cmd=0x%x\n",		spi_nor_info->en4byte.cmd);
	printk("en4byte->dummy=0x%x\n",		spi_nor_info->en4byte.dummy_byte);
	printk("en4byte->addr_nbyte=0x%x\n",	spi_nor_info->en4byte.addr_nbyte);
	printk("en4byte->transfer_mode=0x%x\n",	spi_nor_info->en4byte.transfer_mode);

	printk("quad_set->cmd=0x%x\n",		spi_nor_info->quad_set.cmd);
	printk("quad_set->bit_shift=0x%x\n",		spi_nor_info->quad_set.bit_shift);
	printk("quad_set->mask=0x%x\n",		spi_nor_info->quad_set.mask);
	printk("quad_set->val=0x%x\n",		spi_nor_info->quad_set.val);
	printk("quad_set->len=0x%x\n",		spi_nor_info->quad_set.len);
	printk("quad_set->dummy=0x%x\n",	spi_nor_info->quad_set.dummy);

	printk("quad_get->cmd=0x%x\n",		spi_nor_info->quad_get.cmd);
	printk("quad_get->bit_shift=0x%x\n",		spi_nor_info->quad_get.bit_shift);
	printk("quad_get->mask=0x%x\n",		spi_nor_info->quad_get.mask);
	printk("quad_get->val=0x%x\n",		spi_nor_info->quad_get.val);
	printk("quad_get->len=0x%x\n",		spi_nor_info->quad_get.len);
	printk("quad_get->dummy=0x%x\n",	spi_nor_info->quad_get.dummy);

	printk("busy->cmd=0x%x\n",		spi_nor_info->busy.cmd);
	printk("busy->bit_shift=0x%x\n",		spi_nor_info->busy.bit_shift);
	printk("busy->mask=0x%x\n",		spi_nor_info->busy.mask);
	printk("busy->val=0x%x\n",		spi_nor_info->busy.val);
	printk("busy->len=0x%x\n",		spi_nor_info->busy.len);
	printk("busy->dummy=0x%x\n",		spi_nor_info->busy.dummy);

	printk("quad_ops_mode=%d\n",	spi_nor_info->quad_ops_mode);
	printk("addr_ops_mode=%d\n",	spi_nor_info->addr_ops_mode);

	printk("tCHSH=%d\n",	spi_nor_info->tCHSH);
	printk("tSLCH=%d\n",	spi_nor_info->tSLCH);
	printk("tSHSL_RD=%d\n", spi_nor_info->tSHSL_RD);
	printk("tSHSL_WR=%d\n", spi_nor_info->tSHSL_WR);

	printk("chip_size=%d\n",	spi_nor_info->chip_size);
	printk("page_size=%d\n",	spi_nor_info->page_size);
	printk("erase_size=%d\n",	spi_nor_info->erase_size);

	printk("chip_erase_cmd=0x%x\n",	spi_nor_info->chip_erase_cmd);
}
#endif

#ifndef CONFIG_INGENIC_BUILTIN_PARAMS
#ifdef CONFIG_INGENIC_EXTRA_NOR_INFO_ENABLE 
struct spi_nor_info_tag {
    char tag[8];
    int array_size;
};
#endif

#endif

#ifdef CONFIG_INGENIC_BUILTIN_PARAMS
static int ingenic_sfc_nor_get_builtin_params(struct sfc_flash *flash)
{
	struct spinor_flashinfo *nor_info = flash->flash_info;
	struct builtin_params builtin_params;
	struct nor_params_node *nor_device;
	int chip_id;

	memset(&builtin_params, 0, sizeof(builtin_params));

	dev_info(flash->dev, "%s use builtin params.\n", flash->name);
	chip_id = sfc_nor_read_id(flash);
	if (chip_id < 0) {
		dev_err(flash->dev, "%s failed to read chip id: %d\n",
				flash->name, chip_id);
		return chip_id;
	}
	dev_info(flash->dev, "%s chip_id: 0x%x\n", flash->name, chip_id);

	list_for_each_entry(nor_device, &nor_list, list) {
		if (nor_device->nor_device_info.id == chip_id) {
			builtin_params.spi_nor_info = &nor_device->nor_device_info;
			break;
		}
	}

	if (!builtin_params.spi_nor_info) {
		dev_err(flash->dev, "%s unsupported builtin NOR id: 0x%x\n",
				flash->name, chip_id);
		return -ENODEV;
	}

	get_nor_builtin_params(flash, &builtin_params);
	if (builtin_params.magic != NOR_MAGIC ||
	    builtin_params.version != NOR_VERSION ||
	    !builtin_params.spi_nor_info ||
	    !builtin_params.nor_pri_data) {
		dev_err(flash->dev,
				"%s invalid builtin params magic=0x%x version=0x%x\n",
				flash->name, builtin_params.magic,
				builtin_params.version);
		return -EINVAL;
	}

	nor_info->nor_flash_info = builtin_params.spi_nor_info;
	nor_info->nor_pri_data = builtin_params.nor_pri_data;
	dev_info(flash->dev,
			"%s builtin params id=0x%x size=0x%x erase=0x%x\n",
			flash->name, builtin_params.spi_nor_info->id,
			builtin_params.spi_nor_info->chip_size,
			builtin_params.spi_nor_info->erase_size);

	return 0;
}
#endif

static int ingenic_spi_norflash_get_params(struct sfc_flash *flash)
{
	struct spinor_flashinfo *nor_info = flash->flash_info;

#ifndef CONFIG_INGENIC_BUILTIN_PARAMS
	int32_t ret = 0;
	struct burner_params *burner_params = NULL;
	struct nor_private_data *pri_data = NULL;
#ifdef CONFIG_INGENIC_GLOBAL_INFO
	struct jz_global_info *global_info = NULL;
#endif
#else
	int ret;
#endif


#ifndef CONFIG_INGENIC_BUILTIN_PARAMS
	dev_info(flash->dev, "Use burner params.\n");
#ifdef CONFIG_INGENIC_GLOBAL_INFO
	global_info = kzalloc(sizeof(struct jz_global_info), GFP_KERNEL);
	if (!global_info) {
		ret = -ENOMEM;
		goto err_params;
	}
	ret = ingenic_spi_norflash_read_params(flash, CONFIG_GINFO_ADDR, sizeof(struct jz_global_info), (uint8_t *)global_info);
	if(ret){
		printk("get global_info failed, ret: %d\n", ret);
		goto err_read_params;
	}
	sfc_params_offset = global_info->sfc_params_addr;
	kfree(global_info);
	global_info = NULL;
#else
	sfc_params_offset = SPIFLASH_PARAMER_OFFSET;
#endif

	ret = ingenic_sfc_nor_try_concat_params(flash, sfc_params_offset);
	if (!ret)
		return 0;
	if (ret != -ENOENT) {
		dev_err(flash->dev, "Failed to load NOR concat params: %d\n", ret);
		goto err_read_params;
	}

	burner_params = kzalloc(sizeof(struct burner_params), GFP_KERNEL);
	if (!burner_params) {
		dev_err(flash->dev, "Failed to alloc mem for params\n");
		ret = -ENOMEM;
		goto err_params;
	}

	ret = ingenic_spi_norflash_read_params(flash, sfc_params_offset,
			sizeof(struct burner_params),
			(uint8_t *)burner_params);
	if (ret) {
		dev_err(flash->dev, "Failed to read params (burned by Burner)\n");
		goto err_read_params;
	}
	//add crc check for params
	dev_info(flash->dev, "magic is 0x%x  version is 0x%x\n", burner_params->magic, burner_params->version);
	nor_info->nor_flash_info = NULL;
	if (burner_params->magic == NOR_MAGIC) {
		if (burner_params->version == NOR_VERSION) {
#ifdef CONFIG_INGENIC_EXTRA_NOR_INFO_ENABLE
			int flash_id;
			flash_id = sfc_nor_read_id(flash);
			if (flash_id < 0) {
				dev_err(flash->dev, "Failed to read chip id !\n");
				ret = flash_id;
				goto err_read_params;
			}

			if (flash_id != burner_params->spi_nor_info.id) {
				struct spi_nor_info_tag tag;
				ret = ingenic_spi_norflash_read_params(flash, CONFIG_INGENIC_EXTRA_NOR_INFO_OFF, sizeof(tag), (void *)&tag);
				if (ret) {
					dev_err(flash->dev, "Failed to read params\n");
					goto err_read_params;
				}

				if (!strncmp(tag.tag, "nor_tag", sizeof(tag.tag))) {
					int i;
					for (i = 0; i < tag.array_size; i++) {
						int off = CONFIG_INGENIC_EXTRA_NOR_INFO_OFF + sizeof(tag) + i*sizeof(struct spi_nor_info);
						ret = ingenic_spi_norflash_read_params(flash, off, sizeof(struct spi_nor_info), (void *)&burner_params->spi_nor_info);
						if (ret) {
							dev_err(flash->dev, "Failed to read params\n");
							goto err_read_params;
						}

						if (flash_id == burner_params->spi_nor_info.id) {
							break;
						}
					}
					if (i == tag.array_size) {
						dev_err(flash->dev, "not match extra nor info: %x\n", flash_id);
						ret = -EINVAL;
						goto err_read_params;
					}
				} else {
					dev_err(flash->dev, "not found extra nor info array\n");
					ret = -EINVAL;
					goto err_read_params;
				}
			}
#endif
			pri_data = devm_kzalloc(flash->dev, sizeof(*pri_data), GFP_KERNEL);
			if (!pri_data) {
				dev_err(flash->dev, "Failed to alloc mem for private data\n");
				ret = -ENOMEM;
				goto err_read_params;
			}
			pri_data->fs_erase_size = burner_params->fs_erase_size ?
				burner_params->fs_erase_size :
				burner_params->spi_nor_info.erase_size;
			pri_data->uk_quad = burner_params->uk_quad;
			nor_info->nor_flash_info = &burner_params->spi_nor_info;
			nor_info->norflash_partitions = &burner_params->norflash_partitions;
			nor_info->nor_pri_data = pri_data;
			nor_info->burner_params = burner_params;
		}
	}

	if (!nor_info->nor_flash_info) {
		ret = ingenic_sfc_nor_try_shared_concat_params(flash);
		if (!ret) {
			kfree(burner_params);
			burner_params = NULL;
		}
	}

#else

	ret = ingenic_sfc_nor_get_builtin_params(flash);
	if (ret)
		return ret;
#endif


#ifndef CONFIG_INGENIC_BUILTIN_PARAMS
	if ((!nor_info->nor_flash_info) || (!nor_info->nor_pri_data) ||
#ifdef CONFIG_MTD_INGENIC_SFC_CONCAT
			(!flash->sfc->topology &&
			 !nor_info->norflash_partitions)
#else
			(!nor_info->norflash_partitions)
#endif
			) {
		printk("WARNING : cannot get nor flash params or partitions !!!\n");
		ret = -EINVAL;
		goto err_read_params;
	}

#ifdef DEBUG_CLONER_PARAMS
	dump_cloner_params(burner_params);
#endif
	return 0;

err_read_params:
	kfree(burner_params);
#ifdef CONFIG_INGENIC_GLOBAL_INFO
	kfree(global_info);
	global_info = NULL;
#endif
err_params:
	return ret;
#else

	if (!nor_info->nor_flash_info) {
		printk("WARNING : cannot get nor flash params !!!\n");
		return -EINVAL;
	}

	return 0;
#endif
}
static ssize_t sfc_nor_partition_offset_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf,"0x%x\n", sfc_params_offset + sizeof(int) * 2 + sizeof(struct spi_nor_info));
}

static DEVICE_ATTR(sfc_nor_partition_offset, S_IRUGO | S_IWUSR,
		sfc_nor_partition_offset_show,
		NULL);

static ssize_t sfc_nor_params_offset_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf,"0x%x\n",sfc_params_offset);
}

static DEVICE_ATTR(sfc_nor_params_offset, S_IRUGO | S_IWUSR,
		sfc_nor_params_offset_show,
		NULL);

static ssize_t sfc_nor_dpb_lock_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sfc_flash *flash = platform_get_drvdata(pdev);
	struct mtd_info *mtd = &flash->mtd;
	loff_t ofs = 0;
	uint64_t len = 0;

	if (sscanf(buf, "%llx %llx", &ofs, &len) != 2) {
		printk("e.g., echo [start] [length] > sfc_nor_dpb_lock\n");
		return -EINVAL;
	}
	if ((ofs + len) > mtd->size) {
		printk("start + length exceeds the flash capacity\n");
		return -EINVAL;
	}

	ingenic_spi_norflash_lock(mtd, ofs, len);
	return n;
}

static ssize_t sfc_nor_dpb_unlock_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sfc_flash *flash = platform_get_drvdata(pdev);
	struct mtd_info *mtd = &flash->mtd;
	loff_t ofs = 0;
	uint64_t len = 0;

	if (sscanf(buf, "%llx %llx", &ofs, &len) != 2) {
		printk("e.g., echo [start] [length] > sfc_nor_dpb_unlock\n");
		return -EINVAL;
	}
	if ((ofs + len) > mtd->size) {
		printk("start + length exceeds the flash capacity\n");
		return -EINVAL;
	}

	ingenic_spi_norflash_unlock(mtd, ofs, len);
	return n;
}

static ssize_t sfc_nor_dpb_status_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sfc_flash *flash = platform_get_drvdata(pdev);
	struct mtd_info *mtd = &flash->mtd;
	loff_t ofs = 0;
	uint64_t len = 0;
	int ret = 0;

	if (sscanf(buf, "%llx %llx", &ofs, &len) != 2) {
		printk("e.g., echo [start] [length] > sfc_nor_dpb_status\n");
		return -EINVAL;
	}
	if ((ofs + len) > mtd->size) {
		printk("start + length exceeds the flash capacity\n");
		return -EINVAL;
	}

	ret = ingenic_spi_norflash_is_locked(mtd, ofs, len);
	printk("%s\n", ret ? "locked" : "unlock");

	return n;
}

static ssize_t sfc_nor_get_status_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sfc_flash *flash = platform_get_drvdata(pdev);
	int32_t reg = 0;
	int len = 0;
	int status = 0;

	if (sscanf(buf, "%x %d", &reg, &len) != 2) {
		printk("e.g., echo [register] [length] > sfc_nor_get_status\n");
		return -EINVAL;
	}
	if (reg != SPINOR_OP_RDSR && reg != SPINOR_OP_RDSR_1 && reg != SPINOR_OP_RDSR_2) {
		printk("The register value is invalid\n");
		return -EINVAL;
	}
	if (len == 0 || len > 4) {
		printk("The length value is invalid\n");
		return -EINVAL;
	}

	status = get_status(flash, reg, len);
	printk("status = %x\n",status);

	return n;
}

static ssize_t sfc_nor_set_status_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sfc_flash *flash = platform_get_drvdata(pdev);
	uint32_t reg = 0;
	uint32_t val = 0;
	int len = 0;

	if (sscanf(buf, "%x %x %d", &reg, &val, &len) != 3) {
		printk("e.g., echo [register] [value] [length] > sfc_nor_set_status\n");
		return -EINVAL;
	}
	if (reg != SPINOR_OP_WRSR && reg != SPINOR_OP_WRSR_1 && reg != SPINOR_OP_WRSR_2) {
		printk("The register value is invalid\n");
		return -EINVAL;
	}
	if (len == 0 || len > 4) {
		printk("The length value is invalid\n");
		return -EINVAL;
	}

	add_set_status_to_cdt(flash, reg, val);
	set_status(flash, reg, (uint8_t *)&val, len);

	return n;
}
static DEVICE_ATTR(sfc_nor_dpb_lock, S_IRUGO | S_IWUSR,
		NULL, sfc_nor_dpb_lock_store);

static DEVICE_ATTR(sfc_nor_dpb_unlock, S_IRUGO | S_IWUSR,
		NULL, sfc_nor_dpb_unlock_store);

static DEVICE_ATTR(sfc_nor_dpb_status, S_IRUGO | S_IWUSR,
		NULL, sfc_nor_dpb_status_store);

static DEVICE_ATTR(sfc_nor_get_status, S_IRUGO | S_IWUSR,
		NULL, sfc_nor_get_status_store);

static DEVICE_ATTR(sfc_nor_set_status, S_IRUGO | S_IWUSR,
		NULL, sfc_nor_set_status_store);

/*add your attr in here*/
static struct attribute *sfc_norflash_info_attributes[] = {
	&dev_attr_sfc_nor_partition_offset.attr,
	&dev_attr_sfc_nor_params_offset.attr,
	&dev_attr_sfc_nor_dpb_lock.attr,
	&dev_attr_sfc_nor_dpb_unlock.attr,
	&dev_attr_sfc_nor_dpb_status.attr,
	&dev_attr_sfc_nor_get_status.attr,
	&dev_attr_sfc_nor_set_status.attr,
	NULL
};

static struct attribute_group sfc_norflash_info_attr_group = {
	.attrs = sfc_norflash_info_attributes
};

int ingenic_sfcnor_register(struct nor_params_node *flash) {
	list_add_tail(&flash->list, &nor_list);
	return 0;
}
EXPORT_SYMBOL_GPL(ingenic_sfcnor_register);

/*
 *MK_CMD(cdt, cmd, LINK, ADDRMODE, DATA_EN)
 *MK_ST(cdt, st, LINK, ADDRMODE, ADDR_WIDTH, POLL_EN, DATA_EN, TRAN_MODE)
 */
static void params_to_cdt(struct spi_nor_info *params, struct sfc_cdt *cdt)
{

	/* 6. nor singleRead */
	MK_CMD(cdt[NOR_READ_STANDARD], params->read_standard, 0, ROW_ADDR, ENABLE);

	/* 7. nor quadRead */
	MK_CMD(cdt[NOR_READ_QUAD], params->read_quad, 0, ROW_ADDR, ENABLE);

	/* 8. nor writeStandard */
	MK_CMD(cdt[NOR_WRITE_STANDARD_ENABLE], params->wr_en, 1, DEFAULT_ADDRMODE, DISABLE);
	MK_CMD(cdt[NOR_WRITE_STANDARD], params->write_standard, 1, ROW_ADDR, ENABLE);
	MK_ST(cdt[NOR_WRITE_STANDARD_FINISH], params->busy, 0, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);

	/* 9. nor writeQuad */
	MK_CMD(cdt[NOR_WRITE_QUAD_ENABLE], params->wr_en, 1, DEFAULT_ADDRMODE, DISABLE);
	MK_CMD(cdt[NOR_WRITE_QUAD], params->write_quad, 1, ROW_ADDR, ENABLE);
	MK_ST(cdt[NOR_WRITE_QUAD_FINISH], params->busy, 0, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);

	/* 10. nor erase */
	MK_CMD(cdt[NOR_ERASE_WRITE_ENABLE], params->wr_en, 1, DEFAULT_ADDRMODE, DISABLE);
	MK_CMD(cdt[NOR_ERASE], params->sector_erase, 1, ROW_ADDR, DISABLE);
	MK_ST(cdt[NOR_ERASE_FINISH], params->busy, 0, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);

	/* 11. quad mode */
	if(params->quad_ops_mode){
		MK_CMD(cdt[NOR_QUAD_SET_ENABLE], params->wr_en, 1, DEFAULT_ADDRMODE, DISABLE);
		MK_ST(cdt[NOR_QUAD_SET], params->quad_set, 1, DEFAULT_ADDRMODE, 0, DISABLE, ENABLE, TM_STD_SPI);  //disable poll, enable data

		MK_ST(cdt[NOR_QUAD_FINISH], params->busy, 1, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);

		MK_ST(cdt[NOR_QUAD_GET], params->quad_get, 0, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);
	}

	/* 12. nor write enable */
	MK_CMD(cdt[NOR_WRITE_ENABLE], params->wr_en, 0, DEFAULT_ADDRMODE, DISABLE);

	/* 13. entry 4byte mode */
	MK_CMD(cdt[NOR_EN_4BYTE], params->en4byte, 0, DEFAULT_ADDRMODE, DISABLE);

	/* 14. Dynamic Protection Block/Sector Lock */
	MK_CMD(cdt[NOR_DPB_LOCK_WRITE_ENABLE], params->wr_en, 1, DEFAULT_ADDRMODE, DISABLE);
	cmd_dpb_lock.addr_nbyte = params->sector_erase.addr_nbyte;
	MK_CMD(cdt[NOR_DPB_LOCK], cmd_dpb_lock, 1, ROW_ADDR, DISABLE);
	MK_ST(cdt[NOR_DPB_LOCK_FINISH], params->busy, 0, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);

	/* 15. Dynamic Protection Block/Sector Lock */
	MK_CMD(cdt[NOR_DPB_UNLOCK_WRITE_ENABLE], params->wr_en, 1, DEFAULT_ADDRMODE, DISABLE);
	cmd_dpb_unlock.addr_nbyte = params->sector_erase.addr_nbyte;
	MK_CMD(cdt[NOR_DPB_UNLOCK], cmd_dpb_unlock, 1, ROW_ADDR, DISABLE);
	MK_ST(cdt[NOR_DPB_UNLOCK_FINISH], params->busy, 0, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);

	/* 16. Read DPB Status */
	cmd_dpb_status.addr_nbyte = params->sector_erase.addr_nbyte;
	MK_CMD(cdt[NOR_DPB_STATUS], cmd_dpb_status, 0, ROW_ADDR, ENABLE);

}

static void nor_create_cdt_table(struct sfc *sfc, void *flash_info, uint32_t flag)
{
	struct spinor_flashinfo *nor_info = flash_info;
	struct sfc_cdt *sfc_cdt = NULL;

	sfc_cdt = kzalloc(sizeof(struct sfc_cdt) * INDEX_MAX_NUM, GFP_KERNEL);
	if(sfc_cdt == NULL) {
		printk("Failed to alloc sfc_cdt buffer!\n");
		return;
	}

	if(flag & DEFAULT_CDT){

		/* 1.nor reset */
		sfc_cdt[NOR_RESET_ENABLE].link = CMD_LINK(1, DEFAULT_ADDRMODE, TM_STD_SPI);
		sfc_cdt[NOR_RESET_ENABLE].xfer = CMD_XFER(0, DISABLE, 0, DISABLE, SPINOR_OP_RSTEN);
		sfc_cdt[NOR_RESET_ENABLE].staExp = 0;
		sfc_cdt[NOR_RESET_ENABLE].staMsk = 0;

		sfc_cdt[NOR_RESET].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
		sfc_cdt[NOR_RESET].xfer = CMD_XFER(0, DISABLE, 0, DISABLE, SPINOR_OP_RST);
		sfc_cdt[NOR_RESET].staExp = 0;
		sfc_cdt[NOR_RESET].staMsk = 0;

		/* 2.nor read id */
		sfc_cdt[NOR_READ_ID].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
		sfc_cdt[NOR_READ_ID].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, SPINOR_OP_RDID);
		sfc_cdt[NOR_READ_ID].staExp = 0;
		sfc_cdt[NOR_READ_ID].staMsk = 0;

		/* 3. nor get status */
		sfc_cdt[NOR_GET_STATUS].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
		sfc_cdt[NOR_GET_STATUS].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, SPINOR_OP_RDSR);
		sfc_cdt[NOR_GET_STATUS].staExp = 0;
		sfc_cdt[NOR_GET_STATUS].staMsk = 0;

		/* 4. nor get status 1*/
		sfc_cdt[NOR_GET_STATUS_1].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
		sfc_cdt[NOR_GET_STATUS_1].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, SPINOR_OP_RDSR_1);
		sfc_cdt[NOR_GET_STATUS_1].staExp = 0;
		sfc_cdt[NOR_GET_STATUS_1].staMsk = 0;

		/* 5. nor get status 2*/
		sfc_cdt[NOR_GET_STATUS_2].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
		sfc_cdt[NOR_GET_STATUS_2].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, SPINOR_OP_RDSR_2);
		sfc_cdt[NOR_GET_STATUS_2].staExp = 0;
		sfc_cdt[NOR_GET_STATUS_2].staMsk = 0;

		/* 6.nor singleRead */
		sfc_cdt[NOR_READ_STANDARD].link = CMD_LINK(0, ROW_ADDR, TM_STD_SPI);
		sfc_cdt[NOR_READ_STANDARD].xfer = CMD_XFER(DEFAULT_ADDRSIZE, DISABLE, 0, ENABLE, SPINOR_OP_READ);
		sfc_cdt[NOR_READ_STANDARD].staExp = 0;
		sfc_cdt[NOR_READ_STANDARD].staMsk = 0;

		if (!(flag & UPDATE_CDT)) {
			/* first create cdt table (default)*/
			write_cdt(sfc, sfc_cdt, NOR_RESET_ENABLE,
					NOR_READ_STANDARD);
		}

	}

	if(flag & UPDATE_CDT) {

		params_to_cdt(nor_info->nor_flash_info, sfc_cdt);

		/* 17. nor write disable */
		sfc_cdt[NOR_WRITE_DISABLE].link = CMD_LINK(0, ROW_ADDR, TM_STD_SPI);
		sfc_cdt[NOR_WRITE_DISABLE].xfer = CMD_XFER(DEFAULT_ADDRSIZE, DISABLE, 0, DISABLE, SPINOR_OP_WRDI);
		sfc_cdt[NOR_WRITE_DISABLE].staExp = 0;
		sfc_cdt[NOR_WRITE_DISABLE].staMsk = 0;

		/* 18. die select */
		sfc_cdt[NOR_DIE_SELECT].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
		sfc_cdt[NOR_DIE_SELECT].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, SPINOR_OP_DIE_SEL);
		sfc_cdt[NOR_DIE_SELECT].staExp = 0;
		sfc_cdt[NOR_DIE_SELECT].staMsk = 0;

		/* 19. read active die ID */
		sfc_cdt[NOR_READ_ACTIVE_DIE_ID].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
		sfc_cdt[NOR_READ_ACTIVE_DIE_ID].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, SPINOR_OP_READ_DIE_ID);
		sfc_cdt[NOR_READ_ACTIVE_DIE_ID].staExp = 0;
		sfc_cdt[NOR_READ_ACTIVE_DIE_ID].staMsk = 0;

		if (flag & DEFAULT_CDT) {
			/* create full cdt table (default && update) */
			write_cdt(sfc, sfc_cdt, NOR_RESET_ENABLE,
					NOR_READ_ACTIVE_DIE_ID);
		} else {
			/* second create cdt table (update)*/
			write_cdt(sfc, sfc_cdt, NOR_READ_STANDARD,
					NOR_READ_ACTIVE_DIE_ID);
		}
	}
	//dump_cdt(sfc);
	kfree(sfc_cdt);
}

static struct multi_die_flash die_flash[MULTI_DIE_FLASH_NUM] = {
	[0] = {0xc84019, 2, "GD25S512MD"},
};

int ingenic_spi_norflash_suspend(struct mtd_info *mtd)
{
	return 0;
}

void ingenic_spi_norflash_resume(struct mtd_info *mtd)
{
#if defined(CONFIG_SOC_M300) || defined(CONFIG_SOC_X2000)
	int ret = 0;
	int tchsh = 0;
	int tslch = 0;
	int tshsl_rd = 0;
	int tshsl_wr = 0;
	int fastboot_flag = 0;
	struct sfc_flash *flash = to_ingenic_spi_norflash(mtd);
	struct spinor_flashinfo *nor_info = flash->flash_info;

	fastboot_flag = pm_get_sleep_level();
	if(fastboot_flag == FASTBOOT){/*fastboot sleep level*/

		set_flash_timing(flash->sfc, DEF_TCHSH, DEF_TSLCH, DEF_TSHSL_R, DEF_TSHSL_W);

		ret = sfc_nor_reset(flash);
		if(ret) {
			dev_warn(flash->dev, "Failed to reset nor flash, Try to go on\n");
		}
		set_flash_timing(flash->sfc, tchsh, tslch, tshsl_rd, tshsl_wr);
		/* if nor flash size is greater than 16M, use 4byte mode */
		if(flash->mtd.size > NOR_SIZE_16M) {
			if (nor_info->nor_flash_ops->set_4byte_mode) {
				nor_info->nor_flash_ops->set_4byte_mode(flash);
			}
		}

		/* Update sfc rate */
		if((ret = ingenic_sfc_nor_clk_set_highspeed(flash))) {
			dev_err(flash->dev, "set sfc rate failed\n");
		}
	}
#endif
	return;
}

void ingenic_sfc_nor_free_chip(struct sfc_flash *flash)
{
	struct spinor_flashinfo *nor_info = flash->flash_info;

	if (!nor_info)
		return;

#ifndef CONFIG_INGENIC_BUILTIN_PARAMS
	kfree(nor_info->burner_params);
#endif
	kfree(nor_info);
	flash->flash_info = NULL;
}

int ingenic_sfc_nor_init_chip(struct sfc_flash *flash, const char *name)
{
	int ret = 0;
	struct spinor_flashinfo *nor_info;
	int i;
	int tchsh;
	int tslch;
	int tshsl_rd;
	int tshsl_wr;
	uint32_t die_size;

	nor_info = kzalloc(sizeof(*nor_info), GFP_KERNEL);
	if(!nor_info) {
		dev_err(flash->dev, "alloc nor_info failed!\n");
		return -ENOMEM;
	}
	flash->flash_info = nor_info;
	flash->create_cdt_table = nor_create_cdt_table;

	set_flash_timing(flash->sfc, DEF_TCHSH, DEF_TSLCH, DEF_TSHSL_R, DEF_TSHSL_W);

	/* try creating default CDT table */
	flash->create_cdt_table(flash->sfc, flash->flash_info, DEFAULT_CDT);

	ret = sfc_nor_reset(flash);
	if(ret) {
		dev_warn(flash->dev, "Failed to reset nor flash, Try to go on\n");
	}

	ret = ingenic_spi_norflash_get_params(flash);
	if (ret) {
		ret = -ENODEV;
		dev_err(flash->dev, "Failed to match correct nor flash device!\n");
		goto err_match_device;
	}

	/* Update to private CDT table */
	flash->create_cdt_table(flash->sfc, flash->flash_info, UPDATE_CDT);

	/* Update sfc rate */
	if((ret = ingenic_sfc_nor_clk_set_highspeed(flash))) {
		dev_err(flash->dev, "set sfc rate failed\n");
		goto err_match_device;
	}

	flash->mtd.name     = name ? name : "sfc_mtd";
	flash->mtd.owner    = THIS_MODULE;
	flash->mtd.type     = MTD_NORFLASH;
	flash->mtd.flags    = MTD_CAP_NORFLASH;
	flash->mtd.erasesize    = nor_info->nor_pri_data->fs_erase_size;
	flash->mtd.writesize    = 1;
	flash->mtd.writebufsize = nor_info->nor_flash_info->page_size;
	flash->mtd.size     = nor_info->nor_flash_info->chip_size;
	flash->mtd._erase   = ingenic_spi_norflash_erase;
	flash->mtd._read    = ingenic_spi_norflash_read;
	flash->mtd._write   = ingenic_spi_norflash_write;
	flash->mtd._suspend    = ingenic_spi_norflash_suspend;
	flash->mtd._resume   = ingenic_spi_norflash_resume;
	flash->mtd._lock       = ingenic_spi_norflash_lock;
	flash->mtd._unlock     = ingenic_spi_norflash_unlock;
	flash->mtd._is_locked  = ingenic_spi_norflash_is_locked;

	tchsh = nor_info->nor_flash_info->tCHSH;
	tslch = nor_info->nor_flash_info->tSLCH;
	tshsl_rd = nor_info->nor_flash_info->tSHSL_RD;
	tshsl_wr = nor_info->nor_flash_info->tSHSL_WR;
	set_flash_timing(flash->sfc, tchsh, tslch, tshsl_rd, tshsl_wr);

	sfc_nor_get_special_ops(flash);

	if (nor_info->nor_pri_data->uk_quad) {
		if (nor_info->nor_flash_ops->set_quad_mode) {
			ret = nor_info->nor_flash_ops->set_quad_mode(flash);
			if (ret < 0) {
				nor_info->quad_succeed = 0;
				dev_info(flash->dev, "set quad mode error !\n");
			} else {
				nor_info->quad_succeed = 1;
				dev_info(flash->dev, "nor flash quad mode is set, now use quad mode!\n");
			}
		}
	}

	/* Multi Die support */
	nor_info->die_num = 1;
	for (i = 0; i < MULTI_DIE_FLASH_NUM; i++) {
		if(!(strcmp(die_flash[i].flash_name, nor_info->nor_flash_info->name))) {
			nor_info->die_num = die_flash[i].die_num;
			die_size = nor_info->nor_flash_info->chip_size / nor_info->die_num;
			nor_info->die_shift = ffs(die_size) - 1;
			nor_info->current_die_id = 0;

			printk("Flash :%s support multi die, die number:%d\n", die_flash[i].flash_name, die_flash[i].die_num);
			break;
		}
	}


	/* if nor flash size is greater than 16M, use 4byte mode */
	if(flash->mtd.size > NOR_SIZE_16M) {
		if (nor_info->nor_flash_ops->set_4byte_mode) {
			nor_info->nor_flash_ops->set_4byte_mode(flash);
		}
	}
	return 0;

err_match_device:
	ingenic_sfc_nor_free_chip(flash);
	return ret;
}

int ingenic_sfc_nor_probe(struct sfc_flash *flash)
{
	struct mtd_partition *ingenic_mtd_partition = NULL;
	const char *ingenic_probe_types[] = {"cmdlinepart", "ofpart", NULL};
	int num_partition_info = 0;
	int ret = 0;

	/* request DMA Descriptor space */
	ret = request_sfc_desc(flash->sfc, DESC_MAX_NUM);
	if(ret){
		dev_err(flash->dev,
				"Failure to request DMA descriptor space: %d\n", ret);
		return ret;
	}

	ret = ingenic_sfc_nor_init_chip(flash, "sfc_mtd");
	if (ret)
		goto err_chip_init;

#ifndef CONFIG_INGENIC_BUILTIN_PARAMS
	ingenic_mtd_partition = ingenic_sfc_nor_alloc_burner_parts(flash, &num_partition_info);
	if (!ingenic_mtd_partition) {
		ret = -ENOMEM;
		goto err_alloc_partition;
	}
#endif

	/*
	 * partiton parser:  1.cmdlinepart  2.device tree  3.flash burner
	 */
	flash->dev->of_node = NULL;
	if (flash->pdata_params->use_ofpart_info) {
		flash->dev->of_node = of_find_node_by_name(NULL, "norflash");
		if (flash->dev->of_node < 0) {
			dev_err(flash->dev, "Cannot get 'norflash' node from dtb!\n");
			flash->dev->of_node = NULL;
		}
                mtd_set_of_node(&flash->mtd, flash->dev->of_node);
	}

	ret = mtd_device_parse_register(&flash->mtd, ingenic_probe_types, &flash->ppdata, ingenic_mtd_partition, num_partition_info);
	if (ret) {
		ret = -ENODEV;
		dev_err(flash->dev, "Failed to parse register!\n");
		goto err_parse_register;
	}

	flash->attr_group = &sfc_norflash_info_attr_group;
	ret = sysfs_create_group(&flash->dev->kobj, flash->attr_group);
	if (ret) {
		dev_err(flash->dev, "failed to register sysfs\n");
		ret = -EIO;
		goto err_create_group;
	}

	dev_info(flash->dev,"SPI NOR MTD LOAD OK\n");
	kfree(ingenic_mtd_partition);
	return 0;

err_create_group:
	mtd_device_unregister(&flash->mtd);
err_parse_register:
	kfree(ingenic_mtd_partition);
#ifndef CONFIG_INGENIC_BUILTIN_PARAMS
err_alloc_partition:
#endif
	ingenic_sfc_nor_free_chip(flash);
err_chip_init:
	free_sfc_desc(flash->sfc);
	return ret;
}
