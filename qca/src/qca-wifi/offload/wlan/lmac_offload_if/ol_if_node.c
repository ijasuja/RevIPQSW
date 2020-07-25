/*
 * Copyright (c) 2015, 2017-2020 Qualcomm Innovation Center, Inc.
 * All Rights Reserved
 * Confidential and Proprietary - Qualcomm Innovation Center, Inc.
 *
 * 2015 Qualcomm Atheros, Inc.
 *
 * Copyright (c) 2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * LMAC VAP specific offload interface functions for UMAC - for power and performance offload model
 */
#include "ol_if_athvar.h"
#include "wmi_unified_api.h"
#include "qdf_mem.h"
#if 0
#include "wmi_unified.h"
#endif
#include "a_debug.h"
#include <ieee80211_acfg.h>
#include <cdp_txrx_cmn.h>
#include <cdp_txrx_ctrl.h>
#include <dp_txrx.h>
#include <cdp_txrx_cmn_struct.h>
#include <ol_if_stats.h>
#include <ol_if_stats_api.h>
#if defined(PORT_SPIRENT_HK) && defined(SPT_MULTI_CLIENTS)
#include "ol_if_athpriv.h"
#include "osif_private.h"
#include "wlan_osif_priv.h"
#include <linux/time.h>
#endif
#if MESH_MODE_SUPPORT
#include <if_meta_hdr.h>
#endif

#ifdef WLAN_CONV_CRYPTO_SUPPORTED
#include "wlan_crypto_global_def.h"
#include "wlan_crypto_global_api.h"
#endif
#include "target_type.h"
#include <wlan_lmac_if_api.h>
#include <wlan_utility.h>

#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
#include <osif_nss_wifiol_if.h>
#include <osif_nss_wifiol_vdev_if.h>
#endif
#include "target_if.h"
#include <init_deinit_lmac.h>
#include <wlan_vdev_mgr_ucfg_api.h>
#include <ieee80211_ucfg.h>
#include <target_if_vdev_mgr_rx_ops.h>

#if ATH_PERF_PWR_OFFLOAD

#ifndef HT_RC_2_STREAMS
#define HT_RC_2_STREAMS(_rc)    ((((_rc) & 0x78) >> 3) + 1)
#endif

#define NUM_LEGACY_RATES 12
#define L_BPS_COL         0
#define L_RC_COL          1

#define ALD_MSDU_SIZE 1300
#define DEFAULT_MSDU_SIZE 1000

#define IS_MODE_11AX(is_self_node, _node)                           \
    ((is_self_node) ?                                               \
     (_node->ni_vap->iv_cur_mode >= IEEE80211_MODE_11AXA_HE20) :    \
     (_node->ni_phymode >= IEEE80211_MODE_11AXA_HE20))

static const int legacy_rate_idx[][2] = {
    {1000,        0x1b},
    {2000,        0x1a},
    {5500,        0x19},
    {6000,        0xb},
    {9000,        0xf},
    {11000,       0x18},
    {12000,       0xa},
    {18000,       0xe},
    {24000,       0x9},
    {36000,       0xd},
    {48000,       0x8},
    {54000,       0xc},
};

#define NUM_VHT_HT_RATES 48
#define NUM_HE_RATES 56
#define BW_COL 0
#define MCS_COL 1
#define NBPS_COL 2

/* NDBPS : No of Data Bits per Symbol
 * NSD   : No of Subcarriers per Frequency Segment carrying data
 * NBPSCS: No of Coded Bits per Subcarrier per Spatial Stream
 * R     : Coding Rate
 * NDBPS = NSD*NBPSCS*R
 * Ex    : NDBPS for 20MHz, 256 QAM, MCS 8 is
 * NDBPS(20 MHz, 256Q, MCS8) = 52*8*(3/4) = 312
 *
 * Note: The NDBPS is used to calculate data rate using following
 * eqn   Data Rate = (NDBPS*NSS)/(SymbolDurn+GI). This eqn in
 * ol_ath_node_get_maxphyrate() calculates rate in Kbps and uses
 * the absolute microsecond value of (SymbolDurn+GI). Keeping
 * (NDBPS*1000) value in the following table and using the absoulute
 * microsecond value in the denominator of the calculation gives the
 * desired Kbps rate.
 */
/* MCS 10, 11 NDBPS values in vht_ht_tbl has been calculated based
 * on NBPSCS(1024Q)=10, R(MCS10) = 3/4
 */
static const int vht_ht_tbl[][3] = {
   /*BW        MCS       NDBPS*/
    {0,        0,        26000},
    {0,        1,        52000},
    {0,        2,        78000},
    {0,        3,        104000},
    {0,        4,        156000},
    {0,        5,        208000},
    {0,        6,        234000},
    {0,        7,        260000},
    {0,        8,        312000},
    {0,        9,        346680},
    {0,       10,        390000},
    {0,       11,        433000},
    {1,        0,        54000},
    {1,        1,        108000},
    {1,        2,        162000},
    {1,        3,        216000},
    {1,        4,        324000},
    {1,        5,        432000},
    {1,        6,        486000},
    {1,        7,        540000},
    {1,        8,        648000},
    {1,        9,        720000},
    {1,       10,        810000},
    {1,       11,        900000},
    {2,        0,        117000},
    {2,        1,        234000},
    {2,        2,        351000},
    {2,        3,        468000},
    {2,        4,        702000},
    {2,        5,        936000},
    {2,        6,        1053000},
    {2,        7,        1170000},
    {2,        8,        1404000},
    {2,        9,        1560000},
    {2,       10,        1755000},
    {2,       11,        1950000},
    {3,        0,        234000},
    {3,        1,        468000},
    {3,        2,        702000},
    {3,        3,        936000},
    {3,        4,        1404000},
    {3,        5,        1872000},
    {3,        6,        2106000},
    {3,        7,        2340000},
    {3,        8,        2808000},
    {3,        9,        3120000},
    {3,       10,        3510000},
    {3,       11,        3900000},
};

static const int he_tbl[NUM_HE_RATES][3] = {
   /*BW        MCS       Data bits per symbol*/
    {0,        0,        117000},
    {0,        1,        234000},
    {0,        2,        351000},
    {0,        3,        468000},
    {0,        4,        702000},
    {0,        5,        936000},
    {0,        6,        1053000},
    {0,        7,        1170000},
    {0,        8,        1404000},
    {0,        9,        1560000},
    {0,        10,       1755000},
    {0,        11,       1950000},
    {0,        12,       2106000},
    {0,        13,       2340000},
    {1,        0,        234000},
    {1,        1,        468000},
    {1,        2,        702000},
    {1,        3,        936000},
    {1,        4,        1404000},
    {1,        5,        1872000},
    {1,        6,        2106000},
    {1,        7,        2340000},
    {1,        8,        2808000},
    {1,        9,        3120000},
    {1,        10,       3510000},
    {1,        11,       3900000},
    {1,        12,       4212000},
    {1,        13,       4680000},
    {2,        0,        490000},
    {2,        1,        980000},
    {2,        2,        1470000},
    {2,        3,        1960000},
    {2,        4,        2940000},
    {2,        5,        3920000},
    {2,        6,        4410000},
    {2,        7,        4900000},
    {2,        8,        5880000},
    {2,        9,        6533000},
    {2,        10,       7350000},
    {2,        11,       8166000},
    {2,        12,       8820000},
    {2,        13,       9800000},
    {3,        0,        980000},
    {3,        1,        1960000},
    {3,        2,        2940000},
    {3,        3,        3920000},
    {3,        4,        5880000},
    {3,        5,        7840000},
    {3,        6,        8820000},
    {3,        7,        9800000},
    {3,        8,        11760000},
    {3,        9,        13066000},
    {3,        10,       14700000},
    {3,        11,       16333000},
    {3,        12,       17640000},
    {3,        13,       19600000},
};


#define NG_VHT_MODES 3
/* 11 VHT 1024 QAM Modes: 7 in 5GHz + 4 in 2GHZ
 *
 * 5GHz: 11ACVHT20, 11ACVHT40, 11ACVHT40PLUS,
 * 11ACVHT40MINUS, 11ACVHT80, 11ACVHT160
 * and 11ACVHT80_80.
 * 2GHz: 11NGHT20, 11NGHT40, 11NGHT40PLUS and
 * 11NGHT40MINUS.
 */
#define VHT_1024_QAM_MODES 11

/* 4 GI modes - 0.8us, 0.4us, 1.6us, 3.2us.
 * Till vht - 0.8 & 0.4 is used
 * He - 0.8 , 1.6 us & 3.2us is used
 */
#define RATE_GI_0DOT8_US_IDX  0
#define RATE_GI_0DOT4_US_IDX  1
#define RATE_GI_1DOT6_US_IDX  2
#define RATE_GI_3DOT2_US_IDX  3
#define MAX_GI_MODE           4

#define MAX_NSS               8

/* Max Bit rate calculated from PHY mode, NSS and GI.
 * Max bit rates are represented in Kbps
 * Every Row set represent max bit rate
 *  - NSS (max 8)
 *  - GI options - (max 4)
 * 11ax TODO ( Phase II) - to cover till 8 stream
 */
