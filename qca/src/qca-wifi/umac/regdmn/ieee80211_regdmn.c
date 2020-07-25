/*
 * Copyright (c) 2011,2017-2020 Qualcomm Innovation Center, Inc.
 * All Rights Reserved
 * Confidential and Proprietary - Qualcomm Innovation Center, Inc.
 *
 * 2011 Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Qualcomm Atheros Confidential and Proprietary.
 * 
 *  Copyright (c) 2008 Atheros Communications Inc.
 * All Rights Reserved.
 */

/*
 * Regulatory domain and DFS implementation
 */

#include <osdep.h>
#include <ieee80211_regdmn.h>
#include <ieee80211_channel.h>
#if UMAC_SUPPORT_CFG80211
#include <ieee80211_cfg80211.h>
#endif
#include <wlan_mlme_dispatcher.h>
#include <ieee80211_mlme_dfs_dispatcher.h>
#include <wlan_objmgr_psoc_obj.h>
#include <wlan_objmgr_pdev_obj.h>
#include <reg_services_public_struct.h>
#include <wlan_reg_services_api.h>
#include <ieee80211_mlme_priv.h>
#include <wlan_lmac_if_api.h>
#include "ieee80211_regdmn_dispatcher.h"
#include <wlan_reg_services_api.h>

static int outdoor = FALSE;        /* enable outdoor use */
static int indoor  = FALSE;        /* enable indoor use  */
static int tbl_idx = 0;            /* enable table index use per operating classes defined in Appendix E */

/* Difference between two channel numbers. Ex: 40 - 36 = 4 */
#define CHAN_DIFF 4

static int
ieee80211_set_countrycode(struct ieee80211com *ic, char isoName[3], u_int16_t cc, enum ieee80211_clist_cmd cmd);
static void regdmn_populate_channel_list_from_map(regdmn_op_class_map_t *map,
                          u_int8_t reg_class, struct ieee80211_node *ni);
static void wlan_notify_country_changed(void *arg, wlan_if_t vap) 
{
    char *country = (char*)arg;
    IEEE80211_DELIVER_EVENT_COUNTRY_CHANGE(vap, country);
}

int ieee80211_reg_program_opclass_tbl(struct ieee80211com *ic, uint8_t opclass)
{
    if (opclass > OPCLASS_TBL_MAX) {
        qdf_err("invalid opclass table index");
        return -EINVAL;
    }
    tbl_idx = opclass;
    return 0;
}
qdf_export_symbol(ieee80211_reg_program_opclass_tbl);

int ieee80211_reg_get_opclass_tbl(struct ieee80211com *ic, uint8_t *opclass)
{
    *opclass = tbl_idx;
    return 0;
}
qdf_export_symbol(ieee80211_reg_get_opclass_tbl);

int ieee80211_reg_program_cc(struct ieee80211com *ic,
        char *isoName, u_int16_t cc)
{
    struct cc_regdmn_s rd;

    qdf_mem_zero(&rd, sizeof(rd));

    rd.flags = INVALID_CC;
    if(isoName && isoName[0] && isoName[1]) {
        /* Map the ISO name ' ', 'I', 'O' */
        if (isoName[2] == 'O') {
            outdoor = true;
            indoor  = false;
            tbl_idx = 0;
        }
        else if (isoName[2] == 'I') {
            indoor  = true;
            outdoor = false;
            tbl_idx = 0;
        }
        else if ((isoName[2] == ' ') || (isoName[2] == 0)) {
            outdoor = false;
            indoor  = false;
            tbl_idx = 0;
        }
        else if ((isoName[2] >= '1') && (isoName[2] <= '9')) {
            outdoor = false;
            indoor  = false;
            tbl_idx = isoName[2] - '0';
            /* Convert ascii value to numeric one */
            isoName[2] = isoName[2] - '0';
        }
        rd.cc.alpha[0] = isoName[0];
        rd.cc.alpha[1] = isoName[1];
        rd.cc.alpha[2] = isoName[2];
        rd.flags = ALPHA_IS_SET;
    } else if (cc) {
        rd.cc.country_code = cc;
        rd.flags = CC_IS_SET;
    }

    return ieee80211_regdmn_program_cc(ic->ic_pdev_obj, &rd);
}
qdf_export_symbol(ieee80211_reg_program_cc);

/**
 * ieee80211_is_non_dfs_chans_available() - Compute the number of non-DFS.
 * channels available in the current regdomain.
 * @ic: Pointer to radio object.
 *
 * Return: Number of non-DFS channels.
 */
static int ieee80211_is_non_dfs_chans_available(struct ieee80211com *ic)
{
    int i, num_non_dfs_chans = 0;

    for (i = 0; i < ic->ic_nchans; i++) {
        if (!IEEE80211_IS_PRIMARY_OR_SECONDARY_CHAN_DFS(&ic->ic_channels[i])) {
            num_non_dfs_chans++;
        }
    }
    return num_non_dfs_chans;
}

int ieee80211_reg_get_current_chan_list(
        struct ieee80211com *ic,
        struct regulatory_channel *curr_chan_list)
{
    uint32_t num_chan;
    int i;
    int8_t max_tx_power = 0, min_tx_power = 0;
    uint32_t user_mode = 0, chip_mode, mode_select;
    qdf_freq_t low_2g, high_2g, low_5g, high_5g;
#if defined(WLAN_DFS_FULL_OFFLOAD) && defined(QCA_DFS_NOL_OFFLOAD)
    uint8_t num_non_dfs_chans;
#endif
    if(ic->ic_get_modeSelect) {
        user_mode = ic->ic_get_modeSelect(ic);
    }

    ieee80211_regdmn_get_chip_mode(ic->ic_pdev_obj, &chip_mode);
    ieee80211_regdmn_get_freq_range(ic->ic_pdev_obj, &low_2g, &high_2g, &low_5g, &high_5g);

    mode_select = (user_mode & chip_mode);

    qdf_mem_zero(ic->ic_channels, sizeof(ic->ic_channels));

    regdmn_update_ic_channels(ic->ic_pdev_obj, ic,
            mode_select,
            curr_chan_list,
            ic->ic_channels,
            IEEE80211_CHAN_MAX,
            &num_chan,
            low_2g,
            high_2g,
            low_5g,
            high_5g);

#if defined(WLAN_DFS_FULL_OFFLOAD) && defined(QCA_DFS_NOL_OFFLOAD)
    /* In case of scan failure event from FW due to dfs violation,
     * rebuilding channel list to have only non-DFS channel. If a regdomain
     * has only DFS channel, then bring down the vaps.
     */
    if (ic->ic_is_dfs_scan_violation) {
        num_non_dfs_chans = ieee80211_is_non_dfs_chans_available(ic);

        if (num_non_dfs_chans == 0) {
            ic->no_chans_available = 1;
            ieee80211_bringdown_vaps(ic);
            return -1;
        }
    }
#endif

    if (ic->ic_nchans == 0) {
        return -1;
    }

    if(ic->ic_get_min_and_max_power)
        ic->ic_get_min_and_max_power(ic, &max_tx_power, &min_tx_power);

    for (i = 0; i < ic->ic_nchans; i++) {
        if (ic->ic_is_mode_offload(ic)) {
            ic->ic_channels[i].ic_maxpower = max_tx_power;
            ic->ic_channels[i].ic_minpower = min_tx_power;
        }
    }

    if(ic->ic_fill_hal_chans_from_reg_db)
            ic->ic_fill_hal_chans_from_reg_db(ic);

    return 0;
}
qdf_export_symbol(ieee80211_reg_get_current_chan_list);

int ieee80211_reg_create_ieee_chan_list(
        struct ieee80211com *ic)
{
    struct regulatory_channel *curr_chan_list;
    int err;

    curr_chan_list = qdf_mem_malloc(NUM_CHANNELS*sizeof(struct regulatory_channel));
    if(curr_chan_list == NULL) {
        qdf_print("%s: fail to alloc", __func__);
        return -1;
    }

    ieee80211_regdmn_get_current_chan_list(ic->ic_pdev_obj, curr_chan_list);
    err = ieee80211_reg_get_current_chan_list(ic, curr_chan_list);
    qdf_mem_free(curr_chan_list);

    return err;
}
qdf_export_symbol(ieee80211_reg_create_ieee_chan_list);

uint16_t ieee80211_getCurrentCountry(struct ieee80211com *ic)
{
    struct cc_regdmn_s rd;

    rd.flags = CC_IS_SET;
    ieee80211_regdmn_get_current_cc(ic->ic_pdev_obj, &rd);

    return rd.cc.country_code;
}
qdf_export_symbol(ieee80211_getCurrentCountry);

uint16_t ieee80211_getCurrentCountryISO(struct ieee80211com *ic, char *str)
{
    struct cc_regdmn_s rd;

    rd.flags = ALPHA_IS_SET;
    ieee80211_regdmn_get_current_cc(ic->ic_pdev_obj, &rd);
    qdf_mem_copy(str, rd.cc.alpha, sizeof(rd.cc.alpha));

    if (str[0] && str[1]) {
        if (outdoor)
            str[2] = 'O';
        else if (indoor)
            str[2] = 'I';
        else if (tbl_idx)
            str[2] = tbl_idx;
        else
            str[2] = ' ';
    }

    return 0;
}
qdf_export_symbol(ieee80211_getCurrentCountryISO);

uint16_t ieee80211_get_regdomain(struct ieee80211com *ic)
{
    struct cc_regdmn_s rd;

    rd.flags = REGDMN_IS_SET;
    ieee80211_regdmn_get_current_cc(ic->ic_pdev_obj, &rd);

    return rd.cc.regdmn_id;
}
qdf_export_symbol(ieee80211_get_regdomain);

/*
 * Set country code
 */
int
ieee80211_set_ctry_code(struct ieee80211com *ic, char *isoName, u_int16_t cc, enum ieee80211_clist_cmd cmd)
{
    int  error = 0;
    uint8_t ctry_iso[REG_ALPHA2_LEN + 1];

    ieee80211_getCurrentCountryISO(ic, ctry_iso);
    if (!cc) {
        if (isoName == NULL) {

        } else if((ctry_iso[0] == isoName[0]) &&
                (ctry_iso[1] == isoName[1]) &&
                (ctry_iso[2] == isoName[2])) {
            return 0;
        }
    }

    IEEE80211_DISABLE_11D(ic);

    error = ic->ic_set_country(ic, isoName, cc, cmd);

    return error;
}

int ieee80211_set_ctry_code_continue(struct ieee80211com *ic,
                             bool no_chanchange)
{
    uint8_t ctry_iso[REG_ALPHA2_LEN + 1];
    int err;
    struct wlan_objmgr_pdev *pdev;

#if ATH_SUPPORT_ZERO_CAC_DFS
    pdev = ic->ic_pdev_obj;
    if(pdev == NULL) {
        qdf_print("%s : pdev is null", __func__);
        return -1;
    }
#endif

    /* update the country information for 11D */
    ieee80211_getCurrentCountryISO(ic, ctry_iso);

    /* update channel list */
    err = ieee80211_update_channellist(ic, 1, no_chanchange);
    if (err)
        return err;
    
    ieee80211_build_countryie_all(ic, ctry_iso);

    if (IEEE80211_IS_COUNTRYIE_ENABLED(ic)) {
        IEEE80211_ENABLE_11D(ic);
    }

    if (ic->ic_flags_ext2 & IEEE80211_FEXT2_RESET_PRECACLIST) {
        if (wlan_objmgr_pdev_try_get_ref(pdev, WLAN_DFS_ID) !=
                QDF_STATUS_SUCCESS) {
            return -1;
        }
#if ATH_SUPPORT_ZERO_CAC_DFS
        mlme_dfs_reset_precaclists(pdev);
#endif
        wlan_objmgr_pdev_release_ref(pdev, WLAN_DFS_ID);
    }
    /* notify all vaps that the country changed */
    wlan_iterate_vap_list(ic, wlan_notify_country_changed, (void *)&ctry_iso);

    return 0;
}

