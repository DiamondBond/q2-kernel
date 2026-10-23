#ifndef _COUNTRY_C_
#define _COUNTRY_C_

#include <net/cfg80211.h>

#include "wq_log.h"
#include "country.h"
#include "rwnx_msg_tx.h"
#include "rwnx_mod_params.h"

/**
 * macro definition
*/

#define CH_TO_FREQ_24G(chn) (2407 + (chn * 5))
#define CH_TO_FREQ_5G(chn) (5000 + (chn * 5))

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
#define WQ_RRF_AUTO_BW NL80211_RRF_AUTO_BW
#else
#define WQ_RRF_AUTO_BW 0
#endif

#define REG_RULE_LIGHT(start, end, bw, flags)                                  \
	REG_RULE(start, end, bw, 0, 20, flags)

#define REG_GROUP_MAX_IDX                                                      \
	(sizeof(reg_support_group) / sizeof(struct reg_info_entry))

#define WQ_DOMAIN_INIT(cc)                                                     \
	const struct wq_reg_domain domain_##cc = { .countrycode = #cc,         \
						   .regrules = &wq_regd_##cc }

#define WQ_DOMAIN_80211_DEF(cc) const struct ieee80211_regdomain wq_regd_##cc =

#define WQ_DOMAIN_INIT_RULE(cc) &domain_##cc

#define REG_COUNTRY_GROUP_END(num) NULL, 0,

#define REG_SUB_BAND_INF(class, band, span, fchnum, numch, dfs)                \
	{ class, band, span, fchnum, numch, dfs },

