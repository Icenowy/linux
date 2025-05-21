// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 Radxa Limited
 * Copyright (c) 2022 Edgeble AI Technologies Pvt. Ltd.
 * Copyright (c) 2023 Sipeed
 *
 * Author:
 * - Jagan Teki <jagan@amarulasolutions.com>
 * - Stephen Chen <stephen@radxa.com>
 */

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

struct mingjun_panel_cmd {
	char cmdlen;
	char cmddata[0x40];
};

struct mingjun_panel_desc {
	const struct drm_display_mode *display_mode;

	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
	const struct mingjun_panel_cmd *on_cmds;
	unsigned int on_cmds_num;
};

struct panel_minjun_info {
	struct drm_panel base;
	struct mipi_dsi_device *link;
	const struct mingjun_panel_desc *desc;

	struct gpio_desc	*reset;
	struct regulator	*hsvcc;
	struct regulator	*vspn3v3;

	bool prepared;
	bool enabled;
};

static inline struct panel_minjun_info *to_panel_minjun_info(struct drm_panel *panel)
{
	return container_of(panel, struct panel_minjun_info, base);
}

static int mingjun_send_mipi_cmds(struct drm_panel *panel, const struct mingjun_panel_cmd *cmds)
{
	struct panel_minjun_info *pinfo = to_panel_minjun_info(panel);
	unsigned int i = 0;
	int err;

	for (i = 0; i < pinfo->desc->on_cmds_num; i++) {
		err = mipi_dsi_dcs_write_buffer(pinfo->link, &(cmds[i].cmddata[0]), cmds[i].cmdlen);
		if (err < 0)
			return err;
	}

	return 0;
}

