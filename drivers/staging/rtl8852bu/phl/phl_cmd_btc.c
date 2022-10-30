/******************************************************************************
 *
 * Copyright(c) 2019 Realtek Corporation.
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
 *****************************************************************************/
#define _PHL_CMD_BTC_C_
#include "phl_headers.h"

#ifdef CONFIG_BTCOEX
#ifdef CONFIG_PHL_CMD_BTC
static void _fail_hdlr(void *phl, struct phl_msg *msg)
{
}

static void _hdl_tmr(void *phl, struct phl_msg *msg)
{
	struct phl_info_t *phl_info = (struct phl_info_t *)phl;

	rtw_hal_btc_timer(phl_info->hal, (void *)msg->inbuf);
}

static void _hdl_role_notify(void *phl, struct phl_msg *msg)
{
	struct phl_info_t *phl_info = (struct phl_info_t *)phl;
	struct rtw_role_cmd *rcmd = NULL;
	struct rtw_wifi_role_t *wrole = NULL;
	struct rtw_phl_stainfo_t *sta = NULL;

	if (msg->inbuf && (msg->inlen == sizeof(struct rtw_role_cmd))) {
		rcmd  = (struct rtw_role_cmd *)msg->inbuf;
		wrole = rcmd->wrole;
		sta = rtw_phl_get_stainfo_self(phl_info, wrole);

		rtw_hal_btc_update_role_info_ntfy(phl_info->hal,
			rcmd->wrole->id, wrole, sta, rcmd->rstate);
	} else {
		PHL_ERR("%s: invalid msg, buf = %p, len = %d\n",
			__func__, msg->inbuf, msg->inlen);
	}
}

static void _hdl_pkt_evt_notify(void *phl, struct phl_msg *msg)
{
	struct phl_info_t *phl_info = (struct phl_info_t *)phl;
	enum phl_pkt_evt_type *pkt_evt_type = NULL;
	u8 *cmd = NULL;
	u32 cmd_len = 0;
	enum rtw_phl_status pstatus = RTW_PHL_STATUS_SUCCESS;

	pstatus = phl_cmd_get_cur_cmdinfo(phl_info, msg->band_idx,
					msg, &cmd, &cmd_len);
	if (pstatus != RTW_PHL_STATUS_SUCCESS) {
		PHL_ERR("%s: get cmd info fail, status = %d\n",
			__func__, pstatus);
		return;
	}

	pkt_evt_type = (enum phl_pkt_evt_type *)cmd;
	rtw_hal_btc_packet_event_ntfy(phl_info->hal, *pkt_evt_type);
}

static enum phl_mdl_ret_code _btc_cmd_init(void *phl, void *dispr,
						  void **priv)
{
	PHL_INFO("[BTCCMD], %s(): \n", __func__);

	*priv = phl;
	return MDL_RET_SUCCESS;
}

static void _btc_cmd_deinit(void *dispr, void *priv)
{
	PHL_INFO("[BTCCMD], %s(): \n", __func__);
}

static enum phl_mdl_ret_code _btc_cmd_start(void *dispr, void *priv)
{
	enum phl_mdl_ret_code ret = MDL_RET_SUCCESS;

	PHL_INFO("[BTCCMD], %s(): \n", __func__);

	return ret;
}

static enum phl_mdl_ret_code _btc_cmd_stop(void *dispr, void *priv)
{
	enum phl_mdl_ret_code ret = MDL_RET_SUCCESS;

	PHL_INFO("[BTCCMD], %s(): \n", __func__);

	return ret;
}

static enum phl_mdl_ret_code
_btc_internal_pre_msg_hdlr(struct phl_info_t *phl_info,
                           void *dispr,
                           struct phl_msg *msg)
{
	enum phl_mdl_ret_code ret = MDL_RET_IGNORE;
	enum phl_msg_evt_id evt_id = MSG_EVT_ID_FIELD(msg->msg_id);