int ieee80211_set_channel_for_cc_change(struct ieee80211com *ic, struct ieee80211_ath_channel *chan)
{
    wlan_if_t tmpvap;
    u_int8_t des_cfreq2;

    TAILQ_FOREACH(tmpvap, &ic->ic_vaps, iv_next) {
        if(tmpvap) {
            if (chan) {
                if (ieee80211_chan2mode(chan) != tmpvap->iv_des_mode)
                     des_cfreq2 = tmpvap->iv_des_cfreq2;
                else
                     des_cfreq2 = chan->ic_vhtop_freq_seg2;
                wlan_set_channel(tmpvap, chan->ic_freq, des_cfreq2);
            } else {
                IEEE80211_DPRINTF_IC(ic, IEEE80211_VERBOSE_NORMAL,
                            IEEE80211_MSG_MLME,
                            "No valid channel to be selected in current country\n");
                return -EINVAL;
            }
        }
    }
         return 0;
 }

int
ieee80211_set_country_code(struct ieee80211com *ic, char *isoName, u_int16_t cc, enum ieee80211_clist_cmd cmd)
{
    int err = 0;
    u_int16_t freq;
    u_int64_t flags;
    uint16_t cfreq2;
    u_int32_t mode;
    struct ieee80211_ath_channel *chan, *new_chan;
    wlan_if_t tmpvap;
    bool no_chanchange = false;

    chan = ieee80211_get_current_channel(ic);
    freq = ieee80211_chan2freq(ic, chan);
    flags = chan->ic_flags;
    cfreq2 = chan->ic_vhtop_freq_seg2;
    mode = ieee80211_chan2mode(chan);

    /* Reset precac channel list when user changes the country code */
    ic->ic_flags_ext2 |= IEEE80211_FEXT2_RESET_PRECACLIST;
    err = ieee80211_set_ctry_code(ic, isoName, cc, cmd);
    if (err)
        return err;

   /* skip channel change if any vdev is active, it is handled in below code */
    if (ieee80211_get_num_active_vaps(ic) != 0)
        no_chanchange = true;

    err = ieee80211_set_ctry_code_continue(ic,no_chanchange);
    if (err)
        return err;

    if (ieee80211_get_num_active_vaps(ic) != 0) {
	new_chan = ieee80211_find_channel(ic, freq, cfreq2, flags);
	if (new_chan == NULL) {
	    IEEE80211_DPRINTF_IC(ic, IEEE80211_VERBOSE_NORMAL,
				 IEEE80211_MSG_MLME,
				 "Current channel not supported in new country. Configuring to a random channel\n");
	    new_chan = ieee80211_find_dot11_channel(ic, 0, 0, mode);
	    if (new_chan == NULL) {
		new_chan = ieee80211_find_dot11_channel(ic, 0, 0, 0);
		if(new_chan) {
		    mode = ieee80211_chan2mode(new_chan);
		    TAILQ_FOREACH(tmpvap, &ic->ic_vaps, iv_next) {
			tmpvap->iv_des_mode = mode;
			tmpvap->iv_des_hw_mode = mode;
		    }
		}

	    }
	}
	osif_restart_for_config(ic, &ieee80211_set_channel_for_cc_change, new_chan);
    }
    ic->ic_flags_ext2 &= ~IEEE80211_FEXT2_RESET_PRECACLIST;

    return 0;
}

void ieee80211_set_country_code_assoc_sm(void *data)
{
    struct ieee80211com *ic = (struct ieee80211com *)data;
    struct assoc_sm_set_country *set_country = ic->set_country;
    struct ieee80211_node *ni = set_country->ni;
    struct ieee80211_mlme_priv    *mlme_priv = set_country->vap->iv_mlme_priv;

    if (ieee80211_set_ctry_code(ic, (char*)ni->ni_cc,
                set_country->cc, set_country->cmd) == 0) {
        if (ni->ni_capinfo & IEEE80211_CAPINFO_SPECTRUM_MGMT)
            ieee80211_ic_doth_set(ic);
    }

    OS_CANCEL_TIMER(&mlme_priv->im_timeout_timer);
    IEEE80211_DELIVER_EVENT_MLME_JOIN_COMPLETE_SET_COUNTRY(set_country->vap, 0);
}

void
ieee80211_update_spectrumrequirement(struct ieee80211vap *vap,
        bool *thread_started)
{
    /*
     * 1. If not multiple-domain capable, check to update the country IE.
     * 2. If multiple-domain capable,
     *    a. If the country has been set by using desired country,
     *       then it is done, the ie has been updated.
     *       For IBSS or AP mode, if we are DFS owner, then need to enable Radar detect.
     *    b. If the country is not set, if no AP or peer country info,
     *       just assuming legancy mode.
     *       If we have AP or peer country info, using default to see if AP accept for now.
     */
    struct ieee80211com *ic = vap->iv_ic;
    struct ieee80211_node *ni = vap->iv_bss;
    uint8_t ctry_iso[REG_ALPHA2_LEN + 1];

    if (!ieee80211_ic_2g_csa_is_set(vap->iv_ic))
        ieee80211_ic_doth_clear(ic);
    ieee80211_getCurrentCountryISO(ic, ctry_iso);

    if (ic->ic_country.isMultidomain == 0) {
        if (ni->ni_capinfo & IEEE80211_CAPINFO_SPECTRUM_MGMT) {
            if (!IEEE80211_IS_COUNTRYIE_ENABLED(ic)) {
                ieee80211_build_countryie_all(ic, ctry_iso);
            }
            ieee80211_ic_doth_set(ic);
        }

        IEEE80211_DELIVER_EVENT_MLME_JOIN_COMPLETE_SET_COUNTRY(vap,0);
        return;
    }

    if (IEEE80211_HAS_DESIRED_COUNTRY(ic)) {
        /* If the country has been set, enabled the flag */
        if( (ctry_iso[0] == ni->ni_cc[0]) &&
            (ctry_iso[1] == ni->ni_cc[1]) &&
            (ctry_iso[2] == ni->ni_cc[2])) {
            if (ni->ni_capinfo & IEEE80211_CAPINFO_SPECTRUM_MGMT) {
                ieee80211_ic_doth_set(ic);
            }

            IEEE80211_ENABLE_11D(ic);
            IEEE80211_DELIVER_EVENT_MLME_JOIN_COMPLETE_SET_COUNTRY(vap,0);
            return;
        }
    }

    if ((ni->ni_cc[0] == 0)   ||
        (ni->ni_cc[1] == 0)   ||
        (ni->ni_cc[0] == ' ') ||
        (ni->ni_cc[1] == ' ')) {
        if (ni->ni_capinfo & IEEE80211_CAPINFO_SPECTRUM_MGMT) {
            ieee80211_ic_doth_set(ic);
        }            

        IEEE80211_DELIVER_EVENT_MLME_JOIN_COMPLETE_SET_COUNTRY(vap,0);
        return;
    }

    // Update the cc only for platforms that request this : Currently, only Windows.
    if (ieee80211_ic_disallowAutoCCchange_is_set(ic)) {
        if ((ni->ni_cc[0] == ctry_iso[0] &&
             ni->ni_cc[1] == ctry_iso[1] &&
             ni->ni_cc[2] == ctry_iso[2]))
        {
            ieee80211_build_countryie_all(ic, ctry_iso);
            if (ni->ni_capinfo & IEEE80211_CAPINFO_SPECTRUM_MGMT) {
                ieee80211_ic_doth_set(ic);
            }
        }

        IEEE80211_DELIVER_EVENT_MLME_JOIN_COMPLETE_SET_COUNTRY(vap,0);
    }
    else {
        // If ignore11dBeacon, using the original reg. domain setting.
        if (!IEEE80211_IS_11D_BEACON_IGNORED(ic)) {
            ic->set_country->cc = 0;
            ic->set_country->cmd = CLIST_UPDATE;
            ic->set_country->ni = ni;
            ic->set_country->vap = vap;
            qdf_sched_work(NULL, &ic->assoc_sm_set_country_code);
            *thread_started = true;
        } else {
            /* Post an event to move to next state. */
            IEEE80211_DELIVER_EVENT_MLME_JOIN_COMPLETE_SET_COUNTRY(vap,0);
        }
    }
}

void
ieee80211_set_regclassids(struct ieee80211com *ic, const u_int8_t *regclassids, u_int nregclass)
{
    int i;

    if (nregclass >= IEEE80211_REGCLASSIDS_MAX)
        nregclass = IEEE80211_REGCLASSIDS_MAX;

    ic->ic_nregclass = nregclass;

    for (i = 0; i < nregclass; i++)
        ic->ic_regclassids[i] = regclassids[i];
}

#ifdef BEACON_CHANLIST_COMPRESSION
bool
ieee80211_chanlist_compression_possible(struct ieee80211_ath_channel *curchan,
                                        u_int8_t prevchan, u_int64_t chanflags,
                                        struct country_ie_triplet *pTriplet)
{
    bool cmp_possible = false;
    uint8_t chan_ieee_separation;

    if (chanflags == IEEE80211_CHAN_2GHZ) {
        chan_ieee_separation = 1;
        if ((curchan->ic_ieee == prevchan + chan_ieee_separation) &&
            (pTriplet->maxtxpwr == curchan->ic_maxregpower))
            cmp_possible = true;
    } else if((chanflags == IEEE80211_CHAN_5GHZ) ||
              (chanflags == IEEE80211_CHAN_6GHZ)) {
        if (WLAN_REG_IS_49GHZ_FREQ(curchan->ic_freq)) {
            chan_ieee_separation = 1;
            if ((curchan->ic_ieee == prevchan + chan_ieee_separation) &&
                (pTriplet->maxtxpwr == curchan->ic_maxregpower))
                cmp_possible = true;
        } else {
            chan_ieee_separation = 4;
            if ((curchan->ic_ieee == prevchan + chan_ieee_separation) &&
                (pTriplet->maxtxpwr == curchan->ic_maxregpower))
                cmp_possible = true;
        }
    }
    return cmp_possible;
}
#endif

/*
 * Build the country information element.
 */