static u_int32_t max_rates[IEEE80211_MODE_11AXA_HE80_80+
                           NG_VHT_MODES+VHT_1024_QAM_MODES+
                           1][MAX_NSS][MAX_GI_MODE] = {
/* 0- IEEE80211_MODE_AUTO, autoselect */
{
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
},
/* 1- IEEE80211_MODE_11A, 5GHz, OFDM */
{
  {54000,54000,0,0},{54000,54000,0,0},{54000,54000,0,0},{54000,54000,0,0},
  {54000,54000,0,0},{54000,54000,0,0},{54000,54000,0,0},{54000,54000,0,0},
},
 /* 2- IEEE80211_MODE_11B 2GHz, CCK  */
{
  {11000,11000,0,0},{11000,11000,0,0},{11000,11000,0,0},{11000,11000,0,0},
  {11000,11000,0,0},{11000,11000,0,0},{11000,11000,0,0},{11000,11000,0,0},
},
/* 3- IEEE80211_MODE_11G, 2GHz, OFDM */
{
  {54000,54000,0,0},{54000,54000,0,0},{54000,54000,0,0},{54000,54000,0,0},
  {54000,54000,0,0},{54000,54000,0,0},{54000,54000,0,0},{54000,54000,0,0},
},
/* 4- IEEE80211_MODE_FH 2GHz, GFSK */
{
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
},
/* 5- IEEE80211_MODE_TURBO_A 5GHz, OFDM, 2x clock dynamic turbo   */
{
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
},
/* 6-IEEE80211_MODE_TURBO_G 2GHz, OFDM, 2x clock dynamic turbo   */
{
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
},
/* 7- IEEE80211_MODE_11NA_HT20 5Ghz, HT20 */
{
  {65000,72200,0,0},{130000,144400,0,0},{195000,216700,0,0},{260000,288900,0,0},
  {260000,288900,0,0},{260000,288900,0,0},{260000,288900,0,0},{260000,288900,0,0},
},
/* 8- IEEE80211_MODE_11NG_HT20 2Ghz, HT20 */
{
  {65000,72200,0,0},{130000,144400,0,0},{195000,216700,0,0},{260000,288900,0,0},
  {260000,288900,0,0},{260000,288900,0,0},{260000,288900,0,0},{260000,288900,0,0},
},
/* 9- IEEE80211_MODE_11NA_HT40PLUS 5Ghz, HT40 (ext ch +1)*/
{
  {135000,150000,0,0},{270000,300000,0,0},{405000,450000,0,0},{540000,600000,0,0},
  {540000,600000,0,0},{540000,600000,0,0},{540000,600000,0,0},{540000,600000,0,0},
},
/* 10- IEEE80211_MODE_11NA_HT40MINUS 5Ghz, HT40 (ext ch -1)*/
{
  {135000,150000,0,0},{270000,300000,0,0},{405000,450000,0,0},{540000,600000,0,0},
  {540000,600000,0,0},{540000,600000,0,0},{540000,600000,0,0},{540000,600000,0,0},
},
/* 11- IEEE80211_MODE_11NG_HT40PLUS 2Ghz, HT40 (ext ch +1)*/
{
  {135000,150000,0,0},{270000,300000,0,0},{405000,450000,0,0},{540000,600000,0,0},
  {540000,600000,0,0},{540000,600000,0,0},{540000,600000,0,0},{540000,600000,0,0},
},
/* 12- IEEE80211_MODE_11NG_HT40MINUS 2Ghz, HT40 (ext ch -1)*/
{
  {135000,150000,0,0},{270000,300000,0,0},{405000,450000,0,0},{540000,600000,0,0},
  {540000,600000,0,0},{540000,600000,0,0},{540000,600000,0,0},{540000,600000,0,0},
},
/* 13- IEEE80211_MODE_11NG_HT40  2Ghz, Auto HT40*/
{
  {135000,150000,0,0},{270000,300000,0,0},{405000,450000,0,0},{540000,600000,0,0},
  {540000,600000,0,0},{540000,600000,0,0},{540000,600000,0,0},{540000,600000,0,0},
},
/* 14- IEEE80211_MODE_11NA_HT40  2Ghz, Auto HT40*/
{
  {135000,150000,0,0},{270000,300000,0,0},{405000,450000,0,0},{540000,600000,0,0},
  {540000,600000,0,0},{540000,600000,0,0},{540000,600000,0,0},{540000,600000,0,0},
},
/* 15- IEEE80211_MODE_11AC_VHT20 5Ghz, VHT20*/
{
  {78000,86700,0,0},{156000,173300,0,0},{260000,288900,0,0},{312000,346700,0,0},
  {390000,433300,0,0},{520000,577800,0,0},{546000,606700,0,0},{624000,693300,0,0},
},
/* 16- IEEE80211_MODE_11AC_VHT40PLUS 5Ghz, VHT40 (Ext ch +1)*/
{
  {180000,200000,0,0},{360000,400000,0,0},{540000,600000,0,0},{720000,800000,0,0},
  {900000,1000000,0,0},{1080000,1200000,0,0},{1260000,1400000,0,0},{1440000,1600000,0,0},
},
/* 17- IEEE80211_MODE_11AC_VHT40MINUS  5Ghz  VHT40 (Ext ch -1)*/
{
  {180000,200000,0,0},{360000,400000,0,0},{540000,600000,0,0},{720000,800000,0,0},
  {900000,1000000,0,0},{1080000,1200000,0,0},{1260000,1400000,0,0},{1440000,1600000,0,0},
},
/* 18- IEEE80211_MODE_11AC_VHT40 5Ghz, VHT40 */
{
  {180000,200000,0,0},{360000,400000,0,0},{540000,600000,0,0},{720000,800000,0,0},
  {900000,1000000,0,0},{1080000,1200000,0,0},{1260000,1400000,0,0},{1440000,1600000,0,0},
},
/* 19- IEEE80211_MODE_11AC_VHT80 5Ghz, VHT80 */
{
  {390000,433300,0,0},{780000,866700,0,0},{1170000,1300000,0,0},{1560000,1733300,0,0},
  {1950000,2166700,0,0},{2106000,2340000,0,0},{2730000,3033300,0,0},{3120000,3466700,0,0},
},
/* 20- IEEE80211_MODE_11AC_VHT160 5Ghz, VHT160 */
{
  {780000,866700,0,0},{1560000,1733300,0,0},{2106000,2340000,0,0},{3120000,3466700,0,0},
  {3900000,4333300,0,0},{4680000,5200000,0,0},{5460000,6066700,0,0},{6240000,6933300,0,0},
},
/* 21- IEEE80211_MODE_11AC_VHT80_80 5Ghz, VHT80_80 */
{
  {780000,866700,0,0},{1560000,1733300,0,0},{2106000,2340000,0,0},{3120000,3466700,0,0},
  {3900000,4333300,0,0},{4680000,5200000,0,0},{5460000,6066700,0,0},{6240000,6933300,0,0},
},
/* 22- IEEE80211_MODE_11AXA_HE20  5Ghz , HE20 */
{
  {143400,147700,135400,121900},{286800,295500,270800,243800},{430100,443100,406300,365600},{573500,590900,541700,487500},
  {716900,738600,677100,609400},{860300,886400,812500,731300},{1003700,1034100,947900,853100},{1147100,1181800,1083300,975000},
},
/* 23- IEEE80211_MODE_11AXG_HE20  2Ghz , HE20 */
{
  {143400,147700,135400,121900},{286800,295500,270800,243800},{430100,443100,406300,365600},{573500,590900,541700,487500},
  {716900,738600,677100,609400},{860300,886400,812500,731300},{1003700,1034100,947900,853100},{1147100,1181800,1083300,975000},
},
/* 24- IEEE80211_MODE_11AXA_HE40PLUS 5GHz, HE40 (Ext ch +1)*/
{
  {286800,295500,270800,243800},{573500,590900,541700,487500},{860300,886400,812500,731300},{1147100,1181800,1083300,975000},
  {1433800,1477300,1354200,1218800},{1720600,1772700,1625000,1462500},{2007400,2068200,1895800,1706300},{2294100,2363600,2166700,1950000},
},
/* 25- IEEE80211_MODE_11AXA_HE40MINUS 5GHz, HE40 (Ext ch -1)*/
{
  {286800,295500,270800,243800},{573500,590900,541700,487500},{860300,886400,812500,731300},{1147100,1181800,1083300,975000},
  {1433800,1477300,1354200,1218800},{1720600,1772700,1625000,1462500},{2007400,2068200,1895800,1706300},{2294100,2363600,2166700,1950000},
},
/* 26- IEEE80211_MODE_11AXG_HE40PLUS 2GHz, HE40 (Ext ch +1)*/
{
  {286800,295500,270800,243800},{573500,590900,541700,487500},{860300,886400,812500,731300},{1147100,1181800,1083300,975000},
  {1433800,1477300,1354200,1218800},{1720600,1772700,1625000,1462500},{2007400,2068200,1895800,1706300},{2294100,2363600,2166700,1950000},
},
/* 27- IEEE80211_MODE_11AXG_HE40MINUS 2GHz, HE40 (Ext ch -1)*/
{
  {286800,295500,270800,243800},{573500,590900,541700,487500},{860300,886400,812500,731300},{1147100,1181800,1083300,975000},
  {1433800,1477300,1354200,1218800},{1720600,1772700,1625000,1462500},{2007400,2068200,1895800,1706300},{2294100,2363600,2166700,1950000},
},
/* 28- IEEE80211_MODE_11AXA_HE40 , 5G HE40 */
{
  {286800,295500,270800,243800},{573500,590900,541700,487500},{860300,886400,812500,731300},{1147100,1181800,1083300,975000},
  {1433800,1477300,1354200,1218800},{1720600,1772700,1625000,1462500},{2007400,2068200,1895800,1706300},{2294100,2363600,2166700,1950000},
},
/* 29- IEEE80211_MODE_11AXG_HE40 , 2G HE40 */
{
  {286800,295500,270800,243800},{573500,590900,541700,487500},{860300,886400,812500,731300},{1147100,1181800,1083300,975000},
  {1433800,1477300,1354200,1218800},{1720600,1772700,1625000,1462500},{2007400,2068200,1895800,1706300},{2294100,2363600,2166700,1950000},
},
/* 30- IEEE80211_MODE_11AXA_HE80 , 5G HE80 */
{
  {600400,618600,567100,510400},{1201000,1237300,1134300,1020800},{1801500,1855900,1701400,1531300},{2401900,2474500,2268500,2041600},
  {3002400,3093200,2835600,2552100},{3602900,3711800,3402800,3062500},{4203400,4330500,3969900,3572900},{4803900,4949000,4537000,4083300},
},
/* 31- IEEE80211_MODE_11AXA_HE160 , 5G HE160 */
{
  {1201000,1237300,1134200,1020800},{2401900,2474700,2268500,2041600},{3602900,3712000,3402800,3062500},{4803900,4949400,4537000,4083300},
  {6004900,6186700,5671300,5104100},{7205900,7424000,6805600,6125000},{8406800,8661400,7939800,7145800},{9607800,9898800,9074000,8166600},
},
/* 32- IEEE80211_MODE_11AXA_HE80_80 , 5G HE80_80 */
{
  {1201000,1237300,1134200,1020800},{2401900,2474700,2268500,2041600},{3602900,3712000,3402800,3062500},{4803900,4949400,4537000,4083300},
  {6004900,6186700,5671300,5104100},{7205900,7424000,6805600,6125000},{8406800,8661400,7939800,7145800},{9607800,9898800,9074000,8166600},
},
/* 33- IEEE80211_MODE_11NG_HT20 2Ghz, VHT20 BCC
   8 chain TODO: Fill in values for NSS=5 to NSS=8 when required in the future.
   Not required as of QCA8074. */
{
  {78000,86700,0,0},{156000,173300,0,0},{260000,288900,0,0},{312000,346700,0,0},
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
},
/* 34- IEEE80211_MODE_11NG_HT20 2Ghz, VHT20 LDPC
   8 chain TODO: Fill in values for NSS=5 to NSS=8 when required in the future.
   Not required as of QCA8074. */
{
  {86500,96000,0,0},{173000,192000,0,0},{260000,288900,0,0},{344000,378400,0,0},
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
},
/* 35- IEEE80211_MODE_11NG_HT40(+/-) 2Ghz, VHT40 LDPC
   8 chain TODO: Fill in values for NSS=5 to NSS=8 when required in the future.
   Not required as of QCA8074. */
{
  {180000,200000,0,0},{360000,400000,0,0},{540000,600000,0,0},{720000,800000,0,0},
  {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
},

/* 36- IEEE80211_MODE_11AC_VHT20 5Ghz, VHT20, 1024 QAM */
{
  {108250,120277,0,0},{216500,240554,0,0},{324750,360381,0,0},{433000,481108,0,0},
  {541250,601385,0,0},{649500,721662,0,0},{757750,841939,0,0},{866000,962216,0,0},
},
/* 37- IEEE80211_MODE_11NG_HT20 2Ghz, VHT20, 1024 QAM */
{
  {108250,120277,0,0},{216500,240554,0,0},{324750,360381,0,0},{433000,481108,0,0},
  {541250,601385,0,0},{649500,721662,0,0},{757750,841939,0,0},{866000,962216,0,0},
},
/* 38- IEEE80211_MODE_11AC_VHT40PLUS 5Ghz, VHT40 (Ext ch +1), 1024 QAM */
{
  {225000,250000,0,0},{450000,500000,0,0},{675000,750000,0,0},{900000,1000000,0,0},
  {1125000,1250000,0,0},{1350000,1500000,0,0},{1575000,1750000,0,0},{1800000,2000000,0,0},
},
/* 39- IEEE80211_MODE_11NG_HT40PLUS 2Ghz, VHT40 (Ext ch +1), 1024 QAM */
{
  {225000,250000,0,0},{450000,500000,0,0},{675000,750000,0,0},{900000,1000000,0,0},
  {1125000,1250000,0,0},{1350000,1500000,0,0},{1575000,1750000,0,0},{1800000,2000000,0,0},
},
/* 40- IEEE80211_MODE_11AC_VHT40MINUS 5Ghz, VHT40 (Ext ch -1), 1024 QAM */
{
  {225000,250000,0,0},{450000,500000,0,0},{675000,750000,0,0},{900000,1000000,0,0},
  {1125000,1250000,0,0},{1350000,1500000,0,0},{1575000,1750000,0,0},{1800000,2000000,0,0},
},
/* 41- IEEE80211_MODE_11NG_HT40MINUS 2Ghz, VHT40 (Ext ch -1), 1024 QAM */
{
  {225000,250000,0,0},{450000,500000,0,0},{675000,750000,0,0},{900000,1000000,0,0},
  {1125000,1250000,0,0},{1350000,1500000,0,0},{1575000,1750000,0,0},{1800000,2000000,0,0},
},
/* 42- IEEE80211_MODE_11AC_VHT40 5Ghz, VHT40, 1024 QAM */
{
  {225000,250000,0,0},{450000,500000,0,0},{675000,750000,0,0},{900000,1000000,0,0},
  {1125000,1250000,0,0},{1350000,1500000,0,0},{1575000,1750000,0,0},{1800000,2000000,0,0},
},
/* 43- IEEE80211_MODE_11NG_HT40 2Ghz, VHT40, 1024 QAM */
{
  {225000,250000,0,0},{450000,500000,0,0},{675000,750000,0,0},{900000,1000000,0,0},
  {1125000,1250000,0,0},{1350000,1500000,0,0},{1575000,1750000,0,0},{1800000,2000000,0,0},
},
/* 44- IEEE80211_MODE_11AC_VHT80 5Ghz, VHT80, 1024 QAM */
{
  {487500,541666,0,0},{975000,1083332,0,0},{1462500,1624998,0,0},{1950000,2166664,0,0},
  {2437500,2708330,0,0},{2925000,3249996,0,0},{3412500,3791662,0,0},{3900000,4333328,0,0},
},
/* 45- IEEE80211_MODE_11AC_VHT160 5Ghz, VHT160, 1024 QAM */
{
  {975000,1083333,0,0},{1950000,2166666,0,0},{2925000,3249999,0,0},{3900000,4333332,0,0},
  {4875000,5416665,0,0},{5850000,6499998,0,0},{6825000,7583331,0,0},{7800000,8666664,0,0},
},
/* 46- IEEE80211_MODE_11AC_VHT80_80 5Ghz, VHT80_80, 1024 QAM */
{
  {975000,1083333,0,0},{1950000,2166666,0,0},{2925000,3249999,0,0},{3900000,4333332,0,0},
  {4875000,5416665,0,0},{5850000,6499998,0,0},{6825000,7583331,0,0},{7800000,8666664,0,0},
},

};

#if defined(PORT_SPIRENT_HK) && defined(SPT_MULTI_CLIENTS)
/**
 * enum "WMI_PEER_STA_KICKOUT_REASON_THROTTLE_START", in file- wmi_unified.h
 * can not able to include the header file. so added the macro for it.
 * During Revanche porting, need to be reconsider.
 */
#define KICKOUT_REASON_THROTTLE_START  6
#endif
/* WMI interface functions */
int
ol_ath_node_set_param(struct ol_ath_softc_net80211 *scn, u_int8_t *peer_addr,u_int32_t param_id,
        u_int32_t param_val,u_int32_t vdev_id)
{
    struct peer_set_params param;
    struct wmi_unified *pdev_wmi_handle;

    pdev_wmi_handle = lmac_get_pdev_wmi_handle(scn->sc_pdev);
    qdf_mem_set(&param, sizeof(param), 0);
    param.param_id = param_id;
    param.vdev_id = vdev_id;
    param.param_value = param_val;

    return wmi_set_peer_param_send(pdev_wmi_handle, peer_addr, &param);
}


#ifndef WLAN_CONV_CRYPTO_SUPPORTED
static void
set_node_wep_keys(struct ieee80211vap *vap, const u_int8_t *macaddr)
{
    struct ieee80211com *ic = vap->iv_ic;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    u_int8_t keymac[QDF_MAC_ADDR_SIZE];
    struct ieee80211_key *wkey;
    struct ieee80211_rsnparms *rsn = &vap->iv_rsn;
    int i, opmode;

    wkey = (struct ieee80211_key *)OS_MALLOC(scn->sc_osdev,
						sizeof(struct ieee80211_key),
                                                0);
    OS_MEMZERO(wkey, sizeof(struct ieee80211_key));
    opmode = ieee80211vap_get_opmode(vap);

    /* push only valid static WEP keys from vap */

    if (RSN_AUTH_IS_8021X(rsn)) {
        OS_FREE(wkey);
        return ;
    }

    for(i=0;i<IEEE80211_WEP_NKID;i++) {

        OS_MEMCPY(wkey,&vap->iv_nw_keys[i],sizeof(struct ieee80211_key));

        if(wkey->wk_valid && wkey->wk_cipher->ic_cipher==IEEE80211_CIPHER_WEP) {
               IEEE80211_ADDR_COPY(keymac,macaddr);

            /* setting the broadcast/multicast key for sta */
            if(opmode == IEEE80211_M_STA || opmode == IEEE80211_M_IBSS){
                vap->iv_key_set(vap, wkey, keymac);
            }

            /* setting unicast key */
            wkey->wk_flags &= ~IEEE80211_KEY_GROUP;
            vap->iv_key_set(vap, wkey, keymac);
        }
    }
    OS_FREE(wkey);

}
#endif

#if ATH_SUPPORT_NAC
static void ol_ath_del_nac(struct ieee80211vap *vap, struct ieee80211_nac_info nac_client[],
                           char *macaddr, int nac_index)
{
    /* clear table for the mac */
    OS_MEMZERO(nac_client[nac_index].macaddr, QDF_MAC_ADDR_SIZE);
    nac_client[nac_index].avg_rssi = 0;
    nac_client[nac_index].rssi = 0;
    IEEE80211_DPRINTF(vap, IEEE80211_MSG_NAC,
            "%s Macaddress Slot %d removed:%2x %2x \n", __func__,
            nac_index,nac_client[nac_index].macaddr[0], nac_client[nac_index].macaddr[5]);
}
#endif

/* Interface functions */
static struct ieee80211_node *
ol_ath_node_alloc(struct ieee80211vap *vap, const u_int8_t *macaddr, bool tmpnode, void *peer)
{
    struct ieee80211com *ic = vap->iv_ic;
    struct ol_ath_vap_net80211 *avn = OL_ATH_VAP_NET80211(vap);
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    ol_ath_soc_softc_t *soc = scn->soc;
    struct ol_ath_node_net80211 *anode;
    struct peer_create_params param;
#if UMAC_SUPPORT_ACFG
    acfg_event_data_t *acfg_event = NULL;
#endif
    struct wlan_objmgr_psoc *psoc = NULL;
    target_resource_config *tgt_cfg;
    ol_txrx_soc_handle soc_txrx_handle;
    struct wmi_unified *pdev_wmi_handle;
    bool is_connected_sta_peer;
    bool is_cac_on_nawds_vap = false;
    QDF_STATUS status;
    uint8_t vdev_id = wlan_vdev_get_id(vap->vdev_obj);
#if ATH_SUPPORT_NAC
    struct ieee80211_nac *vap_nac = NULL;
    struct ieee80211vap *tmpvap = NULL;
    int i;
    cdp_config_param_type val = {0};
#endif
    pdev_wmi_handle = lmac_get_pdev_wmi_handle(scn->sc_pdev);
    psoc = scn->soc->psoc_obj;
    tgt_cfg = lmac_get_tgt_res_cfg(psoc);
    if (!tgt_cfg) {
        qdf_print("%s: psoc target res cfg is null", __func__);
        return NULL;
    }
    soc_txrx_handle = wlan_psoc_get_dp_handle(psoc);

    qdf_mem_set(&param, sizeof(param), 0);
    anode = (struct ol_ath_node_net80211 *)qdf_mempool_alloc(soc->qdf_dev, soc->mempool_ol_ath_node);
    if (anode == NULL)
        return NULL;

    wlan_minidump_log((void *)anode, sizeof(*anode), psoc,
                      WLAN_MD_CP_EXT_PEER, "ieee80211_node");
    OS_MEMZERO(anode, sizeof(struct ol_ath_node_net80211));

    anode->an_node.ni_vap = vap;


    is_connected_sta_peer = vap->iv_opmode != IEEE80211_M_STA
        && !(IEEE80211_ADDR_EQ(macaddr, vap->iv_myaddr));


    /* do not create/delete peer on target for temp nodes and self-peers */
    if (!tmpnode && !is_node_self_peer(vap, macaddr) && (vap->iv_opmode != IEEE80211_M_MONITOR)) {
        if (is_connected_sta_peer) {
            if (qdf_atomic_dec_and_test(&scn->peer_count)) {
                AR_DEBUG_PRINTF(ATH_DEBUG_INFO,
                    ("%s: vap (%pK) scn (%pK) the peer count exceeds the configured number) \n",
                     __func__, vap, scn));

                qdf_mempool_free(soc->qdf_dev, soc->mempool_ol_ath_node, anode);
#if UMAC_SUPPORT_ACFG
                acfg_event = (acfg_event_data_t *)qdf_mem_malloc( sizeof(acfg_event_data_t));
                if (acfg_event != NULL){
                    acfg_send_event(scn->sc_osdev->netdev, scn->sc_osdev, WL_EVENT_TYPE_EXCEED_MAX_CLIENT, acfg_event);
                   qdf_mem_free(acfg_event);
               }
#endif
               goto err_node_alloc;
            }
        }

        /* We need to allow creation of NAWDS peers during CAC since there are
         * no frame exchanges involved and since this might help for slightly
         * faster bring-up.
         *
         * Currently, wlan_peer_get_peer_type() doesn't return WLAN_PEER_NAWDS.
         * So we need to tentatively adopt a solution of checking if the VAP has
         * NAWDS enabled, and allowing peer creation during CAC if so. Normal
         * non-NAWDS peers would not be created during CAC since management
         * frame Rx will not happen during this period.
         *
         * NAWDS_CONVERGENCE_TAG: Later once NAWDS convergence is completed,
         * this tentative solution will no longer be required and is to be
         * removed. We can rely on the peer type.
         */
        is_cac_on_nawds_vap =
            (vap->iv_nawds.mode != IEEE80211_NAWDS_DISABLED) &&
                ieee80211_vap_dfswait_is_set(vap);

        /* Do not allow peer craetion if AP vap is not up */
        if ((vap->iv_opmode == IEEE80211_M_HOSTAP) &&
                !ieee80211_is_vap_state_running(vap) &&
                wlan_peer_get_peer_type(peer) == WLAN_PEER_STA &&
                !is_cac_on_nawds_vap) {
            qdf_mempool_free(soc->qdf_dev, soc->mempool_ol_ath_node, anode);
            qdf_print("%s: vap: %d is not up, fail peer create",
                    __func__, vap->iv_unit);
            goto err_node_alloc;
        }

        qdf_spin_lock_bh(&scn->scn_lock);
        status = cdp_peer_create(soc_txrx_handle, vdev_id, (u_int8_t *) macaddr);
        if (QDF_IS_STATUS_ERROR(status)) {
            qdf_spin_unlock_bh(&scn->scn_lock);
            qdf_err("Unable to attach txrx peer for mac %pM, vdev_id %d\n", macaddr, vdev_id);
            qdf_mempool_free(soc->qdf_dev, soc->mempool_ol_ath_node, anode);
            goto err_node_alloc;
        }

        anode->an_node.ni_ext_flags |= IEEE80211_NODE_DP_PEER_EXISTS;

        param.peer_addr = macaddr;
        param.vdev_id = avn->av_if_id;

        IEEE80211_DPRINTF(vap, IEEE80211_MSG_PEER_DELETE,
                "wmi_peer_create vap_id: %d, mac: %s, ni: 0x%pK\n",
                avn->av_if_id, ether_sprintf(macaddr), &anode->an_node);

        if (wmi_unified_peer_create_send(pdev_wmi_handle, &param)) {
            qdf_print("%s : Unable to create peer in Target \n", __func__);
        }

        cdp_peer_setup(soc_txrx_handle, vdev_id, (u_int8_t *)macaddr);

        qdf_spin_unlock_bh(&scn->scn_lock);

#ifndef WLAN_CONV_CRYPTO_SUPPORTED
        /* static wep keys stored in vap needs to be
         * pushed to all nodes except self node
         */
        if(IEEE80211_VAP_IS_PRIVACY_ENABLED(vap) &&
               (OS_MEMCMP(macaddr, vap->iv_myaddr,QDF_MAC_ADDR_SIZE) != 0 )) {
            set_node_wep_keys(vap,macaddr);
        }
#endif
#if ATH_SUPPORT_NAC
        if(vap->iv_smart_monitor_vap) {
            val.cdp_peer_param_nac = 1;
            cdp_txrx_set_peer_param(soc_txrx_handle, vdev_id, (u_int8_t *)macaddr, CDP_CONFIG_NAC, val);
        }
        /* In monitor direct based smart monitor case if NAC
         * client becomes self client we delete the NAC client
         * from our Vap's NAC list.
         */
        if (ic->ic_hw_nac_monitor_support && scn->smart_ap_monitor) {
            TAILQ_FOREACH(tmpvap, &ic->ic_vaps, iv_next) {
                if (tmpvap->iv_smart_monitor_vap) {
                    vap_nac = &tmpvap->iv_nac;
                    for (i = 0; i < NAC_MAX_CLIENT; i++)
                    {
                        if (IEEE80211_ADDR_EQ(vap_nac->client[i].macaddr, macaddr)) {
                            ol_ath_del_nac(tmpvap, vap_nac->client, (uint8_t*)macaddr, i);
                            if(tmpvap->iv_neighbour_rx)
                                tmpvap->iv_neighbour_rx(tmpvap , i,
                                IEEE80211_NAC_PARAM_DEL,
                                IEEE80211_NAC_MACTYPE_CLIENT,
                                (uint8_t *)macaddr);
                            break;
                        }
                    }
                }
            }
        }
#endif
    }

#if WLAN_OBJMGR_REF_ID_TRACE
    anode->an_node.trace = (struct node_trace_all *)OS_MALLOC(ic->ic_osdev, sizeof(struct node_trace_all), GFP_KERNEL);
    if (anode->an_node.trace == NULL) {
        qdf_print("Can't create an node trace");
        return NULL;
    }
    OS_MEMZERO(anode->an_node.trace, sizeof(struct node_trace_all));
#endif
    return &anode->an_node;

err_node_alloc:
    qdf_atomic_inc(&scn->peer_count);
    return NULL;

}

static void
ol_ath_node_free(struct ieee80211_node *ni)
{
    struct ieee80211com *ic = ni->ni_ic;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    u_int32_t ni_flags = ni->ni_flags;

    qdf_spin_lock_bh(&scn->scn_lock);
    if (ni_flags & IEEE80211_NODE_EXT_STATS) {
        scn->peer_ext_stats_count--;
    }
    qdf_spin_unlock_bh(&scn->scn_lock);
    /* Call back the umac node free function */
    scn->soc->net80211_node_free(ni);
    /* Moved node free here from umac layer since allocation is actually done by ol_if layer */
    wlan_minidump_remove(ni);
    qdf_mempool_free(scn->soc->qdf_dev, scn->soc->mempool_ol_ath_node, ni);
}

static void
ol_ath_preserve_node_for_fw_delete_resp(struct ieee80211_node *ni)
{
    struct ieee80211com *ic;
    struct ieee80211vap *vap;
    struct ol_ath_softc_net80211 *scn;
    ol_ath_soc_softc_t *soc;
    uint32_t target_type;
    QDF_STATUS status;

    if (!ni) {
        qdf_print("preserve called with NULL ni!!");
        return;
    }

    ic = ni->ni_ic;
    vap = ni->ni_vap;
    scn = OL_ATH_SOFTC_NET80211(ic);
    soc = scn->soc;
    target_type = lmac_get_tgt_type(soc->psoc_obj);
    IEEE80211_DPRINTF(vap, IEEE80211_MSG_PEER_DELETE,
            "+preserve:%d:0x%pK\n",
            qdf_atomic_read(&(ni->ni_fw_peer_delete_rsp_pending)),
            ni);
    /* take refrence if we not waiting for peer delete response */
    if (!qdf_atomic_read(&(ni->ni_fw_peer_delete_rsp_pending)) &&
            (ni->ni_ext_flags & IEEE80211_NODE_DP_PEER_EXISTS)) {
        /* Hold reference to node for peer delete response */
        status = wlan_objmgr_try_ref_node(ni, WLAN_MLME_OBJ_DEL_ID);
        if (QDF_IS_STATUS_ERROR(status)) {
            IEEE80211_DPRINTF(vap, IEEE80211_MSG_PEER_DELETE,
                    "preserve: failed to get refrence\n");
            return;
        }

        qdf_atomic_set(&(ni->ni_node_preserved), 1);
        if ((target_type == TARGET_TYPE_QCA8074) ||
            (target_type == TARGET_TYPE_QCA8074V2) ||
            (target_type == TARGET_TYPE_QCA6018) ||
            (target_type == TARGET_TYPE_QCA5018) ||
            (target_type == TARGET_TYPE_QCN9000) ||
            (target_type == TARGET_TYPE_QCA6290)) {
            /* mark peer delete response pending */
            qdf_atomic_set(&(ni->ni_fw_peer_delete_rsp_pending), 1);
        }
    } else {
        IEEE80211_DPRINTF(vap, IEEE80211_MSG_PEER_DELETE,
                "preserve: no refrence taken\n");
    }
}

static inline uint32_t
ol_if_drain_mgmt_backlog_queue(struct ieee80211_node *ni)
{
    struct ieee80211com *ic = ni->ni_ic;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    int vdev_id = (OL_ATH_VAP_NET80211(ni->ni_vap))->av_if_id;
    qdf_nbuf_t tx_mgmt_frm = NULL;
    qdf_nbuf_queue_t *MgmtQ = NULL;
    qdf_spinlock_t    *mgmtbufLock;
    qdf_nbuf_queue_t tmpQ;
    qdf_nbuf_queue_t cbQ;
    uint32_t nfreed = 0, mgmt_txrx_desc_id;
    struct ieee80211_node *temp_ni = NULL;
    int temp_vid = 0;
    struct wlan_objmgr_psoc *psoc;
    struct wlan_objmgr_peer *peer;

    /* Init temp queues */
    qdf_nbuf_queue_init(&tmpQ);
    qdf_nbuf_queue_init(&cbQ);

    psoc = wlan_pdev_get_psoc(ic->ic_pdev_obj);

    MgmtQ = (&scn->mgmt_ctx.mgmt_backlog_queue);
    mgmtbufLock = (&scn->mgmt_ctx.mgmt_backlog_queue_lock);

    /* Iterate over backlog management queue and check
     * whether this frames belongs to this particular vap?
     * If this frame belongs to this vap, call completion
     * handler ol_ath_mgmt_tx_complete() with flag
     * IEEE80211_FORCE_FLUSH. Since frames in backlog SW queue
     * are not DMA mapped, unmapping is not required.
     * Frames which do not belong to this vap, add them to a
     * seperate list.
     */
    qdf_spin_lock_bh(mgmtbufLock);
    while(!qdf_nbuf_is_queue_empty(MgmtQ)) {
        tx_mgmt_frm = qdf_nbuf_queue_remove(MgmtQ);
        if (!tx_mgmt_frm) {
            qdf_spin_unlock_bh(mgmtbufLock);
            return 0;
        }

        /*
         * When the nbuf was passed to the mgmt_txrx layer,
         * the peer field in the cb was overwritten with the
         * desc_id. Hence, the peer needs to be fetched from
         * the mgmt_txrx layer.
         */
        mgmt_txrx_desc_id = wbuf_get_txrx_desc_id(tx_mgmt_frm);
        peer = mgmt_txrx_get_peer((struct wlan_objmgr_pdev *)scn->sc_pdev, mgmt_txrx_desc_id);
        QDF_ASSERT(peer != NULL);
        temp_ni = wlan_peer_get_mlme_ext_obj(peer);
        if (temp_ni) {
            temp_vid = (OL_ATH_VAP_NET80211(temp_ni->ni_vap))->av_if_id;
        }
        if (temp_vid == vdev_id) {
            qdf_nbuf_queue_add(&cbQ, tx_mgmt_frm);
        } else {
            qdf_nbuf_queue_add(&tmpQ, tx_mgmt_frm);
        }
    }
    /* Assign temp queue back to management backlog queue
     */
    scn->mgmt_ctx.mgmt_backlog_queue = tmpQ;
    qdf_spin_unlock_bh(mgmtbufLock);

    /* Call completion handler for each mgmt_frm in cbQ */
    while(!qdf_nbuf_is_queue_empty(&cbQ)) {
        ++nfreed;
        tx_mgmt_frm = qdf_nbuf_queue_remove(&cbQ);
        qdf_assert_always(tx_mgmt_frm);
        ol_ath_mgmt_tx_complete(scn->sc_pdev, tx_mgmt_frm, IEEE80211_TX_ERROR);
    }
    /* Clear temp queues to avoid any dangling reference.*/
    qdf_nbuf_queue_init(&tmpQ);
    qdf_nbuf_queue_init(&cbQ);

    return nfreed;
}

QDF_STATUS nbuf_fill_peer(struct wlan_objmgr_peer *peer,
                    qdf_nbuf_t buf)
{
    wbuf_set_peer(buf, peer);
    return 0;
}

void
ol_if_mgmt_drain(struct ieee80211_node *ni, int force)
{
    struct ieee80211com *ic = ni->ni_ic;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    ol_txrx_soc_handle soc_txrx_handle = wlan_psoc_get_dp_handle(scn->soc->psoc_obj);

    /* First drain SW queue to make sure no new frame is queued
     * to Firmware for this vap. It may also give us some time
     * to receive completions for outstanding frames.
     **/
    (void)ol_if_drain_mgmt_backlog_queue(ni);

    if (!wlan_psoc_nif_fw_ext_cap_get(scn->soc->psoc_obj,
                WLAN_SOC_CEXT_WMI_MGMT_REF)) {
        cdp_if_mgmt_drain(soc_txrx_handle, (OL_ATH_VAP_NET80211(ni->ni_vap))->av_if_id, force);
    } else {
        if (force) {
            struct ieee80211_tx_status ts = {0};

            /* drain mgmt packets with an error status */
            ts.ts_flags = IEEE80211_TX_ERROR;
            wlan_mgmt_txrx_vdev_drain(ni->ni_vap->vdev_obj, nbuf_fill_peer, &ts);
        }
    }
}

qdf_export_symbol(ol_if_mgmt_drain);

static void
ol_ath_node_cleanup(struct ieee80211_node *ni)
{
    struct ieee80211com *ic = ni->ni_ic;
    struct ieee80211vap *vap = ni->ni_vap;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    ol_ath_soc_softc_t *soc = scn->soc;
    struct ol_ath_vap_net80211 *avn = OL_ATH_VAP_NET80211(ni->ni_vap);
    struct peer_flush_params param;
    u_int32_t peer_tid_bitmap = 0xffffffff; /* TBD : fill with all valid TIDs */
    ol_txrx_soc_handle soc_txrx_handle;
    struct wlan_objmgr_psoc *psoc;
    struct wlan_objmgr_vdev *vdev = ni->ni_vap->vdev_obj;
    struct wmi_unified *pdev_wmi_handle;
    struct vdev_response_timer *vdev_rsp = NULL;
    struct wlan_lmac_if_mlme_rx_ops *rx_ops;
    uint8_t vdev_id;

    psoc = wlan_vdev_get_psoc(vdev);
    if (psoc == NULL) {
        qdf_print("%s: psoc is NULL ", __func__);
        return;
    }

    pdev_wmi_handle = lmac_get_pdev_wmi_handle(scn->sc_pdev);
    /* TBD: Cleanup the key index mapping */

    qdf_spin_lock_bh(&scn->scn_lock);
    soc_txrx_handle = wlan_psoc_get_dp_handle(psoc);

    if ((ni->ni_ext_flags & IEEE80211_NODE_DP_PEER_EXISTS)) {
        cdp_peer_teardown(soc_txrx_handle,
                      avn->av_if_id, ni->peer_obj->macaddr);

        /* Delete key */
#ifndef WLAN_CONV_CRYPTO_SUPPORTED
        ieee80211_node_clear_keys(ni);
#endif

        IEEE80211_DPRINTF(vap, IEEE80211_MSG_PEER_DELETE,
                "wmi_peer_delete node: 0x%pK(%s), vapid: %d\n",
                ni, ether_sprintf(ni->ni_macaddr), avn->av_if_id);
        /* Delete peer in Target */
        if (ni == vap->iv_bss ||
            qdf_atomic_read(&(ni->ni_peer_del_req_enable))) {

            /*
             * Send peer flush tids command to FW only for pre-lithium chipsets
             * For lithium platforms peer delete command to FW will
             * internally flush the peer tids as well
             **/
            if (!ol_target_lithium(psoc)) {
                /* flush all TIDs except MGMT TID for this peer in Target */
                peer_tid_bitmap &= ~(0x1 << WMI_HOST_MGMT_TID);
                param.peer_tid_bitmap = peer_tid_bitmap;
                param.vdev_id = avn->av_if_id;
                if (wmi_unified_peer_flush_tids_send(pdev_wmi_handle,
                                                     ni->ni_macaddr, &param)) {
                        qdf_err("Unable to Flush tids peer in Target");
                }
            }

            if (wmi_unified_peer_delete_send(pdev_wmi_handle,
                                             ni->ni_macaddr, avn->av_if_id)) {
                qdf_err("Unable to Delete peer in Target ");
                qdf_atomic_set(&(ni->ni_fw_peer_delete_rsp_pending), 0);
            } else {
#ifdef QCA_SUPPORT_CP_STATS
                vdev_cp_stats_peer_delete_req_inc(vap->vdev_obj, 1);
#endif
            }
        } else {
            /* avoid physical deletion when vdev delete all peer is supported */
            qdf_atomic_set(&(ni->ni_fw_peer_delete_rsp_pending), 1);
        }
        qdf_atomic_set(&(ni->ni_peer_del_req_enable), 1);

        /* save peer delete request time for debug purposes */
        ni->ss_last_data_time = OS_GET_TIMESTAMP();
        cdp_peer_delete(soc_txrx_handle, avn->av_if_id, ni->peer_obj->macaddr,
                        CDP_PEER_DELETE_NO_SPECIAL);

        /*
         * It is possible that a node will be cleaned up for multiple times
         * before it is freed. Make sure we only remove TxRx/FW peer once.
         */
        ni->ni_ext_flags &= ~IEEE80211_NODE_DP_PEER_EXISTS;

    }
    qdf_spin_unlock_bh(&scn->scn_lock);

    /* Call back the umac node cleanup function */
    soc->net80211_node_cleanup(ni);
    /*
     *  free the refrence taken in node_preserve if
     *  ni_fw_peer_delete_rsp_pending is ZERO
     */
    vdev_id = wlan_vdev_get_id(vdev);
    rx_ops = target_if_vdev_mgr_get_rx_ops(psoc);
    if (rx_ops && rx_ops->psoc_get_vdev_response_timer_info) {
        vdev_rsp = rx_ops->psoc_get_vdev_response_timer_info(psoc, vdev_id);
    }

    if (!qdf_atomic_read(&(ni->ni_fw_peer_delete_rsp_pending)) &&
        qdf_atomic_read(&(ni->ni_node_preserved)) &&
        ((vap->iv_opmode == IEEE80211_M_STA) ||
         ((vap->iv_bss != ni) && vdev_rsp &&
           !qdf_atomic_test_bit(PEER_DELETE_ALL_RESPONSE_BIT,
                               &vdev_rsp->rsp_status)))) {
        IEEE80211_DPRINTF(vap, IEEE80211_MSG_PEER_DELETE,
            "%s: unref node: 0x%pK, vapid: %d\n",
                __func__, ni, avn->av_if_id);
        qdf_atomic_set(&(ni->ni_node_preserved), 0);
        wlan_objmgr_free_node(ni, WLAN_MLME_OBJ_DEL_ID);
    } else {
           qdf_atomic_set(&(ni->ni_fw_peer_delete_rsp_pending), 1);
    }
}

#define OL_ATH_DUMMY_RSSI    255
static u_int8_t
ol_ath_node_getrssi(const struct ieee80211_node *ni,int8_t chain, u_int8_t flags )
{
    struct ieee80211vap *vap = ni->ni_vap ;
    struct wlan_objmgr_psoc *psoc;
    cdp_peer_stats_param_t buf = {0};
    QDF_STATUS status;

    buf.rx_avg_rssi = OL_ATH_DUMMY_RSSI;
    if (ni->ni_ic->ic_uniform_rssi) {
        psoc = wlan_pdev_get_psoc(ni->ni_ic->ic_pdev_obj);
        if (!psoc)
            return OL_ATH_DUMMY_RSSI;

        status = cdp_txrx_get_peer_stats_param(wlan_psoc_get_dp_handle(psoc),
                                     wlan_vdev_get_id(ni->peer_obj->peer_objmgr.vdev),
                                     ni->peer_obj->macaddr,
                                     cdp_peer_rx_avg_rssi, &buf);
        if (QDF_IS_STATUS_ERROR(status)) {
            return OL_ATH_DUMMY_RSSI;
        }

        return WLAN_RSSI_OUT(buf.rx_avg_rssi);
    }

    if (vap) {
        if (!ni->ni_rssi &&
            (wlan_vap_get_opmode(vap) == IEEE80211_M_HOSTAP &&
            (wlan_vdev_is_up(vap->vdev_obj) == QDF_STATUS_SUCCESS))) {
            return OL_ATH_DUMMY_RSSI ;
        }
    }
    return ni->ni_rssi;
}

static u_int32_t
ol_ath_node_getrate(const struct ieee80211_node *ni, u_int8_t type)
{
    struct wlan_objmgr_psoc *psoc;
    ol_txrx_soc_handle soc;
    QDF_STATUS status;
    cdp_peer_stats_param_t buf = {0};
    uint8_t vdev_id;

    if (!ni)
        return 0;

    psoc = wlan_pdev_get_psoc(ni->ni_ic->ic_pdev_obj);
    if (!psoc)
        return 0;

    soc = wlan_psoc_get_dp_handle(psoc);
    vdev_id = wlan_vdev_get_id(ni->peer_obj->peer_objmgr.vdev);
    switch (type) {
        case IEEE80211_RATE_TX:
             status = cdp_txrx_get_peer_stats_param(soc, vdev_id,
                                 ni->peer_obj->macaddr, cdp_peer_tx_last_tx_rate,
                                 &buf);

             if (QDF_IS_STATUS_ERROR(status))
                 return 0;

             return buf.last_tx_rate;
        case IEEE80211_RATE_RX:
             status = cdp_txrx_get_peer_stats_param(soc, vdev_id,
                                 ni->peer_obj->macaddr, cdp_peer_rx_last_rx_rate,
                                 &buf);

             if (QDF_IS_STATUS_ERROR(status))
                 return 0;

             return buf.last_rx_rate;
        case IEEE80211_RATECODE_TX:
             status = cdp_txrx_get_peer_stats_param(soc, vdev_id,
                                 ni->peer_obj->macaddr, cdp_peer_tx_ratecode,
                                 &buf);

             if (QDF_IS_STATUS_ERROR(status))
                 return 0;

             return buf.tx_ratecode;
        case IEEE80211_RATEFLAGS_TX:
             status = cdp_txrx_get_peer_stats_param(soc, vdev_id,
                                 ni->peer_obj->macaddr, cdp_peer_tx_flags,
                                 &buf);

             if (QDF_IS_STATUS_ERROR(status))
                 return 0;

             return buf.tx_flags;
        case IEEE80211_RATECODE_RX:
             status = cdp_txrx_get_peer_stats_param(soc, vdev_id,
                                 ni->peer_obj->macaddr, cdp_peer_rx_ratecode,
                                 &buf);

             if (QDF_IS_STATUS_ERROR(status))
                 return 0;

             return buf.rx_ratecode;
        case IEEE80211_RATEFLAGS_RX:
             status = cdp_txrx_get_peer_stats_param(soc, vdev_id,
                                 ni->peer_obj->macaddr, cdp_peer_rx_flags,
                                 &buf);

             if (QDF_IS_STATUS_ERROR(status))
                 return 0;

             return buf.rx_flags;
        default:
             return 0;
    }
}

static u_int32_t
ol_ath_node_get_last_txpower(const struct ieee80211_node *ni)
{
    struct wlan_objmgr_psoc *psoc;
    cdp_peer_stats_param_t buf = {0};
    QDF_STATUS status;

    if (!ni)
        return 0;

    psoc = wlan_pdev_get_psoc(ni->ni_ic->ic_pdev_obj);
    if (!psoc)
        return 0;

    status = cdp_txrx_get_peer_stats_param(wlan_psoc_get_dp_handle(psoc),
                                     wlan_vdev_get_id(ni->peer_obj->peer_objmgr.vdev),
                                     ni->peer_obj->macaddr, cdp_peer_tx_power, &buf);
    if (QDF_IS_STATUS_ERROR(status))
        return 0;

    return buf.tx_power / 2;
}

static void
ol_ath_node_psupdate(struct ieee80211_node *ni, int pwrsave, int pause_resume)
{
    struct ieee80211vap *vap = ni->ni_vap;

    if (pwrsave) {
        (void)wmi_unified_set_ap_ps_param(OL_ATH_VAP_NET80211(vap),
                   OL_ATH_NODE_NET80211(ni), WMI_HOST_AP_PS_PEER_PARAM_WNM_SLEEP,
                                          WMI_HOST_AP_PS_PEER_PARAM_WNM_SLEEP_ENABLE);
    } else {
        (void)wmi_unified_set_ap_ps_param(OL_ATH_VAP_NET80211(vap),
                   OL_ATH_NODE_NET80211(ni), WMI_HOST_AP_PS_PEER_PARAM_WNM_SLEEP,
                                          WMI_HOST_AP_PS_PEER_PARAM_WNM_SLEEP_DISABLE);
    }
}

static u_int8_t
ol_ath_node_get_auto_rate_sgi(struct ieee80211_node *ni)
{
    struct ieee80211vap *vap = ni->ni_vap;
    uint8_t he_ar_gi = (vap->iv_he_ar_gi_ltf >> IEEE80211_HE_AR_SGI_S);
    uint8_t he_ar_gi_max;

    IEEE80211_GET_MAX_AR_SGI(he_ar_gi, he_ar_gi_max);

    switch(he_ar_gi_max) {
        case 0: /* (he_ar_gi) - Position 1 corresponds to 400ns*/
            return IEEE80211_GI_0DOT4_US;
        case 1: /* (he_ar_gi) - Position 2 corresponds to 800ns*/
            return IEEE80211_GI_0DOT8_US;
        case 2: /* (he_ar_gi) - Position 3 corresponds to 1600ns*/
            return IEEE80211_GI_1DOT6_US;
        case 3: /* (he_ar_gi) - Position 4 corresponds to 3200ns*/
            return IEEE80211_GI_3DOT2_US;
        default:
            qdf_err("%s: Invalid he_ar_gi setting!", __func__);
            return IEEE80211_GI_0DOT8_US;
    }
    return 0;
}

static u_int32_t
ol_ath_node_get_maxphyrate(struct ieee80211com *ic, struct ieee80211_node *ni)
{
    u_int8_t mcs;
    u_int8_t bw;
    struct ieee80211vap *vap = ni->ni_vap;
    u_int8_t curr_phy_mode = wlan_get_current_phymode(vap);
    enum ieee80211_fixed_rate_mode rate_mode = vap->iv_fixed_rate.mode;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    struct ieee80211_bwnss_map nssmap;
    uint8_t *nss_bw_160 = &nssmap.bw_nss_160;
    uint8_t tx_chainmask  = ieee80211com_get_tx_chainmask(ic);
    struct vdev_mlme_obj *vdev_mlme = vap->vdev_mlme;
    u_int8_t nss = 0;
    u_int8_t sgi = 0;
    int ratekbps = 0;
    uint32_t target_type;
    u_int8_t saved_curr_phy_mode;
    uint32_t iv_ldpc = 0;
    uint32_t iv_nss = 0;
    uint8_t he_ar_sgi = 0;
    /* Determine self node(BSS) or peer node */
    bool is_self_node = (ni == vap->iv_bss) ? true : false;

    target_type = lmac_get_tgt_type(scn->soc->psoc_obj);
    if(is_self_node) {
        bw = wlan_get_param(vap, IEEE80211_CHWIDTH);
    } else {
        bw = ni->ni_chwidth;
    }

    iv_nss = vdev_mlme->proto.generic.nss;
    iv_ldpc = vdev_mlme->proto.generic.ldpc;

    if ((!(is_self_node) && !(IS_MODE_11AX(is_self_node, ni))) ||
            ((ieee80211_vap_get_opmode(vap) == IEEE80211_M_STA) &&
                                        !(IS_MODE_11AX(is_self_node, ni)))) {
        sgi = (((ni->ni_htcap & IEEE80211_HTCAP_C_SHORTGI40) &&
                    (bw == IEEE80211_CWM_WIDTH40)) ||
            ((ni->ni_htcap & IEEE80211_HTCAP_C_SHORTGI20) &&
                    (bw == IEEE80211_CWM_WIDTH20)) ||
            ((ni->ni_vhtcap & IEEE80211_VHTCAP_SHORTGI_80) &&
                    (bw == IEEE80211_CWM_WIDTH80))||
            ((ni->ni_vhtcap & IEEE80211_VHTCAP_SHORTGI_160) &&
                    (bw == IEEE80211_CWM_WIDTH160)));
    } else {
        sgi = wlan_get_param(vap, IEEE80211_SHORT_GI);
    }

    if (rate_mode != IEEE80211_FIXED_RATE_NONE) {
        /* Get rates for fixed rate */
        u_int32_t nbps = 0; /*Number of bits per symbol*/
        u_int32_t rc; /* rate code*/
        u_int32_t i;

        /* For fixed rate ensure that SGI is enabled by user */
        if((vap->iv_cur_mode < IEEE80211_MODE_11AXA_HE20) ||
           (ni->ni_phymode < IEEE80211_MODE_11AXA_HE20) ||
           (rate_mode != IEEE80211_FIXED_RATE_HE))
            sgi = vap->iv_data_sgi;
        else
            sgi = vap->iv_he_data_sgi;

        switch (rate_mode)
        {
            case IEEE80211_FIXED_RATE_MCS:
                nss = HT_RC_2_STREAMS(vap->iv_fixed_rateset);
                rc = wlan_get_param(vap, IEEE80211_FIXED_RATE);
                mcs = (rc & 0x07);
                for (i = 0; i < NUM_VHT_HT_RATES; i++) {
                    if (vht_ht_tbl[i][BW_COL] == bw &&
                        vht_ht_tbl[i][MCS_COL] == mcs) {
                        nbps = vht_ht_tbl[i][NBPS_COL];
                    }
                }
                break;
            case IEEE80211_FIXED_RATE_VHT:
                nss = iv_nss;
                mcs = wlan_get_param(vap, IEEE80211_FIXED_VHT_MCS);
                for (i = 0; i < NUM_VHT_HT_RATES; i++) {
                    if (vht_ht_tbl[i][BW_COL] == bw &&
                        vht_ht_tbl[i][MCS_COL] == mcs) {
                        nbps = vht_ht_tbl[i][NBPS_COL];
                    }
                }
                break;
            case IEEE80211_FIXED_RATE_HE:
                nss = iv_nss;
                mcs = wlan_get_param(vap, IEEE80211_FIXED_HE_MCS);
                for (i = 0; i < NUM_HE_RATES; i++) {
                    if (he_tbl[i][BW_COL] == bw &&
                        he_tbl[i][MCS_COL] == mcs) {
                        nbps = he_tbl[i][NBPS_COL];
                    }
                }
                break;
            case IEEE80211_FIXED_RATE_LEGACY:
                rc = wlan_get_param(vap, IEEE80211_FIXED_RATE);
                for (i = 0; i < NUM_LEGACY_RATES; i++) {
                    if (legacy_rate_idx[i][L_RC_COL] == (rc & 0xff)) {
                        return legacy_rate_idx[i][L_BPS_COL];
                    }
                }
                break;
            default:
                break;
        }

#ifndef PORT_SPIRENT_HK
        IEEE80211_DPRINTF(vap, IEEE80211_MSG_ASSOC,
                          "%s:%d Phymode = %d NSS = %d SGI =%d ",
                          __func__, __LINE__, curr_phy_mode, nss, sgi);
#endif

        if((curr_phy_mode < IEEE80211_MODE_11AXA_HE20) ||
            (rate_mode != IEEE80211_FIXED_RATE_HE)) {
            /* 3.2 - Legacy Symbol duaration */
            if (sgi) {
                 /* Legacy GI -0.4us
                 * 1/(3.2+0.4) = 1/3.6 ~=  10/36 ~= 5/18
                 * calculated in Kbps
                 */
                return (nbps * 5 * nss / 18) ;
            } else {
                /* Default Legacy GI - 0.8us
                 * 1/(3.2+0.8) = 1/4
                 */
                return (nbps * nss / 4) ;
            }
        } else {
                /* 12.8 - HE Symbol duaration */
            if (sgi == IEEE80211_GI_0DOT8_US) {
                /* HE Default GI - 0.8us
                 * 1/(12.8 + 0.8) = 1/13.6 ~= 10/136 = 5/68
                 * calculated in Kbps
                 */
                return (nbps * nss * 5 / 68);

            } else if (sgi == IEEE80211_GI_0DOT4_US) {
                /* HE  GI - 0.4 us
                 * 1/(12.8 + 0.4)= 1/13.2 ~= 10/132 = 5/66
                 * calculated in Kbps
                 */
                return (nbps * nss * 5 / 66 );

            } else if (sgi == IEEE80211_GI_1DOT6_US) {
                /* HE GI 1.6us
                 * 1/(12.8 + 1.6)= 1/14.4 ~= 10/144 = 5/72
                 * calculated in Kbps
                 */
                return (nbps * nss * 5 / 72 );

            } else if (sgi == IEEE80211_GI_3DOT2_US) {
                /* HE GI - 3.2 us
                 * 1/(12.8 + 3.2)= 1/16
                 * calculated in Kbps
                 */
                return (nbps * nss * 1 / 16 );
            }

          /* 11AX TODO ( Phase II) to address DCM Fixed rates display */
        }
    } else {
        /* Get rates for auto rate */
        nss = ni->ni_streams;
        he_ar_sgi = ol_ath_node_get_auto_rate_sgi(ni);
        if((IS_MODE_11AX(is_self_node, ni)) &&
               (vap->iv_he_ar_gi_ltf & IEEE80211_HE_AR_SGI_MASK)) {
            sgi = he_ar_sgi;
        }
        if(ieee80211_vap_get_opmode(vap) == IEEE80211_M_HOSTAP ||
           ieee80211_vap_get_opmode(vap) == IEEE80211_M_MONITOR) {
            nss = (iv_nss >
                   ieee80211_getstreams(ic, ic->ic_tx_chainmask)) ?
                   ieee80211_getstreams(ic, ic->ic_tx_chainmask) :
                   iv_nss;
        }
    }

    if (!is_self_node) {
        curr_phy_mode = get_phymode_from_chwidth(ic, ni);
        nss = ni->ni_streams;
    }

    saved_curr_phy_mode = curr_phy_mode;
    if (ic->ic_he_target) {
        switch(curr_phy_mode) {
            case IEEE80211_MODE_11AC_VHT20:
                 /* 1024QAM max rates for 11ACVHT20 mode are
                  * placed after (IEEE80211_MODE_11AXA_HE80_80
                  * + NG_VHT_MODES) in the max_rates table.
                  */
                curr_phy_mode = IEEE80211_MODE_11AXA_HE80_80 + NG_VHT_MODES + 1;
                break;
            case IEEE80211_MODE_11AC_VHT40:
            case IEEE80211_MODE_11AC_VHT40PLUS:
            case IEEE80211_MODE_11AC_VHT40MINUS:
                 /* 1024QAM max rates for 11ACVHT40PLUS mode are
                  * placed after (IEEE80211_MODE_11AXA_HE80_80
                  * + NG_VHT_MODES+2) in the max_rates table. Rates
                  * 11ACVHT40PLUS, 11ACVHT40MINUS and 11ACVHT40 are
                  * the same.
                  */
                curr_phy_mode = IEEE80211_MODE_11AXA_HE80_80 + NG_VHT_MODES + 3;
                break;
            case IEEE80211_MODE_11AC_VHT80:
                 /* 1024QAM max rates for 11ACVHT80 mode are
                  * placed after (IEEE80211_MODE_11AXA_HE80_80
                  * + NG_VHT_MODES+8) in the max_rates table.
                  */
                curr_phy_mode = IEEE80211_MODE_11AXA_HE80_80 + NG_VHT_MODES + 9;
                break;
            case IEEE80211_MODE_11AC_VHT160:
            case IEEE80211_MODE_11AC_VHT80_80:
                 /* 1024QAM max rates for 11ACVHT160 mode are
                  * placed after (IEEE80211_MODE_11AXA_HE80_80
                  * + NG_VHT_MODES+9) in the max_rates table.
                  * Rates for 11ACVHT160 and 11ACVHT80_80 are
                  * the same.
                  */
                curr_phy_mode = IEEE80211_MODE_11AXA_HE80_80 + NG_VHT_MODES + 10;
                break;
            default:
                /* Nothing to be done for now */
                break;
        }
    }

    if(ieee80211_vap_256qam_is_set(ni->ni_vap) &&
        (((ieee80211_vap_get_opmode(vap) == IEEE80211_M_HOSTAP) && (ic->ic_vhtcap)) ||
        ((ieee80211_vap_get_opmode(vap) == IEEE80211_M_STA) && (ni->ni_vhtcap)))) {
       switch(curr_phy_mode) {
          case IEEE80211_MODE_11NG_HT20:
              if(((ieee80211_vap_get_opmode(vap) == IEEE80211_M_HOSTAP) &&
                     (iv_ldpc == IEEE80211_HTCAP_C_LDPC_NONE)) ||
                      ((ieee80211_vap_get_opmode(vap) == IEEE80211_M_STA) &&
                        !((ni->ni_htcap & IEEE80211_HTCAP_C_ADVCODING) &&
                          (ni->ni_vhtcap & IEEE80211_VHTCAP_RX_LDPC)))) {
                 if (!ic->ic_he_target) {
                     /* 256QAM 2G BCC rate-set */
                     /* 2G NG_VHT rates are placed after
                      * (IEEE80211_MODE_11AXA_HE80_80) in the
                      * max_rates table
                      */
                     curr_phy_mode = IEEE80211_MODE_11AXA_HE80_80 + 1;
                 } else {
                     /* 1024QAM max rates are used even for 2G
                      * in Lithium target */
                     /* 1024QAM max rates for 11NGHT20 mode are
                      * placed after (IEEE80211_MODE_11AXA_HE80_80
                      * + NG_VHT_MODES + 1) in the max_rates table
                      */
                     curr_phy_mode = IEEE80211_MODE_11AXA_HE80_80 + NG_VHT_MODES + 2;
                 }
             } else {
                 if (!ic->ic_he_target) {
                     /* 256 QAM 2G LDPC rateset */
                     curr_phy_mode = IEEE80211_MODE_11AXA_HE80_80 + 2;
                 } else {
                     /* 1024QAM max rates are used even for 2G
                      * in Lithium target */
                     /* 1024QAM max rates for 11NGHT20 mode are
                      * placed after (IEEE80211_MODE_11AXA_HE80_80
                      * + NG_VHT_MODES + 1) in the max_rates table
                      */
                     curr_phy_mode = IEEE80211_MODE_11AXA_HE80_80 + NG_VHT_MODES + 2;
                 }
             }
          break;
          case IEEE80211_MODE_11NG_HT40PLUS:
          case IEEE80211_MODE_11NG_HT40MINUS:
          case IEEE80211_MODE_11NG_HT40:
             if (!ic->ic_he_target) {
                /* 256 QAM 2G */
                 curr_phy_mode = IEEE80211_MODE_11AXA_HE80_80 + 3;
             } else {
                 /* 1024QAM max rates are used even for 2G
                  * in Lithium target */
                 /* 1024QAM max rates for 11NGHT40PLUS mode are
                  * placed after (IEEE80211_MODE_11AXA_HE80_80
                  * + NG_VHT_MODES + 3) in the max_rates table.
                  * Rates for 11NGHT40PLUS, 11NGHT40MINUS and
                  * 11NGHT40 are the same.
                  */
                 curr_phy_mode = IEEE80211_MODE_11AXA_HE80_80 + NG_VHT_MODES + 4;
             }
          break;
          default:
          break;
       }
    }

    if (nss > ieee80211_getstreams(ic, ic->ic_tx_chainmask)) {
        nss = ieee80211_getstreams(ic, ic->ic_tx_chainmask);
    }

    if((saved_curr_phy_mode == IEEE80211_MODE_11AC_VHT160) ||
            (saved_curr_phy_mode == IEEE80211_MODE_11AC_VHT80_80) ||
            (saved_curr_phy_mode == IEEE80211_MODE_11AXA_HE160) ||
            (saved_curr_phy_mode == IEEE80211_MODE_11AXA_HE80_80)) {

        /* For cascade chipset , highest value used for nss is 2 . For future chipsets, nss 3 and 4 might
           be used with bw 160 . There we need to be careful with the size of _s32 value(struct iw_param ).
           This size is not enough for the rate values with nss 3 & 4 with 160 bw */

        u_int8_t nss_160 = 0;
        u_int32_t max_rate_160 = 0;
        u_int32_t max_rate_80 = 0;
        if (target_type == TARGET_TYPE_QCA9984 ){
                switch (ic->ic_tx_chainmask) {
                        case 5:    /* 0101 */
                        case 6:    /* 0110 */
                        case 9:    /* 1001 */
                        case 0xa:  /* 1010 */
                        case 0xc:  /* 1100 */
                                nss_160 = 1;
                                break;
                        case 7:    /* 0111 */ /* As per FR FR32731 we permit chainmask
                                                 0x7 for VHT160 and VHT80_80 mode */
                                nss_160 = 1;
                                break;
                        case 0xf:
                                nss_160 = 2;
                                break;
                        default:
                                break;
                }

                if(nss_160 > nss)
                    nss_160 = nss;

        } else if (target_type == TARGET_TYPE_QCA9888){
            if (nss > 1)
                nss = 1;
        } else if ((target_type == TARGET_TYPE_QCA8074) ||
                (target_type == TARGET_TYPE_QCA8074V2) ||
                (target_type == TARGET_TYPE_QCN9000) ||
                (target_type == TARGET_TYPE_QCA5018) ||
                (target_type == TARGET_TYPE_QCA6018)) {

            if(ic->ic_get_bw_nss_mapping) {
                if(ic->ic_get_bw_nss_mapping(vap, &nssmap, tx_chainmask)) {
                    nss_160 = 0;
                }
                else {
                    nss_160 = *nss_bw_160;
                }
            }

            if(nss_160 > nss) {
                nss_160 = nss;
            }

        } else {
             qdf_assert_always(0);
        }

        if(nss_160) {
            if((saved_curr_phy_mode == IEEE80211_MODE_11AC_VHT160) ||
                    (saved_curr_phy_mode == IEEE80211_MODE_11AC_VHT80_80)) {
                if(sgi) {
                    max_rate_160 =
                        max_rates[curr_phy_mode][nss_160 - 1][RATE_GI_0DOT4_US_IDX];
                    max_rate_80  =
                        max_rates[IEEE80211_MODE_11AC_VHT80][nss - 1][RATE_GI_0DOT4_US_IDX];
                } else {
                    max_rate_160 =
                        max_rates[curr_phy_mode][nss_160 - 1][RATE_GI_0DOT8_US_IDX];
                    max_rate_80  =
                        max_rates[IEEE80211_MODE_11AC_VHT80][nss - 1][RATE_GI_0DOT8_US_IDX];
                }

            }
            else {

                switch(sgi) {
                    case IEEE80211_GI_0DOT8_US:
                        max_rate_160 =
                            (max_rates[curr_phy_mode][nss_160 - 1][RATE_GI_0DOT8_US_IDX]);
                        max_rate_80 =
                            (max_rates[IEEE80211_MODE_11AXA_HE80][nss - 1][RATE_GI_0DOT8_US_IDX]);
                        break;
                    case IEEE80211_GI_0DOT4_US:
                        max_rate_160 =
                            (max_rates[curr_phy_mode][nss_160 - 1][RATE_GI_0DOT4_US_IDX]);
                        max_rate_80 =
                            (max_rates[IEEE80211_MODE_11AXA_HE80][nss - 1][RATE_GI_0DOT4_US_IDX]);
                        break;
                    case IEEE80211_GI_1DOT6_US:
                        max_rate_160 =
                            (max_rates[curr_phy_mode][nss_160 - 1][RATE_GI_1DOT6_US_IDX]);
                        max_rate_80 =
                            (max_rates[IEEE80211_MODE_11AXA_HE80][nss - 1][RATE_GI_1DOT6_US_IDX]);
                        break;
                    case IEEE80211_GI_3DOT2_US:
                        max_rate_160 =
                            (max_rates[curr_phy_mode][nss_160 - 1][RATE_GI_3DOT2_US_IDX]);
                        max_rate_80 =
                            (max_rates[IEEE80211_MODE_11AXA_HE80][nss - 1][RATE_GI_3DOT2_US_IDX]);
                        break;
                    default:
                        break;
                }

            }

            ratekbps = (max_rate_160 >= max_rate_80) ?
                        max_rate_160 : max_rate_80;

            IEEE80211_DPRINTF(vap, IEEE80211_MSG_ASSOC,
                              "%s:%d Phymode = %d NSS = %d SGI =%d  ratekbps=%d ",
                              __func__, __LINE__, curr_phy_mode, nss_160, sgi, ratekbps);

            return ratekbps;
        }
    }

    if (nss < 1) {
        IEEE80211_DPRINTF(vap, IEEE80211_MSG_ASSOC,
                          "%s: WARN: nss is; %d", __func__, nss);
        return 0;
    }


       /* Till 11AC VHT modes , only 0.8us and 0.4us GI are used */
    if((curr_phy_mode < IEEE80211_MODE_11AXA_HE20) ||
       /* 11NG VHT max rates are defined after 11AX mode rates*/
       (curr_phy_mode > IEEE80211_MODE_11AXA_HE80_80)) {
        if (sgi) {
            ratekbps = (max_rates[curr_phy_mode][nss - 1][RATE_GI_0DOT4_US_IDX]);
        } else {
            ratekbps =  (max_rates[curr_phy_mode][nss - 1][RATE_GI_0DOT8_US_IDX]);
        }
    } else  {
       /* 11AX/HE modes, 0.8us, 0.4us, 1.6us, 3.2us GI are used */
        if (sgi == IEEE80211_GI_0DOT8_US) {
            ratekbps = (max_rates[curr_phy_mode][nss - 1][RATE_GI_0DOT8_US_IDX]);
        } else if (sgi == IEEE80211_GI_0DOT4_US) {
            ratekbps = (max_rates[curr_phy_mode][nss - 1][RATE_GI_0DOT4_US_IDX]);
        } else if (sgi == IEEE80211_GI_1DOT6_US) {
            ratekbps = (max_rates[curr_phy_mode][nss - 1][RATE_GI_1DOT6_US_IDX]);
        } else {
            ratekbps = (max_rates[curr_phy_mode][nss - 1][RATE_GI_3DOT2_US_IDX]);
        }
    }

   /*
    * Applicable only to the modes which will be using only legacy rates (max phy rate :54 mbps)
    * This is to display max phy bit rate. In 11g, if user will disable 54 Mbps rate then
    * the VAP will come up in the next highest rate available.
    */

    if (vap->iv_disabled_legacy_rate_set && (ratekbps <= 54000)) {
        ratekbps = (((ni->ni_rates.rs_rates[ni->ni_rates.rs_nrates -1] & IEEE80211_RATE_VAL) * 1000) / 2);
    }

#ifndef PORT_SPIRENT_HK
    IEEE80211_DPRINTF(vap, IEEE80211_MSG_ASSOC,
                      "%s:%d Phymode = %d NSS = %d SGI =%d  ratekbps=%d ",
                      __func__, __LINE__, curr_phy_mode, nss, sgi,  ratekbps );
#endif

    return ratekbps;
}

struct ol_ath_ast_free_cb_params {
    uint8_t mac_addr[QDF_MAC_ADDR_SIZE];
    uint8_t peer_mac_addr[QDF_MAC_ADDR_SIZE];
    uint32_t flags;
    uint8_t pdev_id;
};

void (ol_ath_node_ast_free_cb)(struct cdp_ctrl_objmgr_psoc *ctrl_soc,
                               struct cdp_soc *cdp_soc,
                               void *cookie,
                               enum cdp_ast_free_status status)
{
    struct wlan_objmgr_psoc *psoc = (struct wlan_objmgr_psoc *)ctrl_soc;
    struct wlan_objmgr_pdev *pdev_obj = NULL;
    struct ol_ath_softc_net80211 *scn = NULL;
    struct ieee80211com *ic           = NULL;
    struct ieee80211_node *ni         = NULL;
    struct ol_ath_ast_free_cb_params *param = (struct ol_ath_ast_free_cb_params *)cookie;

    if (!cookie)
        return;

    if (status != CDP_TXRX_AST_DELETED) {
        qdf_mem_free(cookie);
        return;
    }

    pdev_obj = wlan_objmgr_get_pdev_by_id(psoc, param->pdev_id,
                                    WLAN_MLME_NB_ID);
    if (pdev_obj == NULL) {
        qdf_info("%s: pdev object (id: 0) is NULL", __func__);
        qdf_mem_free(cookie);
        return;
    }
    scn = lmac_get_pdev_feature_ptr(pdev_obj);
    if (scn == NULL) {
       qdf_info("%s: scn (id: 0) is NULL", __func__);
       goto out;
    }
    ic = &scn->sc_ic;

    ni = ieee80211_find_node(&ic->ic_sta, &param->peer_mac_addr[0], WLAN_MLME_SB_ID);
    if (!ni) {
       goto out;
    }

    cdp_peer_add_ast((struct cdp_soc_t *)cdp_soc,
                     wlan_vdev_get_id(ni->peer_obj->peer_objmgr.vdev), ni->ni_macaddr,
                     (uint8_t *)param->mac_addr, CDP_TXRX_AST_TYPE_WDS_HM,
                     param->flags);

out:
    if (ni)
        ieee80211_free_node(ni, WLAN_MLME_SB_ID);

    wlan_objmgr_pdev_release_ref(pdev_obj, WLAN_MLME_NB_ID);
    qdf_mem_free(cookie);
}

int ol_ath_node_add_ast_wds_entry(void *vdev_handle, const u_int8_t *dest_mac,
					u_int8_t *peer_mac, u_int32_t flags)
{
    struct ieee80211vap *vap          = NULL;
    struct ol_ath_softc_net80211 *scn = NULL;
    struct ieee80211com *ic           = NULL;
    struct ieee80211_node *ni         = NULL;
    ol_txrx_soc_handle soc_txrx_handle;
    uint32_t target_type;
    int status = -1;
    osif_dev *osdev                   = (osif_dev *)vdev_handle;
    uint8_t pdev_id;
    struct cdp_ast_entry_info ast_entry_info = {0};
    int ast_entry_found = 0;
#if DBDC_REPEATER_SUPPORT
    struct wlan_objmgr_pdev *pdev_obj;
#endif
    int vdev_id;

    vap = ol_ath_getvap(osdev);
    if (!vap) {
        return status;
    }

    scn = OL_ATH_SOFTC_NET80211(vap->iv_ic);
    ic = &scn->sc_ic;
    ni = ieee80211_find_node(&ic->ic_sta, peer_mac, WLAN_WDS_ID);
    if (!ni) {
        return status;
    }

    target_type = lmac_get_tgt_type(scn->soc->psoc_obj);

    soc_txrx_handle = wlan_psoc_get_dp_handle(scn->soc->psoc_obj);
    vdev_id = wlan_vdev_get_id(vap->vdev_obj);

    if (vap->iv_nawds.mode != 0) {
        status = cdp_peer_add_ast((struct cdp_soc_t *)soc_txrx_handle,
                                   vdev_id, ni->peer_obj->macaddr,
                (uint8_t *)dest_mac, CDP_TXRX_AST_TYPE_WDS, flags);
        ieee80211_free_node(ni, WLAN_WDS_ID);
        return status;
    }

    if (target_type != TARGET_TYPE_QCA8074) {
        struct ol_ath_ast_free_cb_params *param = NULL;

        param = (struct ol_ath_ast_free_cb_params*) qdf_mem_malloc(sizeof(struct ol_ath_ast_free_cb_params));
        if (param) {
            pdev_id = wlan_objmgr_pdev_get_pdev_id(ic->ic_pdev_obj);
            qdf_mem_copy(&param->mac_addr, dest_mac, QDF_MAC_ADDR_SIZE);
            qdf_mem_copy(&param->peer_mac_addr, peer_mac, QDF_MAC_ADDR_SIZE);
            param->pdev_id = pdev_id;
            param->flags = flags;

            /* Trigger delete for existing ast entry and register a callback to
	     * add the AST entry from AST unmap
	     *
	     * This also overwrites the existing callback if any already registered
	     * for this ast entry to make sure AST added with latest params
             */
            status = cdp_peer_ast_delete_by_pdev((struct cdp_soc_t *)soc_txrx_handle,
                                            (uint8_t *)dest_mac, pdev_id,
                                            ol_ath_node_ast_free_cb, param);
        }

        /* In case of AST entry found and delete is intiated the HMWDS entry will be added
         * from the callback
         *
         * If ast entry does not exist add it from here
         */
        if (status != QDF_STATUS_SUCCESS) {
            if(param)
                qdf_mem_free(param);

            status = cdp_peer_add_ast((struct cdp_soc_t *)soc_txrx_handle,
                                      vdev_id, ni->peer_obj->macaddr,
                                      (uint8_t *)dest_mac, CDP_TXRX_AST_TYPE_WDS_HM,
                                      flags);
        }
    } else {
        /*
         * HK V1 has a hardware issue that allows only 1 AST entry
         * for a given MAC address across the 2 radios in SoC, so
         * we are adding a WAR to ensure only 1 entry exists
         */
        pdev_id = wlan_objmgr_pdev_get_pdev_id(ic->ic_pdev_obj);
        ast_entry_found = cdp_peer_get_ast_info_by_pdev((struct cdp_soc_t *)soc_txrx_handle, (uint8_t *)dest_mac, pdev_id,
                                         &ast_entry_info);
        if (ast_entry_found) {
            if ((ast_entry_info.type == CDP_TXRX_AST_TYPE_SELF) ||
                (ast_entry_info.type == CDP_TXRX_AST_TYPE_STATIC) ||
                (ast_entry_info.type == CDP_TXRX_AST_TYPE_STA_BSS)) {
                    ieee80211_free_node(ni, WLAN_WDS_ID);
                    return status;
            } else if (ast_entry_info.type == CDP_TXRX_AST_TYPE_WDS) {
                cdp_peer_ast_delete_by_pdev((struct cdp_soc_t *)soc_txrx_handle,
                                            (uint8_t *)dest_mac, pdev_id,
                                            NULL, NULL);
                ast_entry_found = 0;
            }
        }

        if (!ast_entry_found) {
            ast_entry_found = cdp_peer_get_ast_info_by_soc((struct cdp_soc_t *)soc_txrx_handle, (uint8_t *)dest_mac,
                                                   &ast_entry_info);
            if (ast_entry_found) {
                if ((ast_entry_info.type == CDP_TXRX_AST_TYPE_SELF) ||
                    (ast_entry_info.type == CDP_TXRX_AST_TYPE_STATIC) ||
                    (ast_entry_info.type == CDP_TXRX_AST_TYPE_STA_BSS)) {
                        ieee80211_free_node(ni, WLAN_WDS_ID);
                        return status;
                } else if (ast_entry_info.type == CDP_TXRX_AST_TYPE_WDS) {
                    cdp_peer_ast_delete_by_soc(
                                        (struct cdp_soc_t *)soc_txrx_handle,
                                        (uint8_t *)dest_mac, NULL, NULL);
                    ast_entry_found = 0;
                }
            }

            /* If no AST entry exists, simply add AST and return */
            if (!ast_entry_found) {
                status = cdp_peer_add_ast((struct cdp_soc_t *)soc_txrx_handle,
                                           vdev_id, ni->peer_obj->macaddr,
                        (uint8_t *)dest_mac, CDP_TXRX_AST_TYPE_WDS_HM, flags);
                ieee80211_free_node(ni, WLAN_WDS_ID);
                return status;
            }
#if DBDC_REPEATER_SUPPORT
            if (!ic->ic_primary_radio) {
                status = cdp_peer_add_ast((struct cdp_soc_t *)soc_txrx_handle,
                                           vdev_id, ni->peer_obj->macaddr,
                        (uint8_t *)dest_mac, CDP_TXRX_AST_TYPE_WDS_HM_SEC, flags);
#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
                /*
                 * Sending wds add msg for AST HM secondary type explicitly,
                 *  as regular path NSS msg is skipped for HM_SEC
                 */
                if (ic->nss_radio_ops) {
                    ic->nss_radio_ops->ic_nss_ol_pdev_add_wds_peer(scn, vdev_id, ni->peer_obj->macaddr, CDP_INVALID_PEER,
                           (uint8_t *)dest_mac, NULL, CDP_TXRX_AST_TYPE_WDS_HM_SEC);
                }
#endif
                ieee80211_free_node(ni, WLAN_WDS_ID);
                return status;
            } else {
                /*
                 * If the current radio is primary, but the AST entry exists
                 * already for secondary radio, delete the existing entry
                 * and add new AST entries - 1 on primary radio as regular AST entry
                 * and other on secondary radio as HMWDS_SECONDARY entry (entry exists
                 * only on Host, and not installed on H/W)
                 */

                if ((ast_entry_info.type != CDP_TXRX_AST_TYPE_STATIC) &&
                    (ast_entry_info.type != CDP_TXRX_AST_TYPE_SELF) &&
                    (ast_entry_info.type != CDP_TXRX_AST_TYPE_STA_BSS)) {
                     cdp_peer_ast_delete_by_pdev(
                                (struct cdp_soc_t *)soc_txrx_handle,
                                (uint8_t *)dest_mac,
                                ast_entry_info.pdev_id,
                                NULL, NULL);
                } else {
                   ieee80211_free_node(ni, WLAN_WDS_ID);
                   return status;
                }

                /* Primary radio entry */
                status = cdp_peer_add_ast((struct cdp_soc_t *)soc_txrx_handle,
                                           vdev_id, ni->peer_obj->macaddr,
                        (uint8_t *)dest_mac, CDP_TXRX_AST_TYPE_WDS_HM, flags);

                ieee80211_free_node(ni, WLAN_WDS_ID);

                pdev_obj = wlan_objmgr_get_pdev_by_id(scn->soc->psoc_obj, ast_entry_info.pdev_id,
                                                WLAN_MLME_NB_ID);
                if (pdev_obj == NULL) {
                    qdf_info("%s: pdev object (id: 0) is NULL", __func__);
                    return 0;
                }
                scn = lmac_get_pdev_feature_ptr(pdev_obj);
                if (scn == NULL) {
                   qdf_info("%s: scn (id: 0) is NULL", __func__);
                   wlan_objmgr_pdev_release_ref(pdev_obj, WLAN_MLME_NB_ID);
                   return 0;
                }
                ic = &scn->sc_ic;

                ni = ieee80211_find_node(&ic->ic_sta, &ast_entry_info.peer_mac_addr[0], WLAN_WDS_ID);
                if (!ni) {
                   wlan_objmgr_pdev_release_ref(pdev_obj, WLAN_MLME_NB_ID);
                   return 0;
                }
                /* Secondary radio entry */
                status = cdp_peer_add_ast((struct cdp_soc_t *)soc_txrx_handle,
                                           vdev_id, ni->peer_obj->macaddr,
                        (uint8_t *)dest_mac, CDP_TXRX_AST_TYPE_WDS_HM_SEC, flags);

#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
                if (ic->nss_radio_ops) {
                    ic->nss_radio_ops->ic_nss_ol_pdev_add_wds_peer(scn, vdev_id, ni->peer_obj->macaddr, CDP_INVALID_PEER,
                                   (uint8_t *)dest_mac, NULL, CDP_TXRX_AST_TYPE_WDS_HM_SEC);
                }
#endif
                wlan_objmgr_pdev_release_ref(pdev_obj, WLAN_MLME_NB_ID);
            }
#endif
        }
    }

    ieee80211_free_node(ni, WLAN_WDS_ID);
    return status;
}

void
ol_ath_node_del_ast_wds_entry(void *vdev_handle, u_int8_t *dest_mac)
{
    struct ieee80211vap *vap          = NULL;
    struct ol_ath_softc_net80211 *scn = NULL;
    osif_dev *osdev                   = (osif_dev *)vdev_handle;
    struct cdp_ast_entry_info ast_entry_info = {0};
    ol_txrx_soc_handle soc_txrx_handle;
    uint8_t pdev_id;
    int ast_entry_found = 0;

    vap = ol_ath_getvap(osdev);
    if (!vap) {
        return;
    }

    scn = OL_ATH_SOFTC_NET80211(vap->iv_ic);
    soc_txrx_handle = wlan_psoc_get_dp_handle(scn->soc->psoc_obj);
    if(!ol_target_lithium(scn->soc->psoc_obj)) {
        soc_txrx_handle->ol_ops->peer_del_wds_entry((struct cdp_ctrl_objmgr_psoc *)scn->soc->psoc_obj,
                                                    wlan_vdev_get_id(vap->vdev_obj),
                                                    dest_mac, CDP_TXRX_AST_TYPE_WDS_HM, 1);
        return;
    }
    pdev_id =  wlan_objmgr_pdev_get_pdev_id(scn->sc_pdev);

    ast_entry_found = cdp_peer_get_ast_info_by_pdev((struct cdp_soc_t *)soc_txrx_handle, (uint8_t *)dest_mac,
                                       pdev_id, &ast_entry_info);
    if (ast_entry_found) {
        if ((ast_entry_info.type != CDP_TXRX_AST_TYPE_STATIC) &&
            (ast_entry_info.type != CDP_TXRX_AST_TYPE_SELF) &&
            (ast_entry_info.type != CDP_TXRX_AST_TYPE_STA_BSS))
            cdp_peer_ast_delete_by_pdev((struct cdp_soc_t *)soc_txrx_handle,
                                        (uint8_t *)dest_mac, pdev_id,
                                        NULL, NULL);
    } else
         QDF_TRACE(QDF_MODULE_ID_WDS, QDF_TRACE_LEVEL_INFO,
                   "ast_entry not found for MAC: %pM", dest_mac);
}

int
ol_ath_node_update_ast_wds_entry(void *vdev_handle, u_int8_t *wds_macaddr, u_int8_t *peer_macaddr, u_int32_t flags)
{
    struct ieee80211vap *vap          = NULL;
    struct ol_ath_softc_net80211 *scn = NULL;
    struct ieee80211com *ic           = NULL;
    osif_dev *osdev                   = (osif_dev *)vdev_handle;
    ol_txrx_soc_handle soc_txrx_handle;

    vap = ol_ath_getvap(osdev);
    if (!vap) {
        return -1;
    }

    scn = OL_ATH_SOFTC_NET80211(vap->iv_ic);
    soc_txrx_handle = wlan_psoc_get_dp_handle(scn->soc->psoc_obj);
    ic = &scn->sc_ic;


    if ((peer_macaddr != NULL) || (wds_macaddr != NULL)) {
        return cdp_peer_reset_ast((struct cdp_soc_t *)soc_txrx_handle,
                           (uint8_t *)wds_macaddr,
                           (uint8_t *)peer_macaddr, wlan_vdev_get_id(vap->vdev_obj));
    } else {
        return cdp_peer_reset_ast_table((struct cdp_soc_t *)soc_txrx_handle,
                                 wlan_vdev_get_id(vap->vdev_obj));
    }
}

int
ol_ath_node_add_wds_entry(struct cdp_ctrl_objmgr_psoc *soc, uint8_t vdev_id,
                          uint8_t *peer_mac, uint16_t peer_id, const u_int8_t *dest_mac,
                          u_int8_t *next_node_mac, u_int32_t flags, u_int8_t type)
{
    struct ieee80211vap *vap          = NULL;
    struct ol_ath_softc_net80211 *scn = NULL;
    struct peer_add_wds_entry_params param;
    struct wlan_objmgr_vdev *vdev =
           wlan_objmgr_get_vdev_by_id_from_psoc((struct wlan_objmgr_psoc *)soc, vdev_id,
                                                 WLAN_MLME_NB_ID);
    u_int8_t wmi_wds_flags = 0;
    struct wmi_unified *pdev_wmi_handle;
#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
    uint8_t pdev_id;
#endif

    if (!vdev)
        return -1;

    vap = wlan_vdev_get_vap(vdev);
    if (!vap) {
        wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_NB_ID);
        return -1;
    }

    scn = OL_ATH_SOFTC_NET80211(vap->iv_ic);
    pdev_wmi_handle = lmac_get_pdev_wmi_handle(scn->sc_pdev);
    qdf_mem_set(&param, sizeof(param), 0);
    if ((flags & IEEE80211_NODE_F_WDS_HM) || (scn->sc_ic.ic_wds_support)) {
        wmi_wds_flags |= WMI_HOST_WDS_FLAG_STATIC;
    } else {
        wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_NB_ID);
        /* Currently this interface is used only for host managed WDS entries */
        return -1;
    }

    param.dest_addr = dest_mac;
    param.peer_addr = (char *)peer_mac;
    param.flags = wmi_wds_flags;
    param.vdev_id = vdev_id;

