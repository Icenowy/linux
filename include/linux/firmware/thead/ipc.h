// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */

#ifndef _SC_IPC_H
#define _SC_IPC_H

#include <linux/device.h>
#include <linux/types.h>

#define TH1520_AON_RPC_VERSION	1
#define TH1520_AON_RPC_MSG_NUM	7

struct th1520_aon_ipc;

enum th1520_aon_rpc_svc {
	TH1520_AON_RPC_SVC_UNKNOWN = 0,
	TH1520_AON_RPC_SVC_RETURN = 1,
	TH1520_AON_RPC_SVC_PM = 2,
	TH1520_AON_RPC_SVC_MISC = 3,
	TH1520_AON_RPC_SVC_AVFS = 4,
};

enum th1520_aon_misc_func {
	TH1520_AON_MISC_FUNC_UNKNOWN = 0,
	TH1520_AON_MISC_FUNC_SET_CONTROL = 1,
	TH1520_AON_MISC_FUNC_GET_CONTROL = 2,
};

enum th1520_aon_pm_func {
	TH1520_AON_PM_FUNC_UNKNOWN = 0,
	TH1520_AON_PM_FUNC_SET_RESOURCE_REGULATOR = 1,
	TH1520_AON_PM_FUNC_GET_RESOURCE_REGULATOR = 2,
	TH1520_AON_PM_FUNC_SET_RESOURCE_POWER_MODE = 3,
};

struct th1520_aon_rpc_msg {
	uint8_t ver;
	uint8_t size;
	uint8_t svc;
	uint8_t func;
};

/*
 * Defines for SC PM Power Mode
 */
#define TH1520_AON_PM_PW_MODE_OFF	0	/* Power off */
#define TH1520_AON_PM_PW_MODE_STBY	1	/* Power in standby */
#define TH1520_AON_PM_PW_MODE_LP		2	/* Power in low-power */
#define TH1520_AON_PM_PW_MODE_ON		3	/* Power on */

int th1520_aon_call_rpc(struct th1520_aon_ipc *ipc, void *msg, bool have_resp);
int th1520_aon_get_handle(struct th1520_aon_ipc **ipc);
int th1520_aon_misc_set_control(struct th1520_aon_ipc *ipc, u16 resource, u32 ctrl, u32 val);
int th1520_aon_misc_get_control(struct th1520_aon_ipc *ipc, u16 resource, u32 ctrl, u32 *val);
#endif /* _SC_IPC_H */