void
ieee80211_build_countryie(struct ieee80211vap *vap, uint8_t *country_iso)
{
    struct country_ie_triplet *pTriplet;
    struct ieee80211_ath_channel *c;
    int i, j, chancnt, isnewband;
    u_int64_t chanflags;
    u_int8_t *chanlist;
    u_int8_t prevchan;
    struct ieee80211com *ic = vap->iv_ic;
    uint32_t dfs_reg = 0;
    struct wlan_objmgr_pdev *pdev;
    struct wlan_objmgr_psoc *psoc;
    struct wlan_lmac_if_reg_rx_ops *reg_rx_ops;

    pdev = ic->ic_pdev_obj;
    if(pdev == NULL) {
        qdf_print("%s : pdev is null", __func__);
        return;
    }

    psoc = wlan_pdev_get_psoc(pdev);
    if (psoc == NULL) {
        qdf_print("%s : psoc is null", __func__);
        return;
    }

    reg_rx_ops = wlan_lmac_if_get_reg_rx_ops(psoc);
    if (wlan_objmgr_pdev_try_get_ref(pdev, WLAN_REGULATORY_SB_ID) !=
            QDF_STATUS_SUCCESS) {
        return;
    }
    reg_rx_ops->get_dfs_region(pdev, &dfs_reg);
    wlan_objmgr_pdev_release_ref(pdev, WLAN_REGULATORY_SB_ID);

    if (!country_iso[0] || !country_iso[1] || !country_iso[2]) {
        /* Default, no country is set */
        vap->iv_country_ie_data.country_len = 0;
        IEEE80211_DISABLE_COUNTRYIE(ic);
        return;
    }

    IEEE80211_ENABLE_COUNTRYIE(ic);

    /*
     * Construct the country IE:
     * 1. The country string come first.
     * 2. Then construct the channel triplets from lowest channel to highest channel.
     * 3. If we support the regulatory domain class (802.11J)
     *    then add the class triplets before the channel triplets of each band.
     */
    OS_MEMZERO(&vap->iv_country_ie_data, sizeof(vap->iv_country_ie_data));  
    vap->iv_country_ie_data.country_id = IEEE80211_ELEMID_COUNTRY;
    vap->iv_country_ie_data.country_len = 3;

    vap->iv_country_ie_data.country_str[0] = country_iso[0];
    vap->iv_country_ie_data.country_str[1] = country_iso[1];
    vap->iv_country_ie_data.country_str[2] = country_iso[2];

    pTriplet = (struct country_ie_triplet*)&vap->iv_country_ie_data.country_triplet;

    if (IEEE80211_IS_CHAN_2GHZ(vap->iv_bsschan))
        chanflags = IEEE80211_CHAN_2GHZ;
    else if (IEEE80211_IS_CHAN_5GHZ(vap->iv_bsschan))
        chanflags = IEEE80211_CHAN_5GHZ;
    else
        chanflags = IEEE80211_CHAN_6GHZ;

    vap->iv_country_ie_chanflags = chanflags;

    chanlist = OS_MALLOC(ic->ic_osdev, sizeof(u_int8_t) * (IEEE80211_CHAN_MAX + 1), 0);
    if (chanlist == NULL) {
        /* Default, no country is set */
        vap->iv_country_ie_data.country_len = 0;
        IEEE80211_DISABLE_COUNTRYIE(ic);
        return;
    }

    OS_MEMZERO(chanlist, sizeof(uint8_t) * (IEEE80211_CHAN_MAX + 1));
    prevchan = 0;
    chancnt = 0;
    isnewband = 1;
    ieee80211_enumerate_channels(c, ic, i) {
        /* Does channel belong to current operation mode */
        if (!(c->ic_flags & chanflags))
            continue;

        /* Skip previously reported channels */
        for (j=0; j < chancnt; j++) {
            if (c->ic_ieee == chanlist[j])
                break;
        }
    
        if (j != chancnt) /* found a match */
            continue;

        chanlist[chancnt] = c->ic_ieee;
        chancnt++;

        /* Skip turbo channels */
        if (IEEE80211_IS_CHAN_TURBO(c))
            continue;

        if (ic->ic_no_weather_radar_chan 
                && (ieee80211_check_weather_radar_channel(c))
                && (DFS_ETSI_DOMAIN  == dfs_reg))
        {
            /* skipping advertising weather radar channels */
            continue;
        }

        if (isnewband) {
            isnewband = 0;
#ifdef BEACON_CHANLIST_COMPRESSION
        } else if (ieee80211_chanlist_compression_possible(c, prevchan,
                                                           chanflags, pTriplet))
        {
            pTriplet->nchan ++;
            prevchan = c->ic_ieee;
            continue;
#else
        } else if ((pTriplet->maxtxpwr == c->ic_maxregpower) && (c->ic_ieee == prevchan + 1)) {
            pTriplet->nchan ++;
            prevchan = c->ic_ieee;
            continue;
#endif
        } else {
            pTriplet ++;
        }

        prevchan = c->ic_ieee;
        pTriplet->schan = c->ic_ieee;
        pTriplet->nchan = 1; /* init as 1 channel */
        pTriplet->maxtxpwr = c->ic_maxregpower;
        vap->iv_country_ie_data.country_len += 3;
    }

    /* pad */
    if (vap->iv_country_ie_data.country_len & 1) {
        vap->iv_country_ie_data.country_triplet[vap->iv_country_ie_data.country_len] = 0;
        vap->iv_country_ie_data.country_len++;
    }

    OS_FREE(chanlist);
}

static void
ieee80211_build_countryie_vap(uint8_t *ctry_iso, struct ieee80211vap *vap, bool is_last_vap)
{
    ieee80211_build_countryie(vap, ctry_iso);
}

/*
* update the country ie in all vaps.
*/
void
ieee80211_build_countryie_all(struct ieee80211com *ic, uint8_t *ctry_iso)
{
    u_int8_t num_vaps;
    ieee80211_iterate_vap_list_internal(ic,ieee80211_build_countryie_vap,(void *)ctry_iso,num_vaps);
}
qdf_export_symbol(ieee80211_build_countryie_all);

static int
ieee80211_set_countrycode(struct ieee80211com *ic, char isoName[3], u_int16_t cc, enum ieee80211_clist_cmd cmd)
{
    if (ieee80211_set_country_code(ic, isoName, cc, cmd)) {
        IEEE80211_CLEAR_DESIRED_COUNTRY(ic);
        return -EINVAL;
    }
    
    if (!cc) {
        if ((isoName == NULL) || (isoName[0] == '\0') || (isoName[1] == '\0')) {
            IEEE80211_CLEAR_DESIRED_COUNTRY(ic);
        } else {
            IEEE80211_SET_DESIRED_COUNTRY(ic);
        }
    } else {
        IEEE80211_SET_DESIRED_COUNTRY(ic);
    }
#if UMAC_SUPPORT_CFG80211
    wlan_cfg80211_update_channel_list(ic);
#endif
    return 0;
}

int
ieee80211_regdmn_reset(struct ieee80211com *ic)
{
    char cc[3];

    /* Reset to default country if any. */
    cc[0] = cc[1] = cc[2] = 0;
    ieee80211_set_countrycode(ic, cc, 0, CLIST_UPDATE);
    ic->ic_multiDomainEnabled = 0;

    return 0;
}


int
wlan_set_countrycode(wlan_dev_t devhandle, char isoName[3], u_int16_t cc, enum ieee80211_clist_cmd cmd)
{
    return ieee80211_set_countrycode(devhandle, isoName, cc, cmd);
}

u_int16_t
wlan_get_regdomain(wlan_dev_t devhandle)
{
    return devhandle->ic_country.regDmnEnum;
}

int
wlan_set_regdomain(wlan_dev_t devhandle, u_int16_t regdmn)
{
    struct ieee80211com *ic = devhandle;
    struct cc_regdmn_s rd;

    rd.cc.regdmn_id = regdmn;
    rd.flags = REGDMN_IS_SET;

    return ieee80211_regdmn_program_cc(ic->ic_pdev_obj, &rd);
}

u_int8_t
wlan_get_ctl_by_country(wlan_dev_t devhandle, u_int8_t *country, bool is2G)
{
    struct ieee80211com *ic = devhandle;
    return ic->ic_get_ctl_by_country(ic, country, is2G);
}


/* US Operating Classes */
regdmn_op_class_map_t us_operating_class[] = {
    {1,   IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48}},
    {2,   IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {52, 56, 60, 64}},
    {4,   IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144}},
    {5,   IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {149, 153, 157, 161, 165}},
    {12,  IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}},
    {22,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {36, 44}},
    {23,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {52, 60}},
    {24,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {100, 108, 116, 124, 132, 140}},
    {25,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {149, 157}},
    {27,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {40, 48}},
    {28,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {56, 64}},
    {29,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {104, 112, 120, 128, 136, 144}},
    {31,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {153, 161}},
    {32,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {1, 2, 3, 4, 5, 6, 7}},
    {33,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {5, 6, 7, 8, 9, 10, 11}},
    {128, IEEE80211_CWM_WIDTH80, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128,
           132, 136, 140, 144, 149, 153, 157, 161}},
    {129, IEEE80211_CWM_WIDTH160, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128}},
    {130, IEEE80211_CWM_WIDTH80_80, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128,
           132, 136, 140, 144, 149, 153, 157, 161}},
    {0, 0, 0, {0}},
};

/* Europe operating classes */
regdmn_op_class_map_t europe_operating_class[] = {
    {1,   IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48}},
    {2,   IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {52, 56, 60, 64}},
    {3,   IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}},
    {4,   IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}},
    {5,   IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {36, 44}},
    {6,   IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {52, 60}},
    {7,   IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {100, 108, 116, 124, 132}},
    {8,   IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {40, 48}},
    {9,   IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {56, 64}},
    {10,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {104, 112, 120, 128, 136}},
    {11,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {1, 2, 3, 4, 5, 6, 7, 8, 9}},
    {12,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {5, 6, 7, 8, 9, 10, 11, 12, 13}},
    {17,  IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {149, 153, 157, 161, 165, 169}},
    {128, IEEE80211_CWM_WIDTH80, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128}},
    {129, IEEE80211_CWM_WIDTH160, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128}},
    {130, IEEE80211_CWM_WIDTH80_80, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128}},
    {0, 0, 0, {0}},
};

/* Japan operating classes */
regdmn_op_class_map_t japan_operating_class[] = {
    {1,   IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48}},
    {30,  IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}},
    {31,  IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {14}},
    {32,  IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {52, 56, 60, 64}},
    {34,  IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}},
    {36,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {36, 44}},
    {37,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {52, 60}},
    {39,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {100, 108, 116, 124, 132}},
    {41,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {40, 48}},
    {42,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {56, 64}},
    {44,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {104, 112, 120, 128, 136}},
    {128, IEEE80211_CWM_WIDTH80, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128} },
    {129, IEEE80211_CWM_WIDTH160, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128}},
    {130, IEEE80211_CWM_WIDTH80_80, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128}},
    {0, 0, 0, {0}},
};

/* Global operating classes */
regdmn_op_class_map_t global_operating_class[] = {
    {81,  IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}},
    {82,  IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {14}},
    {83,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {1, 2, 3, 4, 5, 6, 7, 8, 9}},
    {84,  IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {5, 6, 7, 8, 9, 10, 11, 12, 13}},
    {115, IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48}},
    {116, IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {36, 44}},
    {117, IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {40, 48}},
    {118, IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {52, 56, 60, 64}},
    {119, IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {52, 60}},
    {120, IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {56, 64}},
    {121, IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144}},
    {122, IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {100, 108, 116, 124, 132, 140}},
    {123, IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {104, 112, 120, 128, 136, 144}},
    {125, IEEE80211_CWM_WIDTH20, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {149, 153, 157, 161, 165, 169}},
    {126, IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCA,
          {149, 157}},
    {127, IEEE80211_CWM_WIDTH40, IEEE80211_SEC_CHAN_OFFSET_SCB,
          {153, 161}},
    {128, IEEE80211_CWM_WIDTH80, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128,
           132, 136, 140, 144, 149, 153, 157, 161}},
    {129, IEEE80211_CWM_WIDTH160, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128}},
    {130, IEEE80211_CWM_WIDTH80_80, IEEE80211_SEC_CHAN_OFFSET_SCN,
          {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128,
           132, 136, 140, 144, 149, 153, 157, 161}},
    {0, 0, 0, {0}},
};

void get_regdmn_op_class_map(struct ieee80211com *ic, regdmn_op_class_map_t **map)
{

    uint8_t country_iso[REG_ALPHA2_LEN + 1];
    ieee80211_getCurrentCountryISO(ic, country_iso);

    if (!qdf_mem_cmp(country_iso, "US", 2)) {
       *map = us_operating_class;
    } else if (!qdf_mem_cmp(country_iso, "EU", 2)) {
       *map = europe_operating_class;
    } else if (!qdf_mem_cmp(country_iso, "JP", 2)) {
       *map = japan_operating_class;
    } else {
       *map = global_operating_class;
    }

    return;
}


uint8_t
regdmn_get_new_opclass_from_channel(struct ieee80211com *ic,
                                    struct ieee80211_ath_channel *channel)
{
    uint8_t chan = channel->ic_ieee;
    uint8_t ch_width = 0;
    regdmn_op_class_map_t *map = NULL;
    uint8_t idx = 0;

    /*
     * 11AX TODO: Revisit below for drafts later than 802.11ax draft 2.0
     */

    ch_width = ieee80211_get_cwm_width_from_channel(channel);

    /*  Get the regdmn map corresponding to country code */
    get_regdmn_op_class_map(ic, &map);

