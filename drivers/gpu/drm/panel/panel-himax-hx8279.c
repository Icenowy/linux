#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct hx8279_panel_desc {
	const struct drm_display_mode *display_mode;

	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
	// const struct jadard_panel_cmd *on_cmds;
	// unsigned int on_cmds_num;
};

struct panel_info {
	struct drm_panel base;
	struct mipi_dsi_device *link;
	const struct hx8279_panel_desc *desc;

	struct gpio_desc	*reset;
	struct regulator	*hsvcc;
	struct regulator	*vspn3v3;

	bool prepared;
	bool enabled;
};

static inline struct panel_info *to_panel_info(struct drm_panel *panel)
{
	return container_of(panel, struct panel_info, base);
}

static int hx8279_panel_disable(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int err;

	if (!pinfo->enabled)
		return 0;

	err = mipi_dsi_dcs_set_display_off(pinfo->link);
	if (err < 0) {
		dev_err(panel->dev, "failed to set display off: %d\n", err);
		return err;
	}

	pinfo->enabled = false;

	return 0;
}

static int hx8279_panel_unprepare(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int err;

	if (!pinfo->prepared)
		return 0;

	err = mipi_dsi_dcs_set_display_off(pinfo->link);
	if (err < 0)
		dev_err(panel->dev, "failed to set display off: %d\n", err);

	err = mipi_dsi_dcs_enter_sleep_mode(pinfo->link);
	if (err < 0)
		dev_err(panel->dev, "failed to enter sleep mode: %d\n", err);

	/* sleep_mode_delay: 1ms - 2ms */
	usleep_range(1000, 2000);

	gpiod_set_value(pinfo->reset, 1);
	regulator_disable(pinfo->hsvcc);
	regulator_disable(pinfo->vspn3v3);

	pinfo->prepared = false;

	return 0;
}

static int hx8279_panel_prepare(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int ret;

	if (pinfo->prepared)
		return 0;
	gpiod_set_value(pinfo->reset, 0);

	/* Power the panel */
	ret = regulator_enable(pinfo->hsvcc);
	if (ret) {
		dev_err(pinfo->base.dev, "Failed to enable hsvcc supply: %d\n", ret);
		return ret;
	}

	usleep_range(1000, 2000);
	ret = regulator_enable(pinfo->vspn3v3);
	if (ret) {
		dev_err(pinfo->base.dev, "Failed to enable vspn3v3 supply: %d\n", ret);
		goto fail;
	}
	usleep_range(5000, 6000);

	gpiod_set_value(pinfo->reset, 1);
	msleep(180);

	pinfo->prepared = true;

	return 0;

fail:
	gpiod_set_value(pinfo->reset, 1);
	regulator_disable(pinfo->hsvcc);
	return ret;
}

static int hx8279_panel_enable(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int ret;
	u8 id1;

	if (pinfo->enabled)
		return 0;

	ret = mipi_dsi_dcs_set_display_on(pinfo->link);
	if (ret < 0) {
		dev_err(panel->dev, "failed to set display on: %d\n", ret);
		return ret;
	}

	msleep(120);

	ret = mipi_dsi_dcs_exit_sleep_mode(pinfo->link);
	if (ret < 0) {
		dev_err(panel->dev, "failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	pinfo->enabled = true;

	return 0;
}

static int hx8279_panel_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct panel_info *pinfo = to_panel_info(panel);
	const struct drm_display_mode *m = pinfo->desc->display_mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, m);
	if (!mode) {
		dev_err(pinfo->base.dev, "failed to add mode %ux%u@%u\n",
			m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs panel_funcs = {
	.disable = hx8279_panel_disable,
	.unprepare = hx8279_panel_unprepare,
	.prepare = hx8279_panel_prepare,
	.enable = hx8279_panel_enable,
	.get_modes = hx8279_panel_get_modes,
};

static const struct drm_display_mode hx8279_default_mode = {
	.clock		= 196500,
	.hdisplay	= 1200,
	.hsync_start	= 1200 + 96,
	.hsync_end	= 1200 + 96 + 128,
	.htotal		= 1200 + 96 + 128 + 224,

	.vdisplay	= 1920,
	.vsync_start	= 1920 + 3,
	.vsync_end	= 1920 + 3 + 10,
	.vtotal		= 1920 + 3 + 10 + 56,

	.width_mm	= 62,
	.height_mm	= 110,
	.flags          = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct hx8279_panel_desc hx8279_desc = {
	.display_mode = &hx8279_default_mode,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO_BURST,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
//	.on_cmds = hx8279_on_cmds,
//	.on_cmds_num = ARRAY_SIZE(hx8279_on_cmds),
};

static const struct of_device_id panel_of_match[] = {
	{
		.compatible = "himax,hx8279",
		.data = &hx8279_desc,
	},
	{
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, panel_of_match);

static int hx8279_panel_add(struct panel_info *pinfo)
{
	struct device *dev = &pinfo->link->dev;
	int ret;

	pinfo->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(pinfo->reset))
		return dev_err_probe(dev, PTR_ERR(pinfo->reset),
				"Couldn't get our reset GPIO\n");

	pinfo->hsvcc =  devm_regulator_get(dev, "hsvcc");
	if (IS_ERR(pinfo->hsvcc))
		return dev_err_probe(dev, PTR_ERR(pinfo->hsvcc),
				"Failed to request hsvcc regulator\n");

	pinfo->vspn3v3 =  devm_regulator_get(dev, "vspn3v3");
	if (IS_ERR(pinfo->vspn3v3))
		return dev_err_probe(dev, PTR_ERR(pinfo->vspn3v3),
				"Failed to request vspn3v3 regulator\n");

	drm_panel_init(&pinfo->base, dev, &panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&pinfo->base);
	if (ret)
		return ret;

	drm_panel_add(&pinfo->base);

	return 0;
}

static int hx8279_panel_probe(struct mipi_dsi_device *dsi)
{
	struct panel_info *pinfo;
	const struct hx8279_panel_desc *desc;
	int err;

	pinfo = devm_kzalloc(&dsi->dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	desc = of_device_get_match_data(&dsi->dev);
	dsi->mode_flags = desc->mode_flags;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;
	pinfo->desc = desc;

	pinfo->link = dsi;
	mipi_dsi_set_drvdata(dsi, pinfo);

	err = hx8279_panel_add(pinfo);
	if (err < 0)
		return err;

	err = mipi_dsi_attach(dsi);
	if (err < 0)
		drm_panel_remove(&pinfo->base);

	return err;
}

static void hx8279_panel_remove(struct mipi_dsi_device *dsi)
{
	struct panel_info *pinfo = mipi_dsi_get_drvdata(dsi);
	int err;

	err = hx8279_panel_disable(&pinfo->base);
	if (err < 0)
		dev_err(&dsi->dev, "failed to disable panel: %d\n", err);

	err = hx8279_panel_unprepare(&pinfo->base);
	if (err < 0)
		dev_err(&dsi->dev, "failed to unprepare panel: %d\n", err);

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);

	drm_panel_remove(&pinfo->base);
}

static void hx8279_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct panel_info *pinfo = mipi_dsi_get_drvdata(dsi);

	hx8279_panel_disable(&pinfo->base);
	hx8279_panel_unprepare(&pinfo->base);
}

static struct mipi_dsi_driver panel_driver = {
	.driver = {
		.name = "panel-himax8279",
		.of_match_table = panel_of_match,
	},
	.probe = hx8279_panel_probe,
	.remove = hx8279_panel_remove,
	.shutdown = hx8279_panel_shutdown,
};
module_mipi_dsi_driver(panel_driver);
