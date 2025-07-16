// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/of_graph.h>
#include <linux/component.h>
#include <linux/iommu.h>
#include <linux/version.h>

#include <drm/drm_of.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_prime.h>
#include <drm/drm_vblank.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_debugfs.h>

#include "vs_drv.h"
#include "vs_fb.h"
#include "vs_gem.h"
#include "vs_plane.h"
#include "vs_crtc.h"
#include "vs_simple_enc.h"
#include "vs_dc.h"
#include "vs_virtual.h"
#include "dw_mipi_dsi.h"
#include "dw_hdmi_th1520.h"

/* debug sysfs */
#include <drm/drm_auth.h>
#include "../drm_crtc_internal.h"



#define DRV_NAME    "vs-drm"
#define DRV_DESC    "VeriSilicon DRM driver"
#define DRV_DATE    "20191101"
#define DRV_MAJOR   1
#define DRV_MINOR   0

/* extern exception info */
int vs_crtc_reset_count;
int TotalFailures;

static bool has_iommu = true;
static struct platform_driver vs_drm_platform_driver;

static const struct file_operations fops = {
	.owner          = THIS_MODULE,
	.open           = drm_open,
	.release        = drm_release,
	.unlocked_ioctl     = drm_ioctl,
	.compat_ioctl       = drm_compat_ioctl,
	.poll           = drm_poll,
	.read           = drm_read,
	.mmap           = vs_gem_mmap,
};

