/******************************************************************************
 *
 * Copyright(c) 2007 - 2020  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "halbb_precomp.h"

#ifdef HALBB_CNSL_CMN_INFO_SUPPORT

void halbb_env_mntr_log_cnsl(struct bb_info *bb, u32 *_used,
			     char *output, u32 *_out_len)
{
	struct bb_env_mntr_info *env = &bb->bb_env_mntr_i;
	u8 i = 0;

	if (bb->bb_watchdog_mode != BB_WATCHDOG_NORMAL)
		return;

	if (env->ccx_watchdog_result == CCX_FAIL) {
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		       "Env_mntr get CCX result failed!\n");
	} else {
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "{Tx, Idle, CCA_p20, CCA_sec, EDCCA_p20} = {%d, %d, %d, %d, %d} %%\n",
			    env->nhm_tx_ratio, env->nhm_idle_ratio,
			    env->nhm_cca_ratio, env->clm_ratio,
			    env->edcca_clm_ratio);
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used, "FA{CCK, OFDM} = {%d, %d} %%\n",
			    env->ifs_clm_cck_fa_ratio, env->ifs_clm_ofdm_fa_ratio);
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used, "CCA_exclu_FA{CCK, OFDM} = {%d, %d} %%\n",
			    env->ifs_clm_cck_cca_excl_fa_ratio,
			    env->ifs_clm_ofdm_cca_excl_fa_ratio);
		if ((bb->ic_type == BB_RTL8852A) ||
		    (bb->ic_type == BB_RTL8852B) ||
		    (bb->ic_type == BB_RTL8852C)) {
			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "%-18s[%.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d]\n",
				    "  Th", NHM_TH_2_RSSI(env->nhm_th[9]),
				    NHM_TH_2_RSSI(env->nhm_th[8]),
				    NHM_TH_2_RSSI(env->nhm_th[7]),
				    NHM_TH_2_RSSI(env->nhm_th[6]),
				    NHM_TH_2_RSSI(env->nhm_th[5]),
				    NHM_TH_2_RSSI(env->nhm_th[4]),
				    NHM_TH_2_RSSI(env->nhm_th[3]),
				    NHM_TH_2_RSSI(env->nhm_th[2]),
				    NHM_TH_2_RSSI(env->nhm_th[1]),
				    NHM_TH_2_RSSI(env->nhm_th[0]));
			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "[NHM]  (pwr:%02d.%d)[%.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d]\n",
				    env->nhm_pwr, 5 * (env->nhm_pwr_0p5 & 0x1),
				    env->nhm_rpt[10], env->nhm_rpt[9],
				    env->nhm_rpt[8], env->nhm_rpt[7],
				    env->nhm_rpt[6], env->nhm_rpt[5],
				    env->nhm_rpt[4], env->nhm_rpt[3],
				    env->nhm_rpt[2], env->nhm_rpt[1],
				    env->nhm_rpt[0]);
		} else {
			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "%-18s[%.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d]\n",
				    "  Th", NHM_TH_2_RSSI(env->nhm_th[10]),
				    NHM_TH_2_RSSI(env->nhm_th[9]),
				    NHM_TH_2_RSSI(env->nhm_th[8]),
				    NHM_TH_2_RSSI(env->nhm_th[7]),
				    NHM_TH_2_RSSI(env->nhm_th[6]),
				    NHM_TH_2_RSSI(env->nhm_th[5]),
				    NHM_TH_2_RSSI(env->nhm_th[4]),
				    NHM_TH_2_RSSI(env->nhm_th[3]),
				    NHM_TH_2_RSSI(env->nhm_th[2]),
				    NHM_TH_2_RSSI(env->nhm_th[1]),
				    NHM_TH_2_RSSI(env->nhm_th[0]));
			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "[NHM]  (pwr:%02d.%d)[%.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d]\n",
				    env->nhm_pwr, 5 * (env->nhm_pwr_0p5 & 0x1),
				    env->nhm_rpt[11], env->nhm_rpt[10],
				    env->nhm_rpt[9], env->nhm_rpt[8],
				    env->nhm_rpt[7], env->nhm_rpt[6],
				    env->nhm_rpt[5], env->nhm_rpt[4],
				    env->nhm_rpt[3], env->nhm_rpt[2],
				    env->nhm_rpt[1], env->nhm_rpt[0]);
		}
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "nhm_ratio = %d %%\n", env->nhm_ratio);
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[IFS] Time(us):[his, ifs_avg(us), cca_avg(us)], total cnt=%d\n",
			    env->ifs_clm_total_ifs);
		for (i = 0; i < IFS_CLM_NUM; i++)
			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    " *[%d](%04d~%04d):[%03d,     %04d,     %04d]\n",
				    i + 1,
				    halbb_ccx_idx_cnt_2_us(bb, env->ifs_clm_th_l[i]),
				    halbb_ccx_idx_cnt_2_us(bb, env->ifs_clm_th_h[i]),
				    env->ifs_clm_his[i], env->ifs_clm_ifs_avg[i],
				    env->ifs_clm_cca_avg[i]);
	}

	if (!((env->fahm_app == FAHM_DIG) ||
	      (env->fahm_app == FAHM_TDMA_DIG))) {
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "Env_mntr get FAHM result failed and app is not DIG!!\n");
		return;
	}

	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "=== FAHM ===\n");
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "{FA, CRC_err} = {%d, %d} %%\n",
	       env->fahm_ratio, env->fahm_denom_ratio);
	if ((bb->ic_type == BB_RTL8852A) || (bb->ic_type == BB_RTL8852B) ||
	    (bb->ic_type == BB_RTL8852C)) {
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "%-18s[%.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d]\n",
			    "  Th", FAHM_TH_2_RSSI(env->fahm_th[9]),
			    FAHM_TH_2_RSSI(env->fahm_th[8]),
			    FAHM_TH_2_RSSI(env->fahm_th[7]),
			    FAHM_TH_2_RSSI(env->fahm_th[6]),
			    FAHM_TH_2_RSSI(env->fahm_th[5]),
			    FAHM_TH_2_RSSI(env->fahm_th[4]),
			    FAHM_TH_2_RSSI(env->fahm_th[3]),
			    FAHM_TH_2_RSSI(env->fahm_th[2]),
			    FAHM_TH_2_RSSI(env->fahm_th[1]),
			    FAHM_TH_2_RSSI(env->fahm_th[0]));
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[FAHM] (pwr:%02d.%d)[%.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d]\n",
			    env->fahm_pwr, 5 * (env->fahm_pwr_0p5 & 0x1),
			    env->fahm_rpt[10], env->fahm_rpt[9], env->fahm_rpt[8],
			    env->fahm_rpt[7], env->fahm_rpt[6], env->fahm_rpt[5],
			    env->fahm_rpt[4], env->fahm_rpt[3], env->fahm_rpt[2],
			    env->fahm_rpt[1], env->fahm_rpt[0]);
	} else {
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "%-18s[%.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d]\n",
			    "  Th", FAHM_TH_2_RSSI(env->fahm_th[10]),
			    FAHM_TH_2_RSSI(env->fahm_th[9]),
			    FAHM_TH_2_RSSI(env->fahm_th[8]),
			    FAHM_TH_2_RSSI(env->fahm_th[7]),
			    FAHM_TH_2_RSSI(env->fahm_th[6]),
			    FAHM_TH_2_RSSI(env->fahm_th[5]),
			    FAHM_TH_2_RSSI(env->fahm_th[4]),
			    FAHM_TH_2_RSSI(env->fahm_th[3]),
			    FAHM_TH_2_RSSI(env->fahm_th[2]),
			    FAHM_TH_2_RSSI(env->fahm_th[1]),
			    FAHM_TH_2_RSSI(env->fahm_th[0]));
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[FAHM] (pwr:%02d.%d)[%.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d  %.2d]\n",
			    env->fahm_pwr, 5 * (env->fahm_pwr_0p5 & 0x1),
			    env->fahm_rpt[11], env->fahm_rpt[10], env->fahm_rpt[9],
			    env->fahm_rpt[8], env->fahm_rpt[7], env->fahm_rpt[6],
			    env->fahm_rpt[5], env->fahm_rpt[4], env->fahm_rpt[3],
			    env->fahm_rpt[2], env->fahm_rpt[1], env->fahm_rpt[0]);
	}
}

void halbb_basic_dbg_msg_pmac_cnsl(struct bb_info *bb, u32 *_used,
				   char *output, u32 *_out_len)
{
#ifdef HALBB_STATISTICS_SUPPORT
	struct bb_stat_info *stat = &bb->bb_stat_i;
	struct bb_fa_info *fa = &stat->bb_fa_i;
	struct bb_cck_fa_info *cck_fa = &fa->bb_cck_fa_i;
	struct bb_legacy_fa_info *legacy_fa = &fa->bb_legacy_fa_i;
	struct bb_ht_fa_info *ht_fa = &fa->bb_ht_fa_i;
	struct bb_vht_fa_info *vht_fa = &fa->bb_vht_fa_i;
	struct bb_he_fa_info *he_fa = &fa->bb_he_fa_i;
	struct bb_cca_info *cca = &stat->bb_cca_i;
	struct bb_crc_info *crc = &stat->bb_crc_i;
	//struct bb_crc2_info *crc2 = &stat_t->bb_crc2_i;

	if (bb->bb_watchdog_mode != BB_WATCHDOG_NORMAL)
		return;

	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "[Tx]{CCK_TxEN, CCK_TxON, OFDM_TxEN, OFDM_TxON}: {%d, %d, %d, %d}\n",
		    stat->bb_tx_cnt_i.cck_mac_txen, stat->bb_tx_cnt_i.cck_phy_txon,
		    stat->bb_tx_cnt_i.ofdm_mac_txen,
		    stat->bb_tx_cnt_i.ofdm_phy_txon);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "[CRC]{B/G/N/AC/AX/All/MPDU} OK:{%d, %d, %d, %d, %d, %d, %d} Err:{%d, %d, %d, %d, %d, %d, %d}\n",
		    crc->cnt_cck_crc32_ok, crc->cnt_ofdm_crc32_ok,
		    crc->cnt_ht_crc32_ok, crc->cnt_vht_crc32_ok,
		    crc->cnt_he_crc32_ok, crc->cnt_crc32_ok_all,
		    crc->cnt_ampdu_crc_ok, crc->cnt_cck_crc32_error,
		    crc->cnt_ofdm_crc32_error, crc->cnt_ht_crc32_error,
		    crc->cnt_vht_crc32_error, crc->cnt_he_crc32_error,
		    crc->cnt_crc32_error_all, crc->cnt_ampdu_crc_error);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "[CCA]{CCK, OFDM, All}: %d, %d, %d\n",
		    cca->cnt_cck_cca, cca->cnt_ofdm_cca, cca->cnt_cca_all);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "[FA]{CCK, OFDM, All}: %d, %d, %d\n",
		    fa->cnt_cck_fail, fa->cnt_ofdm_fail, fa->cnt_fail_all);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    " *[CCK]sfd/sig_GG=%d/%d, *[OFDM]Prty=%d, Rate=%d, LSIG_brk_s/l=%d/%d, SBD=%d\n",
		    cck_fa->sfd_gg_cnt, cck_fa->sig_gg_cnt,
		    legacy_fa->cnt_parity_fail, legacy_fa->cnt_rate_illegal,
		    legacy_fa->cnt_lsig_brk_s_th, legacy_fa->cnt_lsig_brk_l_th,
		    legacy_fa->cnt_sb_search_fail);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    " *[HT]CRC8=%d, MCS=%d, *[VHT]SIGA_CRC8=%d, MCS=%d\n",
		    ht_fa->cnt_crc8_fail, ht_fa->cnt_mcs_fail,
		    vht_fa->cnt_crc8_fail_vhta, vht_fa->cnt_mcs_fail_vht);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    " *[HE]SIGA_CRC4{SU/ERSU/MU}=%d/%d/%d, SIGB_CRC4{ch1/ch2}=%d/%d, MCS{nrml/bcc/dcm}=%d/%d/%d\n",
		    he_fa->cnt_crc4_fail_hea_su, he_fa->cnt_crc4_fail_hea_ersu,
		    he_fa->cnt_crc4_fail_hea_mu, he_fa->cnt_crc4_fail_heb_ch1_mu,
		    he_fa->cnt_crc4_fail_heb_ch2_mu, he_fa->cnt_mcs_fail_he,
		    he_fa->cnt_mcs_fail_he_bcc, he_fa->cnt_mcs_fail_he_dcm);
#endif
}

void halbb_crc32_cnt2_cmn_log_cnsl(struct bb_info *bb, u32 *_used,
				   char *output, u32 *_out_len)
{
	struct bb_stat_info *stat_t = &bb->bb_stat_i;
	struct bb_crc2_info *crc2 = &stat_t->bb_crc2_i;
	struct bb_usr_set_info *usr_set = &stat_t->bb_usr_set_i;
	char dbg_buf[4][HALBB_SNPRINT_SIZE];

	halbb_mem_set(bb, dbg_buf, 0, sizeof(dbg_buf[0][0]) * 4 * HALBB_SNPRINT_SIZE);

	halbb_print_rate_2_buff(bb, usr_set->ofdm2_rate_idx,
				RTW_GILTF_LGI_4XHE32, dbg_buf[0], HALBB_SNPRINT_SIZE);
	halbb_print_rate_2_buff(bb, usr_set->ht2_rate_idx,
				RTW_GILTF_LGI_4XHE32, dbg_buf[1], HALBB_SNPRINT_SIZE);
	halbb_print_rate_2_buff(bb, usr_set->vht2_rate_idx,
				RTW_GILTF_LGI_4XHE32, dbg_buf[2], HALBB_SNPRINT_SIZE);
	halbb_print_rate_2_buff(bb, usr_set->he2_rate_idx,
				RTW_GILTF_LGI_4XHE32, dbg_buf[3], HALBB_SNPRINT_SIZE);

	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "[CRC32 OK Cnt] {%s, %s, %s, %s}= {%d, %d, %d, %d}\n",
		    dbg_buf[0], dbg_buf[1], dbg_buf[2], dbg_buf[3],
		    crc2->cnt_ofdm2_crc32_ok, crc2->cnt_ht2_crc32_ok,
		    crc2->cnt_vht2_crc32_ok, crc2->cnt_he2_crc32_ok);

	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "[CRC32 Err Cnt] {%s, %s, %s , %s}= {%d, %d, %d, %d}\n",
		    dbg_buf[0], dbg_buf[1], dbg_buf[2], dbg_buf[3],
		    crc2->cnt_ofdm2_crc32_error, crc2->cnt_ht2_crc32_error,
		    crc2->cnt_vht2_crc32_error, crc2->cnt_he2_crc32_error);
}

void halbb_crc32_cnt3_cmn_log_cnsl(struct bb_info *bb, u32 *_used,
				   char *output, u32 *_out_len)
{
	struct bb_stat_info *stat_t = &bb->bb_stat_i;
	struct bb_usr_set_info *usr_set = &stat_t->bb_usr_set_i;
	struct bb_crc2_info *crc2 = &stat_t->bb_crc2_i;

	u32 total_cnt = 0;
	u8 pcr = 0;
	total_cnt = crc2->cnt_ofdm3_crc32_ok + crc2->cnt_ofdm3_crc32_error;
	pcr = (u8)HALBB_DIV(crc2->cnt_ofdm3_crc32_ok * 100, total_cnt);

	switch(usr_set->stat_type_sel_i) {
	case STATE_PROBE_RESP:
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[Probe Response Data CRC32 Cnt(OFDM only)] {error, ok}= {%d, %d} (PCR=%d percent)\n",
			    crc2->cnt_ofdm3_crc32_error,
			    crc2->cnt_ofdm3_crc32_ok, pcr);
		break;
	case STATE_BEACON:
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[Beacon CRC32 Cnt(OFDM only)] {error, ok}= {%d, %d} (PCR=%d percent)\n",
			    crc2->cnt_ofdm3_crc32_error,
			    crc2->cnt_ofdm3_crc32_ok, pcr);
		break;
	case STATE_ACTION:
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[Action CRC32 Cnt(OFDM only)] {error, ok}= {%d, %d} (PCR=%d percent)\n",
			    crc2->cnt_ofdm3_crc32_error,
			    crc2->cnt_ofdm3_crc32_ok, pcr);
		break;
	case STATE_BFRP:
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[BFRP CRC32 Cnt(OFDM only)] {error, ok}= {%d, %d} (PCR=%d percent)\n",
			    crc2->cnt_ofdm3_crc32_error,
			    crc2->cnt_ofdm3_crc32_ok, pcr);
		break;
	case STATE_NDPA:
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[NDPA CRC32 Cnt(OFDM only)] {error, ok}= {%d, %d} (PCR=%d percent)\n",
			    crc2->cnt_ofdm3_crc32_error,
			    crc2->cnt_ofdm3_crc32_ok, pcr);
		break;
	case STATE_BA:
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[BA CRC32 Cnt(OFDM only)] {error, ok}= {%d, %d} (PCR=%d percent)\n",
			    crc2->cnt_ofdm3_crc32_error,
			    crc2->cnt_ofdm3_crc32_ok, pcr);
		break;
	case STATE_RTS:
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[RTS CRC32 Cnt(OFDM only)] {error, ok}= {%d, %d} (PCR=%d percent)\n",
			    crc2->cnt_ofdm3_crc32_error,
			    crc2->cnt_ofdm3_crc32_ok, pcr);
		break;
	case STATE_CTS:
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[CTS CRC32 Cnt(OFDM only)] {error, ok}= {%d, %d} (PCR=%d percent)\n",
			    crc2->cnt_ofdm3_crc32_error,
			    crc2->cnt_ofdm3_crc32_ok, pcr);
		break;
	case STATE_ACK:
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[ACK CRC32 Cnt(OFDM only)] {error, ok}= {%d, %d} (PCR=%d percent)\n",
			    crc2->cnt_ofdm3_crc32_error,
			    crc2->cnt_ofdm3_crc32_ok, pcr);
		break;
	case STATE_DATA:
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[DATA CRC32 Cnt(OFDM only)] {error, ok}= {%d, %d} (PCR=%d percent)\n",
			    crc2->cnt_ofdm3_crc32_error,
			    crc2->cnt_ofdm3_crc32_ok, pcr);
		break;
	case STATE_NULL:
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[Null CRC32 Cnt(OFDM only)] {error, ok}= {%d, %d} (PCR=%d percent)\n",
			    crc2->cnt_ofdm3_crc32_error,
			    crc2->cnt_ofdm3_crc32_ok, pcr);
		break;
	case STATE_QOS:
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[QoS CRC32 Cnt(OFDM only)] {error, ok}= {%d, %d} (PCR=%d percent)\n",
			    crc2->cnt_ofdm3_crc32_error,
			    crc2->cnt_ofdm3_crc32_ok, pcr);
		break;
	default:
		break;
	}
}

void halbb_basic_dbg_msg_tx_info_cnsl(struct bb_info *bb, u32 *_used,
				      char *output, u32 *_out_len)
{
	struct bb_ch_info *ch = &bb->bb_ch_i;
	struct rtw_phl_stainfo_t *sta;
	struct rtw_ra_sta_info	*ra;
	u16 sta_cnt = 0;
	u8 i = 0;
	u8 tmp = 0;
	u16 curr_tx_rt = 0;
	enum rtw_gi_ltf curr_gi_ltf = RTW_GILTF_LGI_4XHE32;
	enum hal_rate_bw curr_bw = HAL_RATE_BW_20;

	for (i = 0; i < PHL_MAX_STA_NUM; i++) {
		if (!bb->sta_exist[i])
			continue;
		sta = bb->phl_sta_info[i];
		if (!is_sta_active(sta))
			continue;

		ra = &sta->hal_sta->ra_info;
		curr_tx_rt = (u16)(ra->rpt_rt_i.mcs_ss_idx) | ((u16)(ra->rpt_rt_i.mode) << 7);
		curr_gi_ltf = ra->rpt_rt_i.gi_ltf;
		curr_bw = ra->rpt_rt_i.bw;

		halbb_print_rate_2_buff(bb, curr_tx_rt, curr_gi_ltf, bb->dbg_buf, HALBB_SNPRINT_SIZE);
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "TxRate[%d]=%s (0x%x-%d), PER=(%d), TXBW=(%d)\n",
			    i, bb->dbg_buf, curr_tx_rt, curr_gi_ltf,
			    ra->curr_retry_ratio, (20<<curr_bw));
		sta_cnt++;
		if (sta_cnt >= bb->hal_com->assoc_sta_cnt)
			break;
	}
}

void halbb_basic_dbg_msg_rx_info_cnsl(struct bb_info *bb, u32 *_used,
				      char *output, u32 *_out_len)
{
	struct bb_ch_info *ch = &bb->bb_ch_i;
#ifdef HALBB_CFO_TRK_SUPPORT
	struct bb_cfo_trk_info *cfo_trk = &bb->bb_cfo_trk_i;
#endif
	struct bb_cmn_rpt_info	*cmn_rpt = &bb->bb_cmn_rpt_i;
	struct bb_pkt_cnt_cap_info *pkt_cnt_cap = &cmn_rpt->bb_pkt_cnt_all_i;
	struct bb_physts_pop_info *pop_info = &cmn_rpt->bb_physts_pop_i;
	struct bb_dbg_cr_info *cr = &bb->bb_dbg_i.bb_dbg_cr_i;
	u8 tmp = 0;
	u32 bb_monitor1 = 0;

	if (bb->bb_watchdog_mode != BB_WATCHDOG_NORMAL)
		return;	

	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "rxsc_idx {Lgcy, 20, 40, 80} = {%d, %d, %d, %d}\n",
		    ch->rxsc_l, ch->rxsc_20, ch->rxsc_40, ch->rxsc_80);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		"RX Pkt Cnt: LDPC=(%d), BCC=(%d), STBC=(%d), SU_BF=(%d), MU_BF=(%d), \n",
		    pkt_cnt_cap->pkt_cnt_ldpc, pkt_cnt_cap->pkt_cnt_bcc,
		    pkt_cnt_cap->pkt_cnt_stbc, pkt_cnt_cap->pkt_cnt_subf,
		    pkt_cnt_cap->pkt_cnt_mubf);
#ifdef HALBB_CFO_TRK_SUPPORT
	halbb_print_sign_frac_digit(bb, cfo_trk->cfo_avg_pre, 16, 2, bb->dbg_buf, HALBB_SNPRINT_SIZE);

	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		"CFO[T-1]=(%s kHz), cryst_cap=(%s%d), cfo_ofst=%d\n",
		  bb->dbg_buf,
		  ((cfo_trk->crystal_cap > cfo_trk->def_x_cap) ? "+" : "-"),
		  DIFF_2(cfo_trk->crystal_cap, cfo_trk->def_x_cap),
		  cfo_trk->x_cap_ofst);
#endif
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		"Dly_sprd=(%d)\n", tmp);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "[POP] cnt=%d, hist_cck/ofdm[0:3]={%d | %d, %d, %d}/{%d | %d, %d, %d}\n",
		    bb->bb_stat_i.bb_cca_i.pop_cnt,
		    pop_info->pop_hist_cck[0], pop_info->pop_hist_cck[1],
		    pop_info->pop_hist_cck[2], pop_info->pop_hist_cck[3],
		    pop_info->pop_hist_ofdm[0], pop_info->pop_hist_ofdm[1],
		    pop_info->pop_hist_ofdm[2], pop_info->pop_hist_ofdm[3]);

	halbb_set_reg(bb, cr->bb_monitor_sel1, cr->bb_monitor_sel1_m, 1);
	bb_monitor1 = halbb_get_reg(bb, cr->bb_monitor1, cr->bb_monitor1_m);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "BB monitor1 = (0x%x)\n", bb_monitor1);
}


void halbb_basic_dbg_msg_physts_su_cnsl(struct bb_info *bb, u32 *_used,
					char *output, u32 *_out_len)
{
	struct bb_ch_info *ch = &bb->bb_ch_i;
	struct bb_link_info *link = &bb->bb_link_i;
	struct bb_cmn_rpt_info	*cmn_rpt = &bb->bb_cmn_rpt_i;
	struct bb_pkt_cnt_su_info *pkt_cnt = &cmn_rpt->bb_pkt_cnt_su_i;
	struct bb_rssi_su_acc_info *acc = &cmn_rpt->bb_rssi_su_acc_i;
	struct bb_rssi_su_avg_info *avg = &cmn_rpt->bb_rssi_su_avg_i;
	u8 rssi_avg_tmp = 0;
	u8 rssi_tmp[HALBB_MAX_PATH];
	u16 pkt_cnt_ss = 0;
	u8 i = 0, j =0;
	u8 rate_num = bb->num_rf_path, ss_ofst = 0;
	char dbg_buf2[32] = {0};
	u16 avg_phy_rate = 0, utility = 0;

	/*RX Rate*/
	halbb_print_rate_2_buff(bb, link->rx_rate_plurality,
				RTW_GILTF_LGI_4XHE32, dbg_buf2, 32);

	halbb_print_rate_2_buff(bb, cmn_rpt->bb_pkt_cnt_bcn_i.beacon_phy_rate,
				RTW_GILTF_LGI_4XHE32, bb->dbg_buf, HALBB_SNPRINT_SIZE);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "Plurality_RxRate:%s (0x%x), Bcn_Rate=%s (0x%x), Bcn_cnt=%d\n",
		    dbg_buf2, link->rx_rate_plurality,
		    bb->dbg_buf ,cmn_rpt->bb_pkt_cnt_bcn_i.beacon_phy_rate,
		    cmn_rpt->bb_pkt_cnt_bcn_i.pkt_cnt_beacon);

	/*RX Rate Distribution & RSSI*/