#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
    if ((lmac_get_tgt_type(scn->soc->psoc_obj) == TARGET_TYPE_QCA8074) ||
            (lmac_get_tgt_type(scn->soc->psoc_obj) == TARGET_TYPE_QCA8074V2) ||
            (lmac_get_tgt_type(scn->soc->psoc_obj) == TARGET_TYPE_QCN9000) ||
            (lmac_get_tgt_type(scn->soc->psoc_obj) == TARGET_TYPE_QCA5018) ||
            (lmac_get_tgt_type(scn->soc->psoc_obj) == TARGET_TYPE_QCA6018)) {
        if (scn->sc_ic.nss_radio_ops) {
            scn->sc_ic.nss_radio_ops->ic_nss_ol_pdev_add_wds_peer(scn, vdev_id, (uint8_t *)peer_mac, peer_id, (uint8_t *)dest_mac,
                                 (uint8_t *)next_node_mac, type);
        }
    }
#endif

    if (wmi_unified_peer_add_wds_entry_cmd_send(pdev_wmi_handle, &param)) {
        qdf_print("%s:Unable to add wds entry", __func__);
#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
        if ((lmac_get_tgt_type(scn->soc->psoc_obj) == TARGET_TYPE_QCA8074) ||
                (lmac_get_tgt_type(scn->soc->psoc_obj) == TARGET_TYPE_QCA8074V2) ||
                (lmac_get_tgt_type(scn->soc->psoc_obj) == TARGET_TYPE_QCN9000) ||
                (lmac_get_tgt_type(scn->soc->psoc_obj) == TARGET_TYPE_QCA5018) ||
                (lmac_get_tgt_type(scn->soc->psoc_obj) == TARGET_TYPE_QCA6018)) {
            if (scn->sc_ic.nss_radio_ops) {
                pdev_id = wlan_objmgr_pdev_get_pdev_id(scn->sc_pdev);
                scn->sc_ic.nss_radio_ops->ic_nss_ol_pdev_del_wds_peer(scn,
                            (uint8_t *)dest_mac, (uint8_t *)dest_mac, CDP_TXRX_AST_TYPE_NONE, pdev_id);
            }
        }
#endif
        wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_NB_ID);
        return -1;
    }
    wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_NB_ID);
    return 0;
}