static ssize_t log_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{

	// struct device *dev = kobj_to_dev(kobj);
	// struct drm_device *drm_dev = dev_get_drvdata(dev);

	struct device *dev = kobj_to_dev(kobj);
	struct drm_minor *dminor = dev_get_drvdata(dev);
	struct drm_device *drm_dev = dminor->dev;


	struct drm_framebuffer *fb;
	struct drm_plane *plane;
	struct drm_crtc *drm_crtc;

	ssize_t len = 0;

	/* module version */
	const char *module_version = "1.0.0";
	const char *build_time = "20230101";

	/* module info */
	int crtc_count = 0;

	unsigned int plane_count = 0;
	unsigned int plane_used = 0;
	unsigned int plane_free = 0;

	unsigned int fb_count = 0;

	list_for_each_entry(drm_crtc, &drm_dev->mode_config.crtc_list, head) {
		crtc_count++;
	}

	list_for_each_entry(plane, &drm_dev->mode_config.plane_list, head) {
		plane_count++;
		if (plane->state && plane->state->fb)
			plane_used++;
	}
	plane_free = plane_count - plane_used;

	list_for_each_entry(fb, &drm_dev->mode_config.fb_list, head) {
		fb_count++;
	}

	struct drm_connector *connector;
	enum drm_connector_status status;
	bool hdmi_on = false;
	bool dsi_on = false;

	list_for_each_entry(connector, &drm_dev->mode_config.connector_list, head) {
		status = connector->status;
		if (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA)
			hdmi_on = (status == connector_status_connected);
		else if (connector->connector_type == DRM_MODE_CONNECTOR_DSI)
			dsi_on = (status == connector_status_connected);
	}

	struct drm_file *priv;
	kuid_t uid;
	int client_count = 0;

	list_for_each_entry_reverse(priv, &drm_dev->filelist, lhead) {
		client_count++;
	}

	len += scnprintf(buf + len, PAGE_SIZE - len,
		"----------------------------------------MODULE VERSION----------------------------------------\n"
		"[Video Sub System] Version: %s, Build Time【%s】\n"
		"----------------------------------------MODULE STATUS-----------------------------------------\n"
		"CrtcCount  PlaneCount  PlaneUsed  PlaneFree  FrameBufferCount  ClientCount  HDMI  DSI\n"
		"   %d          %u           %u           %u             %u              %u         %s   %s\n"
		"----------------------------------------EXCEPTION INFO----------------------------------------\n"
		"TotalFailures    Reset\n"
		"     %d             %d\n",

		module_version, build_time,
		crtc_count, plane_count, plane_used, plane_count - plane_used,
		fb_count, client_count, hdmi_on ? "on" : "off", dsi_on ? "on" : "off",
		TotalFailures, vs_crtc_reset_count
	);

	len += scnprintf(buf + len, PAGE_SIZE - len,
		"-----------------------------------------Client Info-----------------------------------------\n"
		"%20s %5s %3s master a %5s %10s\n",
		"command", "pid", "dev", "uid", "magic");
	list_for_each_entry_reverse(priv, &drm_dev->filelist, lhead) {
		struct task_struct *task;
		bool is_current_master = drm_is_current_master(priv);

		rcu_read_lock(); /* locks pid_task()->comm */
		task = pid_task(priv->pid, PIDTYPE_PID);
		uid = task ? __task_cred(task)->euid : GLOBAL_ROOT_UID;
		len += scnprintf(buf + len, PAGE_SIZE - len, "%20s %5d %3d   %c    %c %5u %10u\n",
			task ? task->comm : "<unknown>",
			pid_vnr(priv->pid),
			priv->minor->index,
			is_current_master ? 'y' : 'n',
			priv->authenticated ? 'y' : 'n',
			// from_kuid_munged(seq_user_ns(m), uid)
			uid,
			priv->magic);
		rcu_read_unlock();
	}

	if (drm_dev) {
		len += scnprintf(buf + len, PAGE_SIZE - len,
		"-----------------------------------------MODULE INFO-----------------------------------------\n");
		list_for_each_entry(fb, &drm_dev->mode_config.fb_list, head) {
			unsigned int i;

			len += scnprintf(buf + len, PAGE_SIZE - len,
				"framebuffer[%u]:\n"
				"\tallocated by = %s\n"
				"\tformat=%p4cc\n"
				"\tsize=%ux%u\n",
				fb->base.id,
				fb->comm,
				&fb->format->format,
				fb->width, fb->height
				);
			for (i = 0; i < fb->format->num_planes; i++)
				len += scnprintf(buf + len, PAGE_SIZE - len,
					"\t\tpitch[%u]=%u\n", i, fb->pitches[i]);
		}
	}

	list_for_each_entry(drm_crtc, &drm_dev->mode_config.crtc_list, head) {
		// struct vs_crtc *crtc = to_vs_crtc(drm_crtc);
		struct drm_crtc *crtc = drm_crtc->state->crtc;
		struct vs_crtc_state *crtc_state = to_vs_crtc_state(drm_crtc->state);

		len += scnprintf(buf + len, PAGE_SIZE - len,
			"crtc[%u]: %s\n"
			"\tenable= %d\n"
			"\tplane_mask=%x\n"
			"\tconnector_mask=%x\n"
			"\tencoder_mask=%x\n"
			"\tmode: " DRM_MODE_FMT "\n",
			crtc->base.id, crtc->name,
			drm_crtc->state->enable,
			drm_crtc->state->plane_mask,
			drm_crtc->state->connector_mask,
			drm_crtc->state->encoder_mask,
			DRM_MODE_ARG(&(drm_crtc->state->mode))
			);
	}

	list_for_each_entry(plane, &drm_dev->mode_config.plane_list, head) {
		struct drm_plane_state *state = plane->state;
		struct vs_plane_state *plane_state = to_vs_plane_state(state);
		struct drm_rect src  = drm_plane_state_src(state);
		struct drm_rect dest = drm_plane_state_dest(state);

		len += scnprintf(buf + len, PAGE_SIZE - len,
			"plane[%u]: %s\n"
			"\tcrtc=%s\n"
			"\tcrtc-pos=" DRM_RECT_FMT "\n"
			"\tsrc-pos=" DRM_RECT_FP_FMT "\n"
			"\trotation=%x\n"
			"\tcolor-encoding=%s\n"
			"\tcolor-range=%s\n",
			plane->base.id, plane->name,
			state->crtc ? state->crtc->name : "(null)",
			DRM_RECT_ARG(&dest),
			DRM_RECT_FP_ARG(&src),
			state->rotation,
			drm_get_color_encoding_name(state->color_encoding),
			drm_get_color_range_name(state->color_range)
			);
	}
	return len;
}