    while (map->op_class) {
       if (ch_width == map->ch_width) {
            for (idx = 0; idx < MAX_CHANNELS_PER_OPERATING_CLASS; idx++) {
                if (map->ch_set[idx] == chan) {
                    /* Find exact promary and seconday match for 40 MHz channel */
                    if (ch_width == IEEE80211_CWM_WIDTH40) {
                        /* Compare HT40+/HT40- mode */
                        if (map->sec20_offset == ieee80211_sec_chan_offset(channel)) {
                            return map->op_class;
                        }
                    } else {
                        return map->op_class;
                    }
                }
            }
        }
        map++;
    }
    /* operating class could not be found. return 0. */
    return 0;
}

/**
 * regdmn_get_supp_opclass_list - Get the maximum txpower of the current operating channel.
 * @pdev - Pointer to pdev.
 *
 * Return - return the 2's complement of maximum tx power of the current operating channel.
 */
int regdmn_get_current_chan_txpower(struct wlan_objmgr_pdev *pdev)
{
    struct ieee80211com *ic = wlan_pdev_get_mlme_ext_obj(pdev);

   if (ic == NULL) {
       qdf_err("ic is NULL");
       return -EINVAL;
   }

    return ~(wlan_channel_maxpower(ic->ic_curchan)) + 1;
}

/**
 * regdmn_get_supp_opclass_list - Get the current operating channel number and operating class.
 * @vdev - Pointer to vdev.
 * @chan_num - Pointer to channel number.
 * @opclass - Pointer to opclass.
 *
 * Return - void
 */
void regdmn_get_curr_chan_and_opclass(struct wlan_objmgr_vdev *vdev,
                                      uint8_t *chan_num,
                                      uint8_t *opclass)
{
   wlan_if_t vap;

   vap = (wlan_if_t)wlan_vdev_get_mlme_ext_obj(vdev);
   if (vap == NULL) {
       qdf_err("VAP is NULL");
       return;
   }

   if (opclass)
       *opclass = wlan_get_opclass(vdev);

   if (chan_num)
       *chan_num = vap->iv_ic->ic_curchan->ic_ieee;
}

/**
 * regdmn_get_supp_opclass_list - Get supported opclass list and number of supported opclasses.
 * @pdev - Pointer to pdev.
 * @opclass_list - Pointer to opclass list.
 * @num_supp_op_class - Pointer to number of supported opclass.
 * @global_tbl_lookup - Global table lookup.
 *
 * Return - void
 */
void regdmn_get_supp_opclass_list(struct wlan_objmgr_pdev *pdev,
                                  uint8_t *opclass_list,
                                  uint8_t *num_supp_op_class,
                                  bool global_tbl_lookup)
{
   struct regdmn_ap_cap_opclass_t *reg_ap_cap =  NULL;
   QDF_STATUS status;
   uint8_t index = 0, idx = 0;

   reg_ap_cap = qdf_mem_malloc(REG_MAX_SUPP_OPER_CLASSES * sizeof(*reg_ap_cap));

   if (!reg_ap_cap) {
       qdf_err("reg_ap_cap is NULL");
       return;
   }

   status = wlan_reg_get_opclass_details(pdev, reg_ap_cap,
                                         &index, REG_MAX_SUPP_OPER_CLASSES,
                                         global_tbl_lookup);

  if (status != QDF_STATUS_SUCCESS) {
      qdf_err("Failed to get opclass details");
      qdf_mem_free(reg_ap_cap);
      return;
  }

  while (reg_ap_cap[idx].op_class && (idx < index)) {
         if(opclass_list)
            opclass_list[idx] = reg_ap_cap[idx].op_class;

         idx++;
  }

  *num_supp_op_class  = idx;
  qdf_mem_free(reg_ap_cap);
}

static int64_t
ieee80211_chansort(const void *chan1, const void *chan2)
{
#define CHAN_FLAGS    (IEEE80211_CHAN_ALL)
    const struct ieee80211_ath_channel *tmp_chan1 = chan1;
    const struct ieee80211_ath_channel *tmp_chan2 = chan2;

    return (tmp_chan1->ic_freq == tmp_chan2->ic_freq) ?
        (tmp_chan1->ic_flags & CHAN_FLAGS) -
            (tmp_chan2->ic_flags & CHAN_FLAGS) :
        tmp_chan1->ic_freq - tmp_chan2->ic_freq;
#undef CHAN_FLAGS
}

/*
 * Insertion sort.
 */
#define ieee80211_swap(_chan1, _chan2, _size) {     \
    u_int8_t *tmp_chan2 = _chan2;                   \
    int i = _size;                                  \
    do {                                            \
        u_int8_t tmp = *_chan1;                     \
        *_chan1++ = *tmp_chan2;                     \
        *tmp_chan2++ = tmp;                         \
    } while (--i);                                  \
    _chan1 -= _size;                                \
}

static void ieee80211_channel_sort(void *chans, uint32_t next, uint32_t size)
{
    uint8_t *tmp_chan = chans;
    uint8_t *ptr1, *ptr2;

    for (ptr1 = tmp_chan + size; --next >= 1; ptr1 += size)
        for (ptr2 = ptr1; ptr2 > tmp_chan; ptr2 -= size) {
            uint8_t *index = ptr2 - size;
            if (ieee80211_chansort(index, ptr2) <= 0)
                break;
            ieee80211_swap(index, ptr2, size);
        }
}

enum {
    CHANNEL_40_NO,
    CHANNEL_40_PLUS,
    CHANNEL_40_MINUS,
};

struct reg_cmode {
    uint32_t mode;
    uint64_t flags;
    uint32_t bw;
    uint32_t chan_ext;
};

static const struct reg_cmode modes[] = {
    { HOST_REGDMN_MODE_TURBO,               IEEE80211_CHAN_ST,
        CH_WIDTH_20MHZ, CHANNEL_40_NO }, /* TURBO means 11a Static Turbo */
#ifndef ATH_NO_5G_SUPPORT
    { HOST_REGDMN_MODE_11A,                 IEEE80211_CHAN_A,
        CH_WIDTH_20MHZ, CHANNEL_40_NO },
#endif

    { HOST_REGDMN_MODE_11B,                 IEEE80211_CHAN_B,
        CH_WIDTH_20MHZ, CHANNEL_40_NO },
    { HOST_REGDMN_MODE_11G,                 IEEE80211_CHAN_PUREG,
        CH_WIDTH_20MHZ, CHANNEL_40_NO },

    { HOST_REGDMN_MODE_11NG_HT20,           IEEE80211_CHAN_HT20,
        CH_WIDTH_20MHZ, CHANNEL_40_NO },
    { HOST_REGDMN_MODE_11NG_HT40PLUS,       IEEE80211_CHAN_HT40PLUS,
        CH_WIDTH_40MHZ, CHANNEL_40_PLUS },
    { HOST_REGDMN_MODE_11NG_HT40MINUS,      IEEE80211_CHAN_HT40MINUS,
        CH_WIDTH_40MHZ, CHANNEL_40_MINUS },

#ifndef ATH_NO_5G_SUPPORT
    { HOST_REGDMN_MODE_11NA_HT20,           IEEE80211_CHAN_HT20,
        CH_WIDTH_20MHZ, CHANNEL_40_NO },
    { HOST_REGDMN_MODE_11NA_HT40PLUS,       IEEE80211_CHAN_HT40PLUS,
        CH_WIDTH_40MHZ,  },
    { HOST_REGDMN_MODE_11NA_HT40MINUS,      IEEE80211_CHAN_HT40MINUS,
        CH_WIDTH_40MHZ, CHANNEL_40_MINUS },

    { HOST_REGDMN_MODE_11AC_VHT20,          IEEE80211_CHAN_VHT20,
        CH_WIDTH_20MHZ, CHANNEL_40_NO },
    { HOST_REGDMN_MODE_11AC_VHT40PLUS,      IEEE80211_CHAN_VHT40PLUS,
        CH_WIDTH_40MHZ, CHANNEL_40_PLUS },
    { HOST_REGDMN_MODE_11AC_VHT40MINUS,     IEEE80211_CHAN_VHT40MINUS,
        CH_WIDTH_40MHZ, CHANNEL_40_MINUS },
    { HOST_REGDMN_MODE_11AC_VHT80,          IEEE80211_CHAN_VHT80,
        CH_WIDTH_80MHZ, CHANNEL_40_NO },
    { HOST_REGDMN_MODE_11AC_VHT160,         IEEE80211_CHAN_VHT160,
        CH_WIDTH_160MHZ, CHANNEL_40_NO },
    { HOST_REGDMN_MODE_11AC_VHT80_80,       IEEE80211_CHAN_VHT80_80,
        CH_WIDTH_80P80MHZ, CHANNEL_40_NO },
    { HOST_REGDMN_MODE_11AXG_HE20,          IEEE80211_CHAN_HE20,
        CH_WIDTH_20MHZ, CHANNEL_40_NO },
    { HOST_REGDMN_MODE_11AXA_HE20,          IEEE80211_CHAN_HE20,
        CH_WIDTH_20MHZ, CHANNEL_40_NO },
    { HOST_REGDMN_MODE_11AXG_HE40PLUS,      IEEE80211_CHAN_HE40PLUS,
        CH_WIDTH_40MHZ, CHANNEL_40_PLUS },
    { HOST_REGDMN_MODE_11AXG_HE40MINUS,     IEEE80211_CHAN_HE40MINUS,
        CH_WIDTH_40MHZ, CHANNEL_40_MINUS },
    { HOST_REGDMN_MODE_11AXA_HE40PLUS,      IEEE80211_CHAN_HE40PLUS,
        CH_WIDTH_40MHZ, CHANNEL_40_PLUS },
    { HOST_REGDMN_MODE_11AXA_HE40MINUS,     IEEE80211_CHAN_HE40MINUS,
        CH_WIDTH_40MHZ, CHANNEL_40_MINUS },
    { HOST_REGDMN_MODE_11AXA_HE80,          IEEE80211_CHAN_HE80,
        CH_WIDTH_80MHZ, CHANNEL_40_NO },
    { HOST_REGDMN_MODE_11AXA_HE160,         IEEE80211_CHAN_HE160,
        CH_WIDTH_160MHZ, CHANNEL_40_NO },
    { HOST_REGDMN_MODE_11AXA_HE80_80,       IEEE80211_CHAN_HE80_80,
        CH_WIDTH_80P80MHZ, CHANNEL_40_NO },
#endif
};

static bool
regdmn_duplicate_channel(struct ieee80211_ath_channel *chan,
        struct ieee80211_ath_channel *list, int size)
{
    uint16_t i;

    for (i=0; i<size; i++) {
        if (chan->ic_freq == list[i].ic_freq &&
                chan->ic_flags == list[i].ic_flags &&
                chan->ic_flagext == list[i].ic_flagext &&
                chan->ic_vhtop_freq_seg1 == list[i].ic_vhtop_freq_seg1 &&
                chan->ic_vhtop_freq_seg2 == list[i].ic_vhtop_freq_seg2)
            return true;
    }

    return false;
}

u_int
regdmn_mhz2ieee(u_int freq, u_int flags)
{
#define IS_CHAN_IN_PUBLIC_SAFETY_BAND(_c) ((_c) > 4940 && (_c) < 4990)

    if (freq == 2484)
        return 14;
    if (freq < 2484)
        return (freq - 2407) / 5;
    if (freq < 5000) {
        if (IS_CHAN_IN_PUBLIC_SAFETY_BAND(freq)) {
            return ((freq * 10) +
                    (((freq % 5) == 2) ? 5 : 0) - 49400)/5;
        } else if (freq > 4900) {
            return (freq - 4000) / 5;
        } else {
            return 15 + ((freq - 2512) / 20);
        }
    }
    return (freq - 5000) / 5;
}

