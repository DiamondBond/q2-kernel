#ifndef _COUNTRY_H_
#define _COUNTRY_H_

#include <linux/nl80211.h>
#include "rwnx_defs.h"
#include "rwnx_reg_data.h"

#define MAX_REG_UBBAND_NUM 7
#define COUNTRY_CODE_NULL ((uint16_t)0x0)

#define WQ_PER_BAND_MAX_CHN_NUM 25
#define WQ_2G_BAND_MAX_CHN_NUM 14
#define WQ_5G_BAND_MAX_CHN_NUM 25
#define WQ_MAX_CHN_NUM (WQ_2G_BAND_MAX_CHN_NUM + WQ_5G_BAND_MAX_CHN_NUM)

/* ISO/IEC 3166-1 two-character country codes */
/* Andorra */
#define COUNTRY_CODE_AD (((uint16_t)'A' << 8) | (uint16_t)'D')
/* UAE */
#define COUNTRY_CODE_AE (((uint16_t)'A' << 8) | (uint16_t)'E')
/* Afghanistan */
#define COUNTRY_CODE_AF (((uint16_t)'A' << 8) | (uint16_t)'F')
/* Antigua & Barbuda */
#define COUNTRY_CODE_AG (((uint16_t)'A' << 8) | (uint16_t)'G')
/* Anguilla */
#define COUNTRY_CODE_AI (((uint16_t)'A' << 8) | (uint16_t)'I')
/* Albania */
#define COUNTRY_CODE_AL (((uint16_t)'A' << 8) | (uint16_t)'L')
/* Armenia */
#define COUNTRY_CODE_AM (((uint16_t)'A' << 8) | (uint16_t)'M')
/* Netherlands Antilles */
#define COUNTRY_CODE_AN (((uint16_t)'A' << 8) | (uint16_t)'N')
/* Angola */
#define COUNTRY_CODE_AO (((uint16_t)'A' << 8) | (uint16_t)'O')
/* Argentina */
#define COUNTRY_CODE_AR (((uint16_t)'A' << 8) | (uint16_t)'R')
/* American Samoa (USA) */
#define COUNTRY_CODE_AS (((uint16_t)'A' << 8) | (uint16_t)'S')
/* Austria */
#define COUNTRY_CODE_AT (((uint16_t)'A' << 8) | (uint16_t)'T')
/* Australia */
#define COUNTRY_CODE_AU (((uint16_t)'A' << 8) | (uint16_t)'U')
/* Aruba */
#define COUNTRY_CODE_AW (((uint16_t)'A' << 8) | (uint16_t)'W')
/* Azerbaijan */
#define COUNTRY_CODE_AZ (((uint16_t)'A' << 8) | (uint16_t)'Z')
/* Bosnia and Herzegovina */
#define COUNTRY_CODE_BA (((uint16_t)'B' << 8) | (uint16_t)'A')
/* Barbados */
#define COUNTRY_CODE_BB (((uint16_t)'B' << 8) | (uint16_t)'B')
/* Bangladesh */
#define COUNTRY_CODE_BD (((uint16_t)'B' << 8) | (uint16_t)'D')
/* Belgium  */
#define COUNTRY_CODE_BE (((uint16_t)'B' << 8) | (uint16_t)'E')
/* Burkina Faso */
#define COUNTRY_CODE_BF (((uint16_t)'B' << 8) | (uint16_t)'F')
/* Bulgaria */
#define COUNTRY_CODE_BG (((uint16_t)'B' << 8) | (uint16_t)'G')
/* Bahrain */
#define COUNTRY_CODE_BH (((uint16_t)'B' << 8) | (uint16_t)'H')
/* Burundi */
#define COUNTRY_CODE_BI (((uint16_t)'B' << 8) | (uint16_t)'I')
/* Benin */
#define COUNTRY_CODE_BJ (((uint16_t)'B' << 8) | (uint16_t)'J')
/* Bermuda */
#define COUNTRY_CODE_BM (((uint16_t)'B' << 8) | (uint16_t)'M')
/* Brunei */
#define COUNTRY_CODE_BN (((uint16_t)'B' << 8) | (uint16_t)'N')
/* Bolivia */
#define COUNTRY_CODE_BO (((uint16_t)'B' << 8) | (uint16_t)'O')
/* Brazil */
#define COUNTRY_CODE_BR (((uint16_t)'B' << 8) | (uint16_t)'R')
/* Bahamas  */
#define COUNTRY_CODE_BS (((uint16_t)'B' << 8) | (uint16_t)'S')
/* Bhutan */
#define COUNTRY_CODE_BT (((uint16_t)'B' << 8) | (uint16_t)'T')
/* Botswana */
#define COUNTRY_CODE_BW (((uint16_t)'B' << 8) | (uint16_t)'W')
/* Belarus */
#define COUNTRY_CODE_BY (((uint16_t)'B' << 8) | (uint16_t)'Y')
/* Belize */
#define COUNTRY_CODE_BZ (((uint16_t)'B' << 8) | (uint16_t)'Z')
/* Canada */
#define COUNTRY_CODE_CA (((uint16_t)'C' << 8) | (uint16_t)'A')
/* Democratic Republic of the Congo */
#define COUNTRY_CODE_CD (((uint16_t)'C' << 8) | (uint16_t)'D')
/* Central African Republic */
#define COUNTRY_CODE_CF (((uint16_t)'C' << 8) | (uint16_t)'F')
/* Republic of the Congo */
#define COUNTRY_CODE_CG (((uint16_t)'C' << 8) | (uint16_t)'G')
/* Switzerland */
#define COUNTRY_CODE_CH (((uint16_t)'C' << 8) | (uint16_t)'H')
/* Cote d'lvoire */
#define COUNTRY_CODE_CI (((uint16_t)'C' << 8) | (uint16_t)'I')
/* Cook Island */
#define COUNTRY_CODE_CK (((uint16_t)'C' << 8) | (uint16_t)'K')
/* Chile */
#define COUNTRY_CODE_CL (((uint16_t)'C' << 8) | (uint16_t)'L')
/* Cameroon */
#define COUNTRY_CODE_CM (((uint16_t)'C' << 8) | (uint16_t)'M')
/* China */
#define COUNTRY_CODE_CN (((uint16_t)'C' << 8) | (uint16_t)'N')
/* Columbia */
#define COUNTRY_CODE_CO (((uint16_t)'C' << 8) | (uint16_t)'O')
/* Costa Rica */
#define COUNTRY_CODE_CR (((uint16_t)'C' << 8) | (uint16_t)'R')
/* Cuba */
#define COUNTRY_CODE_CU (((uint16_t)'C' << 8) | (uint16_t)'U')
/* Cape Verde */
#define COUNTRY_CODE_CV (((uint16_t)'C' << 8) | (uint16_t)'V')
/* "Christmas Island(Australia) */
#define COUNTRY_CODE_CX (((uint16_t)'C' << 8) | (uint16_t)'X')
/* Cyprus */
#define COUNTRY_CODE_CY (((uint16_t)'C' << 8) | (uint16_t)'Y')
/* Czech */
#define COUNTRY_CODE_CZ (((uint16_t)'C' << 8) | (uint16_t)'Z')
/* Germany */
#define COUNTRY_CODE_DE (((uint16_t)'D' << 8) | (uint16_t)'E')
/* Djibouti */
#define COUNTRY_CODE_DJ (((uint16_t)'D' << 8) | (uint16_t)'J')
/* Denmark */
#define COUNTRY_CODE_DK (((uint16_t)'D' << 8) | (uint16_t)'K')
/* Dominica */
#define COUNTRY_CODE_DM (((uint16_t)'D' << 8) | (uint16_t)'M')
/* Dominican Republic */
#define COUNTRY_CODE_DO (((uint16_t)'D' << 8) | (uint16_t)'O')
/* Algeria */
#define COUNTRY_CODE_DZ (((uint16_t)'D' << 8) | (uint16_t)'Z')
/* Ecuador */
#define COUNTRY_CODE_EC (((uint16_t)'E' << 8) | (uint16_t)'C')
/* Estonia */
#define COUNTRY_CODE_EE (((uint16_t)'E' << 8) | (uint16_t)'E')
/* Egypt */
#define COUNTRY_CODE_EG (((uint16_t)'E' << 8) | (uint16_t)'G')
/* Western Sahara (Morocco) */
#define COUNTRY_CODE_EH (((uint16_t)'E' << 8) | (uint16_t)'H')
/* Eritrea */
#define COUNTRY_CODE_ER (((uint16_t)'E' << 8) | (uint16_t)'R')
/* Spain */
#define COUNTRY_CODE_ES (((uint16_t)'E' << 8) | (uint16_t)'S')
/* Ethiopia */
#define COUNTRY_CODE_ET (((uint16_t)'E' << 8) | (uint16_t)'T')
/* Europe */
#define COUNTRY_CODE_EU (((uint16_t)'E' << 8) | (uint16_t)'U')
/* Finland */
#define COUNTRY_CODE_FI (((uint16_t)'F' << 8) | (uint16_t)'I')
/* Fiji */
#define COUNTRY_CODE_FJ (((uint16_t)'F' << 8) | (uint16_t)'J')
/* Falkland Island */
#define COUNTRY_CODE_FK (((uint16_t)'F' << 8) | (uint16_t)'K')
/* Micronesia */
#define COUNTRY_CODE_FM (((uint16_t)'F' << 8) | (uint16_t)'M')
/* Faroe Island */
#define COUNTRY_CODE_FO (((uint16_t)'F' << 8) | (uint16_t)'O')
/* France */
#define COUNTRY_CODE_FR (((uint16_t)'F' << 8) | (uint16_t)'R')
/* Wallis and Futuna (France) */
#define COUNTRY_CODE_FR (((uint16_t)'F' << 8) | (uint16_t)'R')
/* Gabon */
#define COUNTRY_CODE_GA (((uint16_t)'G' << 8) | (uint16_t)'A')
/* United Kingdom */
#define COUNTRY_CODE_GB (((uint16_t)'G' << 8) | (uint16_t)'B')
/* Grenada */
#define COUNTRY_CODE_GD (((uint16_t)'G' << 8) | (uint16_t)'D')
/* Georgia */
#define COUNTRY_CODE_GE (((uint16_t)'G' << 8) | (uint16_t)'E')
/* French Guiana */
#define COUNTRY_CODE_GF (((uint16_t)'G' << 8) | (uint16_t)'F')
/* Guernsey */
#define COUNTRY_CODE_GG (((uint16_t)'G' << 8) | (uint16_t)'G')
/* Ghana */
#define COUNTRY_CODE_GH (((uint16_t)'G' << 8) | (uint16_t)'H')
/* Gibraltar */
#define COUNTRY_CODE_GI (((uint16_t)'G' << 8) | (uint16_t)'I')
/* Gambia */
#define COUNTRY_CODE_GM (((uint16_t)'G' << 8) | (uint16_t)'M')
/* Guinea */
#define COUNTRY_CODE_GN (((uint16_t)'G' << 8) | (uint16_t)'N')
/* Guadeloupe */
#define COUNTRY_CODE_GP (((uint16_t)'G' << 8) | (uint16_t)'P')
/* Equatorial Guinea */
#define COUNTRY_CODE_GQ (((uint16_t)'G' << 8) | (uint16_t)'Q')
/* Greece */
#define COUNTRY_CODE_GR (((uint16_t)'G' << 8) | (uint16_t)'R')
/* Guatemala */
#define COUNTRY_CODE_GT (((uint16_t)'G' << 8) | (uint16_t)'T')
/* Guam */
#define COUNTRY_CODE_GU (((uint16_t)'G' << 8) | (uint16_t)'U')
/* Guinea-Bissau */
#define COUNTRY_CODE_GW (((uint16_t)'G' << 8) | (uint16_t)'W')
/* Guyana */
#define COUNTRY_CODE_GY (((uint16_t)'G' << 8) | (uint16_t)'Y')
/* Hong Kong */
#define COUNTRY_CODE_HK (((uint16_t)'H' << 8) | (uint16_t)'K')
/* Honduras */
#define COUNTRY_CODE_HN (((uint16_t)'H' << 8) | (uint16_t)'N')
/* Croatia */
#define COUNTRY_CODE_HR (((uint16_t)'H' << 8) | (uint16_t)'R')
/* Haiti */
#define COUNTRY_CODE_HT (((uint16_t)'H' << 8) | (uint16_t)'T')
/* Hungary */
#define COUNTRY_CODE_HU (((uint16_t)'H' << 8) | (uint16_t)'U')
/* Indonesia */
#define COUNTRY_CODE_ID (((uint16_t)'I' << 8) | (uint16_t)'D')
/* Ireland */
#define COUNTRY_CODE_IE (((uint16_t)'I' << 8) | (uint16_t)'E')
/* Israel */
#define COUNTRY_CODE_IL (((uint16_t)'I' << 8) | (uint16_t)'L')
/* Isle of Man */
#define COUNTRY_CODE_IM (((uint16_t)'I' << 8) | (uint16_t)'M')
/* India */
#define COUNTRY_CODE_IN (((uint16_t)'I' << 8) | (uint16_t)'N')
/* Iraq */
#define COUNTRY_CODE_IQ (((uint16_t)'I' << 8) | (uint16_t)'Q')
/* Iran */
#define COUNTRY_CODE_IR (((uint16_t)'I' << 8) | (uint16_t)'R')
/* Iceland */
#define COUNTRY_CODE_IS (((uint16_t)'I' << 8) | (uint16_t)'S')
/* Italy */
#define COUNTRY_CODE_IT (((uint16_t)'I' << 8) | (uint16_t)'T')
/* Jersey */
#define COUNTRY_CODE_JE (((uint16_t)'J' << 8) | (uint16_t)'E')
/* Jameica */
#define COUNTRY_CODE_JM (((uint16_t)'J' << 8) | (uint16_t)'M')
/* Jordan */
#define COUNTRY_CODE_JO (((uint16_t)'J' << 8) | (uint16_t)'O')
/* Japan */
#define COUNTRY_CODE_JP (((uint16_t)'J' << 8) | (uint16_t)'P')
/* Kenya */
#define COUNTRY_CODE_KE (((uint16_t)'K' << 8) | (uint16_t)'E')
/* Kyrgyzstan */
#define COUNTRY_CODE_KG (((uint16_t)'K' << 8) | (uint16_t)'G')
/* Cambodia */
#define COUNTRY_CODE_KH (((uint16_t)'K' << 8) | (uint16_t)'H')
/* Kiribati */
#define COUNTRY_CODE_KI (((uint16_t)'K' << 8) | (uint16_t)'I')
/* Comoros */
#define COUNTRY_CODE_KM (((uint16_t)'K' << 8) | (uint16_t)'M')
/* Saint Kitts and Nevis */
#define COUNTRY_CODE_KN (((uint16_t)'K' << 8) | (uint16_t)'N')
/* North Korea */
#define COUNTRY_CODE_KP (((uint16_t)'K' << 8) | (uint16_t)'P')
/* South Korea */
#define COUNTRY_CODE_KR (((uint16_t)'K' << 8) | (uint16_t)'R')
/* Kuwait */
#define COUNTRY_CODE_KW (((uint16_t)'K' << 8) | (uint16_t)'W')
/* Cayman Islands */
#define COUNTRY_CODE_KY (((uint16_t)'K' << 8) | (uint16_t)'Y')
/* Kazakhstan */
#define COUNTRY_CODE_KZ (((uint16_t)'K' << 8) | (uint16_t)'Z')
/* Laos */
#define COUNTRY_CODE_LA (((uint16_t)'L' << 8) | (uint16_t)'A')
/* Lebanon */
#define COUNTRY_CODE_LB (((uint16_t)'L' << 8) | (uint16_t)'B')
/* Saint Lucia */
#define COUNTRY_CODE_LC (((uint16_t)'L' << 8) | (uint16_t)'C')
/* Liechtenstein */
#define COUNTRY_CODE_LI (((uint16_t)'L' << 8) | (uint16_t)'I')
/* Sri Lanka */
#define COUNTRY_CODE_LK (((uint16_t)'L' << 8) | (uint16_t)'K')
/* Liberia */
#define COUNTRY_CODE_LR (((uint16_t)'L' << 8) | (uint16_t)'R')
/* Lesotho */
#define COUNTRY_CODE_LS (((uint16_t)'L' << 8) | (uint16_t)'S')
/* Lithuania */
#define COUNTRY_CODE_LT (((uint16_t)'L' << 8) | (uint16_t)'T')
/* Luxemburg */
#define COUNTRY_CODE_LU (((uint16_t)'L' << 8) | (uint16_t)'U')
/* Latvia */
#define COUNTRY_CODE_LV (((uint16_t)'L' << 8) | (uint16_t)'V')
/* Libya */
#define COUNTRY_CODE_LY (((uint16_t)'L' << 8) | (uint16_t)'Y')
/* Morocco */
#define COUNTRY_CODE_MA (((uint16_t)'M' << 8) | (uint16_t)'A')
/* Monaco */
#define COUNTRY_CODE_MC (((uint16_t)'M' << 8) | (uint16_t)'C')
/* Moldova */
#define COUNTRY_CODE_MD (((uint16_t)'M' << 8) | (uint16_t)'D')
/* Montenegro */
#define COUNTRY_CODE_ME (((uint16_t)'M' << 8) | (uint16_t)'E')
/* Saint Martin / Sint Marteen (Added on window's list) */
#define COUNTRY_CODE_MF (((uint16_t)'M' << 8) | (uint16_t)'F')
/* Madagascar */
#define COUNTRY_CODE_MG (((uint16_t)'M' << 8) | (uint16_t)'G')
/* Marshall Islands */
#define COUNTRY_CODE_MH (((uint16_t)'M' << 8) | (uint16_t)'H')
/* Macedonia */
#define COUNTRY_CODE_MK (((uint16_t)'M' << 8) | (uint16_t)'K')
/* Mali */
#define COUNTRY_CODE_ML (((uint16_t)'M' << 8) | (uint16_t)'L')
/* Myanmar */
#define COUNTRY_CODE_MM (((uint16_t)'M' << 8) | (uint16_t)'M')
/* Mongolia */
#define COUNTRY_CODE_MN (((uint16_t)'M' << 8) | (uint16_t)'N')
/* Macao */
#define COUNTRY_CODE_MO (((uint16_t)'M' << 8) | (uint16_t)'O')
/* Northern Mariana Islands (Rota Island Saipan and Tinian Island) */
#define COUNTRY_CODE_MP (((uint16_t)'M' << 8) | (uint16_t)'P')
/* Martinique (France) */
#define COUNTRY_CODE_MQ (((uint16_t)'M' << 8) | (uint16_t)'Q')
/* Mauritania */
#define COUNTRY_CODE_MR (((uint16_t)'M' << 8) | (uint16_t)'R')
/* Montserrat (UK) */
#define COUNTRY_CODE_MS (((uint16_t)'M' << 8) | (uint16_t)'S')
/* Malta */
#define COUNTRY_CODE_MT (((uint16_t)'M' << 8) | (uint16_t)'T')
/* Mauritius */
#define COUNTRY_CODE_MU (((uint16_t)'M' << 8) | (uint16_t)'U')
/* Maldives */
#define COUNTRY_CODE_MV (((uint16_t)'M' << 8) | (uint16_t)'V')
/* Malawi */
#define COUNTRY_CODE_MW (((uint16_t)'M' << 8) | (uint16_t)'W')
/* Mexico */
#define COUNTRY_CODE_MX (((uint16_t)'M' << 8) | (uint16_t)'X')
/* Malaysia */
#define COUNTRY_CODE_MY (((uint16_t)'M' << 8) | (uint16_t)'Y')
/* Mozambique */
#define COUNTRY_CODE_MZ (((uint16_t)'M' << 8) | (uint16_t)'Z')
/* Namibia */
#define COUNTRY_CODE_NA (((uint16_t)'N' << 8) | (uint16_t)'A')
/* New Caledonia */
#define COUNTRY_CODE_NC (((uint16_t)'N' << 8) | (uint16_t)'C')
/* Niger */
#define COUNTRY_CODE_NE (((uint16_t)'N' << 8) | (uint16_t)'E')
/* Norfolk Island */
#define COUNTRY_CODE_NF (((uint16_t)'N' << 8) | (uint16_t)'F')
/* Nigeria */
#define COUNTRY_CODE_NG (((uint16_t)'N' << 8) | (uint16_t)'G')
/* Nicaragua */
#define COUNTRY_CODE_NI (((uint16_t)'N' << 8) | (uint16_t)'I')
/* Netherlands */
#define COUNTRY_CODE_NL (((uint16_t)'N' << 8) | (uint16_t)'L')
/* Norway */
#define COUNTRY_CODE_NO (((uint16_t)'N' << 8) | (uint16_t)'O')
/* Nepal */
#define COUNTRY_CODE_NP (((uint16_t)'N' << 8) | (uint16_t)'P')
/* Nauru */
#define COUNTRY_CODE_NR (((uint16_t)'N' << 8) | (uint16_t)'R')
/* Niue */
#define COUNTRY_CODE_NU (((uint16_t)'N' << 8) | (uint16_t)'U')
/* New Zealand */
#define COUNTRY_CODE_NZ (((uint16_t)'N' << 8) | (uint16_t)'Z')
/* Oman */
#define COUNTRY_CODE_OM (((uint16_t)'O' << 8) | (uint16_t)'M')
/* Panama */
#define COUNTRY_CODE_PA (((uint16_t)'P' << 8) | (uint16_t)'A')
/* Peru */
#define COUNTRY_CODE_PE (((uint16_t)'P' << 8) | (uint16_t)'E')
/* "French Polynesia */
#define COUNTRY_CODE_PF (((uint16_t)'P' << 8) | (uint16_t)'F')
/* Papua New Guinea */
#define COUNTRY_CODE_PG (((uint16_t)'P' << 8) | (uint16_t)'G')
/* Philippines */
#define COUNTRY_CODE_PH (((uint16_t)'P' << 8) | (uint16_t)'H')
/* Pakistan */
#define COUNTRY_CODE_PK (((uint16_t)'P' << 8) | (uint16_t)'K')
/* Poland */
#define COUNTRY_CODE_PL (((uint16_t)'P' << 8) | (uint16_t)'L')
/* Saint Pierre and Miquelon */
#define COUNTRY_CODE_PM (((uint16_t)'P' << 8) | (uint16_t)'M')
/* Pitcairn Islands  */
#define COUNTRY_CODE_PN (((uint16_t)'P' << 8) | (uint16_t)'N')
/* Puerto Rico (USA) */
#define COUNTRY_CODE_PR (((uint16_t)'P' << 8) | (uint16_t)'R')
/* Palestinian Authority */
#define COUNTRY_CODE_PS (((uint16_t)'P' << 8) | (uint16_t)'S')
/* Portugal */
#define COUNTRY_CODE_PT (((uint16_t)'P' << 8) | (uint16_t)'T')
/* Palau */
#define COUNTRY_CODE_PW (((uint16_t)'P' << 8) | (uint16_t)'W')
/* Paraguay */
#define COUNTRY_CODE_PY (((uint16_t)'P' << 8) | (uint16_t)'Y')
/* Qatar */
#define COUNTRY_CODE_QA (((uint16_t)'Q' << 8) | (uint16_t)'A')
/* Reunion (France) */
#define COUNTRY_CODE_RE (((uint16_t)'R' << 8) | (uint16_t)'E')
/* Kosvo (Added on window's list) */
#define COUNTRY_CODE_RKS (((uint16_t)'R' << 8) | (uint16_t)'K')
/* Romania */
#define COUNTRY_CODE_RO (((uint16_t)'R' << 8) | (uint16_t)'O')
/* Serbia */
#define COUNTRY_CODE_RS (((uint16_t)'R' << 8) | (uint16_t)'S')
/* Russia */
#define COUNTRY_CODE_RU (((uint16_t)'R' << 8) | (uint16_t)'U')
/* Rwanda */
#define COUNTRY_CODE_RW (((uint16_t)'R' << 8) | (uint16_t)'W')
/* Saudi Arabia */
#define COUNTRY_CODE_SA (((uint16_t)'S' << 8) | (uint16_t)'A')
/* Solomon Islands */
#define COUNTRY_CODE_SB (((uint16_t)'S' << 8) | (uint16_t)'B')
/* Seychelles */
#define COUNTRY_CODE_SC (((uint16_t)'S' << 8) | (uint16_t)'C')
/* Sudan */
#define COUNTRY_CODE_SD (((uint16_t)'S' << 8) | (uint16_t)'D')
/* Sweden */
#define COUNTRY_CODE_SE (((uint16_t)'S' << 8) | (uint16_t)'E')
/* Singapole */
#define COUNTRY_CODE_SG (((uint16_t)'S' << 8) | (uint16_t)'G')
/* Slovenia */
#define COUNTRY_CODE_SI (((uint16_t)'S' << 8) | (uint16_t)'I')
/* Slovakia */
#define COUNTRY_CODE_SK (((uint16_t)'S' << 8) | (uint16_t)'K')
/* Sierra Leone */
#define COUNTRY_CODE_SL (((uint16_t)'S' << 8) | (uint16_t)'L')
/* San Marino */
#define COUNTRY_CODE_SM (((uint16_t)'S' << 8) | (uint16_t)'M')
/* Senegal */
#define COUNTRY_CODE_SN (((uint16_t)'S' << 8) | (uint16_t)'N')
/* Somalia */
#define COUNTRY_CODE_SO (((uint16_t)'S' << 8) | (uint16_t)'O')
/* Suriname */
#define COUNTRY_CODE_SR (((uint16_t)'S' << 8) | (uint16_t)'R')
/* South_Sudan */
#define COUNTRY_CODE_SS (((uint16_t)'S' << 8) | (uint16_t)'S')
/* Sao Tome and Principe */
#define COUNTRY_CODE_ST (((uint16_t)'S' << 8) | (uint16_t)'T')
/* El Salvador */
#define COUNTRY_CODE_SV (((uint16_t)'S' << 8) | (uint16_t)'V')
/* Syria */
#define COUNTRY_CODE_SY (((uint16_t)'S' << 8) | (uint16_t)'Y')
/* Swaziland */
#define COUNTRY_CODE_SZ (((uint16_t)'S' << 8) | (uint16_t)'Z')
/* Turks and Caicos Islands (UK) */
#define COUNTRY_CODE_TC (((uint16_t)'T' << 8) | (uint16_t)'C')
/* Chad */
#define COUNTRY_CODE_TD (((uint16_t)'T' << 8) | (uint16_t)'D')
/* French Southern and Antarctic Lands */
#define COUNTRY_CODE_TF (((uint16_t)'T' << 8) | (uint16_t)'F')
/* Togo */
#define COUNTRY_CODE_TG (((uint16_t)'T' << 8) | (uint16_t)'G')
/* Thailand */
#define COUNTRY_CODE_TH (((uint16_t)'T' << 8) | (uint16_t)'H')
/* Tajikistan */
#define COUNTRY_CODE_TJ (((uint16_t)'T' << 8) | (uint16_t)'J')
/* East Timor */
#define COUNTRY_CODE_TL (((uint16_t)'T' << 8) | (uint16_t)'L')
/* Turkmenistan */
#define COUNTRY_CODE_TM (((uint16_t)'T' << 8) | (uint16_t)'M')
/* Tunisia */
#define COUNTRY_CODE_TN (((uint16_t)'T' << 8) | (uint16_t)'N')
/* Tonga */
#define COUNTRY_CODE_TO (((uint16_t)'T' << 8) | (uint16_t)'O')
/* Turkey */
#define COUNTRY_CODE_TR (((uint16_t)'T' << 8) | (uint16_t)'R')
/* Trinidad and Tobago */
#define COUNTRY_CODE_TT (((uint16_t)'T' << 8) | (uint16_t)'T')
/* Tuvalu */
#define COUNTRY_CODE_TV (((uint16_t)'T' << 8) | (uint16_t)'V')
/* Taiwan */
#define COUNTRY_CODE_TW (((uint16_t)'T' << 8) | (uint16_t)'W')
/* Tanzania */
#define COUNTRY_CODE_TZ (((uint16_t)'T' << 8) | (uint16_t)'Z')
/* Ukraine */
#define COUNTRY_CODE_UA (((uint16_t)'U' << 8) | (uint16_t)'A')
/* Ugnada */
#define COUNTRY_CODE_UG (((uint16_t)'U' << 8) | (uint16_t)'G')
/* US */
#define COUNTRY_CODE_US (((uint16_t)'U' << 8) | (uint16_t)'S')
/* Uruguay */
#define COUNTRY_CODE_UY (((uint16_t)'U' << 8) | (uint16_t)'Y')
/* Uzbekistan */
#define COUNTRY_CODE_UZ (((uint16_t)'U' << 8) | (uint16_t)'Z')
/* Vatican (Holy See) */
#define COUNTRY_CODE_VA (((uint16_t)'V' << 8) | (uint16_t)'A')
/* Saint Vincent and the Grenadines */
#define COUNTRY_CODE_VC (((uint16_t)'V' << 8) | (uint16_t)'C')
/* Venezuela */
#define COUNTRY_CODE_VE (((uint16_t)'V' << 8) | (uint16_t)'E')
/* British Virgin Islands */
#define COUNTRY_CODE_VG (((uint16_t)'V' << 8) | (uint16_t)'G')
/* US Virgin Islands */
#define COUNTRY_CODE_VI (((uint16_t)'V' << 8) | (uint16_t)'I')
/* Vietnam */
#define COUNTRY_CODE_VN (((uint16_t)'V' << 8) | (uint16_t)'N')
/* Vanuatu */
#define COUNTRY_CODE_VU (((uint16_t)'V' << 8) | (uint16_t)'U')
/* Samoa */
#define COUNTRY_CODE_WS (((uint16_t)'W' << 8) | (uint16_t)'S')
/* Yemen */
#define COUNTRY_CODE_YE (((uint16_t)'Y' << 8) | (uint16_t)'E')
/* Mayotte (France) */
#define COUNTRY_CODE_YT (((uint16_t)'Y' << 8) | (uint16_t)'T')
/* South Africa */
#define COUNTRY_CODE_ZA (((uint16_t)'Z' << 8) | (uint16_t)'A')
/* Zambia */
#define COUNTRY_CODE_ZM (((uint16_t)'Z' << 8) | (uint16_t)'M')
/* Zimbabwe */
#define COUNTRY_CODE_ZW (((uint16_t)'Z' << 8) | (uint16_t)'W')
/* Default country domain */
#define COUNTRY_CODE_DF (((uint16_t)'D' << 8) | (uint16_t)'F')
/* World Wide */
#define COUNTRY_CODE_WW (((uint16_t)'0' << 8) | (uint16_t)'0')