void
ol_ath_node_del_wds_entry(struct cdp_ctrl_objmgr_psoc *soc, uint8_t vdev_id,
                          u_int8_t *dest_mac, u_int8_t type, uint8_t delete_in_fw)
{
    struct ieee80211vap *vap          = NULL;
    struct ol_ath_softc_net80211 *scn = NULL;
    struct wlan_objmgr_vdev *vdev =
           wlan_objmgr_get_vdev_by_id_from_psoc((struct wlan_objmgr_psoc *)soc, vdev_id,
                                                 WLAN_MLME_NB_ID);
    struct peer_del_wds_entry_params param;
#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
    uint32_t tgt_type;
    uint8_t pdev_id;
#endif
    struct wmi_unified *pdev_wmi_handle;

    if (!vdev) {
        return;
    }

    vap = wlan_vdev_get_vap(vdev);
    if (!vap) {
        wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_NB_ID);
        return;
    }

    scn = OL_ATH_SOFTC_NET80211(vap->iv_ic);

#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
    lmac_get_pdev_target_type(scn->sc_pdev, &tgt_type);
    if ((tgt_type == TARGET_TYPE_QCA8074) ||
            (tgt_type == TARGET_TYPE_QCA8074V2) ||
            (tgt_type == TARGET_TYPE_QCN9000) ||
            (tgt_type == TARGET_TYPE_QCA5018) ||
            (tgt_type == TARGET_TYPE_QCA6018)) {
        if (scn->sc_ic.nss_radio_ops) {
            pdev_id = wlan_objmgr_pdev_get_pdev_id(scn->sc_pdev);
            scn->sc_ic.nss_radio_ops->ic_nss_ol_pdev_del_wds_peer(scn,
                    (uint8_t *)dest_mac, (uint8_t *)dest_mac, type, pdev_id);
        }
    }
