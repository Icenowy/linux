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
#include "_usb.h"
#include "../mac_ax.h"

#if MAC_AX_USB_SUPPORT

u32 usb_flush_mode(struct mac_ax_adapter *adapter, u8 mode)
{
	u32 reg, val32;

	if (is_chip_id(adapter, MAC_AX_CHIP_ID_8852A))
		reg = R_AX_USB_WLAN0_1;
	else if (is_chip_id(adapter, MAC_AX_CHIP_ID_8852B))
		reg = R_AX_USB_WLAN0_1;
	else if (is_chip_id(adapter, MAC_AX_CHIP_ID_8852C))
		reg = R_AX_USB_WLAN0_1_V1;
	else if (is_chip_id(adapter, MAC_AX_CHIP_ID_8192XB))
		reg = R_AX_USB_WLAN0_1_V1;
	else
		return MACCHIPID;

	if (mode == MAC_AX_FUNC_DIS) {
		val32 = PLTFM_REG_R32(reg) & ~(B_AX_USBRX_RST | B_AX_USBTX_RST);
		PLTFM_REG_W32(reg, val32);
		return MACSUCCESS;
	} else if (mode == MAC_AX_FUNC_EN) {
		val32 = PLTFM_REG_R32(reg) | B_AX_USBRX_RST | B_AX_USBTX_RST;
		PLTFM_REG_W32(reg, val32);
		return MACSUCCESS;
	} else {
		return MACLV1STEPERR;
	}
}

u32 get_usb_mode(struct mac_ax_adapter *adapter)
{
	u32 reg, val32, hs;

	if (is_chip_id(adapter, MAC_AX_CHIP_ID_8852A))
		reg = R_AX_USB_STATUS;
	else if (is_chip_id(adapter, MAC_AX_CHIP_ID_8852B))
		reg = R_AX_USB_STATUS;
	else if (is_chip_id(adapter, MAC_AX_CHIP_ID_8852C))
		reg = R_AX_USB_STATUS_V1;
	else if (is_chip_id(adapter, MAC_AX_CHIP_ID_8192XB))
		reg = R_AX_USB_STATUS_V1;
	else
		return MACCHIPID;

	val32 = PLTFM_REG_R32(reg) & B_AX_R_USB2_SEL;
	hs = PLTFM_REG_R32(reg) & B_AX_MODE_HS;
	if (val32 == B_AX_R_USB2_SEL)
		val32 = MAC_AX_USB3;
	else if ((val32 != B_AX_R_USB2_SEL) && (hs == B_AX_MODE_HS))
		val32 = MAC_AX_USB2;
	else
		val32 = MAC_AX_USB11;
	return val32;
}

u32 usb_autok_counter_avg(struct mac_ax_adapter *adapter)
{
	return MACSUCCESS;
}

u32 usb_tp_adjust(struct mac_ax_adapter *adapter, struct mac_ax_tp_param tp)
{
	return MACSUCCESS;
}

u32 dbcc_hci_ctrl_usb(struct mac_ax_adapter *adapter,
		      struct mac_ax_dbcc_hci_ctrl *info)
{
	enum mac_ax_func_sw en;
	struct mac_ax_usb_ep cfg;
	u32 ret, cnt, val32, reg = 0;
	u8 pause, val8;

	if (!info)
		return MACNPTR;

	pause = info->pause;
	en = pause ? MAC_AX_FUNC_EN : MAC_AX_FUNC_DIS;

	if (en) {
		cfg.ep4 = DISABLE;
		cfg.ep5 = ENABLE;
		cfg.ep6 = ENABLE;
		cfg.ep7 = ENABLE;
		cfg.ep8 = DISABLE;
		cfg.ep9 = ENABLE;
		cfg.ep10 = ENABLE;
		cfg.ep11 = ENABLE;
		cfg.ep12 = ENABLE;
	} else {
		cfg.ep4 = DISABLE;
		cfg.ep5 = DISABLE;
		cfg.ep6 = DISABLE;
		cfg.ep7 = DISABLE;
		cfg.ep8 = DISABLE;
		cfg.ep9 = DISABLE;
		cfg.ep10 = DISABLE;
		cfg.ep11 = DISABLE;
		cfg.ep12 = DISABLE;
	}

#if MAC_AX_8852A_SUPPORT
	if (is_chip_id(adapter, MAC_AX_CHIP_ID_8852A)) {
		ret = usb_ep_cfg_8852a(adapter, &cfg);
		if (ret != MACSUCCESS) {
			PLTFM_MSG_ERR("dbcc usb_ep_cfg %d fail %d\n", en, ret);
			return ret;
		}
		reg = R_AX_USB_DEBUG_3;
		val32 = PLTFM_REG_R32(reg);
		PLTFM_REG_W32(reg, val32 | BIT20);
		val8 = GET_FIELD(PLTFM_REG_R32(reg), B_AX_TX_PATH_STATE_MACHINE);
		cnt = USB_WAIT_CNT;
		while (cnt--) {
			if (val8 == USB_TX_IDLE)
				break;
			PLTFM_DELAY_US(USB_WAIT_US);
		}
		if (!++cnt) {
			PLTFM_MSG_ERR("[ERR]dbcc_hci_ctrl_usb polling timeout\n");
			return MACPOLLTO;
		}
		return MACSUCCESS;
	}
#endif
#if MAC_AX_8852B_SUPPORT
	if (is_chip_id(adapter, MAC_AX_CHIP_ID_8852B)) {
		ret = usb_ep_cfg_8852b(adapter, &cfg);
		if (ret != MACSUCCESS) {
			PLTFM_MSG_ERR("dbcc usb_ep_cfg %d fail %d\n", en, ret);
			return ret;
		}
		reg = R_AX_USB_DEBUG_3;
		val32 = PLTFM_REG_R32(reg);
		PLTFM_REG_W32(reg, val32 | BIT20);
		val8 = GET_FIELD(PLTFM_REG_R32(reg), B_AX_TX_PATH_STATE_MACHINE);
		cnt = USB_WAIT_CNT;
		while (cnt--) {
			if (val8 == USB_TX_IDLE)
				break;
			PLTFM_DELAY_US(USB_WAIT_US);
		}
		if (!++cnt) {
			PLTFM_MSG_ERR("[ERR]dbcc_hci_ctrl_usb polling timeout\n");
			return MACPOLLTO;
		}
		return MACSUCCESS;
	}
#endif
#if MAC_AX_8852C_SUPPORT
	if (is_chip_id(adapter, MAC_AX_CHIP_ID_8852C))
		return MACNOTSUP;
#endif
#if MAC_AX_8192XB_SUPPORT
	if (is_chip_id(adapter, MAC_AX_CHIP_ID_8192XB))
		return MACNOTSUP;
#endif
	return MACCHIPID;
}
#endif /* #if MAC_AX_USB_SUPPORT */
