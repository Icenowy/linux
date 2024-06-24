// SPDX-License-Identifier: GPL-2.0

#include <linux/version.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_of.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/bridge/dw_hdmi.h>

#include "dw_hdmi_tx_phy_gen2.h"

struct th1520_hdmi {
	struct device *dev;
	struct drm_encoder encoder;
	struct dw_hdmi *dw_hdmi;
	/* TODO: may not be used */
	struct regmap *sysreg;
};

static void dw_hdmi_th1520_encoder_enable(struct drm_encoder *encoder)
{
}

static int dw_hdmi_th1520_encoder_atomic_check(struct drm_encoder *encoder,
					      struct drm_crtc_state *crtc_state,
					      struct drm_connector_state *conn_state)
{
	return 0;
}

static struct dw_hdmi_plat_data th1520_hdmi_drv_data = {
	.mode_valid	= dw_hdmi_tx_phy_gen2_mode_valid,
	.configure_phy	= dw_hdmi_tx_phy_gen2_configure,
	.disable_cec = 0,
};

static const struct of_device_id dw_hdmi_th1520_dt_ids[] = {
	{
		.compatible = "xuantie,th1520-hdmi-tx",
		.data = &th1520_hdmi_drv_data,
	},
	{
		/* sentinel */
	},
};

static const struct drm_encoder_helper_funcs dw_hdmi_th1520_encoder_helper_funcs = {
	.enable		= dw_hdmi_th1520_encoder_enable,
	.atomic_check	= dw_hdmi_th1520_encoder_atomic_check,
};

struct dw_hdmi_th1520_private dw_hdmi_priv_data = {
	.max_pixclock = 594000,
	.max_width    = 0,
	.max_height   = 0,
};

static int dw_hdmi_th1520_bind(struct device *dev, struct device *master,
			      void *data)
{
	int ret = 0;
	u32 max_pixclock = 0;
	u16 max_width    = 0;
	u16 max_height   = 0;
	int property_ret = 0;
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *np = dev->of_node;
	const struct of_device_id *match;
	const struct dw_hdmi_plat_data *plat_data;
	struct th1520_hdmi *hdmi;
	struct drm_device *drm = data;
	struct drm_encoder *encoder;

	if (!np)
		return -ENODEV;
	hdmi = dev_get_drvdata(dev);

	match = of_match_node(dw_hdmi_th1520_dt_ids, np);
	if (unlikely(!match))
		return -ENODEV;

	property_ret = of_property_read_u32(np, "max_pixclock", &max_pixclock);
	if (property_ret == 0) {
		dev_info(dev, "Hdmi max pixel clock = %d\n", max_pixclock);
		if (dw_hdmi_priv_data.max_pixclock > max_pixclock) {
			dw_hdmi_priv_data.max_pixclock = max_pixclock;
			dev_info(dev, "Hdmi max pixel clock adjust to %d\n", max_pixclock);
		} else {
			dev_info(dev, "Hdmi max pixel clock not take effect\n");
		}
	}

	property_ret = of_property_read_u16(np, "max_width", &max_width);
	if (property_ret == 0)
		dw_hdmi_priv_data.max_width = max_width;

	property_ret = of_property_read_u16(np, "max_height", &max_height);
	if (property_ret == 0)
		dw_hdmi_priv_data.max_height = max_height;

	th1520_hdmi_drv_data.priv_data = (void *)&dw_hdmi_priv_data;

	plat_data = match->data;
	hdmi->dev = &pdev->dev;
	encoder = &hdmi->encoder;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, np);
	if (!encoder->possible_crtcs)
		return -EPROBE_DEFER;

	drm_encoder_helper_add(encoder, &dw_hdmi_th1520_encoder_helper_funcs);
	drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_TMDS);

	hdmi->dw_hdmi = dw_hdmi_bind(pdev, encoder, plat_data);
	if (IS_ERR(hdmi->dw_hdmi)) {
		ret = PTR_ERR(hdmi->dw_hdmi);
		drm_encoder_cleanup(encoder);
	}
	pm_runtime_enable(dev);

	return ret;
}

static void dw_hdmi_th1520_unbind(struct device *dev, struct device *master,
				 void *data)
{
	struct th1520_hdmi *hdmi = dev_get_drvdata(dev);

	pm_runtime_disable(dev);

	dw_hdmi_unbind(hdmi->dw_hdmi);
}

static const struct component_ops dw_hdmi_th1520_ops = {
	.bind	= dw_hdmi_th1520_bind,
	.unbind = dw_hdmi_th1520_unbind,
};

static int dw_hdmi_th1520_probe(struct platform_device *pdev)
{
	struct th1520_hdmi *hdmi;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	platform_set_drvdata(pdev, hdmi);

	return component_add(&pdev->dev, &dw_hdmi_th1520_ops);
}

static int dw_hdmi_th1520_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dw_hdmi_th1520_ops);

	return 0;
}

#ifdef CONFIG_PM
static int hdmi_runtime_suspend(struct device *dev)
{
	struct th1520_hdmi *hdmi = dev_get_drvdata(dev);

	return dw_hdmi_runtime_suspend(hdmi->dw_hdmi);
}

static int hdmi_runtime_resume(struct device *dev)
{
	struct th1520_hdmi *hdmi = dev_get_drvdata(dev);

	return dw_hdmi_runtime_resume(hdmi->dw_hdmi);
}
#endif
#ifdef CONFIG_PM_SLEEP
static int hdmi_resume(struct device *dev)
{
	struct th1520_hdmi *hdmi = dev_get_drvdata(dev);

	dev_info(dev, "hdmi resume\n");
	dw_hdmi_resume(hdmi->dw_hdmi);
	return 0;
}
#endif
static const struct dev_pm_ops dw_hdmi_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				 pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(hdmi_runtime_suspend, hdmi_runtime_resume, NULL)
#ifdef CONFIG_PM_SLEEP
	SET_LATE_SYSTEM_SLEEP_PM_OPS(NULL, hdmi_resume)
#endif
};

struct platform_driver dw_hdmi_th1520_platform_driver = {
	.probe	= dw_hdmi_th1520_probe,
	.remove	= dw_hdmi_th1520_remove,
	.driver	= {
		.name = "dwhdmi-th1520",
		.of_match_table = dw_hdmi_th1520_dt_ids,
		.pm = &dw_hdmi_pm_ops,
	},
};

MODULE_AUTHOR("You Xiao <youxiao.fc@linux.alibaba.com>");
MODULE_DESCRIPTION("TH1520 Platforms Specific DW-HDMI Driver Extension");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dwhdmi-th1520");