#endif

    if (delete_in_fw && (type != CDP_TXRX_AST_TYPE_WDS_HM_SEC)) {

        pdev_wmi_handle = lmac_get_pdev_wmi_handle(scn->sc_pdev);
        qdf_mem_set(&param, sizeof(param), 0);
        param.dest_addr = dest_mac;
        param.vdev_id = vdev_id;

        if (wmi_unified_peer_del_wds_entry_cmd_send(pdev_wmi_handle, &param) !=
             QDF_STATUS_SUCCESS) {
            qdf_err("Unable to delete wds entry");
        } else {
            qdf_debug("Sent WDS del for mac %pM", dest_mac);
        }
    }
    wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_NB_ID);
}

int
ol_ath_node_update_wds_entry(struct cdp_ctrl_objmgr_psoc *soc, uint8_t vdev_id,
                             u_int8_t *wds_macaddr, u_int8_t *peer_macaddr, u_int32_t flags)
{
    struct ieee80211vap *vap          = NULL;
    struct wlan_objmgr_vdev *vdev =
           wlan_objmgr_get_vdev_by_id_from_psoc((struct wlan_objmgr_psoc *)soc, vdev_id,
                                                 WLAN_MLME_NB_ID);
    struct peer_update_wds_entry_params param;
#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
    struct ol_ath_softc_net80211 *scn = NULL;
    uint32_t tgt_type;
    uint8_t pdev_id;
#endif
    struct wmi_unified *pdev_wmi_handle;
    QDF_STATUS status;

    if (!vdev)
        return -1;

    vap = wlan_vdev_get_mlme_ext_obj(vdev);
    if (!vap) {
        wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_NB_ID);
        return -1;
    }