#if 1

	avg->rssi_cck_avg = (u8)HALBB_DIV(acc->rssi_cck_avg_acc, pkt_cnt->pkt_cnt_cck);
	avg->rssi_ofdm_avg = (u8)HALBB_DIV(acc->rssi_ofdm_avg_acc, pkt_cnt->pkt_cnt_ofdm);
	avg->rssi_t_avg = (u8)HALBB_DIV(acc->rssi_t_avg_acc, pkt_cnt->pkt_cnt_t);
		
	for (i = 0; i < HALBB_MAX_PATH; i++) {
		if (i >= bb->num_rf_path)
			break;

		avg->rssi_cck[i] = (u8)HALBB_DIV(acc->rssi_cck_acc[i], pkt_cnt->pkt_cnt_cck);
		avg->rssi_ofdm[i] = (u8)HALBB_DIV(acc->rssi_ofdm_acc[i], pkt_cnt->pkt_cnt_ofdm);
		avg->rssi_t[i] = (u8)HALBB_DIV(acc->rssi_t_acc[i], pkt_cnt->pkt_cnt_t);
		//BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used, "*rssi_ofdm_avg %02d =  rssi_ofdm_acc %02d / pkt_cnt_ofdm%02d}\n",
		//	avg->rssi_ofdm_avg, avg->rssi_ofdm[i], acc->rssi_ofdm_acc[i], pkt_cnt->pkt_cnt_ofdm);
	}

	/*@======[Lgcy-non-data]=============================================*/
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "[Lgcy-non-data] {%d, %d, %d, %d | %d, %d, %d, %d, %d, %d, %d, %d} {%d}\n",
		    pkt_cnt->pkt_cnt_legacy_non_data[0], pkt_cnt->pkt_cnt_legacy_non_data[1],
		    pkt_cnt->pkt_cnt_legacy_non_data[2], pkt_cnt->pkt_cnt_legacy_non_data[3],
		    pkt_cnt->pkt_cnt_legacy_non_data[4], pkt_cnt->pkt_cnt_legacy_non_data[5],
		    pkt_cnt->pkt_cnt_legacy_non_data[6], pkt_cnt->pkt_cnt_legacy_non_data[7],
		    pkt_cnt->pkt_cnt_legacy_non_data[8], pkt_cnt->pkt_cnt_legacy_non_data[9],
		    pkt_cnt->pkt_cnt_legacy_non_data[10], pkt_cnt->pkt_cnt_legacy_non_data[11],
		    pkt_cnt->pkt_cnt_else_non_data);

	/*@======CCK=========================================================*/

	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "*CCK     RSSI:{%02d| %02d,%02d} cnt:{%03d| %d, %d, %d, %d}\n",
		    avg->rssi_cck_avg >> 1,
		    avg->rssi_cck[0] >> 1, avg->rssi_cck[1] >> 1,
		    pkt_cnt->pkt_cnt_cck,
		    pkt_cnt->pkt_cnt_legacy[0], pkt_cnt->pkt_cnt_legacy[1],
		    pkt_cnt->pkt_cnt_legacy[2], pkt_cnt->pkt_cnt_legacy[3]);

	/*@======OFDM========================================================*/
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "*OFDM    RSSI:{%02d| %02d,%02d} cnt:{%03d| %d, %d, %d, %d, %d, %d, %d, %d}\n",
		    avg->rssi_ofdm_avg >> 1,
		    avg->rssi_ofdm[0] >> 1, avg->rssi_ofdm[1] >> 1,
		    pkt_cnt->pkt_cnt_ofdm,
		    pkt_cnt->pkt_cnt_legacy[4], pkt_cnt->pkt_cnt_legacy[5],
		    pkt_cnt->pkt_cnt_legacy[6], pkt_cnt->pkt_cnt_legacy[7],
		    pkt_cnt->pkt_cnt_legacy[8], pkt_cnt->pkt_cnt_legacy[9],
		    pkt_cnt->pkt_cnt_legacy[10], pkt_cnt->pkt_cnt_legacy[11]);

	/*@======HT==========================================================*/

	if (pkt_cnt->ht_pkt_not_zero) {
		for (i = 0; i < rate_num; i++) {
			ss_ofst = (i << 3);
			for (j = 0; j < HT_NUM_MCS ; j++) {
					pkt_cnt_ss += pkt_cnt->pkt_cnt_ht[ss_ofst + j];
			}

			if (pkt_cnt_ss == 0) {
				rssi_avg_tmp = 0;
				rssi_tmp[0] = 0;
				rssi_tmp[1] = 0;
			} else {
				rssi_avg_tmp = avg->rssi_t_avg >> 1;
				rssi_tmp[0] = avg->rssi_t[0] >> 1;
				rssi_tmp[1] = avg->rssi_t[1] >> 1;
			}

			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "*HT%02d:%02d RSSI:{%02d| %02d,%02d} cnt:{%03d| %d, %d, %d, %d, %d, %d, %d, %d}\n",
				    (ss_ofst), (ss_ofst + 7),
				    rssi_avg_tmp, rssi_tmp[0], rssi_tmp[1],
				    pkt_cnt_ss,
				    pkt_cnt->pkt_cnt_ht[ss_ofst + 0],
				    pkt_cnt->pkt_cnt_ht[ss_ofst + 1],
				    pkt_cnt->pkt_cnt_ht[ss_ofst + 2],
				    pkt_cnt->pkt_cnt_ht[ss_ofst + 3],
				    pkt_cnt->pkt_cnt_ht[ss_ofst + 4],
				    pkt_cnt->pkt_cnt_ht[ss_ofst + 5],
				    pkt_cnt->pkt_cnt_ht[ss_ofst + 6],
				    pkt_cnt->pkt_cnt_ht[ss_ofst + 7]);

			pkt_cnt_ss = 0;
		}
	}

	/*@======VHT==========================================================*/
	if (pkt_cnt->vht_pkt_not_zero) {
		for (i = 0; i < rate_num; i++) {
			ss_ofst = HE_VHT_NUM_MCS * i;

			for (j = 0; j < HE_VHT_NUM_MCS ; j++) {
				pkt_cnt_ss += pkt_cnt->pkt_cnt_vht[ss_ofst + j];
			}

			if (pkt_cnt_ss == 0) {
				rssi_avg_tmp = 0;
				rssi_tmp[0] = 0;
				rssi_tmp[1] = 0;
			} else {
				rssi_avg_tmp = avg->rssi_t_avg >> 1;
				rssi_tmp[0] = avg->rssi_t[0] >> 1;
				rssi_tmp[1] = avg->rssi_t[1] >> 1;
			}

			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "*VHT %d-S RSSI:{%02d| %02d,%02d} cnt:{%03d| %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				    (i + 1),
				    rssi_avg_tmp, rssi_tmp[0], rssi_tmp[1],
				    pkt_cnt_ss,
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 0],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 1],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 2],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 3],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 4],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 5],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 6],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 7],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 8],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 9],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 10],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 11]);

			pkt_cnt_ss = 0;
		}

	}

	/*@======HE==========================================================*/
	if (pkt_cnt->he_pkt_not_zero) {
		for (i = 0; i < rate_num; i++) {
			ss_ofst = HE_VHT_NUM_MCS * i;

			for (j = 0; j < HE_VHT_NUM_MCS ; j++) {
				pkt_cnt_ss += pkt_cnt->pkt_cnt_he[ss_ofst + j];
			}

			if (pkt_cnt_ss == 0) {
				rssi_avg_tmp = 0;
				rssi_tmp[0] = 0;
				rssi_tmp[1] = 0;
			} else {
				rssi_avg_tmp = avg->rssi_t_avg >> 1;
				rssi_tmp[0] = avg->rssi_t[0] >> 1;
				rssi_tmp[1] = avg->rssi_t[1] >> 1;
			}

			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "*HE %d-SS RSSI:{%02d| %02d,%02d} cnt:{%03d| %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				    (i + 1),
				    rssi_avg_tmp, rssi_tmp[0], rssi_tmp[1],
				    pkt_cnt_ss,
				    pkt_cnt->pkt_cnt_he[ss_ofst + 0],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 1],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 2],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 3],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 4],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 5],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 6],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 7],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 8],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 9],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 10],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 11]);

			pkt_cnt_ss = 0;
		}

	}

	/*@======SC_BW========================================================*/

	if (pkt_cnt->sc20_occur) {
		for (i = 0; i < rate_num; i++) {
			ss_ofst = 12 * i;

			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "*[Low BW 20M] %d-ss MCS[0:11] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				    (i + 1),
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 0],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 1],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 2],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 3],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 4],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 5],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 6],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 7],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 8],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 9],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 10],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 11]);
		}
	}

	if (pkt_cnt->sc40_occur) {
		for (i = 0; i < rate_num; i++) {
			ss_ofst = 12 * i;

			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "*[Low BW 40M] %d-ss MCS[0:11] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				    (i + 1),
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 0],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 1],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 2],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 3],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 4],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 5],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 6],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 7],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 8],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 9],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 10],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 11]);
		}
	}