static void
populate_ic_channel(struct ieee80211_ath_channel *icv,
        const struct reg_cmode *cm,
        struct regulatory_channel *reg_chan,
        struct ch_params *ch_params,
        struct ieee80211_ath_channel *chans,
        int *next, enum channel_state state1,
        enum channel_state state2,
        uint64_t flags)
{

#define CHANNEL_HALF_BW        10
#define CHANNEL_QUARTER_BW    5

    if (cm->flags == IEEE80211_CHAN_HT40PLUS ||
            cm->flags == IEEE80211_CHAN_HT40PLUS ||
            cm->flags == IEEE80211_CHAN_VHT40PLUS ||
            cm->flags == IEEE80211_CHAN_HE40PLUS ||
            cm->flags == IEEE80211_CHAN_HE40PLUS) {
        if (ch_params->sec_ch_offset == HIGH_PRIMARY_CH) {
            return;
        }
    }

    if (cm->flags == IEEE80211_CHAN_HT40MINUS ||
            cm->flags == IEEE80211_CHAN_HT40MINUS ||
            cm->flags == IEEE80211_CHAN_VHT40MINUS ||
            cm->flags == IEEE80211_CHAN_HE40MINUS ||
            cm->flags == IEEE80211_CHAN_HE40MINUS) {
        if (ch_params->sec_ch_offset == LOW_PRIMARY_CH) {
            return ;
        }
    }

#ifdef CONFIG_BAND_6GHZ
        if(wlan_reg_is_6ghz_chan_freq(reg_chan->center_freq)) {
           /* Do not build 6Ghz for non-11AX  phy modes */
          if(!((cm->flags == IEEE80211_CHAN_HE20) ||
             (cm->flags   == IEEE80211_CHAN_HE40MINUS) ||
             (cm->flags   == IEEE80211_CHAN_HE40PLUS) ||
             (cm->flags   == IEEE80211_CHAN_HE80) ||
             (cm->flags   == IEEE80211_CHAN_HE80_80) ||
             (cm->flags   == IEEE80211_CHAN_HE160))){
             return;
          }
        }
#endif

    OS_MEMZERO(icv, sizeof(icv));

    /* Set only 11b flag if REGULATORY_CHAN_NO_OFDM flag
     * is set in the reg-rules.
     */

    icv->ic_flags = cm->flags | flags;

    if (wlan_reg_is_5ghz_ch_freq(reg_chan->center_freq))
        icv->ic_flags = cm->flags | IEEE80211_CHAN_5GHZ;
    else if (wlan_reg_is_24ghz_ch_freq(reg_chan->center_freq))
        icv->ic_flags = cm->flags | IEEE80211_CHAN_2GHZ;
    else if (wlan_reg_is_6ghz_chan_freq(reg_chan->center_freq))
        icv->ic_flags = cm->flags | IEEE80211_CHAN_6GHZ;

    if (reg_chan->chan_flags & REGULATORY_CHAN_NO_OFDM)
        icv->ic_flags = IEEE80211_CHAN_B;

    icv->ic_freq = reg_chan->center_freq;
    icv->ic_maxregpower = reg_chan->tx_power;
    icv->ic_antennamax = reg_chan->ant_gain;
    icv->ic_flagext = 0;

    if (state1 == CHANNEL_STATE_DFS) {
        icv->ic_flags |= IEEE80211_CHAN_PASSIVE;
        if (WLAN_REG_IS_5GHZ_CH_FREQ(reg_chan->center_freq)) {
            icv->ic_flags &= ~IEEE80211_CHAN_DFS_RADAR;
            icv->ic_flagext |= IEEE80211_CHAN_DFS;
            icv->ic_flagext &= ~IEEE80211_CHAN_DFS_RADAR_FOUND;
        }
    }

    if (state2 == CHANNEL_STATE_DFS)
        icv->ic_flagext |= IEEE80211_CHAN_DFS_CFREQ2;
    else
        icv->ic_flagext &= ~IEEE80211_CHAN_DFS_CFREQ2;

    /* Check for ad-hoc allowableness */
    /* To be done: DISALLOW_ADHOC_11A_TURB should allow ad-hoc */
    if (icv->ic_flagext & IEEE80211_CHAN_DFS ||
            icv->ic_flagext & IEEE80211_CHAN_DFS_CFREQ2) {
        icv->ic_flagext |= IEEE80211_CHAN_DISALLOW_ADHOC;
    }

    if (WLAN_REG_IS_6GHZ_PSC_CHAN_FREQ(reg_chan->center_freq)) {
        icv->ic_flagext |= IEEE80211_CHAN_PSC;
    }

    icv->ic_ieee = reg_chan->chan_num;
    icv->ic_vhtop_ch_num_seg1 = ch_params->center_freq_seg0;
    icv->ic_vhtop_ch_num_seg2 = ch_params->center_freq_seg1;
    icv->ic_vhtop_freq_seg1 = ch_params->mhz_freq_seg0;
    icv->ic_vhtop_freq_seg2 = ch_params->mhz_freq_seg1;

    if (regdmn_duplicate_channel(icv, chans, *next+1)) {
        return;
    }

    OS_MEMCPY(&chans[(*next)++], icv, sizeof(struct ieee80211_ath_channel));

#undef CHANNEL_HALF_BW
#undef CHANNEL_QUARTER_BW
}

enum channel_state regdmn_reg_get_bonded_channel_state_for_freq(
        struct wlan_objmgr_pdev *pdev,
        qdf_freq_t freq,
        qdf_freq_t sec_ch_freq_2g,
        enum phy_ch_width bw)
{
        if (WLAN_REG_IS_5GHZ_CH_FREQ(freq))
            return ieee80211_regdmn_get_5g_bonded_channel_state_for_freq(pdev,
                                                                         freq,
                                                                         bw);
        else if  (WLAN_REG_IS_24GHZ_CH_FREQ(freq))
                  return ieee80211_regdmn_get_2g_bonded_channel_state_for_freq(pdev, freq, sec_ch_freq_2g, bw);

    return CHANNEL_STATE_INVALID;
}

#define FREQ_OFFSET_10MHZ 10
#define FREQ_OFFSET_80MHZ 80
#define CHAN_80MHZ_NUM QDF_ARRAY_SIZE(mhz80_5g_chan_list)
#define CHAN_80MHZ_NUM_5G QDF_ARRAY_SIZE(mhz80_5g_freq_list)

#ifdef CONFIG_BAND_6GHZ
#define CHAN_80MHZ_NUM_6G QDF_ARRAY_SIZE(mhz80_6g_freq_list)
#else
#define CHAN_80MHZ_NUM_6G 0
#endif

const uint16_t mhz80_5g_chan_list[] = {
    42,
    58,
    106,
    122,
    138,
    155
};

const uint16_t mhz80_5g_freq_list[] = {
    5210, /* 42 */
    5290, /* 58 */
    5530, /* 106 */
    5610, /* 122 */
    5690, /* 138 */
    5775, /* 155 */
};
#ifdef CONFIG_BAND_6GHZ
const uint16_t mhz80_6g_chan_list[] = {
    7 ,
    23,
    39,
    55,
    71,
    87,
    103,
    119,
    135,
    151,
    167,
    183,
    199,
    215,
};

const uint16_t mhz80_6g_freq_list[] = {
    5975, /* 7 */
    6055, /* 23 */
    6135, /* 39 */
    6215, /* 55 */
    6295, /* 71 */
    6375, /* 87 */
    6455, /* 103 */
    6535, /* 119 */
    6615, /* 135 */
    6695, /* 151 */
    6775, /* 167 */
    6855, /* 183 */
    6935, /* 199 */
    7015, /* 215 */
};
#endif