#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
    scn = OL_ATH_SOFTC_NET80211(vap->iv_ic);
    if (!scn) {
        wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_NB_ID);
        return -1;
    }

    lmac_get_pdev_target_type(scn->sc_pdev, &tgt_type);
    if ((tgt_type == TARGET_TYPE_QCA8074) ||
            (tgt_type == TARGET_TYPE_QCA8074V2) ||
            (tgt_type == TARGET_TYPE_QCN9000) ||
            (tgt_type == TARGET_TYPE_QCA5018) ||
            (tgt_type == TARGET_TYPE_QCA6018)) {
        if (scn->sc_ic.nss_radio_ops) {
            pdev_id = wlan_objmgr_pdev_get_pdev_id(scn->sc_pdev);
            scn->sc_ic.nss_radio_ops->ic_nss_ol_pdev_update_wds_peer(scn, (uint8_t *)peer_macaddr, (uint8_t *)wds_macaddr, pdev_id);
        }
    }
#endif

    pdev_wmi_handle = lmac_get_pdev_wmi_handle(vap->iv_ic->ic_pdev_obj);
    qdf_mem_set(&param, sizeof(param), 0);
    param.flags = (flags & IEEE80211_NODE_F_WDS_HM);
    param.wds_macaddr = wds_macaddr;
    param.peer_macaddr = peer_macaddr;
    param.vdev_id = vdev_id;

    status = wmi_unified_peer_update_wds_entry_cmd_send(pdev_wmi_handle, &param);

    wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_NB_ID);
    return status;
}

/*
 * ol_ath_node_chan_width:
 * Retrieve the information about the connected peers after
 * a channel width switch has occured and send it to the target to update
 * the bandwidth for transmission.
 *
 * Parameters:
 * @data: UMAC structure containing the peer information of all the connected
 *        peers
 * @vap:  Handle to the VAP
 *
 * Return:
 *  0:      Success
 * -EINVAL: Failure
 */
int
ol_ath_node_chan_width_switch(void *data,
                              struct ieee80211vap *vap)
{
    struct wmi_unified *pdev_wmi_handle = NULL;
    struct node_chan_width_switch_params *pi = (struct node_chan_width_switch_params *)data;
    struct peer_chan_width_switch_params param = {0};

    pdev_wmi_handle = lmac_get_pdev_wmi_handle(vap->iv_ic->ic_pdev_obj);
    if (!pdev_wmi_handle) {
        qdf_err("Could not get WMI handle");
        return -EINVAL;
    }

    if (!pi || !pi->chan_width_peer_list) {
        qdf_err("Allocation error for peer list");
        return -EINVAL;
    }

    /* Mapping compiled peer information from UMAC to the WMI structure
     * node_chan_width_switch_params is identical to node_chan_width_switch_params */
    param.chan_width_peer_list = (struct peer_chan_width_switch_info *)
                                                     pi->chan_width_peer_list;
    param.num_peers = pi->num_peers;

    if (wmi_unified_peer_chan_width_switch_cmd_send(pdev_wmi_handle, &param)) {
        qdf_err("Unable to send WMI peer chwidth info");
        return -EINVAL;
    }

    return 0;
}

static int
ol_ath_node_set_su_sounding_int(struct ieee80211_node *ni,
                                uint32_t sounding_interval)
{
    struct ieee80211com *ic = ni->ni_ic;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    struct ol_ath_vap_net80211 *avn = OL_ATH_VAP_NET80211(ni->ni_vap);
    int retval = 0;

    retval = ol_ath_node_set_param(scn, ni->ni_macaddr,
                    WMI_HOST_PEER_PARAM_SU_TXBF_SOUNDING_INTERVAL,
                    sounding_interval, avn->av_if_id);

    return retval;
}

#ifdef AST_HKV1_WORKAROUND
void wds_ast_free_assoc_cb(struct cdp_ctrl_objmgr_psoc *ctrl_psoc,
                          struct cdp_soc *dp_soc,
                          void *cookie,
                          enum cdp_ast_free_status status)
{
    enum wds_auth_defer_action action;
    if (status != CDP_TXRX_AST_DELETED)
        action = IEEE80211_AUTH_ABORT;
    else
        action = IEEE80211_AUTH_CONTINUE;

    wlan_wds_delete_response_handler((struct wlan_objmgr_psoc *)ctrl_psoc,
                                     cookie, action);
}

int ol_ath_node_lookup_ast_wds_and_del(void *vdev_handle, uint8_t *mac,
        struct recv_auth_params_defer *auth_cookie)
{
    struct ieee80211vap *vap          = NULL;
    struct ol_ath_softc_net80211 *scn = NULL;
    ol_ath_soc_softc_t *soc;
    osif_dev *osdev                   = (osif_dev *)vdev_handle;
    ol_txrx_soc_handle soc_txrx_handle;
    struct ieee80211com *ic = NULL;

    vap = ol_ath_getvap(osdev);
    if (!vap) {
        return -1;
    }

    ic = vap->iv_ic;
    scn = OL_ATH_SOFTC_NET80211(ic);
    soc = scn->soc;
    soc_txrx_handle = wlan_psoc_get_dp_handle(soc->psoc_obj);

    /* When a STA roams from RPTR AP to ROOT AP and vice versa, we need to
     * remove the AST entry which was earlier added as a WDS entry.
     */
    return cdp_peer_ast_delete_by_soc((struct cdp_soc_t *)soc_txrx_handle, mac,
		                              wds_ast_free_assoc_cb, auth_cookie);
}
#endif

static int
ol_ath_node_set_mu_sounding_int(struct ieee80211_node *ni,
        uint32_t sounding_interval)
{
    struct ieee80211com *ic = ni->ni_ic;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    struct ol_ath_vap_net80211 *avn = OL_ATH_VAP_NET80211(ni->ni_vap);
    int retval = 0;

    retval = ol_ath_node_set_param(scn, ni->ni_macaddr,
            WMI_HOST_PEER_PARAM_MU_TXBF_SOUNDING_INTERVAL,
            sounding_interval, avn->av_if_id);

    return retval;
}

static int
ol_ath_node_enable_sounding_int(struct ieee80211_node *ni,
        uint32_t enable)
{
    struct ieee80211com *ic = ni->ni_ic;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    struct ol_ath_vap_net80211 *avn = OL_ATH_VAP_NET80211(ni->ni_vap);
    int retval = 0;

    retval = ol_ath_node_set_param(scn, ni->ni_macaddr,
            WMI_HOST_PEER_PARAM_TXBF_SOUNDING_ENABLE,
            enable, avn->av_if_id);

    return retval;
}

static int
ol_ath_node_sched_mu_enable(struct ieee80211_node *ni,
        u_int32_t enable)
{
    struct ieee80211com *ic = ni->ni_ic;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    struct ol_ath_vap_net80211 *avn = OL_ATH_VAP_NET80211(ni->ni_vap);
    int retval = 0;

    retval = ol_ath_node_set_param(scn, ni->ni_macaddr,
            WMI_HOST_PEER_PARAM_MU_ENABLE,
            enable, avn->av_if_id);

    return retval;
}

static int
ol_ath_node_sched_ofdma_enable(struct ieee80211_node *ni,
        u_int32_t enable)
{
    struct ieee80211com *ic = ni->ni_ic;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    struct ol_ath_vap_net80211 *avn = OL_ATH_VAP_NET80211(ni->ni_vap);
    int retval = 0;

    retval = ol_ath_node_set_param(scn, ni->ni_macaddr,
            WMI_HOST_PEER_PARAM_OFDMA_ENABLE,
            enable, avn->av_if_id);

    return retval;
}

int
ol_ath_node_delete_multiple_wds_entries(struct cdp_ctrl_objmgr_psoc *psoc, uint8_t vdev_id,
                                        u_int8_t *wds_macaddr, u_int8_t *peer_macaddr, u_int32_t flags)
{
    struct ieee80211vap *vap          = NULL;
    struct ol_ath_softc_net80211 *scn = NULL;
    struct wmi_unified *pdev_wmi_handle;
    struct peer_del_all_wds_entries_params param;
    QDF_STATUS status;
    struct wlan_objmgr_vdev *vdev =
           wlan_objmgr_get_vdev_by_id_from_psoc((struct wlan_objmgr_psoc *)psoc, vdev_id,
                                                 WLAN_MLME_NB_ID);
    if (!vdev) {
        return -1;
    }

    vap = wlan_vdev_get_mlme_ext_obj(vdev);
    if (!vap) {
        wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_NB_ID);
        return -1;
    }
    scn = OL_ATH_SOFTC_NET80211(vap->iv_ic);
    pdev_wmi_handle = lmac_get_pdev_wmi_handle(scn->sc_pdev);
    if (!pdev_wmi_handle) {
        wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_NB_ID);
        return -EINVAL;
    }
    qdf_mem_set(&param, sizeof(param), 0);
    param.flags = (flags & IEEE80211_NODE_F_WDS_HM);
    param.wds_macaddr = wds_macaddr;
    param.peer_macaddr = peer_macaddr;
    status = wmi_unified_peer_del_all_wds_entries_cmd_send(pdev_wmi_handle, &param);
    wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_NB_ID);

    return status;
}

#if ATH_SUPPORT_HYFI_ENHANCEMENTS
int
ol_ath_node_ext_stats_enable(struct ieee80211_node *ni, u_int32_t enable)
{
    struct ieee80211com *ic = ni->ni_ic;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    struct ol_ath_vap_net80211 *avn = OL_ATH_VAP_NET80211(ni->ni_vap);
    int retval = 0;
    struct wlan_objmgr_psoc *psoc = NULL;
    target_resource_config *tgt_cfg;

    psoc = scn->soc->psoc_obj;
    tgt_cfg = lmac_get_tgt_res_cfg(psoc);
    if (!tgt_cfg) {
        qdf_print("%s: psoc target res cfg is null", __func__);
        return -EINVAL;
    }


    qdf_spin_lock_bh(&scn->scn_lock);
    if (enable && scn->peer_ext_stats_count >= tgt_cfg->max_peer_ext_stats) {
        qdf_spin_unlock_bh(&scn->scn_lock);
        return -ENOMEM;
    }

    if ((retval = ol_ath_node_set_param(scn, ni->ni_macaddr,
                WMI_HOST_PEER_EXT_STATS_ENABLE, enable, avn->av_if_id)) == 0) {
        if (enable) {
            scn->peer_ext_stats_count++;
        } else {
            scn->peer_ext_stats_count--;
        }
    }
    qdf_spin_unlock_bh(&scn->scn_lock);

    return retval;
}
#if ATH_SUPPORT_HYFI_ENHANCEMENTS
static void
ol_ath_node_wrap_ald_stats(struct ieee80211_node *ni, struct cdp_peer_stats *peer_stats)
{
    if ((OL_ATH_NODE_NET80211(ni))->an_tx_cnt > peer_stats->tx.fw_tx_cnt)
        (OL_ATH_NODE_NET80211(ni))->an_tx_cnt = peer_stats->tx.fw_tx_cnt;
    if ((OL_ATH_NODE_NET80211(ni))->an_tx_bytes > peer_stats->tx.fw_tx_bytes)
        (OL_ATH_NODE_NET80211(ni))->an_tx_bytes = peer_stats->tx.fw_tx_bytes;
}

static int
ol_ath_node_prepare_ald_stats(struct ieee80211_node *ni)
{
    struct wlan_objmgr_psoc *psoc;
    struct cdp_peer_stats *peer_stats;
    uint8_t i;
    QDF_STATUS status;

    if (!ni || !ni->ni_ic || !ni->peer_obj) {
        qdf_print("%s ni or ic is null, return.", __func__);
        return -ENOENT;
    }
    psoc = wlan_pdev_get_psoc(ni->ni_ic->ic_pdev_obj);
    if (!psoc)
        return -ENOENT;

    if (!(peer_stats = qdf_mem_malloc(sizeof(struct cdp_peer_stats)))) {
        return -ENOMEM;
    }
    status = cdp_host_get_peer_stats(wlan_psoc_get_dp_handle(psoc),
                                     wlan_vdev_get_id(ni->peer_obj->peer_objmgr.vdev),
                                     ni->peer_obj->macaddr, peer_stats);
    if (QDF_IS_STATUS_ERROR(status)) {
        qdf_mem_free(peer_stats);
        return -ENOENT;
    }
    /* check if an_tx_cnt is less than peer_stats->tx.fw_tx_cnt,
     * to ensure peer_stats were not reset
     */
    ol_ath_node_wrap_ald_stats(ni, peer_stats);
    if ((OL_ATH_NODE_NET80211(ni))->an_tx_cnt != peer_stats->tx.fw_tx_cnt)
        (OL_ATH_NODE_NET80211(ni))->an_tx_cnt = peer_stats->tx.fw_tx_cnt - (OL_ATH_NODE_NET80211(ni))->an_tx_cnt;;

    if ((OL_ATH_NODE_NET80211(ni))->an_tx_bytes != peer_stats->tx.fw_tx_bytes)
        (OL_ATH_NODE_NET80211(ni))->an_tx_bytes = peer_stats->tx.fw_tx_bytes - (OL_ATH_NODE_NET80211(ni))->an_tx_bytes;

    if ((OL_ATH_NODE_NET80211(ni))->an_tx_rates_used != peer_stats->tx.last_tx_rate_used)
        (OL_ATH_NODE_NET80211(ni))->an_tx_rates_used = peer_stats->tx.last_tx_rate_used - (OL_ATH_NODE_NET80211(ni))->an_tx_rates_used;

    if ((OL_ATH_NODE_NET80211(ni))->an_ni_tx_rate != peer_stats->tx.rnd_avg_tx_rate)
        (OL_ATH_NODE_NET80211(ni))->an_ni_tx_rate = peer_stats->tx.rnd_avg_tx_rate;

    if ((OL_ATH_NODE_NET80211(ni))->an_tx_ratecount != peer_stats->tx.fw_ratecount)
        (OL_ATH_NODE_NET80211(ni))->an_tx_ratecount = peer_stats->tx.fw_ratecount - (OL_ATH_NODE_NET80211(ni))->an_tx_ratecount;

    if (ni->ni_ald.ald_txcount != peer_stats->tx.fw_txcount)
        ni->ni_ald.ald_txcount = peer_stats->tx.fw_txcount - ni->ni_ald.ald_txcount;

    if (ni->ni_ald.ald_max4msframelen != peer_stats->tx.fw_max4msframelen)
        ni->ni_ald.ald_max4msframelen = peer_stats->tx.fw_max4msframelen - ni->ni_ald.ald_max4msframelen;

    if (ni->ni_ald.ald_retries != peer_stats->tx.retries)
        ni->ni_ald.ald_retries = peer_stats->tx.retries - ni->ni_ald.ald_retries;

    for (i = 0; i < WME_AC_MAX; i++) {
	 if (ni->ni_ald.ald_ac_nobufs[i] != peer_stats->tx.ac_nobufs[i])
             ni->ni_ald.ald_ac_nobufs[i] = peer_stats->tx.ac_nobufs[i] - ni->ni_ald.ald_ac_nobufs[i];

	 if (ni->ni_ald.ald_ac_excretries[i] != peer_stats->tx.excess_retries_per_ac[i])
             ni->ni_ald.ald_ac_excretries[i] = peer_stats->tx.excess_retries_per_ac[i] - ni->ni_ald.ald_ac_excretries[i];
    }

    ni->ni_ald.ald_lastper = peer_stats->tx.last_per;

    qdf_mem_free(peer_stats);
    return 0;
}

static void ol_ath_node_collect_stats(struct ieee80211_node *ni)
{
    u_int32_t msdu_size, max_msdu_size;

    if (ol_ath_node_prepare_ald_stats(ni))
        return;

    ni->ni_ald.ald_phyerr = ni->ni_vap->iv_ald->phyerr_rate;

	ni->ni_ald.ald_capacity = (OL_ATH_NODE_NET80211(ni))->an_tx_cnt ?
					((OL_ATH_NODE_NET80211(ni))->an_tx_rates_used/ (OL_ATH_NODE_NET80211(ni))->an_tx_cnt) : 0;

	if(ni->ni_ald.ald_capacity == 0)
		ni->ni_ald.ald_capacity = qdf_do_div((OL_ATH_NODE_NET80211(ni))->an_ni_tx_rate, 1000);


	if(!(OL_ATH_NODE_NET80211(ni))->an_tx_bytes || !ni->ni_ald.ald_txcount)
		msdu_size = ALD_MSDU_SIZE;
	else
		msdu_size = (OL_ATH_NODE_NET80211(ni))->an_tx_bytes/ni->ni_ald.ald_txcount;

	if (msdu_size < DEFAULT_MSDU_SIZE)
		ni->ni_ald.ald_msdusize = ALD_MSDU_SIZE;
	else
		ni->ni_ald.ald_msdusize = msdu_size;
	max_msdu_size = (msdu_size > ALD_MSDU_SIZE) ? msdu_size : ALD_MSDU_SIZE;
	if ((OL_ATH_NODE_NET80211(ni))->an_tx_ratecount > 0)
	    ni->ni_ald.ald_avgmax4msaggr = ni->ni_ald.ald_max4msframelen / ((OL_ATH_NODE_NET80211(ni))->an_tx_ratecount * max_msdu_size);

    if(ni->ni_ald.ald_avgmax4msaggr > 192)
	    ni->ni_ald.ald_avgmax4msaggr = 192;

	if(ni->ni_ald.ald_avgmax4msaggr > 0)
		ni->ni_ald.ald_aggr = ni->ni_ald.ald_avgmax4msaggr/2;
	else
		ni->ni_ald.ald_aggr = 96; //Max aggr 192/2

	/* Avg Aggr should be atleast 1 */
	ni->ni_ald.ald_aggr = ni->ni_ald.ald_aggr > 1 ? ni->ni_ald.ald_aggr : 1;

}
#endif
static void ol_ath_node_reset_ald_stats(struct ieee80211_node *ni)
{
    struct wlan_objmgr_psoc *psoc;
    struct cdp_peer_stats *peer_stats;
    uint8_t i;
    QDF_STATUS status;

	/* Do not reset ald_avgmax4msaggr and ald_aggr.
	 Past values are used when there is no traffic */
    psoc = wlan_pdev_get_psoc(ni->ni_ic->ic_pdev_obj);
    if (!(peer_stats = qdf_mem_malloc(sizeof(struct cdp_peer_stats)))) {
        return;
    }
    status = cdp_host_get_peer_stats(wlan_psoc_get_dp_handle(psoc),
                                     wlan_vdev_get_id(ni->peer_obj->peer_objmgr.vdev),
                                     ni->peer_obj->macaddr, peer_stats);
    if (QDF_IS_STATUS_ERROR(status)) {
        qdf_mem_free(peer_stats);
        return;
    }

    (OL_ATH_NODE_NET80211(ni))->an_tx_cnt = peer_stats->tx.fw_tx_cnt;
    (OL_ATH_NODE_NET80211(ni))->an_tx_bytes = peer_stats->tx.fw_tx_bytes;
    (OL_ATH_NODE_NET80211(ni))->an_tx_rates_used = peer_stats->tx.last_tx_rate_used;
    (OL_ATH_NODE_NET80211(ni))->an_tx_ratecount = peer_stats->tx.fw_ratecount;
    ni->ni_ald.ald_max4msframelen = peer_stats->tx.fw_max4msframelen;
    ni->ni_ald.ald_txcount = peer_stats->tx.fw_txcount;
    ni->ni_ald.ald_retries = peer_stats->tx.retries;
    for (i = 0; i < WME_AC_MAX; i++) {
         ni->ni_ald.ald_ac_nobufs[i] = peer_stats->tx.ac_nobufs[i];
         ni->ni_ald.ald_ac_excretries[i] = peer_stats->tx.excess_retries_per_ac[i];
    }
    ni->ni_ald.ald_lastper = 0;
    ni->ni_ald.ald_phyerr = 0;
    ni->ni_ald.ald_msdusize = 0;
    ni->ni_ald.ald_capacity = 0;
    OS_MEMZERO(&ni->ni_ald.ald_ac_txpktcnt, sizeof(ni->ni_ald.ald_ac_txpktcnt));
    qdf_mem_free(peer_stats);
}