static ssize_t log_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	TotalFailures = 0;
	vs_crtc_reset_count = 0;

	return count;
}

static unsigned long period_ms;

static ssize_t updatePeriod_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%lu\n", period_ms);
}

static ssize_t updatePeriod_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf,
				size_t count)
{
	char *start = (char *)buf;
	int ret;

	ret = kstrtoul(start, 10, &period_ms);
	if (ret)
		pr_info("invalid period ms\n");

	return count;
}

static struct kobj_attribute log_attr = __ATTR(log, 0664, log_show, log_store);

static struct kobj_attribute updatePeriod_attr =
						__ATTR(updatePeriod_ms, 0664,
							   updatePeriod_show,
							   updatePeriod_store);


static struct attribute *attrs[] = {
	&log_attr.attr,
	&updatePeriod_attr.attr,
	NULL,
};

static umode_t always_visible(struct kobject *kobj,
					struct attribute *attr,
					int index)
{
	return 0644;
}


static struct attribute_group vs_dev_attr_group = {
	.name = "info",
	.is_visible = always_visible,
	.attrs = attrs,
};


#ifdef CONFIG_DEBUG_FS
static int vs_debugfs_planes_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *)s->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_plane *plane;

	list_for_each_entry(plane, &dev->mode_config.plane_list, head) {
		struct drm_plane_state *state = plane->state;
		struct vs_plane_state *plane_state = to_vs_plane_state(state);

		seq_printf(s, "plane[%u]: %s\n", plane->base.id, plane->name);
		seq_printf(s, "\tcrtc = %s\n", state->crtc ?
			state->crtc->name : "(null)");
		seq_printf(s, "\tcrtc id = %u\n", state->crtc ?
			state->crtc->base.id : 0);
		seq_printf(s, "\tcrtc-pos = " DRM_RECT_FMT "\n",
			DRM_RECT_ARG(&plane_state->status.dest));
		seq_printf(s, "\tsrc-pos = " DRM_RECT_FP_FMT "\n",
			DRM_RECT_FP_ARG(&plane_state->status.src));
		seq_printf(s, "\tformat = %p4cc\n", state->fb ?
			&state->fb->format->format : NULL);
		seq_printf(s, "\trotation = 0x%x\n", state->rotation);
		seq_printf(s, "\ttiling = %u\n",
			plane_state->status.tile_mode);

		seq_puts(s, "\n");
	}

	return 0;
}

static struct drm_info_list vs_debugfs_list[] = {
	{ "planes", vs_debugfs_planes_show, 0, NULL },
};

static void vs_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(vs_debugfs_list,
				ARRAY_SIZE(vs_debugfs_list),
				minor->debugfs_root, minor);
}
#endif

static struct drm_driver vs_drm_driver = {
	.driver_features    = DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM,
	.lastclose      = drm_fb_helper_lastclose,
	.gem_prime_import   = vs_gem_prime_import,
	.gem_prime_import_sg_table = vs_gem_prime_import_sg_table,
	.dumb_create        = vs_gem_dumb_create,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init       = vs_debugfs_init,
#endif
	.fops           = &fops,
	.name           = DRV_NAME,
	.desc           = DRV_DESC,
	.date           = DRV_DATE,
	.major          = DRV_MAJOR,
	.minor          = DRV_MINOR,
};

int vs_drm_dma_attach_device(struct drm_device *drm_dev,
				struct device *dev)
{
	struct vs_drm_private *priv = drm_dev->dev_private;
	int ret;

	priv->dma_dev = dev;

	if (!has_iommu)
		return 0;

