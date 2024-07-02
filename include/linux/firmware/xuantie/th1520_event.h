/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _TH1520_EVENT_H
#define _TH1520_EVENT_H

enum th1520_rebootmode_index {
	/* C902 event rebootmode */
        TH1520_EVENT_PMIC_RESET = 0x0,
        TH1520_EVENT_PMIC_ONKEY,
        TH1520_EVENT_PMIC_POWERUP,

	/* C910 event rebootmode */
        TH1520_EVENT_SW_REBOOT = 0x20,
        TH1520_EVENT_SW_WATCHDOG,
        TH1520_EVENT_SW_PANIC,
        TH1520_EVENT_SW_HANG,
        TH1520_EVENT_MAX,
};

#if IS_ENABLED(CONFIG_TH1520_REBOOTMODE)
extern int th1520_event_set_rebootmode(enum th1520_rebootmode_index mode);
extern int th1520_event_get_rebootmode(enum th1520_rebootmode_index *mode);
#else
static int th1520_event_set_rebootmode(enum th1520_rebootmode_index mode)
{
	return 0;
}
static int th1520_event_get_rebootmode(enum th1520_rebootmode_index *mode)
{
	*mode = TH1520_EVENT_MAX;

	return 0;
}
#endif

#endif