void regdmn_update_ic_channels(
        struct wlan_objmgr_pdev *pdev,
        struct ieee80211com *ic,
        uint32_t mode_select,
        struct regulatory_channel *curr_chan_list,
        struct ieee80211_ath_channel *chans,
        u_int maxchans,
        u_int *nchans,
        qdf_freq_t low_2g,
        qdf_freq_t high_2g,
        qdf_freq_t low_5g,
        qdf_freq_t high_5g)
{
    struct regulatory_channel *reg_chan;
    uint32_t num_chan;
    const struct reg_cmode *cm;
    struct ch_params ch_params;
    qdf_freq_t sec_ch_2g_freq = 0;
    struct ieee80211_ath_channel icv;
    uint32_t next = 0;
    enum channel_state chan_state1, chan_state2;
    uint32_t loop, i;
    uint64_t flags;
    const uint16_t *mhz80_freq_list, *mhz80_chan_list;
    struct wlan_objmgr_psoc *psoc;

    psoc = wlan_pdev_get_psoc(pdev);
    if (psoc == NULL) {
        qdf_err("%s : psoc is null", __func__);
        return;
    }

    /*
     * Clear ic_49ghz_enabled as new channels are initialised.
     * This shall be set as soon as a 4.9 GHz channel in ic_chans[]
     * is initialised.
     */
    ic->ic_49ghz_enabled = false;

    for (cm = modes; cm < &modes[QDF_ARRAY_SIZE(modes)]; cm++) {
        if ((cm->mode & mode_select) == 0) {
            continue;
        }

        reg_chan = curr_chan_list;
        num_chan = NUM_CHANNELS;

        while(num_chan--)
        {
            if(reg_chan->state == CHANNEL_STATE_DISABLE) {
                reg_chan++;
                continue;
            }

            if (cm->flags & IEEE80211_CHAN_5GHZ) {
                if ((reg_chan->center_freq < low_5g) ||
                        (reg_chan->center_freq > high_5g)) {
                    reg_chan++;
                    continue;
                }
            } else if (cm->flags & IEEE80211_CHAN_2GHZ) {
                if ((reg_chan->center_freq < low_2g) ||
                        (reg_chan->center_freq > high_2g)) {
                    reg_chan++;
                    continue;
                }
            }
            if(WLAN_REG_IS_49GHZ_FREQ(reg_chan->center_freq))
            {

                if(cm->mode == HOST_REGDMN_MODE_11A) {
                    ch_params.center_freq_seg0 = reg_chan->chan_num;
                    ch_params.center_freq_seg1 = 0;
                    ch_params.mhz_freq_seg0 = reg_chan->center_freq;
                    ch_params.mhz_freq_seg1 = 0;
                    chan_state1 = reg_chan->state;
                    chan_state2 = CHANNEL_STATE_DISABLE;

                    if(BW_WITHIN(reg_chan->min_bw, FULL_BW, reg_chan->max_bw)) {
                        flags = 0;
                        populate_ic_channel(&icv, cm, reg_chan, &ch_params, chans,
                                &next, chan_state1, chan_state2, flags);
                    }

                    if(BW_WITHIN(reg_chan->min_bw, HALF_BW, reg_chan->max_bw)) {
                        flags = IEEE80211_CHAN_HALF;
                        populate_ic_channel(&icv, cm, reg_chan, &ch_params, chans,
                                &next, chan_state1, chan_state2, flags);
                    }

                    if(BW_WITHIN(reg_chan->min_bw, QRTR_BW, reg_chan->max_bw)) {
                        flags = IEEE80211_CHAN_QUARTER;
                        populate_ic_channel(&icv, cm, reg_chan, &ch_params, chans,
                                &next, chan_state1, chan_state2, flags);
                    }

                    ic->ic_49ghz_enabled = true;
                }

                reg_chan++;
                continue;
            }

#if SPIRENT_AP_EMULATION
            /* only need to get sec_ch_2g_freq when it's 2G regdom and bandwidth is
             * 40Mhz */
            sec_ch_2g_freq = 0;
            if((cm->flags & IEEE80211_CHAN_2GHZ) && (cm->bw == CH_WIDTH_40MHZ)) {
#endif
            switch(cm->chan_ext) {
            case CHANNEL_40_NO:
                sec_ch_2g_freq = 0;
                break;
            case CHANNEL_40_PLUS:
                sec_ch_2g_freq = reg_chan->center_freq + CHAN_HT40_OFFSET;
                break;
            case CHANNEL_40_MINUS:
                sec_ch_2g_freq = reg_chan->center_freq - CHAN_HT40_OFFSET;
                break;
            }
#if SPIRENT_AP_EMULATION
            }
#endif

            if (cm->bw == CH_WIDTH_80P80MHZ) {
#ifdef CONFIG_BAND_6GHZ
                if (wlan_reg_is_6ghz_chan_freq(reg_chan->center_freq)) {
                    loop = CHAN_80MHZ_NUM_6G;
                    mhz80_freq_list = mhz80_6g_freq_list;
                    mhz80_chan_list = mhz80_6g_chan_list;
                } else {
                    loop = CHAN_80MHZ_NUM_5G;
                    mhz80_freq_list = mhz80_5g_freq_list;
                    mhz80_chan_list = mhz80_5g_chan_list;
                }
#else
                loop = CHAN_80MHZ_NUM_5G;
                mhz80_chan_list = mhz80_5g_chan_list;
                mhz80_freq_list = mhz80_5g_freq_list;
#endif
            }
            else
                loop = 1;

            i = 0;
            while (loop--) {
                if (cm->bw == CH_WIDTH_80P80MHZ) {
                    ch_params.center_freq_seg1 = mhz80_chan_list[i];
                    ch_params.mhz_freq_seg1 = mhz80_freq_list[i];
                    i++;
                } else {
                    ch_params.center_freq_seg1 = 0;
                    ch_params.mhz_freq_seg1 = 0;
                }

                ch_params.ch_width = cm->bw;
                ieee80211_regdmn_set_channel_params_for_freq(pdev, reg_chan->center_freq,
                        sec_ch_2g_freq, &ch_params);

                if (cm->bw == ch_params.ch_width) {

                    if (ch_params.ch_width == CH_WIDTH_80P80MHZ) {
                        chan_state1 = regdmn_reg_get_bonded_channel_state_for_freq(pdev,
                                reg_chan->center_freq, sec_ch_2g_freq, CH_WIDTH_80MHZ);
                        chan_state2 = regdmn_reg_get_bonded_channel_state_for_freq(pdev,
                                ch_params.mhz_freq_seg1 - FREQ_OFFSET_10MHZ, sec_ch_2g_freq, CH_WIDTH_80MHZ);
                        if ((abs(ch_params.mhz_freq_seg0 - ch_params.mhz_freq_seg1)) <= FREQ_OFFSET_80MHZ) {
                            continue;
                        }

                        /* In restricted 80P80 MHz enabled, only one 80+80 MHz
                         * channel is supported with cfreq=5690 and cfreq=5775.
                         * Therefore, do not populate other 80+80 MHz channels
                         * in ic channel list.
                         */
                        if (wlan_psoc_nif_fw_ext_cap_get(psoc, WLAN_SOC_RESTRICTED_80P80_SUPPORT) &&
                            !(CHAN_WITHIN_RESTRICTED_80P80(ch_params.mhz_freq_seg0,
                                ch_params.mhz_freq_seg1)))
                            continue;
                    }
                    else if (ch_params.ch_width == CH_WIDTH_160MHZ) {
                        chan_state1 = regdmn_reg_get_bonded_channel_state_for_freq(pdev,
                                reg_chan->center_freq, sec_ch_2g_freq, CH_WIDTH_80MHZ);
                        chan_state2 = regdmn_reg_get_bonded_channel_state_for_freq(pdev,
                                ((2 * ch_params.mhz_freq_seg1) -
                                 ch_params.mhz_freq_seg0 + FREQ_OFFSET_10MHZ),
                                sec_ch_2g_freq, CH_WIDTH_80MHZ);
                    } else {
                        chan_state1 = regdmn_reg_get_bonded_channel_state_for_freq(pdev,
                                reg_chan->center_freq, sec_ch_2g_freq, ch_params.ch_width);

                        chan_state2 = CHANNEL_STATE_DISABLE;
                    }

                    flags = 0;
                    populate_ic_channel(&icv, cm, reg_chan, &ch_params, chans,
                        &next, chan_state1, chan_state2, flags);
                }
            }
            reg_chan++;
        }
    }
    if (next > 0) {
        ieee80211_channel_sort(chans, next, sizeof(struct ieee80211_ath_channel));
    }
    *nchans = next;
    ieee80211_set_nchannels(ic, next);

#if defined(WLAN_DFS_PARTIAL_OFFLOAD) && defined(HOST_DFS_SPOOF_TEST)
    /* If the spoof dfs check has failed, AP should come up only in NON-DFS
     * channels. "Setregdomain and setcountry" cmd will rebuild the ic channel list
     * (could include DFS and non-DFS chans). The following code will ensure
     * that ic channel list still consists of non-DFS channels if spoof check
     * failed.
     */
    ic->dfs_spoof_test_regdmn = 1;
    ieee80211_dfs_non_dfs_chan_config(ic);

#endif /* HOST_DFS_SPOOF_TEST */
}

uint8_t
regdmn_get_band_cap_from_op_class(uint8_t num_of_opclass,
                                  const uint8_t *opclass)
{
    uint8_t  supported_band;
    uint8_t country_iso[REG_ALPHA2_LEN + 1] = {};

    country_iso[2] = OP_CLASS_GLOBAL;
    supported_band = wlan_reg_get_band_cap_from_op_class(country_iso,
                                                         num_of_opclass,
                                                         opclass);

    if (!supported_band) {
        qdf_err("None of the Operating class is found");
        return supported_band;
    }

    if (supported_band & BIT(REG_BAND_2G))
        supported_band |= BIT(IEEE80211_2G_BAND);
    else if (supported_band & BIT(REG_BAND_5G))
             supported_band |= BIT(IEEE80211_5G_BAND);
    else if (supported_band & BIT(REG_BAND_6G))
             supported_band |= BIT(IEEE80211_6G_BAND);
    else
             qdf_err("Unknown band %X", supported_band);

    return supported_band;
}

/**
 * regdmn_get_min_6ghz_chan_freq - Retrieve the minimum 6G channel frequency
 *
 * Return - Return the 6G minimum channel frequency if found, else zero.
 */
uint16_t regdmn_get_min_6ghz_chan_freq(void)
{
    return wlan_reg_min_6ghz_chan_freq();
}
qdf_export_symbol(regdmn_get_min_6ghz_chan_freq);

/**
 * regdmn_get_max_6ghz_chan_freq - Retrieve the maximum 6G channel frequency
 *
 * Return - Return the 6G maximum channel frequency if found, else zero.
 */
uint16_t regdmn_get_max_6ghz_chan_freq(void)
{
    return wlan_reg_max_6ghz_chan_freq();
}
qdf_export_symbol(regdmn_get_max_6ghz_chan_freq);

/**
 * regdmn_get_min_5ghz_chan_freq - Retrieve the minimum 5G channel frequency
 *
 * Return - Return the 5G minimum channel frequency if found, else zero.
 */
uint16_t regdmn_get_min_5ghz_chan_freq(void)
{
    return wlan_reg_min_5ghz_chan_freq();
}
qdf_export_symbol(regdmn_get_min_5ghz_chan_freq);

/**
 * regdmn_get_max_5ghz_chan_freq - Retrieve the maximum 5G channel frequency
 *
 * Return - Return the 5G maximum channel frequency if found, else zero.
 */
uint16_t regdmn_get_max_5ghz_chan_freq(void)
{
    return wlan_reg_max_5ghz_chan_freq();
}
qdf_export_symbol(regdmn_get_max_5ghz_chan_freq);

void regdmn_populate_channel_list_from_map(regdmn_op_class_map_t *map,
                                  u_int8_t reg_class, struct ieee80211_node *ni) {
    uint8_t chanidx = 0;

    if(!map)
        return;

    while (map->op_class) {
        if (map->op_class == reg_class) {
            for (chanidx = 0; chanidx < MAX_CHANNELS_PER_OPERATING_CLASS &&
                                map->ch_set[chanidx] != 0; chanidx++) {
                IEEE80211_MBO_CHAN_BITMAP_SET(ni->ni_supp_op_cl.channels_supported, map->ch_set[chanidx]);
            }
            ni->ni_supp_op_cl.num_chan_supported += chanidx;
            break;
        }
        map++;
    }
    return;
}

void
regdmn_get_channel_list_from_op_class(uint8_t reg_class,
                                      struct ieee80211_node *ni)
{
    struct ieee80211com *ic = ni->ni_ic;
    regdmn_op_class_map_t *map = NULL;

    // Get the regdmn map corresponding to country code
    get_regdmn_op_class_map(ic, &map);
    regdmn_populate_channel_list_from_map(map, reg_class, ni);

    /* Check for global operating class and mark those channels also as true to
     * pass MBO related testcase.
     */
    if (map != global_operating_class) {
        regdmn_populate_channel_list_from_map(global_operating_class, reg_class, ni);
    }

    return;
}

uint8_t regdmn_get_opclass (uint8_t *country_iso, struct ieee80211_ath_channel *channel)
{
    uint8_t ch_width = ieee80211_get_cwm_width_from_channel(channel);
    regdmn_op_class_map_t *map;
    uint8_t idx;

    /* If operating class table index is specified in the country code per
     * 802.11 Annex E, derive operating class table from it. Otherwise use
     * country_iso name
     */
    if (country_iso[2] == 0x01) {
        map = us_operating_class;
    } else if (country_iso[2] == 0x02) {
        map = europe_operating_class;
    } else if (country_iso[2] == 0x03) {
        map = japan_operating_class;
    } else if (country_iso[2] == 0x04) {
        map = global_operating_class;
    } else if (country_iso[2] == 0x05) {
        map = europe_operating_class;
    } else {
        if (!qdf_mem_cmp(country_iso, "US", 2)) {
            map = us_operating_class;
        } else if (!qdf_mem_cmp(country_iso, "EU", 2)) {
            map = europe_operating_class;
        } else if (!qdf_mem_cmp(country_iso, "JP", 2)) {
            map = japan_operating_class;
        } else {
            map = global_operating_class;
        }
    }

    while (map->op_class) {
       if (ch_width == map->ch_width) {
            for (idx = 0; idx < MAX_CHANNELS_PER_OPERATING_CLASS; idx++) {
                if (map->ch_set[idx] == channel->ic_ieee) {
                    /* Find exact promary and seconday match for 40 MHz channel */
                    if (ch_width == IEEE80211_CWM_WIDTH40) {
                        /* Compare HT40+/HT40- mode */
                        if (map->sec20_offset == ieee80211_sec_chan_offset(channel)) {
                            return map->op_class;
                        }
                    } else {
                        return map->op_class;
                    }
                }
            }
        }
        map++;
    }

    /* operating class could not be found. return 0. */
    return 0;
}

#define CHAN_TO_FREQ_SCALE 5

static uint16_t
regdmn_chan_to_freq(struct regdmn_ap_cap_opclass_t *reg_ap_cap, uint8_t chan_num, uint8_t idx)
{
    return reg_ap_cap[idx].start_freq + (chan_num * CHAN_TO_FREQ_SCALE);
}

static void regdmn_fill_apcap(mapapcap_t *apcap,
                              struct regdmn_ap_cap_opclass_t *reg_ap_cap,
                              uint8_t idx,
                              uint8_t *total_n_sup_opclass)
{
    HW_OP_CLASS.opclass = AP_CAP.op_class;
    HW_OP_CLASS.max_tx_pwr_dbm = ~(AP_CAP.max_tx_pwr_dbm) + 1;
    HW_OP_CLASS.num_non_oper_chan = AP_CAP.num_non_supported_chan;
    qdf_mem_copy(HW_OP_CLASS.non_oper_chan_num,
                 AP_CAP.non_sup_chan_list,
                 AP_CAP.num_non_supported_chan);
    apcap->map_ap_radio_basic_capabilities_valid = 1;
    (*total_n_sup_opclass)++;
}

