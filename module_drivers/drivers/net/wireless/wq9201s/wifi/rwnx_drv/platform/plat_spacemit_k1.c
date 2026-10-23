/** @file woal_pcie.c
 *
 *  @brief This file contains PCIE IF (interface) module
 *  related functions.
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
#ifdef CONFIG_SPACEMIT_K1_SDIO_WAR
#include "plat_spacemit_k1.h"

#define K1_AUTO_CLOCK_BASEADDR  0xd4280908
#define SDIO_ENABLE_AUTOCLOCK   0x75091C00
#define SDIO_DISABLE_AUTOCLOCK  0x75090400

void wq_sdio_config_k1_autoclock(bool enable)
{
	u32 data;
	void __iomem *regs = ioremap(K1_AUTO_CLOCK_BASEADDR, 4);
        
	if (enable) {
		iowrite32(SDIO_ENABLE_AUTOCLOCK, regs);
	} 
	else {
		iowrite32(SDIO_DISABLE_AUTOCLOCK, regs);
	}

	data = ioread32(regs);
	iounmap(regs);
	WQ_DBG(DM_TRBUS, DL_INF, "K1 read reg: 0x%08x data: 0x%08x\n", K1_AUTO_CLOCK_BASEADDR, data);
}

WQ_PLAT_SPACEMIT_K1_API(wq_sdio_config_k1_autoclock);
#endif