static void
ol_ath_node_set_bridge_mac_addr(struct ieee80211com *ic, u_int8_t *bridge_mac)
{
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    struct set_bridge_mac_addr_params param;
    struct wmi_unified *pdev_wmi_handle;

    pdev_wmi_handle = lmac_get_pdev_wmi_handle(scn->sc_pdev);
    qdf_mem_set(&param, sizeof(param), 0);
    param.bridge_addr = bridge_mac;

    if (wmi_unified_set_bridge_mac_addr_cmd_send(pdev_wmi_handle, &param)){
        qdf_print("%s:Unable to set bridge MAC address\n", __func__);
    }
}

static int
ol_ath_node_dump_wds_table(struct ieee80211com *ic)
{
    struct wmi_unified *pdev_wmi_handle;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);

    pdev_wmi_handle = lmac_get_pdev_wmi_handle(scn->sc_pdev);
    return wmi_unified_send_dump_wds_table_cmd(pdev_wmi_handle);
}

static int
ol_ath_node_use_4addr(struct ieee80211_node *ni)
{
    struct ieee80211com *ic = ni->ni_ic;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    struct ol_ath_vap_net80211 *avn = OL_ATH_VAP_NET80211(ni->ni_vap);

    return ol_ath_node_set_param(scn, ni->ni_macaddr, WMI_HOST_PEER_USE_4ADDR, 1, avn->av_if_id);
}

#endif /* #if ATH_SUPPORT_HYFI_ENHANCEMENTS */

static void
ol_ath_node_authorize(struct ieee80211_node *ni, u_int32_t authorize)
{
    struct ieee80211com *ic = ni->ni_ic;
    ol_txrx_soc_handle soc_txrx_handle;

    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    struct ol_ath_vap_net80211 *avn = OL_ATH_VAP_NET80211(ni->ni_vap);
    struct ieee80211vap *vap = ni->ni_vap;

    qdf_spin_lock_bh(&scn->scn_lock);

    soc_txrx_handle = wlan_psoc_get_dp_handle(scn->soc->psoc_obj);
    /* Authorize/unauthorize the peer */
    if (soc_txrx_handle)
        cdp_peer_authorize(soc_txrx_handle, wlan_vdev_get_id(vap->vdev_obj), ni->peer_obj->macaddr,
                authorize);
    else {
        qdf_spin_unlock_bh(&scn->scn_lock);
        qdf_warn("%s:soc or peer handle is NULL", __func__);
        return;
    }

    qdf_spin_unlock_bh(&scn->scn_lock);
    IEEE80211_DPRINTF(vap, IEEE80211_MSG_AUTH,
                " macaddr :%s authorize:%d\n", ether_sprintf(ni->ni_macaddr),authorize);


    if(ol_ath_node_set_param(scn,ni->ni_macaddr,WMI_HOST_PEER_AUTHORIZE,
        authorize,avn->av_if_id)) {
        IEEE80211_DPRINTF(vap, IEEE80211_MSG_AUTH,
                    "%s:Unable to authorize peer", __func__);
    }
}

void
ol_ath_node_update(struct ieee80211_node *ni)
{
}

static void
ol_ath_node_smps_update(
        struct ieee80211_node *ni,
        int smen,
        int dyn,
        int ratechg)
{
    struct ieee80211com *ic = ni->ni_ic;
    struct ol_ath_softc_net80211 *scn = OL_ATH_SOFTC_NET80211(ic);
    struct ol_ath_vap_net80211 *avn = OL_ATH_VAP_NET80211(ni->ni_vap);
    A_UINT32 value;

    if (smen) {
        value = WMI_HOST_PEER_MIMO_PS_NONE;
    } else if (dyn) {
        value = WMI_HOST_PEER_MIMO_PS_DYNAMIC;
    } else {
        value = WMI_HOST_PEER_MIMO_PS_STATIC;
    }

    (void)ol_ath_node_set_param(scn, ni->ni_macaddr,
            WMI_HOST_PEER_MIMO_PS_STATE, value, avn->av_if_id);
}


#if UMAC_SUPPORT_ADMCTL
static void
ol_ath_node_update_dyn_uapsd(struct ieee80211_node *ni, uint8_t ac, int8_t ac_delivery, int8_t ac_trigger)
{
	uint8_t i;
	uint8_t uapsd=0;
	struct ieee80211vap *vap = ni->ni_vap;

	if (ac_delivery <= WME_UAPSD_AC_MAX_VAL) {
		ni->ni_uapsd_dyn_delivena[ac] = ac_delivery;
	}

	if (ac_trigger <= WME_UAPSD_AC_MAX_VAL) {
		ni->ni_uapsd_dyn_trigena[ac] = ac_trigger;
	}
	for (i=0;i<WME_NUM_AC;i++) {
		if (ni->ni_uapsd_dyn_trigena[i] == -1) {
			if (ni->ni_uapsd_ac_trigena[i]) {
				uapsd |= WMI_HOST_UAPSD_AC_BIT_MASK(i,WMI_HOST_UAPSD_AC_TYPE_TRIG);
			}
		} else {
			if (ni->ni_uapsd_dyn_trigena[i]) {
				uapsd |= WMI_HOST_UAPSD_AC_BIT_MASK(i,WMI_HOST_UAPSD_AC_TYPE_TRIG);
			}
		}
	}

	for (i=0;i<WME_NUM_AC;i++) {
		if (ni->ni_uapsd_dyn_delivena[i] == -1) {
			if (ni->ni_uapsd_ac_delivena[i]) {
				uapsd |= WMI_HOST_UAPSD_AC_BIT_MASK(i,WMI_HOST_UAPSD_AC_TYPE_DELI);
			}
		} else {
			if (ni->ni_uapsd_dyn_delivena[i]) {
				uapsd |= WMI_HOST_UAPSD_AC_BIT_MASK(i,WMI_HOST_UAPSD_AC_TYPE_DELI);
			}
		}
	}

	(void)wmi_unified_set_ap_ps_param(OL_ATH_VAP_NET80211(vap),
             OL_ATH_NODE_NET80211(ni), WMI_HOST_AP_PS_PEER_PARAM_UAPSD, uapsd);
	return;
}
#endif /* UMAC_SUPPORT_ADMCTL */

int ol_ath_rel_ref_for_logical_del_peer(struct ieee80211vap *vap,
        struct ieee80211_node *ni, uint8_t *peer_mac_addr)
{
    struct wlan_objmgr_psoc *psoc;
    ol_txrx_soc_handle soc_txrx_handle;

    if (!qdf_atomic_read(&(ni->ni_fw_peer_delete_rsp_pending))) {
        IEEE80211_DPRINTF(vap, IEEE80211_MSG_PEER_DELETE,
                "peer_del_resp: dup del resp node: 0x%pK\n", ni);
        return -1;
    }

    psoc = wlan_vdev_get_psoc(vap->vdev_obj);
    soc_txrx_handle = wlan_psoc_get_dp_handle(psoc);

    cdp_cp_peer_del_response(soc_txrx_handle,
                             wlan_vdev_get_id(vap->vdev_obj),
                             peer_mac_addr);

    /* Notify UMAC handler */
    wlan_node_peer_delete_response_handler(vap, ni);

    /* mark peer delete in pending */
    qdf_atomic_set(&(ni->ni_fw_peer_delete_rsp_pending), 0);
    qdf_atomic_set(&(ni->ni_node_preserved), 0);

    /* Free the reference taken for peer delete response*/
    wlan_objmgr_free_node(ni, WLAN_MLME_OBJ_DEL_ID);

    return 0;
}

int ol_ath_find_logical_del_peer_and_release_ref(struct ieee80211vap *vap, uint8_t *peer_mac_addr)
{
    struct ieee80211_node *ni;

    /*
     * Find the node which is in logically deleted state and free the
     * reference so that it can be physically deleted
     */
    ni = _ieee80211_find_logically_deleted_node(&vap->iv_ic->ic_sta,
            peer_mac_addr, vap->iv_myaddr, WLAN_MLME_SB_ID);
    if (ni != NULL) {
        IEEE80211_DPRINTF(vap, IEEE80211_MSG_PEER_DELETE,
                "peer_del_resp: node logically deleted 0x%pK\n", ni);
        if (ol_ath_rel_ref_for_logical_del_peer(vap, ni, peer_mac_addr)) {
            /* Free the reference taken by _ieee80211_find_logically_deleted_node */
            ieee80211_free_node(ni, WLAN_MLME_SB_ID);
            return -1;
        }
        /* Free the reference taken by _ieee80211_find_logically_deleted_node */
        ieee80211_free_node(ni, WLAN_MLME_SB_ID);
    } else {
        IEEE80211_DPRINTF(vap, IEEE80211_MSG_PEER_DELETE,
                "peer_del_resp: already deleted\n");
        return -1;
    }

    return 0;
}

static int
ol_peer_delete_response_event_handler(ol_scn_t sc, u_int8_t *evt_buf, u_int32_t datalen)
{
    ol_ath_soc_softc_t *soc = (ol_ath_soc_softc_t *) sc;
    uint8_t peer_mac_addr[QDF_MAC_ADDR_SIZE];
    struct ieee80211vap *vap = NULL;
    struct wlan_objmgr_vdev *vdev;
    struct wmi_host_peer_delete_response_event event;
    struct wmi_unified *wmi_handle;
    struct ol_ath_softc_net80211 *scn;
    struct wlan_objmgr_pdev *pdev;
    bool is_connected_sta_peer;

    wmi_handle = lmac_get_wmi_hdl(soc->psoc_obj);
    if (!wmi_handle) {
        qdf_err("wmi_handle is null");
        return -EINVAL;
    }

    if (wmi_extract_peer_delete_response_event(wmi_handle, evt_buf, &event)) {
        qdf_err("Failed to extract Peer Delete response message ");
        return -1;
    }
    qdf_mem_copy(&peer_mac_addr, &event.mac_address.bytes[0], QDF_MAC_ADDR_SIZE);

    vdev = wlan_objmgr_get_vdev_by_id_from_psoc(soc->psoc_obj, event.vdev_id, WLAN_MLME_SB_ID);
    if (vdev == NULL) {
        qdf_err("peer_del_resp: mac: %s vdevid: %d Unable to find vdev",
                ether_sprintf(&peer_mac_addr[0]), event.vdev_id);
        return -EINVAL;
    }

    vap = wlan_vdev_get_mlme_ext_obj(vdev);
    if (!vap) {
        qdf_err("peer_del_resp: Null vap, vdev: 0x%pK, vdev_id: %d", vdev, wlan_vdev_get_id(vdev));
        wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);
        return -EINVAL;
    }

    pdev = wlan_vdev_get_pdev(vdev);
    if (!pdev) {
        qdf_err("peer_del_resp: Null pdev, vdev: 0x%pK, vdev_id: %d", vdev, wlan_vdev_get_id(vdev));
        wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);
        return -EINVAL;
    }

    scn = (struct ol_ath_softc_net80211 *)lmac_get_pdev_feature_ptr(pdev);
    if (!scn) {
        qdf_err("peer_del_resp: Null scn, vdev: 0x%pK, vdev_id: %d", vdev, wlan_vdev_get_id(vdev));
        wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);
        return -EINVAL;
    }


#ifdef QCA_SUPPORT_CP_STATS
    vdev_cp_stats_peer_delete_resp_inc(vap->vdev_obj, 1);
#endif
    IEEE80211_DPRINTF(vap, IEEE80211_MSG_PEER_DELETE,
            "peer_del_resp: mac: %s vdevid: %d\n",
            ether_sprintf(&peer_mac_addr[0]), event.vdev_id);

    ol_ath_find_logical_del_peer_and_release_ref(vap, peer_mac_addr);

    /*
     * If peer delete response is enabled, handle the peer_count increment
     * when peer_delete_response is received from FW. If peer delete
     * response is not enabled, this count increment is done on
     * receiving htt peer unmap event from FW
     */
    if (wmi_service_enabled(wmi_handle, wmi_service_sync_delete_cmds)) {
        is_connected_sta_peer = ((wlan_vdev_mlme_get_opmode(vdev) != QDF_STA_MODE)
            && !(IEEE80211_ADDR_EQ(peer_mac_addr, wlan_vdev_mlme_get_macaddr(vdev))));

        if (is_connected_sta_peer) {
            qdf_atomic_inc(&scn->peer_count);
        }
    }

    wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);

    return 0;
}

static int
ol_peer_sta_ps_state_change_handler(ol_scn_t sc, u_int8_t *data, u_int32_t datalen)
{
    ol_ath_soc_softc_t *soc = (ol_ath_soc_softc_t *) sc;
    struct ieee80211_node *ni;
    unsigned long diff_time;
    wmi_host_peer_sta_ps_statechange_event event;
    struct wlan_objmgr_peer *peer_obj;
    struct wmi_unified *wmi_handle;

    wmi_handle = lmac_get_wmi_hdl(soc->psoc_obj);
    if (!wmi_handle) {
        qdf_err("wmi_handle is null");
        return -EINVAL;
    }

    if(wmi_extract_peer_sta_ps_statechange_ev(wmi_handle, data, &event)) {
        qdf_print("Failed to fetch peer PS state change");
        return -1;
    }

    peer_obj = wlan_objmgr_get_peer_by_mac(soc->psoc_obj, event.peer_macaddr, WLAN_MLME_SB_ID);
    if (peer_obj == NULL) {
        qdf_print("%s:Unable to find peer object", __func__);
        return -1;
    }

    if (peer_obj->obj_state == WLAN_OBJ_STATE_LOGICALLY_DELETED) {
        wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);
        return -1;
    }

    ni = wlan_peer_get_mlme_ext_obj(peer_obj);
    if (!ni) {
        wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);
        return -1;
    }
    ni->ps_state = event.peer_ps_state;
    diff_time = qdf_get_system_timestamp() - ni->previous_ps_time;
    ni->previous_ps_time = diff_time;
    if (ni->ps_state == 0)
    {
        ni->ps_time += diff_time;
    }
    else if (ni->ps_state == 1)
    {
        ni->awake_time += diff_time;
    }
    wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);

    return 0;
}

#ifdef ATH_SUPPORT_QUICK_KICKOUT
int peer_sta_kickout(struct ol_ath_softc_net80211 *scn, A_UINT8 *peer_macaddr)
{
    struct ieee80211com *ic;
    struct ieee80211_node *ni;

#if UMAC_SUPPORT_ACFG
    acfg_event_data_t *acfg_event = NULL;
#endif

    if (!scn) {
        return -1;
    }
    ic = &scn->sc_ic;
    ni = ieee80211_find_node(&ic->ic_sta, peer_macaddr, WLAN_MLME_OBJMGR_ID);
    if (!ni) {
        return -1;
    }
    ieee80211_kick_node(ni);
#if UMAC_SUPPORT_ACFG
    acfg_event = (acfg_event_data_t *)qdf_mem_malloc( sizeof(acfg_event_data_t));
    if (acfg_event == NULL){
        ieee80211_free_node(ni, WLAN_MLME_OBJMGR_ID);
        return 0;
    }

    if(ni){
        memcpy(acfg_event->kick_node_mac, ni->ni_macaddr, QDF_MAC_ADDR_SIZE);
    }
    acfg_send_event(scn->netdev, scn->sc_osdev, WL_EVENT_TYPE_QUICK_KICKOUT, acfg_event);
    qdf_mem_free(acfg_event);
#endif

    ieee80211_free_node(ni, WLAN_MLME_OBJMGR_ID);

    return 0;
}
qdf_export_symbol(peer_sta_kickout);

static int
ol_peer_sta_kickout_event_handler(ol_scn_t sc, u_int8_t *data, u_int32_t datalen)
{
    /*if ATH_SUPPORT_QUICK_KICKOUT defined, once got kickout event from fw,
     do kickout the node, and send acfg event up
    */
    ol_ath_soc_softc_t *soc = (ol_ath_soc_softc_t *) sc;
    struct ol_ath_softc_net80211 *scn;
    struct ieee80211_node *ni;
    wmi_host_peer_sta_kickout_event kickout_event;
#if UMAC_SUPPORT_ACFG
    acfg_event_data_t *acfg_event = NULL;
#endif
    struct wlan_objmgr_peer *peer_obj;
    struct wlan_objmgr_vdev *vdev;
    struct ieee80211vap *vap;
    struct ol_ath_vap_net80211 *avn;
    struct wmi_unified *wmi_handle;
#ifdef PORT_SPIRENT_HK
#ifdef SPT_MULTI_CLIENTS
    u_int32_t vap_state = 0;
    u_int8_t pdev_id;
    struct wlan_objmgr_vdev *vdev_kickout;
    struct vdev_osif_priv *vdev_osifp_kickout = NULL;
    osif_dev  *osifp_kickout;
    struct net_device *netdev_kickout;
#endif // SPT_MULTI_CLIENTS
#ifdef SPT_NG
    struct wlan_objmgr_pdev *pdev;
#endif // SPT_NG
#endif

    wmi_handle = lmac_get_wmi_hdl(soc->psoc_obj);
    if (!wmi_handle) {
        qdf_err("wmi_handle is null");
        return -EINVAL;
    }

    if(wmi_extract_peer_sta_kickout_ev(wmi_handle, data, &kickout_event)) {
        qdf_print("Unable to extract kickout ev");
        return -1;
    }

    peer_obj = wlan_objmgr_get_peer_by_mac(soc->psoc_obj, kickout_event.peer_macaddr, WLAN_MLME_SB_ID);
    if (peer_obj == NULL) {
        qdf_print("%s: Unable to find peer object", __func__);
        return -1;
    }
    vdev = wlan_peer_get_vdev(peer_obj);
    vap = wlan_vdev_get_mlme_ext_obj(vdev);
#ifdef PORT_SPIRENT_HK
#ifdef SPT_MULTI_CLIENTS
    vap_state = wlan_vdev_mlme_get_state(vap->vdev_obj);
    qdf_print("Kickout: PEER MAC Address: %02x:%02x:%02x:%02x:%02x:%02x, VDEV MAC Address: %02x:%02x:%02x:%02x:%02x:%02x, \
              Reason: %d\n", kickout_event.peer_macaddr[0], kickout_event.peer_macaddr[1],
              kickout_event.peer_macaddr[2], kickout_event.peer_macaddr[3], kickout_event.peer_macaddr[4],
              kickout_event.peer_macaddr[5], kickout_event.vdev_macaddr[0], kickout_event.vdev_macaddr[1],
              kickout_event.vdev_macaddr[2], kickout_event.vdev_macaddr[3], kickout_event.vdev_macaddr[4],
              kickout_event.vdev_macaddr[5], kickout_event.reason);
#endif // SPT_MULTI_CLIENTS
#ifdef SPT_NG
    /**
    *   Noise generator mode :   
    *       Ignore kickout event.
    *       Becase Kickout event is generated as noise frames are never acked by peer.
    */
    pdev = vap->iv_ic->ic_pdev_obj;
    if (CHK_NG_ENABLE(pdev))
    {
        wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);
        return 0;
    }