	/*PHL_INFO("[BTCCMD], msg->band_idx = %d,  msg->msg_id = 0x%x\n",
		msg->band_idx, msg->msg_id);*/

	switch(evt_id) {
	case MSG_EVT_BTC_REQ_BT_SLOT:
		PHL_INFO("[BTCCMD], MSG_EVT_BTC_REQ_BT_SLOT \n");
		ret = MDL_RET_SUCCESS;
		break;

	default:
		break;
	}

	return ret;
}

static enum phl_mdl_ret_code
_btc_internal_post_msg_hdlr(struct phl_info_t *phl_info,
                            void *dispr,
                            struct phl_msg *msg)
{
	enum phl_mdl_ret_code ret = MDL_RET_IGNORE;
	enum phl_msg_evt_id evt_id = MSG_EVT_ID_FIELD(msg->msg_id);

	switch(evt_id) {
	case MSG_EVT_BTC_TMR:
		PHL_DBG("[BTCCMD], MSG_EVT_BTC_TMR \n");
		_hdl_tmr(phl_info, msg);
		ret = MDL_RET_SUCCESS;
		break;

	case MSG_EVT_BTC_FWEVNT:
		PHL_DBG("[BTCCMD], MSG_EVT_BTC_FWEVNT \n");
		rtw_hal_btc_fwinfo_ntfy(phl_info->hal);
		ret = MDL_RET_SUCCESS;
		break;

	default:
		break;
	}

	return ret;
}

static enum phl_mdl_ret_code
_btc_internal_msg_hdlr(struct phl_info_t *phl_info,
                       void *dispr,
                       struct phl_msg *msg)
{
	enum phl_mdl_ret_code ret = MDL_RET_FAIL;

	if (IS_MSG_IN_PRE_PHASE(msg->msg_id))
		ret = _btc_internal_pre_msg_hdlr(phl_info, dispr, msg);
	else
		ret = _btc_internal_post_msg_hdlr(phl_info, dispr, msg);

	return ret;
}

static enum phl_mdl_ret_code
_btc_external_pre_msg_hdlr(struct phl_info_t *phl_info,
                           void *dispr,
                           struct phl_msg *msg)
{
	enum phl_mdl_ret_code ret = MDL_RET_IGNORE;
	enum phl_msg_evt_id evt_id = MSG_EVT_ID_FIELD(msg->msg_id);
	enum band_type band = BAND_ON_5G;
	struct rtw_hal_com_t *hal_com = rtw_hal_get_halcom(phl_info->hal);
	enum phl_phy_idx phy_idx = HW_PHY_0;

	/*PHL_INFO("[BTCCMD], msg->band_idx = %d,  msg->msg_id = 0x%x\n",
		msg->band_idx, msg->msg_id);*/

	switch(evt_id) {
		case MSG_EVT_SCAN_START:
			if (MSG_MDL_ID_FIELD(msg->msg_id) != PHL_FG_MDL_SCAN)
				break;

			if (msg->band_idx == HW_BAND_1)
				phy_idx = HW_PHY_1;
			PHL_INFO("[BTCCMD], MSG_EVT_SCAN_START \n");
			band = hal_com->band[msg->band_idx].cur_chandef.band;
			rtw_hal_btc_scan_start_ntfy(phl_info->hal, phy_idx, band);

			ret = MDL_RET_SUCCESS;
			break;

		case MSG_EVT_CONNECT_START:
			if (MSG_MDL_ID_FIELD(msg->msg_id) != PHL_FG_MDL_CONNECT)
				break;

			PHL_INFO("[BTCCMD], MSG_EVT_CONNECT_START \n");
			ret = MDL_RET_SUCCESS;
			break;

		case MSG_EVT_PKT_EVT_NTFY:
			PHL_INFO("[BTCCMD], MSG_EVT_PKT_EVT_NTFY \n");
			_hdl_pkt_evt_notify(phl_info, msg);
			ret = MDL_RET_SUCCESS;
			break;

		default:
			break;
	}

