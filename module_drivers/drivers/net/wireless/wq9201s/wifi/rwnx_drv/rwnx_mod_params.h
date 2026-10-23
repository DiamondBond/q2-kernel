/**
 ******************************************************************************
 *
 * @file rwnx_mod_params.h
 *
 * @brief Declaration of module parameters
 *
 * Copyright (C) RivieraWaves 2012-2020
 *
 ******************************************************************************
 */

#ifndef _RWNX_MOD_PARAM_H_
#define _RWNX_MOD_PARAM_H_

struct rwnx_hw;
struct wiphy;

int rwnx_handle_dynparams(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy);
void rwnx_custregd(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy);
void rwnx_enable_wapi(struct rwnx_hw *rwnx_hw);
void rwnx_enable_mfp(struct rwnx_hw *rwnx_hw);
void rwnx_enable_gcmp(struct rwnx_hw *rwnx_hw);
void rwnx_adjust_amsdu_maxnb(struct rwnx_hw *rwnx_hw);

#endif /* _RWNX_MOD_PARAM_H_ */