#define REG_COUNTRY_GROUP_NUM(num)                                             \
	(uint16_t *)reg_country_group##num,                                    \
		sizeof(reg_country_group##num) / sizeof(uint16_t),

/* Define mapping tables between country code and its channel set
 */
#define REG_COUNTRY_GROUP(num, country)                                        \
	static const uint16_t reg_country_group##num[] = country

#define REG_COUNTRY_GROUP0                                                     \
		{                                                                      \
			COUNTRY_CODE_JP, COUNTRY_CODE_CH, COUNTRY_CODE_TR,                 \
			COUNTRY_CODE_ZA                                                    \
		}

#define REG_COUNTRY_GROUP7                                                     \
	{                                                                      \
		COUNTRY_CODE_JO, COUNTRY_CODE_PG                               \
	}

#define REG_COUNTRY_GROUP8                                                     \
	{                                                                      \
		COUNTRY_CODE_MX, COUNTRY_CODE_VE                               \
	}

#define REG_COUNTRY_GROUP9                                                     \
	{                                                                      \
		COUNTRY_CODE_US, COUNTRY_CODE_CA, COUNTRY_CODE_TW              \
	}

#define REG_COUNTRY_GROUP10                                                    \
	{                                                                      \
		COUNTRY_CODE_DM, COUNTRY_CODE_SV, COUNTRY_CODE_HN              \
	}

#define REG_COUNTRY_GROUP11                                                    \
	{                                                                      \
		COUNTRY_CODE_CL, COUNTRY_CODE_EG, COUNTRY_CODE_IN,             \
			COUNTRY_CODE_AG, COUNTRY_CODE_BS, COUNTRY_CODE_BH,     \
			COUNTRY_CODE_BB, COUNTRY_CODE_BN, COUNTRY_CODE_MV,     \
			COUNTRY_CODE_PA, COUNTRY_CODE_ZM, COUNTRY_CODE_CN,      \
			COUNTRY_CODE_SG,                                        \
	}

#define REG_COUNTRY_GROUP12                                                    \
	{                                                                      \
		COUNTRY_CODE_IL, COUNTRY_CODE_AM, COUNTRY_CODE_KW,             \
			COUNTRY_CODE_MA, COUNTRY_CODE_NE, COUNTRY_CODE_TN      \
	}

#define REG_COUNTRY_GROUP13                                                    \
	{                                                                      \
		COUNTRY_CODE_AS, COUNTRY_CODE_AI, COUNTRY_CODE_BM,             \
			COUNTRY_CODE_KY, COUNTRY_CODE_GU, COUNTRY_CODE_FM,     \
			COUNTRY_CODE_PR, COUNTRY_CODE_VI, COUNTRY_CODE_AZ,     \
			COUNTRY_CODE_BW, COUNTRY_CODE_KH, COUNTRY_CODE_CX,     \
			COUNTRY_CODE_CO, COUNTRY_CODE_CR, COUNTRY_CODE_GD,     \
			COUNTRY_CODE_GT, COUNTRY_CODE_KI, COUNTRY_CODE_LB,     \
			COUNTRY_CODE_LR, COUNTRY_CODE_MN, COUNTRY_CODE_AN,     \
			COUNTRY_CODE_NI, COUNTRY_CODE_PW, COUNTRY_CODE_WS,     \
			COUNTRY_CODE_LK, COUNTRY_CODE_TT, COUNTRY_CODE_MM      \
	}

#define REG_COUNTRY_GROUP14                                                    \
	{                                                                      \
		COUNTRY_CODE_AW, COUNTRY_CODE_LA, COUNTRY_CODE_AE,             \
			COUNTRY_CODE_UG                                        \
	}

#define REG_COUNTRY_GROUP15                                                    \
	{                                                                      \
		COUNTRY_CODE_AR, COUNTRY_CODE_BR, COUNTRY_CODE_HK,             \
			COUNTRY_CODE_OM, COUNTRY_CODE_PH, COUNTRY_CODE_SA,     \
			COUNTRY_CODE_VN,                                       \
			COUNTRY_CODE_KR, COUNTRY_CODE_DO, COUNTRY_CODE_FK,     \
			COUNTRY_CODE_KZ, COUNTRY_CODE_MZ, COUNTRY_CODE_NA,     \
			COUNTRY_CODE_LC, COUNTRY_CODE_VC, COUNTRY_CODE_UA,     \
			COUNTRY_CODE_UZ, COUNTRY_CODE_ZW, COUNTRY_CODE_MP      \
	}

#define REG_COUNTRY_GROUP16                                                    \
	{                                                                      \
		COUNTRY_CODE_AT, COUNTRY_CODE_BE, COUNTRY_CODE_BG,             \
			COUNTRY_CODE_HR, COUNTRY_CODE_CZ, COUNTRY_CODE_DK,     \
			COUNTRY_CODE_FI, COUNTRY_CODE_FR, COUNTRY_CODE_GR,     \
			COUNTRY_CODE_HU, COUNTRY_CODE_IS, COUNTRY_CODE_IE,     \
			COUNTRY_CODE_IT, COUNTRY_CODE_LU, COUNTRY_CODE_NL,     \
			COUNTRY_CODE_NO, COUNTRY_CODE_PL, COUNTRY_CODE_PT,     \
			COUNTRY_CODE_RO, COUNTRY_CODE_SK, COUNTRY_CODE_SI,     \
			COUNTRY_CODE_ES, COUNTRY_CODE_SE,                      \
			COUNTRY_CODE_GB, COUNTRY_CODE_AL, COUNTRY_CODE_AD,     \
			COUNTRY_CODE_BY, COUNTRY_CODE_BA, COUNTRY_CODE_VG,     \
			COUNTRY_CODE_CV, COUNTRY_CODE_CY, COUNTRY_CODE_EE,     \
			COUNTRY_CODE_ET, COUNTRY_CODE_GF, COUNTRY_CODE_PF,     \
			COUNTRY_CODE_TF, COUNTRY_CODE_GE, COUNTRY_CODE_DE,     \
			COUNTRY_CODE_GH, COUNTRY_CODE_GP, COUNTRY_CODE_IQ,     \
			COUNTRY_CODE_KE, COUNTRY_CODE_LV, COUNTRY_CODE_LS,     \
			COUNTRY_CODE_LI, COUNTRY_CODE_LT, COUNTRY_CODE_MK,     \
			COUNTRY_CODE_MT, COUNTRY_CODE_MQ, COUNTRY_CODE_MR,     \
			COUNTRY_CODE_MU, COUNTRY_CODE_YT, COUNTRY_CODE_MD,     \
			COUNTRY_CODE_MC, COUNTRY_CODE_ME, COUNTRY_CODE_MS,     \
			COUNTRY_CODE_RE, COUNTRY_CODE_MF, COUNTRY_CODE_SM,     \
			COUNTRY_CODE_SN, COUNTRY_CODE_RS,      \
			COUNTRY_CODE_TC, COUNTRY_CODE_VA, COUNTRY_CODE_EU,     \
			COUNTRY_CODE_DZ                                        \
	}

#define REG_COUNTRY_GROUP17                                                    \
	{                                                                      \
		COUNTRY_CODE_AU, COUNTRY_CODE_NZ, COUNTRY_CODE_EC,             \
			COUNTRY_CODE_PY, COUNTRY_CODE_PE, COUNTRY_CODE_TH,     \
			COUNTRY_CODE_UY                                        \
	}

#define REG_COUNTRY_GROUP18                                                    \
	{                                                                      \
		COUNTRY_CODE_PK, COUNTRY_CODE_QA, COUNTRY_CODE_BF,             \
			COUNTRY_CODE_GY, COUNTRY_CODE_HT, COUNTRY_CODE_JM,     \
			COUNTRY_CODE_MO, COUNTRY_CODE_MW, COUNTRY_CODE_RW,     \
			COUNTRY_CODE_KN, COUNTRY_CODE_TZ, COUNTRY_CODE_BD      \
	}

#define REG_COUNTRY_GROUP19                                                    \
	{                                                                      \
		COUNTRY_CODE_AO, COUNTRY_CODE_BZ, COUNTRY_CODE_BJ,             \
			COUNTRY_CODE_BT, COUNTRY_CODE_BO, COUNTRY_CODE_BI,     \
			COUNTRY_CODE_CM, COUNTRY_CODE_CF, COUNTRY_CODE_TD,     \
			COUNTRY_CODE_KM, COUNTRY_CODE_CD, COUNTRY_CODE_CG,     \
			COUNTRY_CODE_CI, COUNTRY_CODE_DJ, COUNTRY_CODE_GQ,     \
			COUNTRY_CODE_ER, COUNTRY_CODE_FJ, COUNTRY_CODE_GA,     \
			COUNTRY_CODE_GM, COUNTRY_CODE_GN, COUNTRY_CODE_GW,     \
			COUNTRY_CODE_RKS, COUNTRY_CODE_KG, COUNTRY_CODE_LY,    \
			COUNTRY_CODE_MG, COUNTRY_CODE_ML, COUNTRY_CODE_NR,     \
			COUNTRY_CODE_NC, COUNTRY_CODE_ST, COUNTRY_CODE_SC,     \
			COUNTRY_CODE_SL, COUNTRY_CODE_SB, COUNTRY_CODE_SO,     \
			COUNTRY_CODE_SR, COUNTRY_CODE_SZ, COUNTRY_CODE_TJ,     \
			COUNTRY_CODE_TG, COUNTRY_CODE_TO, COUNTRY_CODE_TM,     \
			COUNTRY_CODE_TV, COUNTRY_CODE_VU, COUNTRY_CODE_YE      \
	}

#define REG_COUNTRY_GROUP20                                                    \
	{                                                                      \
		COUNTRY_CODE_CK, COUNTRY_CODE_CU, COUNTRY_CODE_TL,             \
			COUNTRY_CODE_FO, COUNTRY_CODE_GI, COUNTRY_CODE_GG,     \
			COUNTRY_CODE_IR, COUNTRY_CODE_IM, COUNTRY_CODE_JE,     \
			COUNTRY_CODE_KP, COUNTRY_CODE_MH, COUNTRY_CODE_NU,     \
			COUNTRY_CODE_NF, COUNTRY_CODE_PS, COUNTRY_CODE_PN,     \
			COUNTRY_CODE_PM, COUNTRY_CODE_SS, COUNTRY_CODE_SD,     \
			COUNTRY_CODE_SY                                        \
	}

REG_COUNTRY_GROUP(0, REG_COUNTRY_GROUP0);
REG_COUNTRY_GROUP(1, { COUNTRY_CODE_RU });
REG_COUNTRY_GROUP(2, { COUNTRY_CODE_MY });
REG_COUNTRY_GROUP(3, { COUNTRY_CODE_NP });
REG_COUNTRY_GROUP(4, { COUNTRY_CODE_AF });
REG_COUNTRY_GROUP(5, { COUNTRY_CODE_NG });
REG_COUNTRY_GROUP(6, { COUNTRY_CODE_ID });
REG_COUNTRY_GROUP(7, REG_COUNTRY_GROUP7);
REG_COUNTRY_GROUP(8, REG_COUNTRY_GROUP8);
REG_COUNTRY_GROUP(9, REG_COUNTRY_GROUP9);
REG_COUNTRY_GROUP(10, REG_COUNTRY_GROUP10);
REG_COUNTRY_GROUP(11, REG_COUNTRY_GROUP11);
REG_COUNTRY_GROUP(12, REG_COUNTRY_GROUP12);
REG_COUNTRY_GROUP(13, REG_COUNTRY_GROUP13);
REG_COUNTRY_GROUP(14, REG_COUNTRY_GROUP14);
REG_COUNTRY_GROUP(15, REG_COUNTRY_GROUP15);
REG_COUNTRY_GROUP(16, REG_COUNTRY_GROUP16);
REG_COUNTRY_GROUP(17, REG_COUNTRY_GROUP17);
REG_COUNTRY_GROUP(18, REG_COUNTRY_GROUP18);
REG_COUNTRY_GROUP(19, REG_COUNTRY_GROUP19);
REG_COUNTRY_GROUP(20, REG_COUNTRY_GROUP20);

/**
 * All country codes, number of channels defined by group
*/
struct reg_info_entry reg_support_group[] = {
	{ REG_COUNTRY_GROUP_NUM(0){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 2G4 14_14 */
		//REG_SUB_BAND_INF(82, BAND_2G4, CHN_SPAN_5, 14, 1, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel 5G 100_140 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 11, true)
		/* channel  uii upper NA */
		REG_SUB_BAND_INF(125, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(1){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel 5G 132_140 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 132, 3, true)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(2){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel 5G 100_128 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 8, true)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(3){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* not support NA */
		REG_SUB_BAND_INF(121, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* channel  149_161*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 4, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(4){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* not support NA */
		REG_SUB_BAND_INF(118, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(125, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(121, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(5){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* not support NA */
		REG_SUB_BAND_INF(115, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel 5G 100_140 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 11, true)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(6){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* not support NA */
		REG_SUB_BAND_INF(115, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(118, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(121, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* channel 5G 149_161 */
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 4, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(7){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* not support NA */
		REG_SUB_BAND_INF(118, BAND_NULL, CHN_SPAN_0, 0, 0, true)
		/* not support NA */
		REG_SUB_BAND_INF(121, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(8){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 53_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* not support NA */
		REG_SUB_BAND_INF(121, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(9){
		/* channel 2G4 1_11 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 11, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel 5G 100_144 */
		//REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 12, true)
		/* channel 5G 100_116 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 5, true)
		/* channel 5G 132_140 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 132, 3, true)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(10){
		/* channel 2G4 1_11 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 11, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel 5G 100_116 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 5, true)
		/* channel 5G 132_140 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 132, 3, true)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false) } },
	{ REG_COUNTRY_GROUP_NUM(11){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* not support NA */
		REG_SUB_BAND_INF(121, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(12){
		/* channel 2G4 3_9 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 3, 7, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* not support NA */
		REG_SUB_BAND_INF(121, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(125, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(13){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel 5G 100_144 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 12, true)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(14){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel 5G 100_144 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 12, true)
		/* channel  149_161*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 4, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(15){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel 5G 100_140 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 11, true)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(16){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel 5G 100_140 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 11, true)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(17){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel 5G 100_116 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 5, true)
		/* channel 5G 132_140 */
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 132, 3, true)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)

	} },
	{ REG_COUNTRY_GROUP_NUM(18){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* not support NA */
		REG_SUB_BAND_INF(115, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(118, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(121, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* channel 5G 149_165 */
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(19){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* not support NA */
		REG_SUB_BAND_INF(115, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(118, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(121, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(125, BAND_NULL, CHN_SPAN_0, 0, 0, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_NUM(20){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel  149_144*/
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 12, true)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
	{ REG_COUNTRY_GROUP_END(21){
		/* channel 2G4 1_13 */
		REG_SUB_BAND_INF(81, BAND_2G4, CHN_SPAN_5, 1, 13, false)
		/* channel 5G 36_48 */
		REG_SUB_BAND_INF(115, BAND_5G, CHN_SPAN_20, 36, 4, false)
		/* channel 5G 52_64 */
		REG_SUB_BAND_INF(118, BAND_5G, CHN_SPAN_20, 52, 4, true)
		/* channel  149_144*/
		REG_SUB_BAND_INF(121, BAND_5G, CHN_SPAN_20, 100, 12, true)
		/* channel  149_165*/
		REG_SUB_BAND_INF(125, BAND_5G, CHN_SPAN_20, 149, 5, false)
		/* not support NA */
		REG_SUB_BAND_INF(0, BAND_NULL, CHN_SPAN_0, 0, 0, false) } },
};

/**
 * Customize regulatory rules
*/
WQ_DOMAIN_80211_DEF(
	CN){ .n_reg_rules = 4,
	     .alpha2 = "CN",
	     .dfs_region = NL80211_DFS_FCC,
	     .reg_rules = {
		     /* channels 1..13 */
		     REG_RULE_LIGHT(2412 - 10, 2472 + 10, 40, 0),
		     /* channels 36..48 */
		     REG_RULE_LIGHT(5180 - 10, 5240 + 10, 80, WQ_RRF_AUTO_BW),
		     /* channels 52..64 */
		     REG_RULE_LIGHT(5260 - 10, 5320 + 10, 80,
				    NL80211_RRF_DFS | WQ_RRF_AUTO_BW),
		     /* channels 149..165 */
		     REG_RULE_LIGHT(5745 - 10, 5825 + 10, 80, 0) } };

WQ_DOMAIN_80211_DEF(
	SG){ .n_reg_rules = 4,
	     .alpha2 = "SG",
	     .dfs_region = NL80211_DFS_FCC,
	     .reg_rules = {
		     /* channels 1..13 */
		     REG_RULE_LIGHT(2412 - 10, 2472 + 10, 40, 0),
		     /* channels 36..48 */
		     REG_RULE_LIGHT(5180 - 10, 5240 + 10, 80, WQ_RRF_AUTO_BW),
		     /* channels 52..64 */
		     REG_RULE_LIGHT(5260 - 10, 5320 + 10, 80,
				    NL80211_RRF_DFS | WQ_RRF_AUTO_BW),
		     /* channels 149..165 */
		     REG_RULE_LIGHT(5745 - 10, 5825 + 10, 80, 0) } };

WQ_DOMAIN_80211_DEF(
	US){ .n_reg_rules = 6,
	     .alpha2 = "US",
	     .dfs_region = NL80211_DFS_FCC,
	     .reg_rules = {
		     /* channels 1..11 */
		     REG_RULE_LIGHT(2412 - 10, 2462 + 10, 40, 0),
		     /* channels 36..48 */
		     REG_RULE_LIGHT(5180 - 10, 5240 + 10, 80, WQ_RRF_AUTO_BW),
		     /* channels 52..64 */
		     REG_RULE_LIGHT(5260 - 10, 5320 + 10, 80,
				    NL80211_RRF_DFS | WQ_RRF_AUTO_BW),
		     /* channels 100..140 */
		     //REG_RULE_LIGHT(5500 - 10, 5720 + 10, 160, NL80211_RRF_DFS),
		     /* channels 100..116 */
		     REG_RULE_LIGHT(5500 - 10, 5580 + 10, 80, NL80211_RRF_DFS),
		     /* channels 132..140 */
		     REG_RULE_LIGHT(5660 - 10, 5700 + 10, 40, NL80211_RRF_DFS),
		     /* channels 149..165 */
		     REG_RULE_LIGHT(5745 - 10, 5825 + 10, 80, 0) } };

WQ_DOMAIN_80211_DEF(
	CA){ .n_reg_rules = 6,
	     .alpha2 = "CA",
	     .dfs_region = NL80211_DFS_FCC,
	     .reg_rules = {
		     /* channels 1..11 */
		     REG_RULE_LIGHT(2412 - 10, 2462 + 10, 40, 0),
		     /* channels 36..48 */
		     REG_RULE_LIGHT(5180 - 10, 5240 + 10, 80, WQ_RRF_AUTO_BW),
		     /* channels 52..64 */
		     REG_RULE_LIGHT(5260 - 10, 5320 + 10, 80,
				    NL80211_RRF_DFS | WQ_RRF_AUTO_BW),
		     /* channels 100..116 */
		     REG_RULE_LIGHT(5500 - 10, 5580 + 10, 80, NL80211_RRF_DFS),
		     /* channels 132..140 */
		     REG_RULE_LIGHT(5660 - 10, 5700 + 10, 40, NL80211_RRF_DFS),
		     /* channels 149..165 */
		     REG_RULE_LIGHT(5745 - 10, 5825 + 10, 80, 0) } };

WQ_DOMAIN_80211_DEF(
	JP){ .n_reg_rules = 7,
	     .alpha2 = "JP",
	     .dfs_region = NL80211_DFS_JP,
	     .reg_rules = {
		     /* channels 1..13 */
		     REG_RULE_LIGHT(2412 - 10, 2472 + 10, 40, 0),
		     /* channels 14 */
		     //REG_RULE_LIGHT(2484 - 10, 2484 + 10, 20, NL80211_RRF_NO_OFDM),
		     /* channels 184..196 */
		     //REG_RULE_LIGHT(4920 - 10, 4980 + 10, 40, 0),
		     /* channels 8..16 */
		     //REG_RULE_LIGHT(5040 - 10, 5080 + 10, 40, 0),
		     /* channels 36..48 */
		     REG_RULE_LIGHT(5180 - 10, 5240 + 10, 80, WQ_RRF_AUTO_BW),
		     /* channels 52..64 */
		     REG_RULE_LIGHT(5260 - 10, 5320 + 10, 80,
				    NL80211_RRF_DFS | WQ_RRF_AUTO_BW),
		     /* channels 100..140 */
		     REG_RULE_LIGHT(5500 - 10, 5700 + 10, 160,
				    NL80211_RRF_DFS) } };

WQ_DOMAIN_80211_DEF(
	CH){ .n_reg_rules = 7,
	     .alpha2 = "CH",
	     .dfs_region = NL80211_DFS_JP,
	     .reg_rules = {
		     /* channels 1..13 */
		     REG_RULE_LIGHT(2412 - 10, 2472 + 10, 40, 0),
		     /* channels 36..48 */
		     REG_RULE_LIGHT(5180 - 10, 5240 + 10, 80, WQ_RRF_AUTO_BW),
		     /* channels 52..64 */
		     REG_RULE_LIGHT(5260 - 10, 5320 + 10, 80,
				    NL80211_RRF_DFS | WQ_RRF_AUTO_BW),
		     /* channels 100..140 */
		     REG_RULE_LIGHT(5500 - 10, 5700 + 10, 160,
				    NL80211_RRF_DFS) } };

WQ_DOMAIN_80211_DEF(
	TR){ .n_reg_rules = 7,
	     .alpha2 = "TR",
	     .dfs_region = NL80211_DFS_JP,
	     .reg_rules = {
		     /* channels 1..13 */
		     REG_RULE_LIGHT(2412 - 10, 2472 + 10, 40, 0),
		     /* channels 36..48 */
		     REG_RULE_LIGHT(5180 - 10, 5240 + 10, 80, WQ_RRF_AUTO_BW),
		     /* channels 52..64 */
		     REG_RULE_LIGHT(5260 - 10, 5320 + 10, 80,
				    NL80211_RRF_DFS | WQ_RRF_AUTO_BW),
		     /* channels 100..140 */
		     REG_RULE_LIGHT(5500 - 10, 5700 + 10, 160,
				    NL80211_RRF_DFS) } };

WQ_DOMAIN_80211_DEF(
	ZA){ .n_reg_rules = 7,
	     .alpha2 = "ZA",
	     .dfs_region = NL80211_DFS_JP,
	     .reg_rules = {
		     /* channels 1..13 */
		     REG_RULE_LIGHT(2412 - 10, 2472 + 10, 40, 0),
		     /* channels 36..48 */
		     REG_RULE_LIGHT(5180 - 10, 5240 + 10, 80, WQ_RRF_AUTO_BW),
		     /* channels 52..64 */
		     REG_RULE_LIGHT(5260 - 10, 5320 + 10, 80,
				    NL80211_RRF_DFS | WQ_RRF_AUTO_BW),
		     /* channels 100..140 */
		     REG_RULE_LIGHT(5500 - 10, 5700 + 10, 160,
				    NL80211_RRF_DFS) } };

WQ_DOMAIN_80211_DEF(
	AT){ .n_reg_rules = 4,
	     .alpha2 = "AT",
	     .dfs_region = NL80211_DFS_ETSI,
	     .reg_rules = {
		     /* channels 1..13 */
		     REG_RULE_LIGHT(2412 - 10, 2472 + 10, 40, 0),
		     /* channels 36..48 */
		     REG_RULE_LIGHT(5180 - 10, 5240 + 10, 80, WQ_RRF_AUTO_BW),
		     /* channels 52..64 */
		     REG_RULE_LIGHT(5260 - 10, 5320 + 10, 80,
				    NL80211_RRF_DFS | WQ_RRF_AUTO_BW),
		     /* channels 100..140 */
		     REG_RULE_LIGHT(5500 - 10, 5700 + 10, 160,
				    NL80211_RRF_DFS) } };

WQ_DOMAIN_80211_DEF(
	EU){ .n_reg_rules = 5,
	     .alpha2 = "EU",
	     .dfs_region = NL80211_DFS_ETSI,
	     .reg_rules = {
		     /* channels 1..13 */
		     REG_RULE_LIGHT(2412 - 10, 2472 + 10, 40, 0),
		     /* channels 36..48 */
		     REG_RULE_LIGHT(5180 - 10, 5240 + 10, 80, WQ_RRF_AUTO_BW),
		     /* channels 52..64 */
		     REG_RULE_LIGHT(5260 - 10, 5320 + 10, 80,
				    NL80211_RRF_DFS | WQ_RRF_AUTO_BW),
		     /* channels 100..140 */
		     REG_RULE_LIGHT(5500 - 10, 5700 + 10, 160,
				    NL80211_RRF_DFS),
             /* channels 149..165 */
		     REG_RULE_LIGHT(5745 - 10, 5825 + 10, 80, 0) } };

WQ_DOMAIN_80211_DEF(
	AU){ .n_reg_rules = 6,
	     .alpha2 = "AU",
	     .dfs_region = NL80211_DFS_ETSI,
	     .reg_rules = {
		     /* channels 1..13 */
		     REG_RULE_LIGHT(2412 - 10, 2472 + 10, 40, 0),
		     /* channels 36..48 */
		     REG_RULE_LIGHT(5180 - 10, 5240 + 10, 80, WQ_RRF_AUTO_BW),
		     /* channels 52..64 */
		     REG_RULE_LIGHT(5260 - 10, 5320 + 10, 80,
				    NL80211_RRF_DFS | WQ_RRF_AUTO_BW),
		     /* channels 100..116 */
		     REG_RULE_LIGHT(5500 - 10, 5580 + 10, 80, NL80211_RRF_DFS),
		     /* channels 132..140 */
		     REG_RULE_LIGHT(5660 - 10, 5700 + 10, 40, NL80211_RRF_DFS),
             /* channels 149..165 */
		     REG_RULE_LIGHT(5745 - 10, 5825 + 10, 80, 0) } };

WQ_DOMAIN_80211_DEF(
	IL){ .n_reg_rules = 3,
	     .alpha2 = "IL",
	     .dfs_region = NL80211_DFS_ETSI,
	     .reg_rules = {
		     /* channels 3..9 */
		     REG_RULE_LIGHT(2422 - 10, 2452 + 10, 40, 0),
		     /* channels 36..48 */
		     REG_RULE_LIGHT(5180 - 10, 5240 + 10, 80, WQ_RRF_AUTO_BW),
		     /* channels 52..64 */
		     REG_RULE_LIGHT(5260 - 10, 5320 + 10, 80,
				    NL80211_RRF_DFS | WQ_RRF_AUTO_BW) } };


/**
 * The default Customize regulatory rules
*/
const struct ieee80211_regdomain
	wq_regd_ww = { .n_reg_rules = 4,
		       .alpha2 = "99",
		       .reg_rules = {
			       /* channels 1..13 */
			       REG_RULE_LIGHT(2412 - 10, 2472 + 10, 40, 0),
			       /* channels 14 */
			       REG_RULE_LIGHT(2484 - 10, 2484 + 10, 20, 0),
			       /* channel 36..64 */
			       REG_RULE_LIGHT(5150 - 10, 5350 + 10, 80, 0),
			       /* channel 100..165 */
			       REG_RULE_LIGHT(5470 - 10, 5850 + 10, 80, 0),
		       } };

WQ_DOMAIN_INIT(US);
WQ_DOMAIN_INIT(CA);
WQ_DOMAIN_INIT(CN);
WQ_DOMAIN_INIT(SG);
WQ_DOMAIN_INIT(JP);
WQ_DOMAIN_INIT(CH);
WQ_DOMAIN_INIT(TR);
WQ_DOMAIN_INIT(ZA);
WQ_DOMAIN_INIT(EU);
WQ_DOMAIN_INIT(AU);
WQ_DOMAIN_INIT(IL);
WQ_DOMAIN_INIT(AT);

const struct wq_reg_domain *wq_regrule_table[] = {
	WQ_DOMAIN_INIT_RULE(CN),
	WQ_DOMAIN_INIT_RULE(SG),
	WQ_DOMAIN_INIT_RULE(US),
	WQ_DOMAIN_INIT_RULE(CA),
	WQ_DOMAIN_INIT_RULE(JP),
	WQ_DOMAIN_INIT_RULE(CH),
	WQ_DOMAIN_INIT_RULE(TR),
	WQ_DOMAIN_INIT_RULE(ZA),
	WQ_DOMAIN_INIT_RULE(EU),
	WQ_DOMAIN_INIT_RULE(AU),
	WQ_DOMAIN_INIT_RULE(IL),
	WQ_DOMAIN_INIT_RULE(AT),
	NULL,
};

struct wq_regd_control wq_regd_ctrl;
struct wiphy *wq_wiphy = NULL;

static char *country_code = NULL;
module_param(country_code, charp, 0);
MODULE_PARM_DESC(country_code, "Country code: CN/US...");

static char wq_country_bin_file[64] = {0};

#define WQ_GET_REGD_CC() wq_regd_ctrl.countycode
#define WQ_SET_REGD_CC(cc) wq_regd_ctrl.countycode = cc
#define WQ_GET_TMP_REGD_CC() wq_regd_ctrl.tmp_countycode
#define WQ_SET_TMP_REGD_CC(cc) wq_regd_ctrl.tmp_countycode = cc

/**
 * Drive regulation operation
*/
const struct ieee80211_regdomain *wq_get_driver_regd(char *countrycode)
{
	u_int8_t i = 0;
	const struct wq_reg_domain *regd;
	while (wq_regrule_table[i]) {
		regd = wq_regrule_table[i];
		if ((regd->countrycode[0] == countrycode[0]) &&
		    (regd->countrycode[1] == countrycode[1]))
			return regd->regrules;
		i++;
	}
	WQ_DBG(DM_GENERIC, DL_WRN, "%s set defualt regd\n", __func__);
	return &wq_regd_ww; /*default world wide*/
}

/**
 * Update regd rule
*/
void wq_apply_custom_regd(struct wiphy *wiphy,
			  const struct ieee80211_regdomain *regd)
{
	u32 idx, ch;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;

	/*reset cha->flags*/
	for (idx = 0; idx < NUM_NL80211_BANDS; idx++) {
		sband = wiphy->bands[idx];

		if (!sband)
			continue;

		for (ch = 0; ch < sband->n_channels; ch++) {
			chan = &sband->channels[ch];
			/*reset chan->flags*/
			chan->flags = 0;
		}
	}

	/* update to kernel */
	wiphy_apply_custom_regulatory(wiphy, regd);
}

void wq_set_country_code(char *alpha2, u8 alpha2_size)
{
	uint16_t countrycode = 0;

	if (alpha2_size >= 2) {
		countrycode = (alpha2[0] | (alpha2[1] << 8));
		if (WQ_GET_REGD_CC() != countrycode)
			WQ_SET_REGD_CC(countrycode);
	}
}

uint16_t wq_get_country_code(void)
{
	return WQ_GET_REGD_CC();
}

/*
*Update the support channel information based on the country code
*/
void wq_reg_get_channel_list(struct reg_info_entry *rinfo, bool dfs,
			     enum REG_CHN_BAND band,
			     struct phy_channel_info *chnlist, u8 *chnmum)
{
	uint8_t chn_num = 0, chn, i, k;
	struct reg_suband_info *sub_band;
	for (i = 0; i < MAX_REG_UBBAND_NUM; i++) {
		sub_band = &rinfo->reg_sub_band[i];
		/*Unsupported channels can be filtered*/
		if (sub_band->band == BAND_NULL || sub_band->band > BAND_NUM)
			continue;

		/* repoert to upper layer only non-DFS channel for ap mode usage*/
		if ((dfs == true) && (sub_band->dfs == true))
			continue;

		if (band == BAND_NULL || sub_band->band == band) {
			for (k = 0; k < sub_band->numchn; k++) {
				chn = sub_band->fchnum + k * sub_band->chspan;
				/*Check whether the channel is valid*/
				chnlist[chn_num].band = sub_band->band;
				chnlist[chn_num].chnnum = chn;
				chnlist[chn_num].dfs = sub_band->dfs;
				if (chn_num++ >= WQ_5G_BAND_MAX_CHN_NUM)
					break;
			}
		}
	}
	*chnmum = chn_num;
}

void wq_reg_get_channel_list_by_band(struct reg_info_entry *rinfo,
				     enum REG_CHN_BAND band, struct rwnx_hw *hw)
{
	uint8_t num_chn, i, k;
	struct wiphy *wiphy = hw->wiphy;
	struct phy_channel_info channel_list[WQ_PER_BAND_MAX_CHN_NUM] = {};
	struct ieee80211_supported_band *band_24ghz =
		wiphy->bands[NL80211_BAND_2GHZ];
	struct ieee80211_supported_band *band_5ghz =
		wiphy->bands[NL80211_BAND_5GHZ];

	/*get rinfo channel list*/
	wq_reg_get_channel_list(rinfo, false, band, channel_list, &num_chn);
	WQ_DBG(DM_GENERIC, DL_WRN, "%s %d\n", __func__, num_chn);
	/* Enable specific channel based on reg channel list */
	for (i = 0; i < num_chn; i++) {
		switch (channel_list[i].band) {
		case BAND_2G4:
			if (!band_24ghz)
				break;
			for (k = 0; band_24ghz && (k < band_24ghz->n_channels);
			     k++) {
				if (band_24ghz->channels[k].hw_value ==
				    CH_TO_FREQ_24G(channel_list[i].chnnum)) {
					band_24ghz->channels[k].flags &=
						~IEEE80211_CHAN_DISABLED;
					band_24ghz->channels[k].orig_flags &=
						~IEEE80211_CHAN_DISABLED;
					WQ_DBG(DM_GENERIC, DL_WRN, "2.4G chan:%d\n",
					       channel_list[i].chnnum);
				}
			}
			break;
		case BAND_5G:
			if (!band_5ghz)
				break;
			for (k = 0; band_5ghz && (k < band_5ghz->n_channels);
			     k++) {
				if (band_5ghz->channels[k].hw_value ==
				    CH_TO_FREQ_5G(channel_list[i].chnnum)) {
					band_5ghz->channels[k].flags &=
						~IEEE80211_CHAN_DISABLED;
					band_5ghz->channels[k].orig_flags &=
						~IEEE80211_CHAN_DISABLED;
					band_5ghz->channels[k].dfs_state =
						(channel_list[k].dfs !=
						 NL80211_DFS_USABLE) ?
							NL80211_DFS_USABLE :
							NL80211_DFS_UNAVAILABLE;
					if (band_5ghz->channels[k].dfs_state ==
					    NL80211_DFS_USABLE) {
						band_5ghz->channels[k].flags |=
							IEEE80211_CHAN_RADAR;
					} else {
						band_5ghz->channels[k].flags &=
							~IEEE80211_CHAN_RADAR;
					}
					WQ_DBG(DM_GENERIC, DL_WRN, "5G chan:%d\n",
					       channel_list[i].chnnum);
				}
			}
			break;
		default:
			WQ_DBG(DM_GENERIC, DL_WRN, "%s-%d unkonw band\n",
			       __func__, band);
			break;
		}
	}
}

void wq_reg_update_channel_info(struct reg_info_entry *rinfo,
				struct rwnx_hw *hw)
{
	int i = 0;
	uint8_t band_id;
	struct wiphy *wiphy = hw->wiphy;
	struct ieee80211_supported_band *band_24ghz =
		wiphy->bands[NL80211_BAND_2GHZ];
	struct ieee80211_supported_band *band_5ghz =
		wiphy->bands[NL80211_BAND_5GHZ];

	WQ_DBG(DM_GENERIC, DL_WRN, "%s-%d 2.4G channels %d 5G channels %d\n",
	       __func__, __LINE__, band_24ghz ? band_24ghz->n_channels : 0,
	       band_5ghz ? band_5ghz->n_channels : 0);

	/*disable all channels*/
	for (i = 0; band_24ghz && (i < band_24ghz->n_channels); i++) {
		band_24ghz->channels[i].flags |= IEEE80211_CHAN_DISABLED;
		band_24ghz->channels[i].orig_flags |= IEEE80211_CHAN_DISABLED;
	}
	for (i = 0; band_5ghz && (i < band_5ghz->n_channels); i++) {
		band_5ghz->channels[i].flags |= IEEE80211_CHAN_DISABLED;
		band_5ghz->channels[i].orig_flags |= IEEE80211_CHAN_DISABLED;
	}

	for (band_id = BAND_2G4; band_id < BAND_NUM; band_id++)
		wq_reg_get_channel_list_by_band(rinfo, band_id, hw);
}

/*
*Get channel information by country code
*/
struct reg_info_entry *wq_get_reg_info(uint16_t country_code)
{
	struct reg_info_entry *groupinfo;
	int i = 0, j = 0;

	for (i = 0; i < REG_GROUP_MAX_IDX; i++) {
		groupinfo = &reg_support_group[i];
		if (groupinfo->country_num && groupinfo->country_group) {
			for (j = 0; j < groupinfo->country_num; j++) {
				/*Find the target country code*/
				if (groupinfo->country_group[j] ==
				    country_code) {
					WQ_DBG(DM_GENERIC, DL_WRN,
					       "%s "
					       "i = [%d] j = [%d]\n",
					       __func__, i, j);
					break;
				}
			}
			/*Find the target country code*/
			if (j < groupinfo->country_num)
				break;
		}
	}
	/*Find information in group*/
	if (i >= REG_GROUP_MAX_IDX) {
		WQ_DBG(DM_GENERIC, DL_WRN, "No mactch %c%c\n",
		       ((country_code & 0xff00) >> 8), (country_code & 0xff));
		groupinfo = &reg_support_group[REG_GROUP_MAX_IDX - 1];
	}
	WQ_DBG(DM_GENERIC, DL_WRN, "%s-%d %d\n", __func__, __LINE__, i);
	return groupinfo;
}

void wq_reg_notify(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct rwnx_hw *hw = wiphy_priv(wiphy);
	const struct ieee80211_regdomain *regd;
	uint16_t countrycode;
	char alpha2[4];

	WQ_DBG(DM_GENERIC, DL_WRN, "%s-%d\n", __func__, __LINE__);

	if (!wiphy || !wq_wiphy || !request) {
		WQ_DBG(DM_GENERIC, DL_WRN, "wiphy or request is  NULL!\n");
	}

	if (wiphy != wq_wiphy)
		wiphy = wq_wiphy;

	if (!hw->mod_params.driver_reg_enable) {
		rwnx_radar_set_domain(&hw->radar, request->dfs_region);
		rwnx_send_me_chan_config_req(hw);
		return;
	}

	/*
        * Ignore the CORE's WW setting when using local data base of regulatory
        * rules
        */
	if (request->initiator == NL80211_REGDOM_SET_BY_CORE) {
/*Ignore the CORE's WW setting*/
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
		if ((wiphy->flags & WIPHY_FLAG_CUSTOM_REGULATORY))
			return;
#else
		if ((wiphy->regulatory_flags & REGULATORY_CUSTOM_REG))
			return;
#endif
	}

	if (request->initiator != NL80211_REGDOM_SET_BY_DRIVER) {
		/*set country code*/
		wq_set_country_code(request->alpha2, sizeof(request->alpha2));
	} else {
		/*efuse contry code or ini */
		countrycode = WQ_GET_TMP_REGD_CC();
		if (!hw->mod_params.drvcc[0] || !wq_get_country_code()) {
			if (wq_get_country_code() == countrycode) {
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "%s country is same\n", __func__);
				return;
			}
			wq_set_country_code((char *)&countrycode,
					    sizeof(countrycode));
		} else {
			return;
		}
	}

	countrycode = wq_get_country_code();
	alpha2[0] = (countrycode >> 8) & 0xFF;
	alpha2[1] = countrycode & 0xFF;
	regd = wq_get_driver_regd(alpha2);
	if (!regd || !countrycode) {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s get regd failed\n", __func__);
		return;
	}


	/**update regd*/
	wq_apply_custom_regd(wiphy, regd);
	/**update channels*/
	wq_reg_update_channel_info(wq_get_reg_info(countrycode), hw);

	/**update to firmware*/
	rwnx_radar_set_domain(&hw->radar, request->dfs_region);
	rwnx_send_me_chan_config_req(hw);
}

static u8 wq_alpha2_to_domain(char *alpha2)
{
	uint16_t code = COUNTRY_CODE_STR_2_CODE(alpha2);

	switch (code) {
		case COUNTRY_CODE_CN:
		case COUNTRY_CODE_SG:
			return REG_DM_SRRC;

		case COUNTRY_CODE_US:
		case COUNTRY_CODE_CA:
			return REG_DM_FCC;

		case COUNTRY_CODE_EU:
		case COUNTRY_CODE_CH:
		case COUNTRY_CODE_TR:
		case COUNTRY_CODE_ZA:
		case COUNTRY_CODE_JP:
			return REG_DM_ETSI;

		case COUNTRY_CODE_AU:
		case COUNTRY_CODE_IL:
		default:
			WQ_DBG(DM_GENERIC, DL_WRN, "Unsupported alpha2: %s.\n", alpha2);
	}

	return REG_DM_DFLT;
}

u8 wq_get_country_code_and_bin_path(struct rwnx_hw *rwnx_hw, char *country)
{
	u8 domain = REG_DM_MAX;

read_bin_again:

	if (REG_DM_MAX == domain && reg_data_file && wq_country_bin_file != reg_data_file) {
		WQ_DBG(DM_GENERIC, DL_WRN, "User specified: %s.\n", reg_data_file);
	} else {
		if (REG_DM_MAX == domain) {
			domain = wq_alpha2_to_domain(country);
			WQ_DBG(DM_GENERIC, DL_WRN, "alpha2: %s -> %02d.\n", country, domain);
		}
		sprintf(wq_country_bin_file, "/vendor/lib/modules/RegData_%02d_%04X.bin", domain, rwnx_hw->core->dev_mod_id);
		reg_data_file = wq_country_bin_file;
	}

	if (get_pwr_data_from_bin_file()) {
		gv_get_pwr_from_bin_flag = true;
		WQ_DBG(DM_GENERIC, DL_WRN, "Load %s success.\n", reg_data_file);
	} else if (REG_DM_DFLT != domain) {
		domain = REG_DM_DFLT;
		WQ_DBG(DM_GENERIC, DL_WRN, "Load %s failed, try domain %02d.\n", reg_data_file, domain);
		goto read_bin_again;
	} else {
		WQ_DBG(DM_GENERIC, DL_WRN, "Load %s failed.\n", reg_data_file);
	}

	/* Get from user specific file successfully, not country code. */
	if (REG_DM_MAX == domain) {
		/* TODO: Fix the domain */
		domain = REG_DM_DFLT;
	}

	return domain;
}

int wq_regd_set_country(struct rwnx_hw *rwnx_hw, char *country)
{
	int ret = 0;
	uint16_t country_code;
	u8 code = 0;
	if (rwnx_hw->mod_params.driver_reg_enable) {
		struct regulatory_request request;
		memset(&request, 0x0, sizeof(request));
		country_code =
			(((uint16_t)country[0]) << 8) | ((uint16_t)country[1]);
		WQ_SET_TMP_REGD_CC(country_code);
		request.initiator = NL80211_REGDOM_SET_BY_DRIVER;
		wq_reg_notify(rwnx_hw->wiphy, &request);
	} else {
		ret = regulatory_hint(rwnx_hw->wiphy, country);
	}
	code = wq_get_country_code_and_bin_path(rwnx_hw, country);
	rwnx_send_reg_dm_code_req(rwnx_hw, code);
	return 0;
}

void wq_set_default_country_code(struct rwnx_hw *rwnx_hw)
{
	char f_code[4] = { 0 };
	uint16_t country = 0;

	if (country_code) {
		// from insmod.
		strncpy(f_code, country_code, 2);
	} else if (rwnx_hw->mod_params.drvcc[0]) {
		//from insert module argument
		strncpy(f_code, rwnx_hw->mod_params.drvcc, 4);
	} else if (rwnx_hw->version_cfm.country_code[0] != '\0') {
		//from FW
		strncpy(f_code, rwnx_hw->version_cfm.country_code, 2);
	} else {
		//default value
		strncpy(f_code, "CN", 2);
	}
	country = (((uint16_t)f_code[0]) << 8) | ((uint16_t)f_code[1]);
	rwnx_hw->priv_ioctl.country_code = country;
	wq_regd_set_country(rwnx_hw, f_code);
	WQ_DBG(DM_GENERIC, DL_WRN, "[Country Code] %s \n", f_code);
}

void wq_set_regd_wiphy(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
	/*register callback*/
	wiphy->reg_notifier = wq_reg_notify;
	if (!rwnx_hw->mod_params.driver_reg_enable) {
		rwnx_custregd(rwnx_hw, wiphy);
		wq_set_default_country_code(rwnx_hw);
		return;
	}

	/*clear REGULATORY_CUSTOM_REG flag and  set REGULATORY_CUSTOM_REG flag*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	wiphy->regulatory_flags &= ~(REGULATORY_CUSTOM_REG);
	/*ignore the hint from IE*/
	wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE;
	wiphy->regulatory_flags |= (REGULATORY_CUSTOM_REG);
#else
	wiphy->flags &= ~(WIPHY_FLAG_CUSTOM_REGULATORY);
	wiphy->flags |= (WIPHY_FLAG_CUSTOM_REGULATORY);
#endif

	wq_wiphy = wiphy;

	/**Initialize reg ctrl */
	memset(&wq_regd_ctrl, 0x0, sizeof(struct wq_regd_control));

	/*assigbed a default one*/
	wiphy_apply_custom_regulatory(wiphy, &wq_regd_ww);
	/*set default country code*/
	wq_set_default_country_code(rwnx_hw);
	WQ_DBG(DM_GENERIC, DL_WRN, "%s-%d\n", __func__, __LINE__);
}
#endif
