/**
 ****************************************************************************************
 *
 * @file rwnx_strs.h
 *
 * @brief Miscellaneous debug strings
 *
 * Copyright (C) RivieraWaves 2014-2020
 *
 ****************************************************************************************
 */

#ifndef _RWNX_STRS_H_
#define _RWNX_STRS_H_

#include "fw_api/wifi/mac/cp_api.h"

#define RWNX_ID2STR(tag)                                                       \
	(((MSG_T(tag) < ARRAY_SIZE(rwnx_id2str)) &&                            \
	  (rwnx_id2str[MSG_T(tag)]) &&                                         \
	  ((rwnx_id2str[MSG_T(tag)])[MSG_I(tag)])) ?                           \
		 (rwnx_id2str[MSG_T(tag)])[MSG_I(tag)] :                       \
		 "unknown")

extern const char *const *rwnx_id2str[TASK_LAST_EMB + 1];

#endif /* _RWNX_STRS_H_ */
