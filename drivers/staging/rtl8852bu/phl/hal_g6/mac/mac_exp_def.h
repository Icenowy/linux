/** @file */
/******************************************************************************
 *
 * Copyright(c) 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#ifndef _MAC_EXP_DEF_H_
#define _MAC_EXP_DEF_H_

// for core layer ref definition defined here
/*--------------------Define -------------------------------------------*/
#define RTW_PHL_PROXY_V4_ADDR_NUM 4
#define RTW_PHL_PROXY_V6_ADDR_NUM 4

#define RTW_PHL_PROXY_MDNS_MAX_MACHINE_NUM 3
#define RTW_PHL_PROXY_MDNS_MAX_MACHINE_LEN 64
#define RTW_PHL_PROXY_MDNS_MAX_DOMAIN_LEN 64
#define RTW_PHL_PROXY_MDNS_MAX_SERVNAME_LEN 86
#define RTW_PHL_PROXY_MDNS_MAX_TARGET_LEN 64
#define RTW_PHL_PROXY_MDNS_MAX_SERV_NUM 10
#define RTW_PHL_PROXY_MDNS_RSP_HDR_LEN sizeof(struct rtw_hal_mac_proxy_mdns_rsp_hdr)

/*--------------------Define MACRO--------------------------------------*/
/*--------------------Define Enum---------------------------------------*/
enum rtw_mac_gfunc {
	RTW_MAC_GPIO_WL_PD,
	RTW_MAC_GPIO_BT_PD,
	RTW_MAC_GPIO_WL_EXTWOL,
	RTW_MAC_GPIO_BT_GPIO,
	RTW_MAC_GPIO_WL_SDIO_INT,
	RTW_MAC_GPIO_BT_SDIO_INT,
	RTW_MAC_GPIO_WL_FLASH,
	RTW_MAC_GPIO_BT_FLASH,
	RTW_MAC_GPIO_SIC,
	RTW_MAC_GPIO_LTE_UART,
	RTW_MAC_GPIO_LTE_3W,
	RTW_MAC_GPIO_WL_PTA,
	RTW_MAC_GPIO_BT_PTA,
	RTW_MAC_GPIO_MAILBOX,
	RTW_MAC_GPIO_WL_LED,
	RTW_MAC_GPIO_OSC,
	RTW_MAC_GPIO_XTAL_CLK,
	RTW_MAC_GPIO_EXT_XTAL_CLK,
	RTW_MAC_GPIO_DBG_GNT,
	RTW_MAC_GPIO_WL_RFE_CTRL,
	RTW_MAC_GPIO_BT_UART_RQB,
	RTW_MAC_GPIO_BT_WAKE_HOST,
	RTW_MAC_GPIO_HOST_WAKE_BT,
	RTW_MAC_GPIO_DBG,
	RTW_MAC_GPIO_WL_UART_TX,
	RTW_MAC_GPIO_WL_UART_RX,
	RTW_MAC_GPIO_WL_JTAG,
	RTW_MAC_GPIO_SW_IO,

	/* keep last */
	RTW_MAC_GPIO_LAST,
	RTW_MAC_GPIO_MAX = RTW_MAC_GPIO_LAST,
	RTW_MAC_GPIO_INVALID = RTW_MAC_GPIO_LAST,
	RTW_MAC_GPIO_DFLT = RTW_MAC_GPIO_LAST,
};

/*--------------------Define Struct-------------------------------------*/
struct hal_txmap_cfg {
	u32 macid:8;
	u32 n_tx_en:4;
	u32 map_a:2;
	u32 map_b:2;
	u32 map_c:2;
	u32 map_d:2;
	u32 rsvd:12;
};

struct rtw_phl_ax_ru_rate_ent {
	u8 dcm:1;
	u8 ss:3;
	u8 mcs:4;
};

struct rtw_phl_ax_rura_report {
	u8 rt_tblcol: 6;
	u8 prtl_alloc: 1;
	u8 rate_chg: 1;
};

struct rtw_phl_ax_ulru_out_sta_ent {
	u8 dropping: 1;
	u8 tgt_rssi: 7;
	u8 mac_id;
	u8 ru_pos;
	u8 coding: 1;
	u8 vip_flag: 1;
	u8 rsvd1: 6;
	u16 bsr_length: 15;
	u16 rsvd2: 1;
	struct rtw_phl_ax_ru_rate_ent rate;
	struct rtw_phl_ax_rura_report rpt;
};

struct  rtw_phl_ax_tbl_hdr {
	u8 rw:1;
	u8 idx:7;
	u16 offset:5;
	u16 len:10;
	u16 type:1;
};

#define RTW_PHL_MAX_RU_NUM 8
struct  rtw_phl_ax_ulrua_output {
	u8 ru2su: 1;
	u8 ppdu_bw: 2;
	u8 gi_ltf: 3;
	u8 stbc: 1;
	u8 doppler: 1;
	u8 n_ltf_and_ma: 3;
	u8 sta_num: 4;
	u8 rsvd1: 1;
	u16 rf_gain_fix: 1;
	u16 rf_gain_idx: 10;
	u16 tb_t_pe_nom: 2;
	u16 rsvd2: 3;