static void regdmn_fill_map_op_chan(struct wlan_objmgr_pdev *pdev,
                                    struct map_op_chan_t *map_op_chan,
                                    struct regdmn_ap_cap_opclass_t *reg_ap_cap,
                                    uint8_t idx,
                                    uint8_t *total_n_sup_opclass,
                                    bool dfs_required)
{
    if (dfs_required) {
        uint8_t chan_idx = 0, i = 0;

        MAP_OP_CHAN.opclass = AP_CAP.op_class;
        switch(AP_CAP.ch_width) {
               case BW_20_MHZ:
                              MAP_OP_CHAN.ch_width = IEEE80211_CWM_WIDTH20;
                              break;
               case BW_25_MHZ:
                              MAP_OP_CHAN.ch_width = IEEE80211_CWM_WIDTH20;
                              break;
               case BW_40_MHZ:
                              MAP_OP_CHAN.ch_width = IEEE80211_CWM_WIDTH40;
                              break;
               case BW_80_MHZ:
                              if (AP_CAP.behav_limit == BIT(BEHAV_BW80_PLUS))
                                  MAP_OP_CHAN.ch_width = IEEE80211_CWM_WIDTH80_80;
                              else
                                  MAP_OP_CHAN.ch_width = IEEE80211_CWM_WIDTH80;

                              break;
              case BW_160_MHZ:
                              MAP_OP_CHAN.ch_width = IEEE80211_CWM_WIDTH160;
                              break;
              default:
                      MAP_OP_CHAN.ch_width = IEEE80211_CWM_WIDTHINVALID;
                      break;
       }

       while (AP_CAP.sup_chan_list[chan_idx]) {
              qdf_freq_t search_freq =
              regdmn_chan_to_freq(reg_ap_cap,
                                  AP_CAP.
                                  sup_chan_list[chan_idx],
                                  idx);

              if (wlan_reg_is_dfs_for_freq(pdev, search_freq))
                  MAP_OP_CHAN.oper_chan_num[i++] = AP_CAP.sup_chan_list[chan_idx++];
              else
                  chan_idx++;
       }

       MAP_OP_CHAN.num_oper_chan = i;

       if (i)
           (*total_n_sup_opclass)++;

    } else if (AP_CAP.op_class == map_op_chan->opclass) {
               map_op_chan->ch_width = AP_CAP.ch_width;
               switch(AP_CAP.ch_width) {
                      case BW_20_MHZ:
                                     map_op_chan->ch_width =
                                                          IEEE80211_CWM_WIDTH20;
                                     break;
                      case BW_25_MHZ:
                                     map_op_chan->ch_width =
                                                          IEEE80211_CWM_WIDTH20;
                                     break;
                      case BW_40_MHZ:
                                     map_op_chan->ch_width =
                                                         IEEE80211_CWM_WIDTH40;
                                     break;
                      case BW_80_MHZ:
                                     if (AP_CAP.behav_limit == BIT(BEHAV_BW80_PLUS))
                                         map_op_chan->ch_width =
                                                       IEEE80211_CWM_WIDTH80_80;
                                     else
                                         map_op_chan->ch_width =
                                                          IEEE80211_CWM_WIDTH80;
                                     break;
                      case BW_160_MHZ:
                                      map_op_chan->ch_width =
                                                         IEEE80211_CWM_WIDTH160;
                                      break;
                      default:
                              map_op_chan->ch_width = IEEE80211_CWM_WIDTHINVALID;
                              break;
                }

                map_op_chan->num_oper_chan = AP_CAP.num_supported_chan;
                qdf_mem_copy(map_op_chan->oper_chan_num,
                             AP_CAP.sup_chan_list,
                             AP_CAP.num_supported_chan);
    }
}

static void regdmn_fill_map_op_class_t(struct map_op_class_t *map_op_class,
                                       struct regdmn_ap_cap_opclass_t *reg_ap_cap,
                                       uint8_t idx)
{
    uint8_t chan_idx = 0, i = 0;

    if (map_op_class->opclass != AP_CAP.op_class)
        return;

    switch(AP_CAP.ch_width) {
           case BW_20_MHZ:
                   map_op_class->ch_width = IEEE80211_CWM_WIDTH20;
                   break;
           case BW_25_MHZ:
                   map_op_class->ch_width = IEEE80211_CWM_WIDTH20;
                   break;
           case BW_40_MHZ:
                   map_op_class->ch_width = IEEE80211_CWM_WIDTH40;
                   break;
           case BW_80_MHZ:
                   if (AP_CAP.behav_limit == BIT(BEHAV_BW80_PLUS))
                       map_op_class->ch_width = IEEE80211_CWM_WIDTH80_80;
                   else
                       map_op_class->ch_width = IEEE80211_CWM_WIDTH80;
                   break;
           case BW_160_MHZ:
                   map_op_class->ch_width = IEEE80211_CWM_WIDTH160;
                   break;
           default:
                   map_op_class->ch_width = IEEE80211_CWM_WIDTHINVALID;
                   break;
    }

    switch(AP_CAP.behav_limit) {
           case BIT(BEHAV_NONE):
                                map_op_class->sc_loc =
                                                 IEEE80211_SEC_CHAN_OFFSET_SCN;
                                break;
            case BIT(BEHAV_BW40_LOW_PRIMARY):
                                map_op_class->sc_loc =
                                                  IEEE80211_SEC_CHAN_OFFSET_SCA;
                                break;
            case BIT(BEHAV_BW40_HIGH_PRIMARY):
                                map_op_class->sc_loc =
                                                  IEEE80211_SEC_CHAN_OFFSET_SCB;
                                break;
            case BIT(BEHAV_BW80_PLUS):
                                map_op_class->sc_loc =
                                                  IEEE80211_SEC_CHAN_OFFSET_SCN;
                                break;
            default:
                    map_op_class->sc_loc = IEEE80211_SEC_CHAN_OFFSET_SCN;
            break;
    }

    map_op_class->num_chan = AP_CAP.num_supported_chan + AP_CAP.num_non_supported_chan;

    while(chan_idx < AP_CAP.num_supported_chan)
          map_op_class->channels[i++] = AP_CAP.sup_chan_list[chan_idx++];
    chan_idx = 0;

    while(chan_idx < AP_CAP.num_non_supported_chan)
          map_op_class->channels[i++] = AP_CAP.non_sup_chan_list[chan_idx++];

}

/**
 * @brief Populate AP capabilities with opclass and operable channels for MultiAP
 *
 * @param pdev Pointer to pdev
 * @param apcap Pointer to structure to populate AP capabilities
 * @param map_op_chan Pointer to structure to populate operable channel
 * @param map_op_class Pointer to structure to populate operating class
 * @param global_tbl_lookup Whether to lookup global op class tbl
 *
 * @return Total number of operating opclass, else return 0 in case of failure
 */
uint8_t regdmn_get_map_opclass(struct wlan_objmgr_pdev *pdev,
                               mapapcap_t *apcap,
                               struct map_op_chan_t *map_op_chan,
                               struct map_op_class_t *map_op_class,
                               bool global_tbl_lookup,
                               bool dfs_required)
{
    uint8_t total_n_sup_opclass = 0;
    uint8_t idx = 0, n_opclasses = 0, max_supp_op_class = REG_MAX_SUPP_OPER_CLASSES;
    struct regdmn_ap_cap_opclass_t *reg_ap_cap =  NULL;
    struct ieee80211com *ic = wlan_pdev_get_mlme_ext_obj(pdev);
    QDF_STATUS status;

    if (!ic) {
        qdf_err("ic is NULL");
        return 0;
    }

    reg_ap_cap = qdf_mem_malloc(max_supp_op_class * sizeof(*reg_ap_cap));

    if (!reg_ap_cap) {
        qdf_err("Failed to allocate reg_ap_cap");
        return 0;
    }

    status = wlan_reg_get_opclass_details(pdev, reg_ap_cap, &n_opclasses,
                                          max_supp_op_class,
                                          global_tbl_lookup);

    if (status != QDF_STATUS_SUCCESS) {
        qdf_err("Failed to get opclass details");
        return 0;
    }
    /* Only one of 'apcap' and 'map_op_chan' is expected to be not-NULL at a time*/
    if (apcap && map_op_chan) {
        qdf_err("Both apcap and map_op_chan should not be allocated at the same time");
        return 0;
    }

    while (AP_CAP.op_class && (idx < n_opclasses)) {
           /* check radio capability and skip unsupported channel width */
           if (((AP_CAP.ch_width == BW_160_MHZ) &&
               !(ic->ic_modecaps & (1 << IEEE80211_MODE_11AC_VHT160))) ||
               ((AP_CAP.behav_limit == BIT(BEHAV_BW80_PLUS)) &&
                !(ic->ic_modecaps & (1 << IEEE80211_MODE_11AC_VHT80_80)))) {
               idx++;
               continue;
           }

           if (apcap != NULL) {
               regdmn_fill_apcap(apcap, reg_ap_cap, idx, &total_n_sup_opclass);
           } else if (map_op_chan != NULL) {
               regdmn_fill_map_op_chan(pdev, map_op_chan, reg_ap_cap, idx,
                                       &total_n_sup_opclass, dfs_required);
           }

           if (map_op_class != NULL) {
               regdmn_fill_map_op_class_t(map_op_class, reg_ap_cap, idx);
           }
           idx++;
    }

    qdf_mem_free(reg_ap_cap);
    return total_n_sup_opclass;
}

/**
 * regdmn_convert_chanflags_to_chanwidth() - Convert chan flag to channel width
 * @chan: Pointer to current channel.
 *
 * Return: Channel width.
 */
static inline uint32_t regdmn_convert_chanflags_to_chanwidth(
        struct ieee80211_ath_channel *chan)
{
    uint32_t chanwidth = CH_WIDTH_INVALID;

    if (IEEE80211_IS_CHAN_ST(chan) ||
            IEEE80211_IS_CHAN_A(chan) ||
            IEEE80211_IS_CHAN_B(chan) ||
            IEEE80211_IS_CHAN_PUREG(chan) ||
            IEEE80211_IS_CHAN_HT20(chan))
        chanwidth = CH_WIDTH_20MHZ;
    else if (IEEE80211_IS_CHAN_HT40(chan))
        chanwidth = CH_WIDTH_40MHZ;
    else if (IEEE80211_IS_CHAN_VHT80(chan))
        chanwidth = CH_WIDTH_80MHZ;
    else if (IEEE80211_IS_CHAN_VHT160(chan))
        chanwidth = CH_WIDTH_160MHZ;
    else if (IEEE80211_IS_CHAN_VHT80_80(chan))
        chanwidth = CH_WIDTH_80P80MHZ;

    return chanwidth;
}

/**
 * regdmn_get_sec_ch_offset() - Get second channel offset.
 * @chan: Pointer to current channel.
 *
 * Return: Second channel offset.
 */
static inline uint8_t regdmn_get_sec_ch_offset(
        struct ieee80211_ath_channel *chan)
{
    uint8_t sec_ch_offset = 0;

    if (IEEE80211_IS_CHAN_HT40PLUS(chan))
        sec_ch_offset = chan->ic_ieee + CHAN_DIFF;
    else if (IEEE80211_IS_CHAN_HT40MINUS(chan))
        sec_ch_offset = chan->ic_ieee - CHAN_DIFF;

    return sec_ch_offset;
}

void ieee80211_regdmn_get_des_chan_params(struct ieee80211vap *vap,
        struct ch_params *ch_params)
{
    struct ieee80211_ath_channel *chan;
    chan = vap->iv_des_chan[vap->iv_des_hw_mode];
    if ((chan != IEEE80211_CHAN_ANYC) && chan) {
        ch_params->ch_width = ieee80211_get_chan_width_from_phymode(vap->iv_des_hw_mode);
        ch_params->center_freq_seg0 = chan->ic_vhtop_ch_num_seg1;
        ch_params->center_freq_seg1 = chan->ic_vhtop_ch_num_seg2;
        ch_params->mhz_freq_seg0 = chan->ic_vhtop_freq_seg1;
        ch_params->mhz_freq_seg1 = chan->ic_vhtop_freq_seg2;
        ch_params->sec_ch_offset = regdmn_get_sec_ch_offset(chan);
    } else {
        ieee80211_regdmn_get_chan_params(vap->iv_ic, ch_params);
    }
}

