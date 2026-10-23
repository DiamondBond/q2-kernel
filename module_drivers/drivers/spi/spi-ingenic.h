/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPI_INGENIC_H__
#define __SPI_INGENIC_H__

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#define SSI_DR			0x000
#define SSI_CR0			0x004
#define SSI_CR1			0x008
#define SSI_SR			0x00c
#define SSI_ITR			0x010
#define SSI_GR			0x018

#define SSI_CR0_TENDIAN_LSB	BIT(19)
#define SSI_CR0_RENDIAN_LSB	BIT(17)
#define SSI_CR0_SSIE		BIT(15)
#define SSI_CR0_RIE		BIT(13)
#define SSI_CR0_LOOP		BIT(10)
#define SSI_CR0_EACLRUN	BIT(7)
#define SSI_CR0_FSEL		BIT(6)
#define SSI_CR0_TFLUSH		BIT(2)
#define SSI_CR0_RFLUSH		BIT(1)

#define SSI_CR1_FRMHL_MASK	(BIT(31) | BIT(30))
#define SSI_CR1_FRMHL		BIT(30)
#define SSI_CR1_LFST		BIT(25)
#define SSI_CR1_UNFIN		BIT(23)
#define SSI_CR1_TTRG_MASK	GENMASK(19, 16)
#define SSI_CR1_RTRG_MASK	GENMASK(11, 8)
#define SSI_CR1_PHA		BIT(1)
#define SSI_CR1_POL		BIT(0)

#define SSI_SR_END		BIT(7)
#define SSI_SR_BUSY		BIT(6)
#define SSI_SR_TFF		BIT(5)
#define SSI_SR_RFE		BIT(4)
#define SSI_SR_RFHF		BIT(2)
#define SSI_SR_UNDR		BIT(1)
#define SSI_SR_OVER		BIT(0)

static inline u32 ingenic_ssi_readl(void __iomem *base, u32 reg)
{
	return readl(base + reg);
}

static inline void ingenic_ssi_writel(void __iomem *base, u32 reg, u32 value)
{
	writel(value, base + reg);
}

static inline void ingenic_ssi_update_bits(void __iomem *base, u32 reg,
					   u32 mask, u32 value)
{
	u32 tmp = ingenic_ssi_readl(base, reg);

	ingenic_ssi_writel(base, reg, (tmp & ~mask) | (value & mask));
}

static inline void ingenic_ssi_set_bits(void __iomem *base, u32 reg, u32 bits)
{
	ingenic_ssi_update_bits(base, reg, bits, bits);
}

static inline void ingenic_ssi_clear_bits(void __iomem *base, u32 reg, u32 bits)
{
	ingenic_ssi_update_bits(base, reg, bits, 0);
}

static inline void ingenic_ssi_set_flen(void __iomem *base, u8 shift,
					unsigned int bits_per_word)
{
	u32 mask = GENMASK(shift + 4, shift);

	ingenic_ssi_update_bits(base, SSI_CR1, mask,
			       (bits_per_word - 2) << shift);
}

static inline void ingenic_ssi_set_rx_threshold(void __iomem *base,
						 unsigned int entries)
{
	ingenic_ssi_update_bits(base, SSI_CR1, SSI_CR1_RTRG_MASK,
			       (entries / 8) << 8);
}

static inline void ingenic_ssi_set_tx_threshold(void __iomem *base,
						 unsigned int entries)
{
	ingenic_ssi_update_bits(base, SSI_CR1, SSI_CR1_TTRG_MASK,
			       (entries / 8) << 16);
}

static inline int ingenic_ssi_wait_status(void __iomem *base, u32 mask,
						   bool set, unsigned int timeout_us)
{
	u32 status;

	return readl_poll_timeout(base + SSI_SR, status,
				  !!(status & mask) == set, 100, timeout_us);
}

#endif