#endif

	/*RX Utility*/
	avg_phy_rate = halbb_rx_avg_phy_rate(bb);
	utility = halbb_rx_utility(bb, avg_phy_rate, bb->num_rf_path, bb->hal_com->band[0].cur_chandef.bw);

	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "Avg_rx_rate = %d, rx_utility=( %d / 1000 )\n",
		    avg_phy_rate, utility);
}


void halbb_show_phy_hitogram_su_cnsl(struct bb_info *bb, u32 *_used,
						 char *output, u32 *_out_len)
{
	struct bb_cmn_rpt_info	*cmn_rpt = &bb->bb_cmn_rpt_i;
	struct bb_pkt_cnt_su_info *pkt_cnt = &cmn_rpt->bb_pkt_cnt_su_i;
	struct bb_physts_acc_info *acc = &cmn_rpt->bb_physts_acc_i;
	struct bb_physts_avg_info *avg = &cmn_rpt->bb_physts_avg_i;
	struct bb_physts_hist_info *hist = &cmn_rpt->bb_physts_hist_i;
	struct bb_physts_hist_th_info *hist_th = &cmn_rpt->bb_physts_hist_th_i;
	char buf[HALBB_SNPRINT_SIZE] = {0};
	u16 valid_cnt = pkt_cnt->pkt_cnt_t + pkt_cnt->pkt_cnt_ofdm;

	/*=== [EVM, SNR] =====================================================*/

	halbb_print_hist_2_buf_u8(bb, hist_th->evm_hist_th, BB_HIST_TH_SIZE, bb->dbg_buf,
			       HALBB_SNPRINT_SIZE);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "  %-8s %-9s  %s\n", "[TH]", "(Avg)", bb->dbg_buf);
	/*val*/
	avg->evm_1ss = (u8)HALBB_DIV(acc->evm_1ss, (pkt_cnt->pkt_cnt_1ss + pkt_cnt->pkt_cnt_ofdm));
	halbb_print_hist_2_buf(bb, hist->evm_1ss, BB_HIST_SIZE, bb->dbg_buf,
			       HALBB_SNPRINT_SIZE);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "%-9s (%02d.%03d)  %s\n", "[EVM_1ss]",
		    (avg->evm_1ss >> 2),
	       halbb_show_fraction_num(avg->evm_1ss & 0x3, 2), bb->dbg_buf);

	avg->evm_max = (u8)HALBB_DIV(acc->evm_max_acc, pkt_cnt->pkt_cnt_2ss);
	halbb_print_hist_2_buf(bb, hist->evm_max_hist, BB_HIST_SIZE, bb->dbg_buf,
			       HALBB_SNPRINT_SIZE);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "%-9s (%02d.%03d)  %s\n", "[EVM_max]",
		    (avg->evm_max >> 2),
		    halbb_show_fraction_num(avg->evm_max & 0x3, 2), bb->dbg_buf);
	
	avg->evm_min = (u8)HALBB_DIV(acc->evm_min_acc, pkt_cnt->pkt_cnt_2ss);
	halbb_print_hist_2_buf(bb, hist->evm_min_hist, BB_HIST_SIZE, bb->dbg_buf,
			       HALBB_SNPRINT_SIZE);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "%-9s (%02d.%03d)  %s\n", "[EVM_min]",
		    (avg->evm_min >> 2),
		    halbb_show_fraction_num(avg->evm_min & 0x3, 2), bb->dbg_buf);
	

	avg->snr_avg = (u8)HALBB_DIV(acc->snr_avg_acc, valid_cnt);
	halbb_print_hist_2_buf(bb, hist->snr_avg_hist, BB_HIST_SIZE, bb->dbg_buf,
			       HALBB_SNPRINT_SIZE);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "%-9s (%02d.000)  %s\n", "[SNR_avg]",
		    avg->snr_avg, bb->dbg_buf);

	/*=== [CN] ===========================================================*/
	/*Threshold*/
	halbb_print_hist_2_buf_u8(bb, hist_th->cn_hist_th, BB_HIST_TH_SIZE, bb->dbg_buf,
			       HALBB_SNPRINT_SIZE);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "  %-8s %-9s  %s\n", "[TH]", "(Avg)", bb->dbg_buf);
	/*val*/
	avg->cn_avg = (u8)HALBB_DIV(acc->cn_avg_acc, pkt_cnt->pkt_cnt_2ss);
	halbb_print_hist_2_buf(bb, hist->cn_avg_hist, BB_HIST_SIZE, bb->dbg_buf,
			       HALBB_SNPRINT_SIZE);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "%-9s (%02d.%03d)  %s\n", "[CN_avg]",
		    (avg->cn_avg >> 1),
		    halbb_show_fraction_num(avg->cn_avg & 0x1, 1), bb->dbg_buf);

	/*=== [CFO] ==========================================================*/
	/*Threshold*/
	halbb_print_hist_2_buf_u8(bb, hist_th->cfo_hist_th, BB_HIST_TH_SIZE, bb->dbg_buf,
			       HALBB_SNPRINT_SIZE);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "  %-8s %-9s  %s\n", "[TH]", "(Avg)", bb->dbg_buf);
	/*val*/
	avg->cfo_avg = (s16)HALBB_DIV(acc->cfo_avg_acc, valid_cnt);

	halbb_print_sign_frac_digit(bb, avg->cfo_avg, 16, 2, buf, HALBB_SNPRINT_SIZE);
	halbb_print_hist_2_buf(bb, hist->cfo_avg_hist, BB_HIST_SIZE, bb->dbg_buf,
			       HALBB_SNPRINT_SIZE);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "%-9s (%s K) %s\n", "[CFO_avg]",
		    buf, bb->dbg_buf);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "CFO_src: %s\n",
		    (bb->bb_cfo_trk_i.cfo_src == CFO_SRC_FD) ? "FD" : "Preamble");
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "valid_cnt = %d\n", valid_cnt);
}