void ieee80211_regdmn_get_chan_params(struct ieee80211com *ic,
        struct ch_params *ch_params)
{
    ch_params->ch_width = regdmn_convert_chanflags_to_chanwidth(ic->ic_curchan);
    ch_params->center_freq_seg0 = ic->ic_curchan->ic_vhtop_ch_num_seg1;
    ch_params->center_freq_seg1 = ic->ic_curchan->ic_vhtop_ch_num_seg2;
    ch_params->mhz_freq_seg0 = ic->ic_curchan->ic_vhtop_freq_seg1;
    ch_params->mhz_freq_seg1 = ic->ic_curchan->ic_vhtop_freq_seg2;
    ch_params->sec_ch_offset = regdmn_get_sec_ch_offset(ic->ic_curchan);
}

/**
 * ieee80211_convert_width_to_11ngflags() - Convert channel width to 11NG flags.
 * @ic: Pointer to ieee80211com structure.
 * @ch_params: Pointer to channel params structure.
 *
 * Return: Return 11NG flags.
 */
static inline enum ieee80211_phymode
ieee80211_convert_width_to_11ngflags(
        struct ieee80211com *ic,
        struct ch_params *ch_params)
{
    enum ieee80211_phymode mode = IEEE80211_MODE_AUTO;

    switch (ch_params->ch_width) {
        case CH_WIDTH_20MHZ:
            mode = IEEE80211_MODE_11NG_HT20;
            break;
        case CH_WIDTH_40MHZ:
            if (ch_params->sec_ch_offset == HIGH_PRIMARY_CH)
                mode = IEEE80211_MODE_11NG_HT40MINUS;
            else if (ch_params->sec_ch_offset == LOW_PRIMARY_CH)
                mode = IEEE80211_MODE_11NG_HT40PLUS;
            break;
        default:
            mode = IEEE80211_MODE_AUTO;
            break;
    }

    return mode;
}

/**
 * ieee80211_convert_width_to_11naflags() - Convert channel width to 11NA flags.
 * @ic: Pointer to ieee80211com structure.
 * @ch_params: Pointer to channel params structure.
 *
 * Return: Return 11NA flags.
 */
static inline enum ieee80211_phymode
ieee80211_convert_width_to_11naflags(
        struct ieee80211com *ic,
        struct ch_params *ch_params)
{
    enum ieee80211_phymode mode = IEEE80211_MODE_AUTO;

    switch (ch_params->ch_width) {
        case CH_WIDTH_20MHZ:
            mode = IEEE80211_MODE_11NA_HT20;
            break;
        case CH_WIDTH_40MHZ:
            if (ch_params->sec_ch_offset == HIGH_PRIMARY_CH)
                mode = IEEE80211_MODE_11NA_HT40MINUS;
            else if (ch_params->sec_ch_offset == LOW_PRIMARY_CH)
                mode = IEEE80211_MODE_11NA_HT40PLUS;
            break;
        default:
            mode = IEEE80211_MODE_AUTO;
            break;
    }

    return mode;
}

/**
 * ieee80211_convert_width_to_11acflags() - Convert channel width to 11AC flags.
 * @ic: Pointer to ieee80211com structure.
 * @ch_params: Pointer to channel params structure.
 *
 * Return: Return 11AC flags.
 */
static inline enum ieee80211_phymode
ieee80211_convert_width_to_11acflags(
        struct ieee80211com *ic,
        struct ch_params *ch_params)
{
    enum ieee80211_phymode mode = IEEE80211_MODE_AUTO;

    switch (ch_params->ch_width) {
        case CH_WIDTH_20MHZ:
            mode = IEEE80211_MODE_11AC_VHT20;
            break;
        case CH_WIDTH_40MHZ:
            if (ch_params->sec_ch_offset == HIGH_PRIMARY_CH)
                mode = IEEE80211_MODE_11AC_VHT40MINUS;
            else if (ch_params->sec_ch_offset == LOW_PRIMARY_CH)
                mode = IEEE80211_MODE_11AC_VHT40PLUS;
            break;
        case CH_WIDTH_80MHZ:
            mode = IEEE80211_MODE_11AC_VHT80;
            break;
        case CH_WIDTH_160MHZ:
            mode = IEEE80211_MODE_11AC_VHT160;
            break;
        case CH_WIDTH_80P80MHZ:
            mode = IEEE80211_MODE_11AC_VHT80_80;
            break;
        default:
            mode = IEEE80211_MODE_AUTO;
            break;
    }

    return mode;
}

/**
 * ieee80211_convert_width_to_11axgflags() - Convert chan width to 11AXG flags.
 * @ic: Pointer to ieee80211com structure.
 * @ch_params: Pointer to channel params structure.
 *
 * Return: Return 11AXG flags.
 */
static inline enum ieee80211_phymode
ieee80211_convert_width_to_11axgflags(
        struct ieee80211com *ic,
        struct ch_params *ch_params)
{
    enum ieee80211_phymode mode = IEEE80211_MODE_AUTO;

    switch (ch_params->ch_width) {
        case CH_WIDTH_20MHZ:
            mode = IEEE80211_MODE_11AXG_HE20;
            break;
        case CH_WIDTH_40MHZ:
            if (ch_params->sec_ch_offset == HIGH_PRIMARY_CH)
                mode = IEEE80211_MODE_11AXG_HE40MINUS;
            else if (ch_params->sec_ch_offset == LOW_PRIMARY_CH)
                mode = IEEE80211_MODE_11AXG_HE40PLUS;
            break;
        default:
            mode = IEEE80211_MODE_AUTO;
            break;
    }

    return mode;
}

/**
 * ieee80211_convert_width_to_11axaflags() - Convert chan width to 11AXA flags.
 * @ic: Pointer to ieee80211com structure.
 * @ch_params: Pointer to channel params structure.
 *
 * Return: Return 11AXA flags.
 */
static inline enum ieee80211_phymode
ieee80211_convert_width_to_11axaflags(
        struct ieee80211com *ic,
        struct ch_params *ch_params)
{
    enum ieee80211_phymode mode = IEEE80211_MODE_AUTO;

    switch (ch_params->ch_width) {
        case CH_WIDTH_20MHZ:
            mode = IEEE80211_MODE_11AXA_HE20;
            break;
        case CH_WIDTH_40MHZ:
            if (ch_params->sec_ch_offset == HIGH_PRIMARY_CH)
                mode = IEEE80211_MODE_11AXA_HE40MINUS;
            else if (ch_params->sec_ch_offset == LOW_PRIMARY_CH)
                mode = IEEE80211_MODE_11AXA_HE40PLUS;
            break;
        case CH_WIDTH_80MHZ:
            mode = IEEE80211_MODE_11AXA_HE80;
            break;
        case CH_WIDTH_160MHZ:
            mode = IEEE80211_MODE_11AXA_HE160;
            break;
        case CH_WIDTH_80P80MHZ:
            mode = IEEE80211_MODE_11AXA_HE80_80;
            break;
        default:
            mode = IEEE80211_MODE_AUTO;
            break;
    }

    return mode;
}

enum ieee80211_phymode
ieee80211_get_target_channel_mode(
        struct ieee80211com *ic,
        struct ch_params *ch_params)
{
    enum ieee80211_phymode mode;

    if (IEEE80211_IS_CHAN_11NG(ic->ic_curchan))
        mode = ieee80211_convert_width_to_11ngflags(ic, ch_params);
    else if (IEEE80211_IS_CHAN_11NA(ic->ic_curchan))
        mode = ieee80211_convert_width_to_11naflags(ic, ch_params);
    else if (IEEE80211_IS_CHAN_11AC(ic->ic_curchan))
        mode = ieee80211_convert_width_to_11acflags(ic, ch_params);
    else if (IEEE80211_IS_CHAN_11AXG(ic->ic_curchan))
        mode = ieee80211_convert_width_to_11axgflags(ic, ch_params);
    else if (IEEE80211_IS_CHAN_11AXA(ic->ic_curchan))
        mode = ieee80211_convert_width_to_11axaflags(ic, ch_params);
    else
        mode = IEEE80211_MODE_AUTO;

    return mode;
}

#if defined(WLAN_DFS_PARTIAL_OFFLOAD) && defined(HOST_DFS_SPOOF_TEST)
int ieee80211_dfs_rebuild_chan_list_with_non_dfs_channels(struct ieee80211com *ic)
{
    struct ieee80211_ath_channel *tmp_chans = NULL;
    int i,j, num_non_dfs_chans = 0;

    if (ic->ic_rebuilt_chanlist == 1) {
        qdf_print("%s: Channel list already rebuilt, avoiding another iteration",__func__);
        return 0;
    }
    /* Iterate through the ic_nchans and find num of non-DFS chans.*/
    num_non_dfs_chans = ieee80211_is_non_dfs_chans_available(ic);

    if (num_non_dfs_chans == 0) {
        qdf_print("Current country: 0x%x does not support any"
                "non-DFS channels",ic->ic_country.countryCode);
        return 1;
    }
    /* Ensure that we save the current channel's flags (HT/VHT mode) before
     * we do a memzero of ic channel list. This is to replenish to the same
     * mode after re-starting vaps with non-DFS channels.
     */
    ic->ic_curchan_flags = (ic->ic_curchan->ic_flags) & IEEE80211_CHAN_ALL;
    ic->ic_tmp_ch_width = regdmn_convert_chanflags_to_chanwidth(ic->ic_curchan);
    ic->ic_tmp_center_freq_seg0 = ic->ic_curchan->ic_vhtop_ch_num_seg1;
    ic->ic_tmp_center_freq_seg1 = ic->ic_curchan->ic_vhtop_ch_num_seg2;
    ic->ic_tmp_sec_ch_offset = regdmn_get_sec_ch_offset(ic->ic_curchan);

    /* Allocating memory for tmp_chans from heap. This is due to stack overflow
     * if statically allocated as IEEE80211_CHAN_MAX = 1023 & each chan
     *structure occupies 16 Bytes (16 * 1023 = 16,638 Bytes).
     */
    tmp_chans = (struct ieee80211_ath_channel *) OS_MALLOC(ic->ic_osdev,
            num_non_dfs_chans * sizeof(struct ieee80211_ath_channel),
            GFP_KERNEL);

    if (tmp_chans == NULL) {
        qdf_print("%s.. Could not allocate memory for tmp chan list ",__func__);
        return (-ENOMEM);
    }

    OS_MEMZERO(tmp_chans, sizeof(num_non_dfs_chans *
                sizeof(struct ieee80211_ath_channel)));

    for (i = j = 0; i < ic->ic_nchans; i++) {
        if (!(IEEE80211_IS_CHAN_DFS(&ic->ic_channels[i])||
                    ((IEEE80211_IS_CHAN_11AC_VHT160(&ic->ic_channels[i]) ||
                      IEEE80211_IS_CHAN_11AC_VHT80_80(&ic->ic_channels[i])) &&
                     IEEE80211_IS_CHAN_DFS_CFREQ2(&ic->ic_channels[i])))) {
            OS_MEMCPY(&tmp_chans[j++], &(ic->ic_channels[i]),
                    sizeof(struct ieee80211_ath_channel));
        }
    }
    /*Copy the tmp_chans of only non-DFS channels to ic_channels list.*/
    OS_MEMZERO(&ic->ic_channels, sizeof(ic->ic_channels));
    OS_MEMCPY(ic->ic_channels, tmp_chans,
            sizeof(struct ieee80211_ath_channel) * num_non_dfs_chans);
    ic->ic_curchan = &ic->ic_channels[0];
    ic->ic_nchans = num_non_dfs_chans;
    ic->ic_rebuilt_chanlist = 1;
    ic->ic_tempchan = 1;
    OS_FREE(tmp_chans);
    return 0;
}
qdf_export_symbol(ieee80211_dfs_rebuild_chan_list_with_non_dfs_channels);
#endif /* HOST_DFS_SPOOF_TEST */
