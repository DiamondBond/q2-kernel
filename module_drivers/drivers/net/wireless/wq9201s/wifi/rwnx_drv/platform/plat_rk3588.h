/** @file pcie.h
 *
 *  @brief This file contains definitions for PCIE interface.
 *  driver.
 *
 * Copyright (C) 2016-2023, WuQi Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */
#ifndef _PLAT_RK3588_H_
#define _PLAT_RK3588_H_

#include "core.h"

#ifdef CONFIG_RK3588_ENABLE_WAKEUP_OOB
/* system wakeup via oob */
int wq_core_register_oob_wakeup_host(struct wq_core *wq_core);
void wq_core_unregister_oob_wakeup_host(struct wq_core *wq_core);
#endif

#endif /* _PLAT_RK3588_H_ */