#endif // SPT_NG
#ifdef SPT_MULTI_CLIENTS
    if ((kickout_event.reason == KICKOUT_REASON_THROTTLE_START) && (vap_state == WLAN_VDEV_S_UP)) {
        pdev_id = wlan_objmgr_pdev_get_pdev_id(wlan_vdev_get_pdev(vdev));
        vdev_kickout = wlan_objmgr_get_vdev_by_macaddr_from_psoc(soc->psoc_obj, pdev_id, kickout_event.vdev_macaddr, WLAN_MLME_SB_ID);
        if (vdev_kickout != NULL ) {
            struct timespec kickout_time;

            vdev_osifp_kickout = wlan_vdev_get_ospriv(vdev_kickout);
            if (!vdev_osifp_kickout) {
                wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);
                qdf_print("%s: Unable to find kickout os priv pointer", __func__);
                return -1;
            }
            osifp_kickout = (osif_dev *)(vdev_osifp_kickout->legacy_osif_priv);
            if (osifp_kickout == NULL) {
                wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);
                qdf_print("%s: Unable to find kickout OS IF pointer ", __func__);
                return -1;
            }
            netdev_kickout = osifp_kickout->netdev;
            if (netdev_kickout == NULL) {
                wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);
                qdf_print("%s: Unable to find kickout netdev", __func__);
                return -1;
            }
            getnstimeofday(&kickout_time);
            netdev_kickout->last_kickout_time = kickout_time.tv_sec;
            /* Set dormant flag on station device */
            netif_dormant_on(netdev_kickout);
            qdf_print("Kickout: Sta_name = %s, last_kickout_time = %llu\n", netdev_kickout->name, netdev_kickout->last_kickout_time);
        }
        wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);
        return 0;
    }
#endif // SPT_MULTI_CLIENTS
#endif
    avn = OL_ATH_VAP_NET80211(vap);
    if (!avn) {
        wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);
        qdf_print("%s: Unable to find VAP", __func__);
        return -1;
    }

    scn = avn->av_sc;

    ni = wlan_peer_get_mlme_ext_obj(peer_obj);
    if (!ni) {
        wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);
        return -1;
    }
    ieee80211_kick_node(ni);
#if UMAC_SUPPORT_ACFG
    acfg_event = (acfg_event_data_t *)qdf_mem_malloc( sizeof(acfg_event_data_t));
    if (acfg_event == NULL){
        wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);
        return 0;
    }

    if(ni){
        memcpy(acfg_event->kick_node_mac, ni->ni_macaddr, QDF_MAC_ADDR_SIZE);
    }
    acfg_send_event(scn->netdev, scn->sc_osdev, WL_EVENT_TYPE_QUICK_KICKOUT, acfg_event);
    qdf_mem_free(acfg_event);
#endif

    wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);

    return 0;
}
#else
static int
ol_peer_sta_kickout_event_handler(ol_scn_t sc, u_int8_t *data, u_int32_t datalen)
{
    /*if ATH_SUPPORT_QUICK_KICKOUT not defined, once got kickout event from fw,
     send acfg event up, let upper layer to decide what to do.
    */
    ol_ath_soc_softc_t *soc = (ol_ath_soc_softc_t *) sc;
    struct ol_ath_softc_net80211 *scn;
    struct ieee80211_node *ni;
    wmi_host_peer_sta_kickout_event kickout_event;
#if UMAC_SUPPORT_ACFG
    acfg_event_data_t *acfg_event = NULL;
    struct wlan_objmgr_vdev *vdev;
    struct ieee80211vap *vap;
    struct ol_ath_vap_net80211 *avn;
#endif
    struct wlan_objmgr_peer *peer_obj;
	struct wmi_unified *wmi_handle;

	wmi_handle = lmac_get_wmi_hdl(soc->psoc_obj);
    if (!wmi_handle) {
        qdf_err("wmi_handle is null");
        return -EINVAL;
    }

    if(wmi_extract_peer_sta_kickout_ev(wmi_handle, data, &kickout_event)) {
        qdf_print("Unable to extract kickout ev");
        return -1;
    }

    peer_obj = wlan_objmgr_get_peer_by_mac(soc->psoc_obj, kickout_event.peer_macaddr, WLAN_MLME_SB_ID);
    if (peer_obj == NULL) {
        qdf_print("%s: Unable to find peer object", __func__);
        return -1;
    }
    ni = wlan_peer_get_mlme_ext_obj(peer_obj);
    if (!ni) {
        wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);
        return -1;
    }
#if UMAC_SUPPORT_ACFG
    vdev = wlan_peer_get_vdev(peer_obj);
    vap = wlan_vdev_get_mlme_ext_obj(vdev);
    avn = OL_ATH_VAP_NET80211(vap);
    scn = avn->av_sc;

    acfg_event = (acfg_event_data_t *)qdf_mem_malloc( sizeof(acfg_event_data_t));
    if (acfg_event == NULL){
        wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);
        return 0;
    }

    if(ni){
        memcpy(acfg_event->kick_node_mac, ni->ni_macaddr, QDF_MAC_ADDR_SIZE);
    }
    acfg_send_event(scn->netdev, scn->sc_osdev, WL_EVENT_TYPE_QUICK_KICKOUT, acfg_event);
    qdf_mem_free(acfg_event);
#endif

    wlan_objmgr_peer_release_ref(peer_obj, WLAN_MLME_SB_ID);

    return 0;
}
#endif /* ATH_SUPPORT_QUICK_KICKOUT */

/* Intialization functions */
int
ol_ath_node_attach(struct ol_ath_softc_net80211 *scn, struct ieee80211com *ic)
{
    /* Register the umac callback functions */
    scn->soc->net80211_node_free = ic->ic_node_free;
    scn->soc->net80211_node_cleanup = ic->ic_node_cleanup;

    /* Register the node specific offload interface functions */
    ic->ic_node_alloc = ol_ath_node_alloc;
    ic->ic_node_free = ol_ath_node_free;
    ic->ic_preserve_node_for_fw_delete_resp =
        ol_ath_preserve_node_for_fw_delete_resp;
    ic->ic_node_cleanup = ol_ath_node_cleanup;
    ic->ic_node_getrssi = ol_ath_node_getrssi;
    ic->ic_node_getrate = ol_ath_node_getrate;
    ic->ic_node_psupdate = ol_ath_node_psupdate;
    ic->ic_get_maxphyrate = ol_ath_node_get_maxphyrate;
    ic->ic_node_add_wds_entry = ol_ath_node_add_ast_wds_entry;
    ic->ic_node_del_wds_entry = ol_ath_node_del_ast_wds_entry;
    ic->ic_node_update_wds_entry = ol_ath_node_update_ast_wds_entry;
#ifdef AST_HKV1_WORKAROUND
    ic->ic_node_lookup_wds_and_del = ol_ath_node_lookup_ast_wds_and_del;
#endif
#if ATH_SUPPORT_HYFI_ENHANCEMENTS
    ic->ic_node_ext_stats_enable = ol_ath_node_ext_stats_enable;
    ic->ic_reset_ald_stats = ol_ath_node_reset_ald_stats;
    ic->ic_collect_stats = ol_ath_node_collect_stats;
    ic->ic_node_set_bridge_mac_addr = ol_ath_node_set_bridge_mac_addr;
#endif
    ic->ic_node_authorize = ol_ath_node_authorize;
    ic->ic_sm_pwrsave_update = ol_ath_node_smps_update;
#if UMAC_SUPPORT_ADMCTL
    ic->ic_node_update_dyn_uapsd = ol_ath_node_update_dyn_uapsd;
#endif
    ic->ic_node_get_last_txpower = ol_ath_node_get_last_txpower;
#if ATH_SUPPORT_HYFI_ENHANCEMENTS
    ic->ic_node_dump_wds_table = ol_ath_node_dump_wds_table;
    ic->ic_node_use_4addr = ol_ath_node_use_4addr;
#endif
    ic->ic_node_set_su_sounding_int = ol_ath_node_set_su_sounding_int;
    ic->ic_node_set_mu_sounding_int = ol_ath_node_set_mu_sounding_int;
    ic->ic_node_enable_sounding_int = ol_ath_node_enable_sounding_int;
    ic->ic_node_sched_mu_enable = ol_ath_node_sched_mu_enable;
    ic->ic_node_sched_ofdma_enable = ol_ath_node_sched_ofdma_enable;
    ic->ic_node_chan_width_switch = ol_ath_node_chan_width_switch;
    return 0;
}

int ol_ath_node_soc_attach(ol_ath_soc_softc_t *soc)
{
    wmi_unified_t wmi_handle;

    wmi_handle = lmac_get_wmi_unified_hdl(soc->psoc_obj);
	/* register for STA kickout function */
    wmi_unified_register_event_handler(wmi_handle, wmi_peer_sta_kickout_event_id,
            ol_peer_sta_kickout_event_handler, WMI_RX_UMAC_CTX);
    wmi_unified_register_event_handler(wmi_handle, wmi_peer_sta_ps_statechg_event_id,
            ol_peer_sta_ps_state_change_handler, WMI_RX_UMAC_CTX);
    wmi_unified_register_event_handler(wmi_handle, wmi_peer_delete_response_event_id,
            ol_peer_delete_response_event_handler, WMI_RX_UMAC_CTX);

    return 0;
}

void
ol_rx_err(
    ol_pdev_handle pdev,
    u_int8_t vdev_id,
    u_int8_t *peer_mac_addr,
    int tid,
    u_int32_t tsf32,
    enum ol_rx_err_type err_type,
    qdf_nbuf_t rx_frame)
{
    struct ieee80211vap *vap;

    vap = ol_ath_pdev_vap_get((struct wlan_objmgr_pdev *)pdev, vdev_id);
    if(vap == NULL) {
        qdf_err("vap is NULL");
        return;
    }

    if (err_type == OL_RX_ERR_TKIP_MIC)
        ieee80211_notify_michael_failure(vap, peer_mac_addr, 0);

    ol_ath_release_vap(vap);
}
qdf_export_symbol(ol_rx_err);

int
ol_rx_intrabss_fwd_check(
        ol_pdev_handle pdev,
        u_int8_t vdev_id,
        u_int8_t *peer_mac_addr)
{
    struct ieee80211vap *vap = NULL;
    struct ieee80211_node *ni;
    struct ieee80211com *ic;
    int result = 0;

    vap = ol_ath_pdev_vap_get((struct wlan_objmgr_pdev *)pdev, vdev_id);
    if(!vap) {
        return 0;
    }
    ic = vap->iv_ic;
    ni = ieee80211_vap_find_node(vap, peer_mac_addr, WLAN_MLME_SB_ID);

    if(ni != NULL) {
        if (ni->ni_vap == vap &&
                ieee80211_node_is_authorized(ni) &&
                ni != vap->iv_bss)
        {
            result =1;
        }

    }
    else {
        ol_ath_release_vap(vap);
        return 0;
    }
    ieee80211_free_node(ni, WLAN_MLME_SB_ID);
    ol_ath_release_vap(vap);

    return result;
}
qdf_export_symbol(ol_rx_intrabss_fwd_check);

int ol_ath_peer_sta_kickout(struct cdp_ctrl_objmgr_psoc *psoc, uint16_t pdev_id, uint8_t *peer_mac)
{
    struct ol_ath_softc_net80211 *scn;
    struct wlan_objmgr_pdev *pdev_obj = wlan_objmgr_get_pdev_by_id((struct wlan_objmgr_psoc *)psoc,
                                                                    pdev_id, WLAN_MLME_SB_ID);

    scn = (struct ol_ath_softc_net80211 *)lmac_get_pdev_feature_ptr(pdev_obj);

    if (scn)
        peer_sta_kickout(scn, peer_mac);

    wlan_objmgr_pdev_release_ref(pdev_obj, WLAN_MLME_SB_ID);
    return 0;
}

int ol_ath_peer_unref_delete(struct cdp_ctrl_objmgr_psoc *psoc, uint8_t pdev_id,
        uint8_t *peer_mac, uint8_t *vdev_mac, enum wlan_op_mode opmode)
{
    struct ol_ath_softc_net80211 *scn;
    struct wlan_objmgr_pdev *pdev_obj = wlan_objmgr_get_pdev_by_id((struct wlan_objmgr_psoc *)psoc,
                                                                    pdev_id, WLAN_MLME_SB_ID);
    bool is_connected_sta_peer = 0;
    struct wmi_unified *wmi_handle;

    if (!pdev_obj)
        return -1;

    wmi_handle = lmac_get_wmi_hdl((struct wlan_objmgr_psoc *)psoc);
    if (!wmi_handle) {
        qdf_err("wmi_handle is null");
        return -EINVAL;
    }

    scn = (struct ol_ath_softc_net80211 *)lmac_get_pdev_feature_ptr(pdev_obj);

    /*
     * If peer delete response is not enabled, handle the peer_count
     * increment on receiving htt peer unmap event from FW. If peer delete
     * response is enabled, this count increment is done on receiving peer
     * delete response from FW.
     */
    if (!wmi_service_enabled(wmi_handle, wmi_service_sync_delete_cmds)) {
        is_connected_sta_peer = ((opmode != wlan_op_mode_sta)
            && !(IEEE80211_ADDR_EQ(peer_mac, vdev_mac)));

        if (is_connected_sta_peer) {
            qdf_atomic_inc(&scn->peer_count);
        }
    }

    wlan_objmgr_pdev_release_ref(pdev_obj, WLAN_MLME_SB_ID);
    return 0;
}

void
ol_ath_update_dp_stats(void *soc, enum WDI_EVENT event, void *stats, uint16_t id, uint32_t type)
{
    struct ol_ath_softc_net80211 *scn;
    struct wlan_objmgr_pdev *pdev_obj = (struct wlan_objmgr_pdev *)soc;

    scn = (struct ol_ath_softc_net80211 *)lmac_get_pdev_feature_ptr(pdev_obj);
    if (scn && scn->soc->ol_if_ops->update_dp_stats)
        scn->soc->ol_if_ops->update_dp_stats(soc, stats, id, type);
}
qdf_export_symbol(ol_ath_update_dp_stats);

int ol_peer_map_event(struct cdp_ctrl_objmgr_psoc *psoc, uint16_t peer_id,
                uint16_t hw_peer_id, uint8_t vdev_id, uint8_t *peer_mac_addr,
                enum cdp_txrx_ast_entry_type peer_type, uint32_t tx_ast_hash)
{
#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
    uint16_t ol_peer_id = peer_id;
    ol_ath_soc_softc_t *soc  = (ol_ath_soc_softc_t *)
                      lmac_get_psoc_feature_ptr((struct wlan_objmgr_psoc *)psoc);
    if (soc && soc->nss_soc.ops) {
        if ((peer_type == CDP_TXRX_AST_TYPE_STATIC) || (peer_type == CDP_TXRX_AST_TYPE_STA_BSS)) {
            soc->nss_soc.ops->nss_soc_wifi_peer_create(soc, peer_id, hw_peer_id, vdev_id, peer_mac_addr, tx_ast_hash);
            return 0;
        } else if (peer_type == CDP_TXRX_AST_TYPE_MEC) {
            ol_peer_id = NSS_WIFILI_MEC_PEER_ID;
        } else if (peer_type == CDP_TXRX_AST_TYPE_DA) {
            ol_peer_id = NSS_WIFILI_DA_PEER_ID;
        }
        soc->nss_soc.ops->nss_soc_map_wds_peer(soc, ol_peer_id, hw_peer_id, vdev_id, peer_mac_addr);
    }
#endif
    return 0 ;
}

int ol_peer_unmap_event(struct cdp_ctrl_objmgr_psoc *psoc, uint16_t peer_id, uint8_t vdev_id)
{
#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
    ol_ath_soc_softc_t *soc  = (ol_ath_soc_softc_t *)lmac_get_psoc_feature_ptr((struct wlan_objmgr_psoc *)psoc);
    if (soc && soc->nss_soc.ops) {
        soc->nss_soc.ops->nss_soc_wifi_peer_delete(soc, peer_id, vdev_id);
    }
#endif
    return 0 ;
}

int ol_ath_pdev_update_lmac_n_target_pdev_id(struct cdp_ctrl_objmgr_psoc *psoc,
            uint8_t *pdev_id, uint8_t *lmac_id, uint8_t *target_pdev_id)
{
#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
    uint32_t tgt_type;
    struct ol_ath_softc_net80211 *scn;
    struct wlan_objmgr_pdev *pdev_obj = wlan_objmgr_get_pdev_by_id((struct wlan_objmgr_psoc *)psoc,
                                                                    *pdev_id, WLAN_MLME_SB_ID);
    if (!pdev_obj)
        return -1;

    scn = (struct ol_ath_softc_net80211 *)lmac_get_pdev_feature_ptr(pdev_obj);
    if (scn == NULL)
        return -1;

    lmac_get_pdev_target_type(scn->sc_pdev, &tgt_type);
    if ((tgt_type == TARGET_TYPE_QCA8074) ||
        (tgt_type == TARGET_TYPE_QCA8074V2) ||
        (tgt_type == TARGET_TYPE_QCA6018) ||
        (tgt_type == TARGET_TYPE_QCA5018) ||
        (tgt_type == TARGET_TYPE_QCN9000)) {

        if (scn->sc_ic.nss_radio_ops) {
            scn->sc_ic.nss_radio_ops->ic_nss_ol_pdev_update_lmac_n_target_pdev_id(scn, pdev_id, lmac_id, target_pdev_id);
        }

    }
    wlan_objmgr_pdev_release_ref(pdev_obj, WLAN_MLME_SB_ID);
    return 0;
#endif
    return 0;

}

int ol_peer_ast_flowid_map(struct cdp_ctrl_objmgr_psoc *soc_handle, uint16_t peer_id,
                uint8_t vdev_id, uint8_t *peer_mac_addr)
{
#ifdef QCA_NSS_WIFI_OFFLOAD_SUPPORT
    ol_ath_soc_softc_t *soc  = (ol_ath_soc_softc_t *)lmac_get_psoc_feature_ptr(
                                        (struct wlan_objmgr_psoc *)soc_handle);
    if (soc && soc->nss_soc.ops) {
        soc->nss_soc.ops->nss_soc_wifi_peer_ast_flowid_map(soc, peer_id, vdev_id, peer_mac_addr);
    }
#endif
    return 0 ;
}

#ifdef FEATURE_NAC_RSSI
uint8_t ol_ath_rx_invalid_peer(struct cdp_ctrl_objmgr_psoc *psoc, uint8_t pdev_id, void *msg)
{
    void *scn;
    struct wlan_objmgr_pdev *pdev_obj = wlan_objmgr_get_pdev_by_id((struct wlan_objmgr_psoc *)psoc,
                                                                   pdev_id, WLAN_MLME_SB_ID);

    if (!pdev_obj)
        return -1;

    scn = (struct ol_ath_softc_net80211 *)lmac_get_pdev_feature_ptr(pdev_obj);
    rx_dp_peer_invalid(scn, WDI_EVENT_RX_PEER_INVALID, msg, CDP_INVALID_PEER);

    wlan_objmgr_pdev_release_ref(pdev_obj, WLAN_MLME_SB_ID);
    return 0;
}
#endif

void ol_ath_rx_mic_error(struct cdp_ctrl_objmgr_psoc *psoc, uint8_t pdev_id, struct cdp_rx_mic_err_info *info)
{
    struct ieee80211vap *vap = NULL;
    struct wlan_objmgr_vdev *vdev;

    vdev = wlan_objmgr_get_vdev_by_id_from_psoc((struct wlan_objmgr_psoc *)psoc, info->vdev_id, WLAN_MLME_SB_ID);
    if(vdev == NULL) {
        qdf_err("%s: vdev is NULL, not processing mic error ", __func__);
        return;
    }

    vap = wlan_vdev_get_vap(vdev);
    if(vap == NULL) {
       qdf_err("vap is NULL, not processing mic error ");
       wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);
       return;
    }

    ieee80211_notify_michael_failure(vap, info->ta_mac_addr.bytes, 0);
    wlan_objmgr_vdev_release_ref(vdev, WLAN_MLME_SB_ID);
    return;
}

#endif

