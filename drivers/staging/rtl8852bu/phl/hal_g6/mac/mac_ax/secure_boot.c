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
#include "secure_boot.h"

//#if WIFI_HAL_G6

// OTP zone1 map
u8 otp_sec_dis_zone_map_v01[] = {0xFC, 0xFF, 0xFF, 0x3F};

// OTP zone3 map
u8 otp_secure_zone_map_v01[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0x00, 0x99, 0xB6, 0x34, 0xA0, 0xCD, 0xA1, 0x6B,
	0xA1, 0xC5, 0x06, 0x8F, 0xCA, 0xDC, 0xE2, 0x95,
	0xA5, 0xCC, 0x8B, 0x33, 0x13, 0x6E, 0x4F, 0x28,
	0xFC, 0x1A, 0xE7, 0x91, 0x84, 0x4F, 0x62, 0x43,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// OTP zone4 values
u8 otp_key_info_cell_01_val = 0xFF; // OTP 0x5EC
u8 otp_key_info_cell_02_val = 0x6E; // OTP 0x5ED

u32 mac_chk_sec_rec(struct mac_ax_adapter *adapter)
{
	u32 ret = 0;
	u8 byte_val = 0;
	struct mac_ax_ops *mac_ops = adapter_to_mac_ops(adapter);
	u32 otp_key_info_cell_02_addr = OTP_KEY_INFO_CELL_02_ADDR;

	// read efuse OTP_KEY_INFO_CELL_02 to byte_val
	ret = mac_ops->read_efuse(adapter, otp_key_info_cell_02_addr,
				  1, &byte_val, MAC_AX_EFUSE_BANK_WIFI);
	if (ret != MACSUCCESS) {
		PLTFM_MSG_ERR("[ERR] chk_sec_rec read_efuse fail!\n");
		return ret;
	} else {
		PLTFM_MSG_TRACE("[TRACE] chk_sec_rec ret = %x\n",
				_security_rec(byte_val));
		return _security_rec(byte_val); // 0x0 for PG, 0x1 for non-PG
	}
}

u32 mac_pg_sec_phy_wifi(struct mac_ax_adapter *adapter)
{
	u32 ret = 0;
	u32 i;
	struct mac_ax_ops *mac_ops = adapter_to_mac_ops(adapter);
	u32 otp_secure_zone_base = OTP_SECURE_ZONE_BASE;

	for (i = 0; i < OTP_SECURE_ZONE_SIZE; i++) {
		// write OTP_SECURE_ZONE_BASE + i = otp_secure_zone_map_v01[i]
		if (otp_secure_zone_map_v01[i] == 0xFF) {
			continue;
		} else {
			ret = mac_ops->write_efuse(adapter, otp_secure_zone_base + i,
						   otp_secure_zone_map_v01[i],
						   MAC_AX_EFUSE_BANK_WIFI);
			if (ret != MACSUCCESS) {
				PLTFM_MSG_ERR("[ERR] pg_sec_phy_wifi write_efuse fail!\n");
				return ret;
			}
		}
	}

	PLTFM_MSG_TRACE("[TRACE] pg_sec_phy_wifi success!\n");
	return MACSUCCESS;
}

u32 mac_cmp_sec_phy_wifi(struct mac_ax_adapter *adapter)
{
	u32 ret = 0;
	u32 i;
	u8 byte_val[OTP_SECURE_ZONE_SIZE];
	struct mac_ax_ops *mac_ops = adapter_to_mac_ops(adapter);
	u32 otp_secure_zone_base = OTP_SECURE_ZONE_BASE;
	u32 otp_secure_zone_size = OTP_SECURE_ZONE_SIZE;

	ret = mac_ops->read_efuse(adapter, otp_secure_zone_base,
				  otp_secure_zone_size, byte_val,
				  MAC_AX_EFUSE_BANK_WIFI);
	if (ret != MACSUCCESS) {
		PLTFM_MSG_ERR("[ERR] cmp_sec_phy_wifi read_efuse fail!\n");
		return ret;
	}

	for (i = 0; i < OTP_SECURE_ZONE_SIZE; i++) {
		if (byte_val[i] != otp_secure_zone_map_v01[i]) {
			PLTFM_MSG_ERR("[ERR] cmp_sec_phy_wifi fail!\n");
			return MACEFUSECMP;
		}
	}

	PLTFM_MSG_TRACE("[TRACE] cmp_sec_phy_wifi success!\n");
	return MACSUCCESS;
}

u32 mac_pg_sec_hid_wifi(struct mac_ax_adapter *adapter)
{
	u32 ret = 0;
	struct mac_ax_ops *mac_ops = adapter_to_mac_ops(adapter);
	u32 otp_key_info_cell_01_addr = OTP_KEY_INFO_CELL_01_ADDR;
	u32 otp_key_info_cell_02_addr = OTP_KEY_INFO_CELL_02_ADDR;

	// write OTP_KEY_INFO_CELL_01_ADDR = otp_key_info_cell_01_val
	if (otp_key_info_cell_01_val == 0xFF) {
		// do nothing
	} else {
		ret = mac_ops->write_efuse(adapter, otp_key_info_cell_01_addr,
					   otp_key_info_cell_01_val,
					   MAC_AX_EFUSE_BANK_WIFI);
		if (ret != MACSUCCESS) {
			PLTFM_MSG_ERR("[ERR] pg_sec_hid_wifi write_efuse fail!\n");
			return ret;
		}
	}

	// write OTP_KEY_INFO_CELL_02_ADDR = otp_key_info_cell_02_val
	ret = mac_ops->write_efuse(adapter, otp_key_info_cell_02_addr,
				   otp_key_info_cell_02_val,
				   MAC_AX_EFUSE_BANK_WIFI);
	if (ret != MACSUCCESS) {
		PLTFM_MSG_ERR("[ERR] pg_sec_hid_wifi write_efuse fail!\n");
		return ret;
	}

	PLTFM_MSG_TRACE("[TRACE] pg_sec_hid_wifi success!\n");
	return MACSUCCESS;
}

u32 mac_cmp_sec_hid_wifi(struct mac_ax_adapter *adapter)
{
	u32 ret = 0;
	u8 byte_val;
	struct mac_ax_ops *mac_ops = adapter_to_mac_ops(adapter);
	u32 otp_key_info_cell_01_addr = OTP_KEY_INFO_CELL_01_ADDR;
	u32 otp_key_info_cell_02_addr = OTP_KEY_INFO_CELL_02_ADDR;

	ret = mac_ops->read_efuse(adapter, otp_key_info_cell_01_addr,
				  1, &byte_val, MAC_AX_EFUSE_BANK_WIFI);
	if (ret != MACSUCCESS) {
		PLTFM_MSG_ERR("[ERR] cmp_sec_hid_wifi read_efuse fail!\n");
		return ret;
	}

	if (byte_val != otp_key_info_cell_01_val) {
		PLTFM_MSG_ERR("[ERR] cmp_sec_hid_wifi cell_01 fail!\n");
		return MACEFUSECMP;
	}

	ret = mac_ops->read_efuse(adapter, otp_key_info_cell_02_addr,
				  1, &byte_val, MAC_AX_EFUSE_BANK_WIFI);
	if (ret != MACSUCCESS) {
		PLTFM_MSG_ERR("[ERR] cmp_sec_hid_wifi read_efuse fail!\n");
		return ret;
	}

	if (byte_val != otp_key_info_cell_02_val) {
		PLTFM_MSG_ERR("[ERR] cmp_sec_hid_wifi cell_02 fail!\n");
		return MACEFUSECMP;
	}

	PLTFM_MSG_TRACE("[TRACE] cmp_sec_hid_wifi success!\n");
	return MACSUCCESS;
}

u32 mac_pg_sec_dis(struct mac_ax_adapter *adapter)
{
	u32 ret = 0;
	u32 i;
	struct mac_ax_ops *mac_ops = adapter_to_mac_ops(adapter);
	u32 otp_sec_dis_zone_base = OTP_SEC_DIS_ZONE_BASE;

	// write efuse OTP_SEC_CTRL_ZONE_BASE
	for (i = 0; i < OTP_SEC_DIS_ZONE_SIZE; i++) {
		// write OTP_SEC_DIS_ZONE_BASE + i = otp_sec_dis_zone_map_v01[i]
		if (otp_sec_dis_zone_map_v01[i] == 0xFF) {
			continue;
		} else {
			ret = mac_ops->write_efuse(adapter, otp_sec_dis_zone_base + i,
						   otp_sec_dis_zone_map_v01[i],
						   MAC_AX_EFUSE_BANK_WIFI);
			if (ret != MACSUCCESS) {
				PLTFM_MSG_ERR("[ERR] pg_sec_dis write_efuse fail!\n");
				return ret;
			}
		}
	}

	PLTFM_MSG_TRACE("[TRACE] pg_sec_dis success!\n");
	return MACSUCCESS;
}

u32 mac_cmp_sec_dis(struct mac_ax_adapter *adapter)
{
	u32 ret = 0;
	u32 i;
	u8 byte_val[OTP_SEC_DIS_ZONE_SIZE];
	struct mac_ax_ops *mac_ops = adapter_to_mac_ops(adapter);
	u32 otp_sec_dis_zone_base = OTP_SEC_DIS_ZONE_BASE;
	u32 otp_sec_dis_zone_size = OTP_SEC_DIS_ZONE_SIZE;

	ret = mac_ops->read_efuse(adapter, otp_sec_dis_zone_base,
				  otp_sec_dis_zone_size, byte_val,
				  MAC_AX_EFUSE_BANK_WIFI);
	if (ret != MACSUCCESS) {
		PLTFM_MSG_ERR("[ERR] cmp_sec_dis read_efuse fail!\n");
		return ret;
	}

	for (i = 0; i < OTP_SEC_DIS_ZONE_SIZE; i++) {
		if (byte_val[i] != otp_sec_dis_zone_map_v01[i]) {
			PLTFM_MSG_ERR("[ERR] cmp_sec_dis fail!\n");
			return MACEFUSECMP;
		}
	}

	PLTFM_MSG_TRACE("[TRACE] cmp_sec_dis success!\n");
	return MACSUCCESS;
}

//#endif