#define COUNTRY_CODE_STR_2_CODE(str) (((uint16_t)str[0] << 8) | (uint16_t)str[1])
#define COUNTRY_CODE_CODE_2_STR(str, code) do {str[0] = (code >> 8) & 0xFF; str[1] = code & 0xFF;} while(0)

/* This starting freq of the band is unit of kHz */
enum REG_CHN_BAND { BAND_NULL, BAND_2G4, BAND_5G, BAND_NUM };

/* Define channel offset in unit of 5MHz bandwidth */
enum REG_CHN_SPAN {
	CHN_SPAN_0 = 0,
	CHN_SPAN_5 = 1,
	CHN_SPAN_10 = 2,
	CHN_SPAN_20 = 4,
	CHN_SPAN_40 = 8,
	CHN_SPAN_80 = 16
};

enum REG_DM{
	/* Sync with FW */
    REG_DM_DFLT = 0,
    REG_DM_SRRC,
    REG_DM_FCC,
    REG_DM_ETSI,

    REG_DM_MAX
};

/* In all bands, the first channel will be SCA and the second channel is SCB,
 * then iteratively. Note the final channel will not be SCA.
 */
struct reg_suband_info {
	/* Note1: regulation class depends on operation bandwidth and RF band.
         *  For example: 2.4GHz, 1~13, 20MHz ==> regulation class = 81
         *      2.4GHz, 1~13, SCA   ==> regulation class = 83
         *      2.4GHz, 1~13, SCB   ==> regulation class = 84
         */
	uint8_t rclass; /* Regulation class for 20MHz */
	uint8_t band; /* Type:  REG_CHN_BAND*/
	uint8_t chspan; /*channel span Type:  REG_CHN_SPAN*/
	uint8_t fchnum; /*first channer number*/
	uint8_t numchn; /*Number of channels*/
	uint8_t dfs; /* Type: BOOLEAN*/
};