void halbb_basic_dbg_msg_physts_mu_cnsl(struct bb_info *bb, u32 *_used,
					char *output, u32 *_out_len)
{
	struct bb_ch_info *ch = &bb->bb_ch_i;
	struct bb_link_info *link = &bb->bb_link_i;
	struct bb_cmn_rpt_info	*cmn_rpt = &bb->bb_cmn_rpt_i;
	struct bb_pkt_cnt_mu_info *pkt_cnt = &cmn_rpt->bb_pkt_cnt_mu_i;
	struct bb_rssi_mu_acc_info *acc = &cmn_rpt->bb_rssi_mu_acc_i;
	struct bb_rssi_mu_avg_info *avg = &cmn_rpt->bb_rssi_mu_avg_i;
	u8 rssi_avg_tmp = 0;
	u8 rssi_tmp[HALBB_MAX_PATH];
	u16 pkt_cnt_ss = 0;
	u8 i = 0, j =0;
	u8 rate_num = bb->num_rf_path, ss_ofst = 0;

	if (bb->bb_cmn_rpt_i.bb_pkt_cnt_mu_i.pkt_cnt_all == 0) {
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "NO MU pkt\n");
		return;
	}

	/*RX Rate*/
	halbb_print_rate_2_buff(bb, link->rx_rate_plurality_mu,
				RTW_GILTF_LGI_4XHE32, bb->dbg_buf, 32);

	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "Plurality_RxRate:%s (0x%x)\n",
		    bb->dbg_buf, link->rx_rate_plurality);

	/*RX Rate Distribution & RSSI*/

	avg->rssi_t_avg = (u8)HALBB_DIV(acc->rssi_t_avg_acc, pkt_cnt->pkt_cnt_all);
		
	for (i = 0; i < HALBB_MAX_PATH; i++) {
		if (i >= bb->num_rf_path)
			break;

		avg->rssi_t[i] = (u8)HALBB_DIV(acc->rssi_t_acc[i], pkt_cnt->pkt_cnt_all);
	}

	/*@======VHT==========================================================*/
	if (pkt_cnt->vht_pkt_not_zero) {
		for (i = 0; i < rate_num; i++) {
			ss_ofst = HE_VHT_NUM_MCS * i;

			for (j = 0; j < HE_VHT_NUM_MCS ; j++) {
				pkt_cnt_ss += pkt_cnt->pkt_cnt_vht[ss_ofst + j];
			}

			if (pkt_cnt_ss == 0) {
				rssi_avg_tmp = 0;
				rssi_tmp[0] = 0;
				rssi_tmp[1] = 0;
			} else {
				rssi_avg_tmp = avg->rssi_t_avg >> 1;
				rssi_tmp[0] = avg->rssi_t[0] >> 1;
				rssi_tmp[1] = avg->rssi_t[1] >> 1;
			}

			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "*[MU] VHT %d-S RSSI:{%02d| %02d,%02d} cnt:{%03d| %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				    (i + 1),
				    rssi_avg_tmp, rssi_tmp[0], rssi_tmp[1],
				    pkt_cnt_ss,
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 0],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 1],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 2],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 3],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 4],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 5],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 6],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 7],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 8],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 9],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 10],
				    pkt_cnt->pkt_cnt_vht[ss_ofst + 11]);

			pkt_cnt_ss = 0;
		}

	}

	/*@======HE==========================================================*/
	if (pkt_cnt->he_pkt_not_zero) {
		for (i = 0; i < rate_num; i++) {
			ss_ofst = HE_VHT_NUM_MCS * i;

			for (j = 0; j < HE_VHT_NUM_MCS ; j++) {
				pkt_cnt_ss += pkt_cnt->pkt_cnt_he[ss_ofst + j];
			}

			if (pkt_cnt_ss == 0) {
				rssi_avg_tmp = 0;
				rssi_tmp[0] = 0;
				rssi_tmp[1] = 0;
			} else {
				rssi_avg_tmp = avg->rssi_t_avg >> 1;
				rssi_tmp[0] = avg->rssi_t[0] >> 1;
				rssi_tmp[1] = avg->rssi_t[1] >> 1;
			}

			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "*[MU] HE %d-SS RSSI:{%02d| %02d,%02d} cnt:{%03d| %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				    (i + 1),
				    rssi_avg_tmp, rssi_tmp[0], rssi_tmp[1],
				    pkt_cnt_ss,
				    pkt_cnt->pkt_cnt_he[ss_ofst + 0],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 1],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 2],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 3],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 4],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 5],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 6],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 7],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 8],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 9],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 10],
				    pkt_cnt->pkt_cnt_he[ss_ofst + 11]);

			pkt_cnt_ss = 0;
		}

	}

	/*@======SC_BW========================================================*/
	
	if (pkt_cnt->sc20_occur) {
		for (i = 0; i < rate_num; i++) {
			ss_ofst = 12 * i;

			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "*[MU][Low BW 20M] %d-ss MCS[0:11] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				    (i + 1),
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 0],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 1],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 2],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 3],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 4],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 5],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 6],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 7],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 8],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 9],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 10],
				    pkt_cnt->pkt_cnt_sc20[ss_ofst + 11]);
		}
	}

	if (pkt_cnt->sc40_occur) {
		for (i = 0; i < rate_num; i++) {
			ss_ofst = 12 * i;

			BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
				    "*[MU][Low BW 40M] %d-ss MCS[0:11] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				    (i + 1),
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 0],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 1],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 2],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 3],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 4],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 5],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 6],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 7],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 8],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 9],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 10],
				    pkt_cnt->pkt_cnt_sc40[ss_ofst + 11]);
		}
	}
}