	u32 grp_mode: 1;
	u32 grp_id: 6;
	u32 fix_mode: 1;
	u32 rsvd3: 24;
	struct  rtw_phl_ax_ulru_out_sta_ent sta[RTW_PHL_MAX_RU_NUM];
};

struct  rtw_phl_ul_macid_info {
	u8 macid;
	u8 pref_AC:2;
	u8 rsvd:6;
};

struct  rtw_phl_ul_mode_cfg {
	u32 mode:2; /* 0: peoridic ; 1: normal ; 2: non_tgr; 3 tf_peoridic;*/
	u32 interval:6; /* unit: sec */
	u32 bsr_thold:8;
	u32 storemode:2;
	u32 rsvd:14;
};

struct  rtw_phl_ax_ul_fixinfo {
	struct  rtw_phl_ax_tbl_hdr tbl_hdr;
	struct  rtw_phl_ul_mode_cfg cfg;

	u32 ndpa_dur:16;
	u32 tf_type:3;
	u32 sig_ta_pkten:1;
	u32 sig_ta_pktsc:4;
	u32 murts_flag:1;
	u32 ndpa:2;
	u32 snd_pkt_sel:2;
	u32 gi_ltf:3;

	u32 data_rate:9;
	u32 data_er:1;
	u32 data_bw:2;
	u32 data_stbc:2;
	u32 data_ldpc:1;
	u32 data_dcm:1;
	u32 apep_len:12;
	u32 more_tf:1;
	u32 data_bw_er:1;
	u32 istwt:1;
	u32 ul_logo_test:1;

	u32 multiport_id:3;
	u32 mbssid:4;
	u32 txpwr_mode:3;
	u32 ulfix_usage:3;
	u32 twtgrp_stanum_sel:2;
	u32 store_idx:4;
	u32 rsvd1:13;
	struct  rtw_phl_ul_macid_info sta[RTW_PHL_MAX_RU_NUM];
	struct  rtw_phl_ax_ulrua_output ulrua;
};

struct rtw_hal_mac_ax_cctl_info {
	/* dword 0 */
	u32 datarate:9;
	u32 force_txop:1;
	u32 data_bw:2;
	u32 data_gi_ltf:3;
	u32 darf_tc_index:1;
	u32 arfr_ctrl:4;
	u32 acq_rpt_en:1;
	u32 mgq_rpt_en:1;
	u32 ulq_rpt_en:1;
	u32 twtq_rpt_en:1;
	u32 rsvd0:1;
	u32 disrtsfb:1;
	u32 disdatafb:1;
	u32 tryrate:1;
	u32 ampdu_density:4;
	/* dword 1 */
	u32 data_rty_lowest_rate:9;
	u32 ampdu_time_sel:1;
	u32 ampdu_len_sel:1;
	u32 rts_txcnt_lmt_sel:1;
	u32 rts_txcnt_lmt:4;
	u32 rtsrate:9;
	u32 rsvd1:2;
	u32 vcs_stbc:1;
	u32 rts_rty_lowest_rate:4;
	/* dword 2 */
	u32 data_tx_cnt_lmt:6;
	u32 data_txcnt_lmt_sel:1;
	u32 max_agg_num_sel:1;
	u32 rts_en:1;
	u32 cts2self_en:1;
	u32 cca_rts:2;
	u32 hw_rts_en:1;
	u32 rts_drop_data_mode:2;
	u32 preld_en:1;
	u32 ampdu_max_len:11;
	u32 ul_mu_dis:1;
	u32 ampdu_max_time:4;
	/* dword 3 */
	u32 max_agg_num:9;
	u32 ba_bmap:2;
	u32 rsvd3:5;
	u32 vo_lftime_sel:3;
	u32 vi_lftime_sel:3;
	u32 be_lftime_sel:3;
	u32 bk_lftime_sel:3;
	u32 sectype:4;
	/* dword 4 */
	u32 multi_port_id:3;
	u32 bmc:1;
	u32 mbssid:4;
	u32 navusehdr:1;
	u32 txpwr_mode:3;
	u32 data_dcm:1;
	u32 data_er:1;
	u32 data_ldpc:1;
	u32 data_stbc:1;
	u32 a_ctrl_bqr:1;
	u32 a_ctrl_uph:1;
	u32 a_ctrl_bsr:1;
	u32 a_ctrl_cas:1;
	u32 data_bw_er:1;
	u32 lsig_txop_en:1;
	u32 rsvd4:5;
	u32 ctrl_cnt_vld:1;
	u32 ctrl_cnt:4;
	/* dword 5 */
	u32 resp_ref_rate:9;
	u32 rsvd5:3;
	u32 all_ack_support:1;
	u32 bsr_queue_size_format:1;
	u32 bsr_om_upd_en:1;
	u32 macid_fwd_idc:1;
	u32 ntx_path_en:4;
	u32 path_map_a:2;
	u32 path_map_b:2;
	u32 path_map_c:2;
	u32 path_map_d:2;
	u32 antsel_a:1;
	u32 antsel_b:1;
	u32 antsel_c:1;
	u32 antsel_d:1;
	/* dword 6 */
	u32 addr_cam_index:8;
	u32 paid:9;
	u32 uldl:1;
	u32 doppler_ctrl:2;
	u32 nominal_pkt_padding:2;
	u32 nominal_pkt_padding40:2;
	u32 txpwr_tolerence:6;
	/*u32 rsvd9:2;*/
	u32 nominal_pkt_padding80:2;
	/* dword 7 */
	u32 nc:3;
	u32 nr:3;
	u32 ng:2;
	u32 cb:2;
	u32 cs:2;
	u32 csi_txbf_en:1;
	u32 csi_stbc_en:1;
	u32 csi_ldpc_en:1;
	u32 csi_para_en:1;
	u32 csi_fix_rate:9;
	u32 csi_gi_ltf:3;
	u32 nominal_pkt_padding160:2;
	u32 csi_bw:2;
};