	return ret;
}

static enum phl_mdl_ret_code
_btc_external_post_msg_hdlr(struct phl_info_t *phl_info,
                            void *dispr,
                            struct phl_msg *msg)
{
	enum phl_mdl_ret_code ret = MDL_RET_IGNORE;
	enum phl_msg_evt_id evt_id = MSG_EVT_ID_FIELD(msg->msg_id);
	struct hal_info_t *hal_info = (struct hal_info_t *)phl_info->hal;
	enum phl_phy_idx phy_idx = HW_PHY_0;

	switch(evt_id) {
		case MSG_EVT_SCAN_END:
			if (MSG_MDL_ID_FIELD(msg->msg_id) != PHL_FG_MDL_SCAN)
				break;

			if (msg->band_idx == HW_BAND_1)
				phy_idx = HW_PHY_1;
			PHL_DBG("[BTCCMD], MSG_EVT_SCAN_END \n");
			rtw_hal_btc_scan_finish_ntfy(hal_info, phy_idx);
			ret = MDL_RET_SUCCESS;
			break;

		case MSG_EVT_CONNECT_END:
			if (MSG_MDL_ID_FIELD(msg->msg_id) != PHL_FG_MDL_CONNECT)
				break;

			PHL_DBG("[BTCCMD], MSG_EVT_CONNECT_END \n");
			ret = MDL_RET_SUCCESS;
			break;

		case MSG_EVT_ROLE_NTFY:
			if (MSG_MDL_ID_FIELD(msg->msg_id) != PHL_MDL_MRC)
				break;

			PHL_DBG("[BTCCMD], MSG_EVT_ROLE_NTFY \n");
			_hdl_role_notify(phl_info, msg);
			ret = MDL_RET_SUCCESS;
			break;

		case MSG_EVT_BTC_TMR:
			PHL_DBG("[BTCCMD], MSG_EVT_BTC_TMR \n");
			_hdl_tmr(phl_info, msg);
			ret = MDL_RET_SUCCESS;
			break;

		case MSG_EVT_BTC_FWEVNT:
			PHL_DBG("[BTCCMD], MSG_EVT_BTC_FWEVNT \n");
			rtw_hal_btc_fwinfo_ntfy(phl_info->hal);
			ret = MDL_RET_SUCCESS;
			break;

		default:
			break;
	}

	return ret;
}

static enum phl_mdl_ret_code
_btc_external_msg_hdlr(struct phl_info_t *phl_info,
                       void *dispr,
                       struct phl_msg *msg)
{
	enum phl_mdl_ret_code ret = MDL_RET_FAIL;

	if (IS_MSG_IN_PRE_PHASE(msg->msg_id))
		ret = _btc_external_pre_msg_hdlr(phl_info, dispr, msg);
	else
		ret = _btc_external_post_msg_hdlr(phl_info, dispr, msg);

	return ret;
}

static enum phl_mdl_ret_code
_btc_msg_hdlr(void *dispr, void *priv, struct phl_msg *msg)
{
	enum phl_mdl_ret_code ret = MDL_RET_IGNORE;
	struct phl_info_t *phl_info = (struct phl_info_t *)priv;

	FUNCIN();

	if (IS_MSG_FAIL(msg->msg_id)) {
		PHL_TRACE(COMP_PHL_DBG, _PHL_WARNING_,
			  "%s: cmd dispatcher notify cmd failure: 0x%x.\n",
			   __FUNCTION__, msg->msg_id);
		_fail_hdlr(phl_info, msg);
		FUNCOUT();
		return MDL_RET_FAIL;
	}

	if (IS_PRIVATE_MSG(msg->msg_id)) {
		FUNCOUT();
		return ret;
	}

	switch(MSG_MDL_ID_FIELD(msg->msg_id)) {
		case PHL_MDL_BTC:
			ret = _btc_internal_msg_hdlr(phl_info, dispr, msg);
			break;

		default:
			ret = _btc_external_msg_hdlr(phl_info, dispr, msg);
			break;
	}

	FUNCOUT();
	return ret;
}

