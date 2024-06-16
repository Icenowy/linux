// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */

#include <dt-bindings/firmware/thead/rsrc.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/firmware/thead/ipc.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_domain.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

struct th1520_aon_msg_req_set_resource_power_mode {
	struct th1520_aon_rpc_msg_hdr hdr;
	u16 resource;
	u16 mode;
	u16 reserved[10];
} __packed __aligned(1);

#define TH1520_AONU_PD_NAME_SIZE 20
#define TH1520_AONU_PD_STATE_NAME_SIZE 10

struct th1520_aon_pm_domain {
	struct generic_pm_domain pd;
	char name[TH1520_AONU_PD_NAME_SIZE];
	u16 rsrc;
};

struct th1520_aon_pd_range {
	char *name;
	u32 rsrc;
	u8 num;

	/* add domain index */
	bool postfix;
	u8 start_from;
};

struct th1520_aon_pd_soc {
	const struct th1520_aon_pd_range *pd_ranges;
	u8 num_ranges;
};

static const struct th1520_aon_pd_range th1520_aon_pd_ranges[] = {
	/* AUDIO SS */
	{ "audio", TH1520_AON_AUDIO_PD, 1, false, 0 },
	{ "vdec", TH1520_AON_VDEC_PD, 1, false, 0},
	{ "npu", TH1520_AON_NPU_PD, 1, false, 0},
	{ "venc", TH1520_AON_VENC_PD, 1, false, 0},
	{ "gpu", TH1520_AON_GPU_PD, 1, false, 0},
	{ "dsp0", TH1520_AON_DSP0_PD, 1, false, 0},
	{ "dsp1", TH1520_AON_DSP1_PD, 1, false, 0},
	{},
};

static const struct th1520_aon_pd_soc th1520_aon_pd = {
	.pd_ranges = th1520_aon_pd_ranges,
	.num_ranges = ARRAY_SIZE(th1520_aon_pd_ranges),
};

static struct th1520_aon_ipc *pm_ipc_handle;
static struct dentry *pd_debugfs_root;
struct dentry *pd_pde;
struct genpd_onecell_data *genpd_data;

static inline struct th1520_aon_pm_domain *to_th1520_aon_pd(struct generic_pm_domain *genpd)
{
	return container_of(genpd, struct th1520_aon_pm_domain, pd);
}

static int th1520_aon_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	struct th1520_aon_msg_req_set_resource_power_mode msg;
	struct th1520_aon_rpc_ack_common  ack_msg;
	struct th1520_aon_rpc_msg_hdr *hdr = &msg.hdr;
	struct th1520_aon_pm_domain *pd;
	int ret;

	pd = to_th1520_aon_pd(domain);

	hdr->svc = TH1520_AON_RPC_SVC_PM;
	hdr->func = TH1520_AON_PM_FUNC_SET_RESOURCE_POWER_MODE;
	hdr->size = TH1520_AON_RPC_MSG_NUM;

	RPC_SET_BE16(&msg.resource, 0, pd->rsrc);
	RPC_SET_BE16(&msg.resource, 2,
	     (power_on ? TH1520_AON_PM_PW_MODE_ON : TH1520_AON_PM_PW_MODE_OFF));

	ret = th1520_aon_call_rpc(pm_ipc_handle, &msg, &ack_msg, true);
	if (ret)
		dev_err(&domain->dev, "failed to power %s resource %d ret %d\n",
			power_on ? "up" : "off", pd->rsrc, ret);

	return ret;
}

static int th1520_aon_pd_power_on(struct generic_pm_domain *domain)
{
	return th1520_aon_pd_power(domain, true);
}

static int th1520_aon_pd_power_off(struct generic_pm_domain *domain)
{
	return th1520_aon_pd_power(domain, false);
}

static struct generic_pm_domain *th1520_aon_pd_xlate(struct of_phandle_args *spec,
						  void *data)
{
	struct generic_pm_domain *domain = ERR_PTR(-ENOENT);
	struct genpd_onecell_data *pd_data = data;
	unsigned int i;

	for (i = 0; i < pd_data->num_domains; i++) {
		struct th1520_aon_pm_domain *aon_pd;

		aon_pd = to_th1520_aon_pd(pd_data->domains[i]);
		if (aon_pd->rsrc == spec->args[0]) {
			domain = &aon_pd->pd;
			break;
		}
	}

	return domain;
}

