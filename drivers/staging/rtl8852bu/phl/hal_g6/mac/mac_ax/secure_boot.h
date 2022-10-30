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

#ifndef _MAC_AX_SECURE_BOOT_H_
#define _MAC_AX_SECURE_BOOT_H_

#include "../type.h"
#include "efuse.h"
//#include "fwcmd.h"

//#if WIFI_HAL_G6

#define OTP_SEC_DIS_ZONE_BASE 0x0
#define OTP_SEC_DIS_ZONE_SIZE 4
#define OTP_SECURE_ZONE_BASE 0x4C0
#define OTP_SECURE_ZONE_SIZE 192

#define OTP_KEY_INFO_CELL_01_ADDR 0x5EC
#define OTP_KEY_INFO_CELL_02_ADDR 0x5ED

// externalPN  = 0x5EC[7:0]
// customer    = 0x5ED[3:0]
// serialNum   = 0x5ED[6:4]
// securityRec = 0x5ED[7:7]
#define _external_pn(byte)   ((byte & 0xFF) >> 0)
#define _customer(byte)     ((byte & 0x0F) >> 0)
#define _serial_num(byte)    ((byte & 0x70) >> 4)
#define _security_rec(byte)  ((byte & 0x80) >> 7)

u32 mac_chk_sec_rec(struct mac_ax_adapter *adapter);
u32 mac_pg_sec_phy_wifi(struct mac_ax_adapter *adapter);
u32 mac_cmp_sec_phy_wifi(struct mac_ax_adapter *adapter);
u32 mac_pg_sec_hid_wifi(struct mac_ax_adapter *adapter);
u32 mac_cmp_sec_hid_wifi(struct mac_ax_adapter *adapter);
u32 mac_pg_sec_dis(struct mac_ax_adapter *adapter);
u32 mac_cmp_sec_dis(struct mac_ax_adapter *adapter);

#endif
//#endif