static enum phl_mdl_ret_code
_btc_set_info(void *dispr, void *priv,
			 struct phl_module_op_info *info)
{
	PHL_INFO("[BTCCMD], %s(): \n", __func__);

	return MDL_RET_SUCCESS;
}

static enum phl_mdl_ret_code
_btc_query_info(void *dispr, void *priv,
			   struct phl_module_op_info *info)
{
	PHL_INFO("[BTCCMD], %s(): \n", __func__);

	return MDL_RET_SUCCESS;
}

static void _btc_set_stbc(struct phl_info_t *phl_info, u8 *buf)
{
	struct rtw_hal_com_t *hal_com = rtw_hal_get_halcom(phl_info->hal);

	hal_com->btc_ctrl.disable_rx_stbc = buf[0];
	PHL_INFO("[BTCCMD], %s(): disable_rx_stbc(%d) \n",
			__func__, hal_com->btc_ctrl.disable_rx_stbc);
}

enum rtw_phl_status phl_register_btc_module(struct phl_info_t *phl_info)
{
	enum rtw_phl_status phl_status = RTW_PHL_STATUS_FAILURE;
	struct phl_bk_module_ops bk_ops = {0};

	PHL_INFO("[BTCCMD], %s(): \n", __func__);

	bk_ops.init = _btc_cmd_init;
	bk_ops.deinit = _btc_cmd_deinit;
	bk_ops.start = _btc_cmd_start;
	bk_ops.stop = _btc_cmd_stop;
	bk_ops.msg_hdlr = _btc_msg_hdlr;
	bk_ops.set_info = _btc_set_info;
	bk_ops.query_info = _btc_query_info;

	phl_status = phl_disp_eng_register_module(phl_info, HW_BAND_0,
						PHL_MDL_BTC, &bk_ops);
	if (RTW_PHL_STATUS_SUCCESS != phl_status) {
		PHL_ERR("Failed to register BTC module in cmd dispr of hw band 0\n");
	}

	return phl_status;
}

static void
_phl_btc_req_evt_done(void* priv, struct phl_msg* msg)
{
	struct phl_info_t *phl_info = (struct phl_info_t *)priv;

	if (msg->inbuf && msg->inlen) {
		_os_kmem_free(phl_to_drvpriv(phl_info),
			msg->inbuf, msg->inlen);
	}
}

static bool
_btc_send_hub_msg(struct phl_info_t *phl_info, u8 *buf, u16 evt_id, u32 len)
{
	struct phl_msg msg = {0};
	struct phl_msg_attribute attr = {0};
	struct rtw_btc_req_msg *btc_req_msg = NULL;

	if (len > BTC_MAX_DATA_SIZE) {
		PHL_ERR("%s: Buf len exeeds Max.\n", __func__);
		return false;
	}

	btc_req_msg = (struct rtw_btc_req_msg *)_os_kmem_alloc(
			phl_to_drvpriv(phl_info), sizeof(struct rtw_btc_req_msg));
	if (btc_req_msg == NULL) {
		PHL_ERR("%s: alloc btc msg fail.\n", __func__);
		return false;
	}

	SET_MSG_MDL_ID_FIELD(msg.msg_id, PHL_MDL_BTC);
	SET_MSG_EVT_ID_FIELD(msg.msg_id,
		MSG_EVT_BTC_REQ);

	if ((len != 0) && (len <= BTC_MAX_DATA_SIZE))
		_os_mem_cpy(phl_to_drvpriv(phl_info), btc_req_msg->buf, buf, len);
	btc_req_msg->len = len;
	msg.inbuf = (u8 *)btc_req_msg;
	msg.inlen = sizeof(struct rtw_btc_req_msg);
	attr.completion.completion = _phl_btc_req_evt_done;
	attr.completion.priv = phl_info;

	switch (evt_id) {
	case BTC_HMSG_BT_LINK_CHG:
		btc_req_msg->type = EVT_BTC_LINK_CHG;
		break;
	default:
		PHL_ERR("%s: Unknown msg !\n", __func__);
		_os_kmem_free(phl_to_drvpriv(phl_info), btc_req_msg,
				sizeof(struct rtw_btc_req_msg));
		return false;
	}

	if (phl_msg_hub_send(phl_info, &attr, &msg) !=
				RTW_PHL_STATUS_SUCCESS) {
		PHL_ERR("%s: [BTC] msg_hub_send failed !\n", __func__);
		_os_kmem_free(phl_to_drvpriv(phl_info), btc_req_msg,
				sizeof(struct rtw_btc_req_msg));
		return false;
	} else {
		return true;
	}

}