void halbb_dig_cmn_log_cnsl(struct bb_info *bb, u32 *_used,
			    char *output, u32 *_out_len)
{
	struct bb_dig_cr_info *cr = &bb->bb_dig_i.bb_dig_cr_i;
	u8 i = 0;
	u8 lna = 0, tia = 0, rxbb = 0;
	u8 ofdm_pd_th = 0, ofdm_pd_th_en = 0, cck_pd_th_en = 0;
	u8 rx_num_path = bb->hal_com->rfpath_rx_num;
	s8 cck_pd_th = 0;

	for (i = 0; i < rx_num_path; i++) {
		lna = halbb_get_lna_idx(bb, i);
		tia = halbb_get_tia_idx(bb, i);
		rxbb = halbb_get_rxb_idx(bb, i);
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[DIG][Path-%d] Get(lna,tia,rxb)=(%d,%d,%d)\n",
			    i, lna, tia, rxbb);
	}

	ofdm_pd_th = (u8)halbb_get_reg_cmn(bb, cr->seg0r_pd_lower_bound_a,
					   cr->seg0r_pd_lower_bound_a_m,
					   bb->bb_phy_idx);
	ofdm_pd_th_en = (u8)halbb_get_reg_cmn(bb, cr->seg0r_pd_spatial_reuse_en_a,
					      cr->seg0r_pd_spatial_reuse_en_a_m,
					      bb->bb_phy_idx);
	cck_pd_th = (s8)halbb_get_reg(bb, cr->rssi_nocca_low_th_a,
				      cr->rssi_nocca_low_th_a_m);
	cck_pd_th_en = (u8)halbb_get_reg(bb, cr->cca_rssi_lmt_en_a,
					 cr->cca_rssi_lmt_en_a_m);

	if ((bb->ic_type == BB_RTL8852A && bb->hal_com->cv < CCV) ||
	    (bb->ic_type == BB_RTL8852B && bb->hal_com->cv < CBV))
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "PD_low_bd_en(ofdm) : (%d), PD_low_bd(ofdm) = (-%d) dBm\n",
			    ofdm_pd_th_en, 102 - (ofdm_pd_th << 1));
	else
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "PD_low_bd_en(ofdm, cck) : (%d, %d), PD_low_bd(ofdm, cck) = (-%d, %d) dBm\n",
			    ofdm_pd_th_en, cck_pd_th_en, 102 - (ofdm_pd_th << 1),
			    cck_pd_th);
}