#pragma pack(push)
#pragma pack(1)

struct rtw_hal_mac_proxyofld {
	u8 proxy_en:1;
	u8 arp_rsp:1;
	u8 ns_rsp:1;
	u8 icmp_v4_rsp:1;
	u8 icmp_v6_rsp:1;
	u8 netbios_rsp:1;
	u8 llmnr_v4_rsp:1;
	u8 llmnr_v6_rsp:1;
	u8 snmp_v4_rsp:1;
	u8 snmp_v6_rsp:1;
	u8 snmp_v4_wake:1;
	u8 snmp_v6_wake:1;
	u8 ssdp_v4_wake:1;
	u8 ssdp_v6_wake:1;
	u8 wsd_v4_wake:1;
	u8 wsd_v6_wake:1;
	u8 slp_v4_wake:1;
	u8 slp_v6_wake:1;
	u8 mdns_v4_rsp:1;
	u8 mdns_v6_rsp:1;
	u8 target_mac_wake:1;
	u8 lltd_wake:1;
	u8 mdns_v4_wake:1;
	u8 mdns_v6_wake:1;
	u8 rsvd0;
	u8 v4addr[RTW_PHL_PROXY_V4_ADDR_NUM][4];
	u8 v6addr[RTW_PHL_PROXY_V6_ADDR_NUM][16];
};

struct rtw_hal_mac_proxy_mdns_machine {
	u32 len;
	u8 name[RTW_PHL_PROXY_MDNS_MAX_MACHINE_LEN];
};

struct rtw_hal_mac_proxy_mdns_rsp_hdr {
	u8 rspTypeB0;
	u8 rspTypeB1;
	u8 cache_class_B0;
	u8 cache_class_B1;
	u32 ttl;
	u16 dataLen;
};

struct rtw_hal_mac_proxy_mdns_a {
	struct rtw_hal_mac_proxy_mdns_rsp_hdr hdr;
	u8 ipv4Addr[4];
};

struct rtw_hal_mac_proxy_mdns_aaaa {
	struct rtw_hal_mac_proxy_mdns_rsp_hdr hdr;
	u8 ipv6Addr[16];
};

struct rtw_hal_mac_proxy_mdns_ptr {
	struct rtw_hal_mac_proxy_mdns_rsp_hdr hdr;
	u8 domain[RTW_PHL_PROXY_MDNS_MAX_DOMAIN_LEN];
	u8 compression;
	u8 compression_loc;
};

struct rtw_hal_mac_proxy_mdns {
	u8 ipv4_pktid;
	u8 ipv6_pktid;
	u8 num_supported_services;
	u8 num_machine_names;
	u8 macid;
	u8 serv_pktid[RTW_PHL_PROXY_MDNS_MAX_SERV_NUM];
	u8 rsvd;
	struct rtw_hal_mac_proxy_mdns_machine machines[RTW_PHL_PROXY_MDNS_MAX_MACHINE_NUM];
	struct rtw_hal_mac_proxy_mdns_a a_rsp;
	struct rtw_hal_mac_proxy_mdns_aaaa aaaa_rsp;
	struct rtw_hal_mac_proxy_mdns_ptr ptr_rsp;
};


struct rtw_hal_mac_proxy_mdns_txt {
	struct rtw_hal_mac_proxy_mdns_rsp_hdr hdr;
	u16 content_len;
	u8 *content;
};

struct rtw_hal_mac_proxy_mdns_service {
	u8 name_len;
	u8 *name; //should contains 1 byte of delimiter at the end
	struct rtw_hal_mac_proxy_mdns_rsp_hdr hdr;
	u16 priority;
	u16 weight;
	u16 port;
	u8 target_len;
	u8 *target;
	u8 compression;
	u8 compression_loc;
	u8 has_txt;
	u8 txt_pktid;
	u8 txt_id;
};

#pragma pack(pop)

#endif