bool rtw_phl_btc_send_cmd(struct rtw_phl_com_t *phl_com,
				u8 *buf, u32 len, u16 ev_id)
{
	struct phl_info_t *phl_info = phl_com->phl_priv;
	u8 band_idx = HW_BAND_0;
	struct phl_msg msg = {0};
	struct phl_msg_attribute attr = {0};

	msg.inbuf = buf;
	msg.inlen = len;
	SET_MSG_MDL_ID_FIELD(msg.msg_id, PHL_MDL_BTC);
	msg.band_idx = band_idx;
	switch (ev_id) {
	case BTC_HMSG_TMR_EN:
		SET_MSG_EVT_ID_FIELD(msg.msg_id,
			MSG_EVT_BTC_TMR);
		break;
	case BTC_HMSG_SET_BT_REQ_SLOT:
		SET_MSG_EVT_ID_FIELD(msg.msg_id,
			MSG_EVT_BTC_REQ_BT_SLOT);
		break;
	case BTC_HMSG_FW_EV:
		SET_MSG_EVT_ID_FIELD(msg.msg_id,
			MSG_EVT_BTC_FWEVNT);
		break;
	case BTC_HMSG_BT_LINK_CHG:
		return _btc_send_hub_msg(phl_info, buf, ev_id, len);
	case BTC_HMSG_SET_BT_REQ_STBC:
		_btc_set_stbc(phl_info, buf);
		return true;
	default:
		PHL_ERR("%s: Unknown msg !\n", __func__);
		return false;
	}

	if (phl_disp_eng_send_msg(phl_info, &msg, &attr, NULL) !=
				RTW_PHL_STATUS_SUCCESS) {
		PHL_ERR("%s: [BTC] dispr_send_msg failed !\n", __func__);
		return false;
	}

	return true;
}
#endif /*CONFIG_PHL_CMD_BTC*/

#ifndef CONFIG_FSM
int rtw_phl_btc_notify(void *phl, enum RTW_PHL_BTC_NOTIFY notify,
				struct rtw_phl_btc_ntfy *ntfy)
{
	PHL_ERR("CMD_BTC not support :%s\n", __func__);
	return 0;
}
void rtw_phl_btc_role_notify(void *phl, u8 role_id, enum role_state rstate)
{
	struct rtw_phl_btc_ntfy ntfy = {0};
	struct rtw_phl_btc_role_info_param *prinfo = &ntfy.u.rinfo;

	prinfo->role_id = role_id;
	prinfo->rstate = rstate;

	ntfy.notify = PHL_BTC_NTFY_ROLE_INFO;
	ntfy.ops = NULL;
	ntfy.priv = NULL;
	ntfy.ntfy_cb = NULL;

	rtw_phl_btc_notify(phl, ntfy.notify, &ntfy);
}

void rtw_phl_btc_hub_msg_hdl(void *phl, struct phl_msg *msg)
{
}
#endif

#endif /*CONFIG_BTCOEX*/