/* Use it as all available channel list for STA */
struct reg_info_entry {
	uint16_t *country_group;
	uint32_t country_num;

	/* If different attributes, put them into different reg_sub_band.
         * For example, DFS shall be used or not.
         */
	struct reg_suband_info reg_sub_band[MAX_REG_UBBAND_NUM];
};

/* Provide supported channel list to other components in array format */
struct phy_channel_info {
	enum REG_CHN_BAND band;
	/* To record Channel Center Frequency Segment 0 (MHz) from CFG80211 */
	uint32_t freq1;
	/* To record Channel Center Frequency Segment 1 (MHz) from CFG80211 */
	uint32_t req2;
	/* To record primary channel frequency (MHz) from CFG80211 */
	uint16_t pchfreq;
	/* To record channel bandwidth from CFG80211 */
	uint8_t chnbw;
	uint8_t chnnum;
	enum nl80211_dfs_state dfs;
};

struct wq_regd_channel {
	uint16_t chnnum;
	uint32_t eFlags; /*enum ieee80211_channel_flags*/
};

struct wq_regd_control {
	u_int8_t en;
	uint16_t countycode;
	uint16_t tmp_countycode; /*store country code"*/
	u8 n_channel_24g; /*Number of valid channels*/
	u8 n_channel_5g; /*Number of valid channels*/
	struct wq_regd_channel channels[WQ_MAX_CHN_NUM];
	enum nl80211_dfs_regions dfs_region;
};

struct wq_reg_domain {
	char *countrycode;
	const struct ieee80211_regdomain *regrules;
};

uint16_t wq_get_country_code(void);
int wq_regd_set_country(struct rwnx_hw *rwnx_hw, char *country);
void wq_set_regd_wiphy(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy);
void wq_set_default_country_code(struct rwnx_hw *rwnx_hw);
u8 wq_get_country_code_and_bin_path(struct rwnx_hw *rwnx_hw, char *country);
int rwnx_send_reg_dm_code_req(struct rwnx_hw *rwnx_hw, u8 country_code);
extern bool gv_get_pwr_from_bin_flag;
extern char *reg_data_file;
#endif