static struct th1520_aon_pm_domain *
th1520_aon_add_pm_domain(struct device *dev, int idx,
		      const struct th1520_aon_pd_range *pd_ranges)
{
	struct th1520_aon_pm_domain *aon_pd;
	int ret;

	aon_pd = devm_kzalloc(dev, sizeof(*aon_pd), GFP_KERNEL);
	if (!aon_pd)
		return ERR_PTR(-ENOMEM);

	aon_pd->rsrc = pd_ranges->rsrc + idx;
	aon_pd->pd.power_off = th1520_aon_pd_power_off;
	aon_pd->pd.power_on = th1520_aon_pd_power_on;

	if (pd_ranges->postfix)
		snprintf(aon_pd->name, sizeof(aon_pd->name),
			 "%s%i", pd_ranges->name, pd_ranges->start_from + idx);
	else
		snprintf(aon_pd->name, sizeof(aon_pd->name),
			 "%s", pd_ranges->name);

	aon_pd->pd.name = aon_pd->name;

	if (aon_pd->rsrc >= TH1520_AON_R_LAST) {
		dev_warn(dev, "invalid pd %s rsrc id %d found",
			 aon_pd->name, aon_pd->rsrc);

		devm_kfree(dev, aon_pd);
		return NULL;
	}

	ret = pm_genpd_init(&aon_pd->pd, NULL, true);
	if (ret) {
		dev_warn(dev, "failed to init pd %s rsrc id %d",
			 aon_pd->name, aon_pd->rsrc);
		devm_kfree(dev, aon_pd);
		return NULL;
	}

	return aon_pd;
}

static int th1520_aon_init_pm_domains(struct device *dev,
				    const struct th1520_aon_pd_soc *pd_soc)
{
	const struct th1520_aon_pd_range *pd_ranges = pd_soc->pd_ranges;
	struct generic_pm_domain **domains;
	struct genpd_onecell_data *pd_data;
	struct th1520_aon_pm_domain *aon_pd;
	u32 count = 0;
	int i, j;

	for (i = 0; i < pd_soc->num_ranges; i++)
		count += pd_ranges[i].num;

	domains = devm_kcalloc(dev, count, sizeof(*domains), GFP_KERNEL);
	if (!domains)
		return -ENOMEM;

	pd_data = devm_kzalloc(dev, sizeof(*pd_data), GFP_KERNEL);
	if (!pd_data)
		return -ENOMEM;

	count = 0;
	for (i = 0; i < pd_soc->num_ranges; i++) {
		for (j = 0; j < pd_ranges[i].num; j++) {
			aon_pd = th1520_aon_add_pm_domain(dev, j, &pd_ranges[i]);
			if (IS_ERR_OR_NULL(aon_pd))
				continue;

			domains[count++] = &aon_pd->pd;
			dev_dbg(dev, "added power domain %s\n", aon_pd->pd.name);
		}
	}

	pd_data->domains = domains;
	pd_data->num_domains = count;
	pd_data->xlate = th1520_aon_pd_xlate;
	genpd_data = pd_data;

	of_genpd_add_provider_onecell(dev->of_node, pd_data);

	return 0;
}

static char *pd_get_user_string(const char __user *userbuf, size_t userlen)
{
	char *buffer;

	buffer = vmalloc(userlen + 1);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(buffer, userbuf, userlen) != 0) {
		vfree(buffer);
		return ERR_PTR(-EFAULT);
	}

	/* got the string, now strip linefeed. */
	if (buffer[userlen - 1] == '\n')
		buffer[userlen - 1] = '\0';
	else
		buffer[userlen] = '\0';

	pr_debug("buffer = %s\n", buffer);

	return buffer;
}