void halbb_reset_cnsl(struct bb_info *bb)
{
	if (!bb->bb_cmn_hooker->bb_cmn_dbg_i.cmn_log_2_cnsl_en)
		return;

	halbb_store_data(bb);
	#ifdef HALBB_STATISTICS_SUPPORT
	halbb_statistics_reset(bb);
	#endif
	halbb_cmn_info_rpt_reset(bb);
}

void halbb_basic_dbg_message_cnsl_dbg(struct bb_info *bb, char input[][16], u32 *_used,
				      char *output, u32 *_out_len)
{
	struct bb_link_info	*link = &bb->bb_link_i;
	struct bb_ch_info	*ch = &bb->bb_ch_i;
	struct bb_dbg_info	*dbg = &bb->bb_dbg_i;
	struct bb_physts_info	*physts = &bb->bb_physts_i;
	struct bb_cmn_dbg_info *cmn_dbg = &bb->bb_cmn_hooker->bb_cmn_dbg_i;
	enum channel_width bw = bb->hal_com->band[bb->bb_phy_idx].cur_chandef.bw;
	u32 var[10] = {0};
	u8 fc = bb->hal_com->band[bb->bb_phy_idx].cur_chandef.center_ch;
	u8 sta_cnt = 0;
	u8 i;

	if (_os_strcmp(input[1], "-h") == 0) {
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			 "{0:to log, 1:to consol}\n");
		return;
	}

	HALBB_SCAN(input[1], DCMD_DECIMAL, &var[0]);
	cmn_dbg->cmn_log_2_cnsl_en = (bool)var[0];

	/*base on 2021.12.28 halbb master (028C)*/
	if (!cmn_dbg->cmn_log_2_cnsl_en)
		return;

	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		"====[1. System] (%08d sec) (Ability=0x%08llx)\n",
	        bb->bb_sys_up_time, bb->support_ability);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		"[%s mode], TP{T,R,ALL}={%d, %d, %d}, BW:%d, CH_fc:%d\n",
	       ((bb->bb_watchdog_mode == BB_WATCHDOG_NORMAL) ? "Normal" :
	       ((bb->bb_watchdog_mode == BB_WATCHDOG_LOW_IO) ? "LowIO" : "NonIO")),
	       link->tx_tp, link->rx_tp, link->total_tp, 20 << bw, fc);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
	       "Phy:%d, linked: %d, Num_sta: %d, rssi_max/min= {%02d.%d, %02d.%d}, Noisy:%d\n",
	       bb->bb_phy_idx,
	       link->is_linked, bb->hal_com->assoc_sta_cnt,
	       ch->rssi_max >> 1, (ch->rssi_max & 1) * 5,
	       ch->rssi_min >> 1, (ch->rssi_min & 1) * 5,
	       ch->is_noisy);

	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "physts_cnt{all, 2_self, err_len, ok_ie, err_ie}={%d,%d,%d,%d,%d}\n",
		    physts->bb_physts_cnt_i.all_cnt, physts->bb_physts_cnt_i.is_2_self_cnt,
		    physts->bb_physts_cnt_i.ok_ie_cnt, physts->bb_physts_cnt_i.err_ie_cnt,
		    physts->bb_physts_cnt_i.err_len_cnt);

	for (i = 0; i< PHL_MAX_STA_NUM; i++) {
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "[%d] Linked macid=%d\n", i, bb->sta_exist[i]);
		sta_cnt++;
		if (sta_cnt >= bb->hal_com->assoc_sta_cnt)
			break;
	}
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used, "\n");
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used, "====[2. ENV Mntr]\n");
	halbb_env_mntr_log_cnsl(bb, _used, output, _out_len);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used, "\n");
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "====[3. PMAC]\n");
	halbb_basic_dbg_msg_pmac_cnsl(bb, _used, output, _out_len);
	halbb_crc32_cnt2_cmn_log_cnsl(bb, _used, output, _out_len);
	halbb_crc32_cnt3_cmn_log_cnsl(bb, _used, output, _out_len);
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "\n");
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "====[4. TX General]\n");

	if (bb->bb_link_i.is_linked) {
		halbb_basic_dbg_msg_tx_info_cnsl(bb, _used, output, _out_len);
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "\n");
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "====[5. RX General]\n");
		halbb_basic_dbg_msg_rx_info_cnsl(bb, _used, output, _out_len);
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "\n");
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "====[6. AVG RSSI/RxRate]\n");
		halbb_basic_dbg_msg_physts_su_cnsl(bb, _used, output, _out_len);
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "\n");
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "====[7. BB Hist]\n");
		halbb_show_phy_hitogram_su_cnsl(bb, _used, output, _out_len);
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "\n");
		BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
			    "====[8. [MU] AVG RSSI/RxRate]\n");
		halbb_basic_dbg_msg_physts_mu_cnsl(bb, _used, output, _out_len);
	}
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "\n");
	BB_DBG_CNSL(*_out_len, *_used, output + *_used, *_out_len - *_used,
		    "====[9. DIG]\n");
	halbb_dig_cmn_log_cnsl(bb, _used, output, _out_len);

	/*Reste Counter*/
	halbb_reset_cnsl(bb);
}
#endif