static int mingjun_disable(struct drm_panel *panel)
{
	struct panel_minjun_info *pinfo = to_panel_minjun_info(panel);
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

static int mingjun_unprepare(struct drm_panel *panel)
{
	struct panel_minjun_info *pinfo = to_panel_minjun_info(panel);
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

static int mingjun_prepare(struct drm_panel *panel)
{
	struct panel_minjun_info *pinfo = to_panel_minjun_info(panel);
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

static int mingjun_enable(struct drm_panel *panel)
{
	struct panel_minjun_info *pinfo = to_panel_minjun_info(panel);
	int ret;

	if (pinfo->enabled)
		return 0;

	ret = mingjun_send_mipi_cmds(panel, pinfo->desc->on_cmds);
	if (ret < 0) {
		dev_err(panel->dev, "failed to send DCS Init Code: %d\n", ret);
		return ret;
	}

	msleep(110);

	ret = mipi_dsi_dcs_exit_sleep_mode(pinfo->link);
	if (ret < 0) {
		dev_err(panel->dev, "failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	msleep(120);

	ret = mipi_dsi_dcs_set_display_on(pinfo->link);
	if (ret < 0) {
		dev_err(panel->dev, "failed to set display on: %d\n", ret);
		return ret;
	}

	pinfo->enabled = true;

	return 0;
}

static int mingjun_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct panel_minjun_info *pinfo = to_panel_minjun_info(panel);
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
	.disable = mingjun_disable,
	.unprepare = mingjun_unprepare,
	.prepare = mingjun_prepare,
	.enable = mingjun_enable,
	.get_modes = mingjun_get_modes,
};

static const struct drm_display_mode mingjun_default_mode = {
	.clock		= 75750,
	.hdisplay	= 800,
	.hsync_start	= 800 + 48,
	.hsync_end	= 800 + 48 + 32,
	.htotal		= 800 + 48 + 32 + 80,

	.vdisplay	= 1280,
	.vsync_start	= 1280 + 3,
	.vsync_end	= 1280 + 3 + 10,
	.vtotal		= 1280 + 3 + 10 + 24,

	.width_mm	= 90,
	.height_mm	= 150,
	.flags          = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct mingjun_panel_cmd mingjun_on_cmds[] = {
	// { .cmdlen = 4,	.cmddata = {0xB9, 0xFF, 0x83, 0x94} },
	// { .cmdlen = 11,	.cmddata = {0xB1, 0x48, 0x0A, 0x6A, 0x09, 0x33, 0x54,
	// 			0x71, 0x71, 0x2E, 0x45} },
	// { .cmdlen = 7,	.cmddata = {0xBA, 0x63, 0x03, 0x68, 0x6B, 0xB2, 0xC0} },
	// { .cmdlen = 7,	.cmddata = {0xB2, 0x00, 0x80, 0x64, 0x0C, 0x06, 0x2F} },
	// { .cmdlen = 22, .cmddata = {0xB4, 0x1C, 0x78, 0x1C, 0x78, 0x1C, 0x78, 0x01,
	// 			0x0C, 0x86, 0x75, 0x00, 0x3F, 0x1C, 0x78, 0x1C,
	// 			0x78, 0x1C, 0x78, 0x01, 0x0C, 0x86} },
	// { .cmdlen = 34, .cmddata = {0xD3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
	// 			0x08, 0x32, 0x10, 0x05, 0x00, 0x05, 0x32, 0x13,
	// 			0xC1, 0x00, 0x01, 0x32, 0x10, 0x08, 0x00, 0x00,
	// 			0x37, 0x03, 0x07, 0x07, 0x37, 0x05, 0x05, 0x37,
	// 			0x0C, 0x40} },
	// { .cmdlen = 45, .cmddata = {0xD5, 0x18, 0x18, 0x18, 0x18, 0x22, 0x23, 0x20,
	// 			0x21, 0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02,
	// 			0x03, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	// 			0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	// 			0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	// 			0x18, 0x19, 0x19, 0x19, 0x19} },
	// { .cmdlen = 45, .cmddata = {0xD6, 0x18, 0x18, 0x19, 0x19, 0x21, 0x20, 0x23,
	// 			0x22, 0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05,
	// 			0x04, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	// 			0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	// 			0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	// 			0x18, 0x19, 0x19, 0x18, 0x18} },
	// { .cmdlen = 59, .cmddata = {0xE0, 0x07, 0x08, 0x09, 0x0D, 0x10, 0x14, 0x16,
	// 			0x13, 0x24, 0x36, 0x48, 0x4A, 0x58, 0x6F, 0x76,
	// 			0x80, 0x97, 0xA5, 0xA8, 0xB5, 0xC6, 0x62, 0x63,
	// 			0x68, 0x6F, 0x72, 0x78, 0x7F, 0x7F, 0x00, 0x02,
	// 			0x08, 0x0D, 0x0C, 0x0E, 0x0F, 0x10, 0x24, 0x36,
	// 			0x48, 0x4A, 0x58, 0x6F, 0x78, 0x82, 0x99, 0xA4,
	// 			0xA0, 0xB1, 0xC0, 0x5E, 0x5E, 0x64, 0x6B, 0x6C,
	// 			0x73, 0x7F, 0x7F} },
	// { .cmdlen = 2, .cmddata = {0xCC, 0x03} },
	// { .cmdlen = 3, .cmddata = {0xC0, 0x1F, 0x73} },
	// { .cmdlen = 3, .cmddata = {0xB6, 0x90, 0x90} },
	// { .cmdlen = 2, .cmddata = {0xD4, 0x02} },
	// { .cmdlen = 2, .cmddata = {0xBD, 0x01} },
	// { .cmdlen = 2, .cmddata = {0xB1, 0x00} },
	// { .cmdlen = 2, .cmddata = {0xBD, 0x00} },
	// { .cmdlen = 8, .cmddn bata = {0xBF, 0x40, 0x81, 0x50, 0x00, 0x1A, 0xFC, 0x01} },

	// { .cmdlen = 2, .cmddata = {0x36, 0x02} },
{ .cmdlen =4, .cmddata = {0xFF,0x98,0x81,0x03} },
{ .cmdlen = 2, .cmddata = {0x01,0x00} },
{ .cmdlen = 2, .cmddata = {0x02,0x00} },
{ .cmdlen = 2, .cmddata = {0x03,0x73} },
{ .cmdlen = 2, .cmddata = {0x04,0x13} },
{ .cmdlen = 2, .cmddata = {0x05,0x00} },
{ .cmdlen = 2, .cmddata = {0x06,0x0A} },
{ .cmdlen = 2, .cmddata = {0x07,0x05} },
{ .cmdlen = 2, .cmddata = {0x11,0x00} },
{ .cmdlen = 2, .cmddata = {0x09,0x28} },
{ .cmdlen = 2, .cmddata = {0x0A,0x00} },
{ .cmdlen = 2, .cmddata = {0x0B,0x00} },
{ .cmdlen = 2, .cmddata = {0x0C,0x00} },
{ .cmdlen = 2, .cmddata = {0x0D,0x28} },
{ .cmdlen = 2, .cmddata = {0x0E,0x00} },
{ .cmdlen = 2, .cmddata = {0x0F,0x28} },
{ .cmdlen = 2, .cmddata = {0x10,0x28} },
{ .cmdlen = 2, .cmddata = {0x11,0x00} },
{ .cmdlen = 2, .cmddata = {0x12,0x00} },
{ .cmdlen = 2, .cmddata = {0x13,0x00} },
{ .cmdlen = 2, .cmddata = {0x14,0x00} },
{ .cmdlen = 2, .cmddata = {0x15,0x00} },
{ .cmdlen = 2, .cmddata = {0x16,0x00} },
{ .cmdlen = 2, .cmddata = {0x17,0x00} },
{ .cmdlen = 2, .cmddata = {0x18,0x00} },
{ .cmdlen = 2, .cmddata = {0x19,0x00} },
{ .cmdlen = 2, .cmddata = {0x1A,0x00} },
{ .cmdlen = 2, .cmddata = {0x1B,0x00} },
{ .cmdlen = 2, .cmddata = {0x1C,0x00} },
{ .cmdlen = 2, .cmddata = {0x1D,0x00} },
{ .cmdlen = 2, .cmddata = {0x1E,0x40} },
{ .cmdlen = 2, .cmddata = {0x1F,0x80} },
{ .cmdlen = 2, .cmddata = {0x20,0x06} },
{ .cmdlen = 2, .cmddata = {0x21,0x01} },
{ .cmdlen = 2, .cmddata = {0x22,0x00} },
{ .cmdlen = 2, .cmddata = {0x23,0x00} },
{ .cmdlen = 2, .cmddata = {0x24,0x00} },
{ .cmdlen = 2, .cmddata = {0x25,0x00} },
{ .cmdlen = 2, .cmddata = {0x26,0x00} },
{ .cmdlen = 2, .cmddata = {0x27,0x00} },
{ .cmdlen = 2, .cmddata = {0x28,0x33} },
{ .cmdlen = 2, .cmddata = {0x29,0x33} },
{ .cmdlen = 2, .cmddata = {0x2A,0x00} },
{ .cmdlen = 2, .cmddata = {0x2B,0x00} },
{ .cmdlen = 2, .cmddata = {0x2C,0x04} },
{ .cmdlen = 2, .cmddata = {0x2D,0x0C} },
{ .cmdlen = 2, .cmddata = {0x2E,0x05} },
{ .cmdlen = 2, .cmddata = {0x2F,0x05} },
{ .cmdlen = 2, .cmddata = {0x30,0x00} },
{ .cmdlen = 2, .cmddata = {0x31,0x00} },
{ .cmdlen = 2, .cmddata = {0x32,0x31} },
{ .cmdlen = 2, .cmddata = {0x33,0x00} },
{ .cmdlen = 2, .cmddata = {0x34,0x00} },
{ .cmdlen = 2, .cmddata = {0x35,0x0A} },
{ .cmdlen = 2, .cmddata = {0x36,0x00} },
{ .cmdlen = 2, .cmddata = {0x37,0x08} },
{ .cmdlen = 2, .cmddata = {0x70,0x00} },
{ .cmdlen = 2, .cmddata = {0x39,0x00} },
{ .cmdlen = 2, .cmddata = {0x3A,0x00} },
{ .cmdlen = 2, .cmddata = {0x3B,0x00} },
{ .cmdlen = 2, .cmddata = {0x3C,0x00} },
{ .cmdlen = 2, .cmddata = {0x3D,0x00} },
{ .cmdlen = 2, .cmddata = {0x3E,0x00} },
{ .cmdlen = 2, .cmddata = {0x3F,0x00} },
{ .cmdlen = 2, .cmddata = {0x40,0x00} },
{ .cmdlen = 2, .cmddata = {0x41,0x00} },
{ .cmdlen = 2, .cmddata = {0x42,0x00} },
{ .cmdlen = 2, .cmddata = {0x43,0x08} },
{ .cmdlen = 2, .cmddata = {0x44,0x00} },
{ .cmdlen = 2, .cmddata = {0xA0,0x02} },
{ .cmdlen = 2, .cmddata = {0x51,0x23} },
{ .cmdlen = 2, .cmddata = {0x52,0x44} },
{ .cmdlen = 2, .cmddata = {0x53,0x67} },
{ .cmdlen = 2, .cmddata = {0x54,0x89} },
{ .cmdlen = 2, .cmddata = {0x55,0xAB} },
{ .cmdlen = 2, .cmddata = {0x56,0x01} },
{ .cmdlen = 2, .cmddata = {0x57,0x23} },
{ .cmdlen = 2, .cmddata = {0x58,0x45} },
{ .cmdlen = 2, .cmddata = {0x59,0x67} },
{ .cmdlen = 2, .cmddata = {0x5A,0x89} },
{ .cmdlen = 2, .cmddata = {0x5B,0xAB} },
{ .cmdlen = 2, .cmddata = {0x5C,0xCD} },
{ .cmdlen = 2, .cmddata = {0x5D,0xEF} },
{ .cmdlen = 2, .cmddata = {0x5E,0x11} },
{ .cmdlen = 2, .cmddata = {0x5F,0x02} },
{ .cmdlen = 2, .cmddata = {0x60,0x08} },
{ .cmdlen = 2, .cmddata = {0x61,0x0E} },
{ .cmdlen = 2, .cmddata = {0x62,0x0F} },
{ .cmdlen = 2, .cmddata = {0x63,0x0C} },
{ .cmdlen = 2, .cmddata = {0x64,0x0D} },
{ .cmdlen = 2, .cmddata = {0x65,0x17} },
{ .cmdlen = 2, .cmddata = {0x66,0x01} },
{ .cmdlen = 2, .cmddata = {0x67,0x01} },
{ .cmdlen = 2, .cmddata = {0x68,0x02} },
{ .cmdlen = 2, .cmddata = {0x69,0x02} },
{ .cmdlen = 2, .cmddata = {0x6A,0x00} },
{ .cmdlen = 2, .cmddata = {0x6B,0x00} },
{ .cmdlen = 2, .cmddata = {0x6C,0x02} },
{ .cmdlen = 2, .cmddata = {0x6D,0x02} },
{ .cmdlen = 2, .cmddata = {0x6E,0x16} },
{ .cmdlen = 2, .cmddata = {0x6F,0x16} },
{ .cmdlen = 2, .cmddata = {0x70,0x06} },
{ .cmdlen = 2, .cmddata = {0x71,0x06} },
{ .cmdlen = 2, .cmddata = {0x72,0x07} },
{ .cmdlen = 2, .cmddata = {0x73,0x07} },
{ .cmdlen = 2, .cmddata = {0x74,0x02} },
{ .cmdlen = 2, .cmddata = {0x75,0x02} },
{ .cmdlen = 2, .cmddata = {0x76,0x08} },
{ .cmdlen = 2, .cmddata = {0x77,0x0E} },
{ .cmdlen = 2, .cmddata = {0x78,0x0F} },
{ .cmdlen = 2, .cmddata = {0x79,0x0C} },
{ .cmdlen = 2, .cmddata = {0x7A,0x0D} },
{ .cmdlen = 2, .cmddata = {0x7B,0x17} },
{ .cmdlen = 2, .cmddata = {0x7C,0x01} },
{ .cmdlen = 2, .cmddata = {0x7D,0x01} },
{ .cmdlen = 2, .cmddata = {0x7E,0x02} },
{ .cmdlen = 2, .cmddata = {0x7F,0x02} },
{ .cmdlen = 2, .cmddata = {0x80,0x00} },
{ .cmdlen = 2, .cmddata = {0x81,0x00} },
{ .cmdlen = 2, .cmddata = {0x82,0x02} },
{ .cmdlen = 2, .cmddata = {0x83,0x02} },
{ .cmdlen = 2, .cmddata = {0x84,0x16} },
{ .cmdlen = 2, .cmddata = {0x85,0x16} },
{ .cmdlen = 2, .cmddata = {0x86,0x06} },
{ .cmdlen = 2, .cmddata = {0x87,0x06} },
{ .cmdlen = 2, .cmddata = {0x88,0x07} },
{ .cmdlen = 2, .cmddata = {0x89,0x07} },
{ .cmdlen = 2, .cmddata = {0x8A,0x02} },
{ .cmdlen = 4, .cmddata = {0xFF,0x98,0x81,0x04} },
{ .cmdlen = 2, .cmddata = {0x6E,0x1A} },
{ .cmdlen = 2, .cmddata = {0x6F,0x37} },
{ .cmdlen = 2, .cmddata = {0x3A,0xA4} },
{ .cmdlen = 2, .cmddata = {0x8D,0x1F} },
{ .cmdlen = 2, .cmddata = {0x87,0xBA} },
{ .cmdlen = 2, .cmddata = {0xB2,0xD1} },
{ .cmdlen = 2, .cmddata = {0x88,0x0B} },
{ .cmdlen = 2, .cmddata = {0x38,0x01} },
{ .cmdlen = 2, .cmddata = {0x39,0x00} },
{ .cmdlen = 2, .cmddata = {0xB5,0x02} },
{ .cmdlen = 2, .cmddata = {0x31,0x25} },
{ .cmdlen = 2, .cmddata = {0x3B,0x98} },
{ .cmdlen = 4, .cmddata = {0xFF,0x98,0x81,0x01} },
{ .cmdlen = 2, .cmddata = {0x22,0x0A} },
{ .cmdlen = 2, .cmddata = {0x31,0x00} },
{ .cmdlen = 2, .cmddata = {0xA6,0xA6} },
{ .cmdlen = 2, .cmddata = {0x55,0x3D} },
{ .cmdlen = 2, .cmddata = {0x50,0x9E} },
{ .cmdlen = 2, .cmddata = {0x51,0x99} },
{ .cmdlen = 2, .cmddata = {0x60,0x06} },
{ .cmdlen = 2, .cmddata = {0x62,0x20} },
{ .cmdlen = 2, .cmddata = {0xA0,0x00} },
{ .cmdlen = 2, .cmddata = {0xA1,0x17} },
{ .cmdlen = 2, .cmddata = {0xA2,0x26} },
{ .cmdlen = 2, .cmddata = {0xA3,0x13} },
{ .cmdlen = 2, .cmddata = {0xA4,0x16} },
{ .cmdlen = 2, .cmddata = {0xA5,0x29} },
{ .cmdlen = 2, .cmddata = {0xA6,0x1E} },
{ .cmdlen = 2, .cmddata = {0xA7,0x1F} },
{ .cmdlen = 2, .cmddata = {0xA8,0x8B} },
{ .cmdlen = 2, .cmddata = {0xA9,0x1D} },
{ .cmdlen = 2, .cmddata = {0xAA,0x2A} },
{ .cmdlen = 2, .cmddata = {0xAB,0x7B} },
{ .cmdlen = 2, .cmddata = {0xAC,0x1A} },
{ .cmdlen = 2, .cmddata = {0xAD,0x19} },
{ .cmdlen = 2, .cmddata = {0xAE,0x4E} },
{ .cmdlen = 2, .cmddata = {0xAF,0x24} },
{ .cmdlen = 2, .cmddata = {0xB0,0x29} },
{ .cmdlen = 2, .cmddata = {0xB1,0x4F} },
{ .cmdlen = 2, .cmddata = {0xB2,0x5C} },
{ .cmdlen = 2, .cmddata = {0xB3,0x3E} },
{ .cmdlen = 2, .cmddata = {0xC0,0x00} },
{ .cmdlen = 2, .cmddata = {0xC1,0x17} },
{ .cmdlen = 2, .cmddata = {0xC2,0x26} },
{ .cmdlen = 2, .cmddata = {0xC3,0x13} },
{ .cmdlen = 2, .cmddata = {0xC4,0x16} },
{ .cmdlen = 2, .cmddata = {0xC5,0x29} },
{ .cmdlen = 2, .cmddata = {0xC6,0x1E} },
{ .cmdlen = 2, .cmddata = {0xC7,0x1F} },
{ .cmdlen = 2, .cmddata = {0xC8,0x8B} },
{ .cmdlen = 2, .cmddata = {0xC9,0x1D} },
{ .cmdlen = 2, .cmddata = {0xCA,0x2A} },
{ .cmdlen = 2, .cmddata = {0xCB,0x7B} },
{ .cmdlen = 2, .cmddata = {0xCC,0x1A} },
{ .cmdlen = 2, .cmddata = {0xCD,0x19} },
{ .cmdlen = 2, .cmddata = {0xCE,0x4E} },
{ .cmdlen = 2, .cmddata = {0xCF,0x24} },
{ .cmdlen = 2, .cmddata = {0xD0,0x29} },
{ .cmdlen = 2, .cmddata = {0xD1,0x4D} },
{ .cmdlen = 2, .cmddata = {0xD2,0x5C} },
{ .cmdlen = 2, .cmddata = {0xD3,0x3E} },
{ .cmdlen = 4, .cmddata = {0xFF,0x98,0x81,0x00} },
{ .cmdlen = 2, .cmddata = {0x11,0x00} },
{ .cmdlen = 2, .cmddata = {0x29,0x00} },
{ .cmdlen = 2, .cmddata = {0x35,0x00} },
{ .cmdlen = 2, .cmddata = {0x00,0x00} },
};

static const struct mingjun_panel_desc mingjun_panel_desc = {
	.display_mode = &mingjun_default_mode,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO_BURST,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
	.on_cmds = mingjun_on_cmds,
	.on_cmds_num = ARRAY_SIZE(mingjun_on_cmds),
};

static const struct of_device_id panel_of_match[] = {
	{
		.compatible = "mingjun,mj070bi30ia2",
		.data = &mingjun_panel_desc,
	},
	{
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, panel_of_match);

static int mj070bi30ia2_panel_add(struct panel_minjun_info *pinfo)
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

static int mingjun_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct panel_minjun_info *pinfo;
	const struct mingjun_panel_desc *desc;
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

	err = mj070bi30ia2_panel_add(pinfo);
	if (err < 0)
		return err;

	err = mipi_dsi_attach(dsi);
	if (err < 0)
		drm_panel_remove(&pinfo->base);

	return err;
}

static void mingjun_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct panel_minjun_info *pinfo = mipi_dsi_get_drvdata(dsi);
	int err;

	err = mingjun_disable(&pinfo->base);
	if (err < 0)
		dev_err(&dsi->dev, "failed to disable panel: %d\n", err);

	err = mingjun_unprepare(&pinfo->base);
	if (err < 0)
		dev_err(&dsi->dev, "failed to unprepare panel: %d\n", err);

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);

	drm_panel_remove(&pinfo->base);
}

static void mj070bi30ia2_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct panel_minjun_info *pinfo = mipi_dsi_get_drvdata(dsi);

	mingjun_disable(&pinfo->base);
	mingjun_unprepare(&pinfo->base);
}

static struct mipi_dsi_driver mingjun_driver = {
	.driver = {
		.name = "mingjun-mj070bi30ia2",
		.of_match_table = panel_of_match,
	},
	.probe = mingjun_dsi_probe,
	.remove = mingjun_dsi_remove,
	.shutdown = mj070bi30ia2_panel_shutdown,
};
module_mipi_dsi_driver(mingjun_driver);

MODULE_AUTHOR("Jagan Teki <jagan@edgeble.ai>");
MODULE_AUTHOR("Stephen Chen <stephen@radxa.com>");
MODULE_DESCRIPTION("MingJun 070BI30IA2 DSI panel");
MODULE_LICENSE("GPL");