static ssize_t th1520_power_domain_write(struct file *file,
					const char __user *userbuf,
					size_t userlen, loff_t *ppos)
{
	char *buffer, *start, *end;
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct genpd_onecell_data *aon_pds_data = m->private;
	struct generic_pm_domain *hitted_pm_genpd;
	struct generic_pm_domain *domain;
	char pd_name[TH1520_AONU_PD_NAME_SIZE];
	char pd_state[TH1520_AONU_PD_STATE_NAME_SIZE];
	int idx, ret;
	size_t origin_len = userlen;

	buffer = pd_get_user_string(userbuf, userlen);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	start = skip_spaces(buffer);
	end = start;
	while (!isspace(*end) && *end != '\0')
		end++;

	*end = '\0';
	strcpy(pd_name, start);
	pr_debug("power domain name: %s\n", pd_name);

	/* find the target power domain */
	for (idx = 0; idx < aon_pds_data->num_domains; idx++) {
		domain = aon_pds_data->domains[idx];
		pr_debug("generic pm domain name: %s, pd_name: %s, ret = %d\n",
				domain->name, pd_name, strcmp(pd_name, domain->name));
		if (strcmp(pd_name, domain->name))
			continue;
		else {
			hitted_pm_genpd = aon_pds_data->domains[idx];
			pr_debug("target pm power domain-%s found, index: %d\n",
					hitted_pm_genpd->name, idx);
			break;
		}
	}

	if (idx >= aon_pds_data->num_domains) {
		pr_err("no taget power domain-%s found, idx = %d, total pd numbers = %d\n",
				pd_name, idx, aon_pds_data->num_domains);
		userlen = -EINVAL;
		goto out;
	}

	if (!hitted_pm_genpd->power_on && !hitted_pm_genpd->power_off) {
		pr_err("no power operations registered for power domain-%s\n", pd_name);
		userlen = -EINVAL;
		goto out;
	}

	end = end + 1;
	start = skip_spaces(end);
	end = start;
	while (!isspace(*end) && *end != '\0')
		end++;

	*end = '\0';
	strcpy(pd_state, start);
	pr_debug("power domain target state: %s\n", pd_state);

	if (!strcmp(pd_state, "on")) {
		ret = hitted_pm_genpd->power_on(hitted_pm_genpd);
		if (ret) {
			userlen = ret;
			goto out;
		}
	} else if (!strcmp(pd_state, "off")) {
		ret = hitted_pm_genpd->power_off(hitted_pm_genpd);
		if (ret) {
			userlen = ret;
			goto out;
		}
	} else {
		pr_err("invalid power domain target state, not 'on' or 'off'\n");
		userlen = -EINVAL;
		goto out;
	}

out:
	memset(buffer, 0, origin_len);
	vfree(buffer);

	return userlen;
}

static int th1520_power_domain_show(struct seq_file *m, void *v)
{
	struct genpd_onecell_data *pd_data = m->private;
	u32 count = pd_data->num_domains;
	int idx;

	seq_puts(m, "[Power domain name list]: ");
	for (idx = 0; idx < count; idx++)
		seq_printf(m, "%s ", pd_data->domains[idx]->name);
	seq_puts(m, "\n");
	seq_puts(m, "[Power on  domain usage]: echo power_name on  > domain\n");
	seq_puts(m, "[Power off domain usage]: echo power_name off > domain\n");

	return 0;
}

static int th1520_power_domain_open(struct inode *inode, struct file *file)
{
	struct genpd_onecell_data *pd_data = inode->i_private;

	return single_open(file, th1520_power_domain_show, pd_data);
}

static const struct file_operations th1520_power_domain_fops = {
	.owner	= THIS_MODULE,
	.write	= th1520_power_domain_write,
	.read	= seq_read,
	.open	= th1520_power_domain_open,
	.llseek	= generic_file_llseek,
};

static void pd_debugfs_init(struct genpd_onecell_data *aon_pds_data)
{
	pd_debugfs_root = debugfs_create_dir("power_domain", NULL);
	if (!pd_debugfs_root || IS_ERR(pd_debugfs_root))
		return;

	pd_pde = debugfs_create_file("domain", 0600, pd_debugfs_root,
			(void *)aon_pds_data, &th1520_power_domain_fops);
}

static int th1520_aon_pd_probe(struct platform_device *pdev)
{
	const struct th1520_aon_pd_soc *pd_soc;
	int ret;

	ret = th1520_aon_get_handle(&pm_ipc_handle);
	if (ret)
		return ret;

	pd_soc = of_device_get_match_data(&pdev->dev);
	if (!pd_soc)
		return -ENODEV;

	ret = th1520_aon_init_pm_domains(&pdev->dev, pd_soc);
	if (ret)
		return ret;

	pd_debugfs_init(genpd_data);

	return 0;
}

static const struct of_device_id th1520_aon_pd_match[] = {
	{ .compatible = "thead,th1520-aon-pd", &th1520_aon_pd},
	{ /* sentinel */ }
};

static struct platform_driver th1520_aon_pd_driver = {
	.driver = {
		.name = "th1520-aon-pd",
		.of_match_table = th1520_aon_pd_match,
	},
	.probe = th1520_aon_pd_probe,
};
builtin_platform_driver(th1520_aon_pd_driver);

MODULE_AUTHOR("fugang.duan <duanfugang.dfg@linux.alibaba.com>");
MODULE_DESCRIPTION("Thead Light firmware protocol driver");
MODULE_LICENSE("GPL");
