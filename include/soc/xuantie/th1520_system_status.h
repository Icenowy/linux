
#ifndef __SOC_TH1520_SYSTEM_STATUS_H
#define __SOC_TH1520_SYSTEM_STATUS_H

#if IS_ENABLED(CONFIG_TH1520_SYSTEM_MONITOR)
int th1520_register_system_status_notifier(struct notifier_block *nb);
int th1520_unregister_system_status_notifier(struct notifier_block *nb);
void th1520_set_system_status(unsigned long status);
void th1520_clear_system_status(unsigned long status);
unsigned long th1520_get_system_status(void);
void th1520_update_system_status_byname(const char *buf);
#else
static int th1520_register_system_status_notifier(struct notifier_block *nb)
{
	return -ENOTSUPP;
};
static int th1520_unregister_system_status_notifier(struct notifier_block *nb)
{
	return -ENOTSUPP;
};
static void th1520_set_system_status(unsigned long status)
{
};
static void th1520_clear_system_status(unsigned long status)
{
};
static unsigned long th1520_get_system_status(void)
{
	return 0;
};
static void th1520_update_system_status(const char *buf)
{
};
#endif

#endif