	if (!priv->domain) {
		priv->domain = iommu_get_domain_for_dev(dev);
		if (IS_ERR(priv->domain))
			return PTR_ERR(priv->domain);
	}

	ret = iommu_attach_device(priv->domain, dev);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to attach iommu device\n");
		return ret;
	}

	return 0;
}

void vs_drm_dma_detach_device(struct drm_device *drm_dev,
				struct device *dev)
{
	struct vs_drm_private *priv = drm_dev->dev_private;

	if (priv->dma_dev == dev)
		priv->dma_dev = drm_dev->dev;

	if (!has_iommu)
		return;

	iommu_detach_device(priv->domain, dev);
}

void vs_drm_update_pitch_alignment(struct drm_device *drm_dev,
				unsigned int alignment)
{
	struct vs_drm_private *priv = drm_dev->dev_private;

	if (alignment > priv->pitch_alignment)
		priv->pitch_alignment = alignment;
}

static int vs_drm_bind(struct device *dev)
{
	struct drm_device *drm_dev;
	struct vs_drm_private *priv;
	int ret;
#ifdef CONFIG_VERISILICON_MMU
	static u64 dma_mask = DMA_BIT_MASK(40);
#else
	static u64 dma_mask = DMA_BIT_MASK(32);
#endif
	drm_dev = drm_dev_alloc(&vs_drm_driver, dev);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);

	dev_set_drvdata(dev, drm_dev);

	priv = devm_kzalloc(drm_dev->dev, sizeof(struct vs_drm_private),
				GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto err_put_dev;
	}

	priv->pitch_alignment = 64;
	priv->dma_dev = drm_dev->dev;
	priv->dma_dev->coherent_dma_mask = dma_mask;

	drm_dev->dev_private = priv;

	drm_mode_config_init(drm_dev);

	/* Now try and bind all our sub-components */
	ret = component_bind_all(dev, drm_dev);
	if (ret)
		goto err_mode;

	vs_mode_config_init(drm_dev);

	ret = drm_vblank_init(drm_dev, drm_dev->mode_config.num_crtc);
	if (ret)
		goto err_bind;

	drm_mode_config_reset(drm_dev);

	drm_kms_helper_poll_init(drm_dev);

	ret = drm_dev_register(drm_dev, 0);
	if (ret)
		goto err_helper;


	ret = sysfs_create_group(&drm_dev->primary->kdev->kobj, &vs_dev_attr_group);
	if (ret) {
		dev_err(drm_dev->dev, "Failed to create drm dev sysfs.\n");
		goto err_drm_dev_register;
	}

	drm_fbdev_generic_setup(drm_dev, 32);

	return 0;

err_drm_dev_register:
	drm_dev_unregister(drm_dev);
err_helper:
	drm_kms_helper_poll_fini(drm_dev);
err_bind:
	component_unbind_all(drm_dev->dev, drm_dev);
err_mode:
	drm_mode_config_cleanup(drm_dev);
	if (priv->domain)
		iommu_domain_free(priv->domain);
err_put_dev:
	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm_dev);
	return ret;
}

static void vs_drm_unbind(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct vs_drm_private *priv = drm_dev->dev_private;

	drm_dev_unregister(drm_dev);

	sysfs_remove_group(&drm_dev->primary->kdev->kobj, &vs_dev_attr_group);

	drm_kms_helper_poll_fini(drm_dev);

	component_unbind_all(drm_dev->dev, drm_dev);

	drm_mode_config_cleanup(drm_dev);

	if (priv->domain) {
		iommu_domain_free(priv->domain);
		priv->domain = NULL;
	}

	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm_dev);
}

static const struct component_master_ops vs_drm_ops = {
	.bind = vs_drm_bind,
	.unbind = vs_drm_unbind,
};

static struct platform_driver *drm_sub_drivers[] = {
	/* put display control driver at start */
	&dc_platform_driver,

	/* connector */

	/* bridge */
#ifdef CONFIG_VERISILICON_DW_MIPI_DSI
	&dw_mipi_dsi_driver,
#endif

#ifdef CONFIG_VERISILICON_DW_HDMI_TH1520
	&dw_hdmi_th1520_platform_driver,
#endif

	/* encoder */
	&simple_encoder_driver,

#ifdef CONFIG_VERISILICON_VIRTUAL_DISPLAY
	&virtual_display_platform_driver,
#endif
};
#define NUM_DRM_DRIVERS \
		(sizeof(drm_sub_drivers) / sizeof(struct platform_driver *))

static int compare_dev(struct device *dev, void *data)
{
	return dev == (struct device *)data;
}

static struct component_match *vs_drm_match_add(struct device *dev)
{
	struct component_match *match = NULL;
	int i;

	for (i = 0; i < NUM_DRM_DRIVERS; ++i) {
		struct platform_driver *drv = drm_sub_drivers[i];
		struct device *p = NULL, *d;

		while ((d = platform_find_device_by_driver(p, &drv->driver))) {
			put_device(p);

			component_match_add(dev, &match, compare_dev, d);
			p = d;
		}
		put_device(p);
	}

	return match ?: ERR_PTR(-ENODEV);
}

static int vs_drm_platform_of_probe(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *port;
	bool found = false;
	int i;

	if (!np)
		return -ENODEV;

	for (i = 0;; i++) {
		struct device_node *iommu;

		port = of_parse_phandle(np, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
		}

		iommu = of_parse_phandle(port->parent, "iommus", 0);

		/*
		 * if there is a crtc not support iommu, force set all
		 * crtc use non-iommu buffer.
		 */
		if (!iommu || !of_device_is_available(iommu->parent))
			has_iommu = false;

		found = true;

		of_node_put(iommu);
		of_node_put(port);
	}

	if (i == 0) {
		DRM_DEV_ERROR(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	if (!found) {
		DRM_DEV_ERROR(dev, "No available DC found.\n");
		return -ENODEV;
	}

	return 0;
}

static int vs_drm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match;
	int ret;

	ret = vs_drm_platform_of_probe(dev);
	if (ret)
		return ret;

	match = vs_drm_match_add(dev);
	if (IS_ERR(match))
		return PTR_ERR(match);

	return component_master_add_with_match(dev, &vs_drm_ops, match);
}

static int vs_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &vs_drm_ops);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int vs_drm_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm);
}

static int vs_drm_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm);
}
#endif

static SIMPLE_DEV_PM_OPS(vs_drm_pm_ops, vs_drm_suspend, vs_drm_resume);


static const struct of_device_id vs_drm_dt_ids[] = {

	{ .compatible = "verisilicon,display-subsystem", },

	{ /* sentinel */ },

};

MODULE_DEVICE_TABLE(of, vs_drm_dt_ids);

static struct platform_driver vs_drm_platform_driver = {
	.probe = vs_drm_platform_probe,
	.remove = vs_drm_platform_remove,

	.driver = {
		.name = DRV_NAME,
		.of_match_table = vs_drm_dt_ids,
		.pm = &vs_drm_pm_ops,
	},
};

static int __init vs_drm_init(void)
{
	int ret;

	ret = platform_register_drivers(drm_sub_drivers, NUM_DRM_DRIVERS);
	if (ret)
		return ret;

	ret = platform_driver_register(&vs_drm_platform_driver);
	if (ret)
		platform_unregister_drivers(drm_sub_drivers, NUM_DRM_DRIVERS);

	return ret;
}

static void __exit vs_drm_fini(void)
{
	platform_driver_unregister(&vs_drm_platform_driver);
	platform_unregister_drivers(drm_sub_drivers, NUM_DRM_DRIVERS);
}

module_init(vs_drm_init);
module_exit(vs_drm_fini);

MODULE_DESCRIPTION("VeriSilicon DRM Driver");
MODULE_LICENSE("GPL");
