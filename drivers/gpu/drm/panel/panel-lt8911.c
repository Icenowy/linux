#include "panel-lt8911.h"
#include <linux/version.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

#define ILI9881_PAGE(_page)	DSI_DCS_WRITE(dsi, 0xff, 0x98, 0x81, _page)
#define IILI9881_COMMAND(_cmd, _data...)		DSI_DCS_WRITE(dsi, _cmd, _data)
#define DCS_CMD_READ_ID1        0xDA

#define LT_8911_I2C_ADAPTER	3
#define LT_8911_I2C_ADDR	0x45

static struct i2c_mipi_dsi g_lt8911_mipi_dsi;
static bool g_is_std_suspend __nosavedata;

static const struct drm_display_mode lt8911_default_mode = {
	.clock		= 152840,
	.hdisplay	= 1920,
	.hsync_start = 1920 + 140,
	.hsync_end	= 1920 + 140 + 160,
	.htotal		= 1920 + 140 + 160 + 30,

	.vdisplay	= 1080,
	.vsync_start	= 1080 + 18,
	.vsync_end	= 1080 + 18 + 28,
	.vtotal		= 1080 + 18 + 28 + 6,

	.width_mm	= 110,
	.height_mm	= 62,
	.flags      = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static struct panel_data lt8911_panel_data = {
	.display_mode = &lt8911_default_mode,
	.mode_flags   = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO_BURST,
	.format       = MIPI_DSI_FMT_RGB888,
	.lanes        = 4,
};

enum {
	hfp = 0,
	hs,
	hbp,
	hact,
	htotal,
	vfp,
	vs,
	vbp,
	vact,
	vtotal,
	pclk_10khz
};

static int mipi_timing[] = {
	140,  /* hfp */
	30,   /* hs */
	160,  /* hbp */
	1920, /* hact */
	2250, /* htotal */
	18,   /* vfp */
	6,    /* vs */
	28,   /* vbp */
	1080, /* vact */
	1132, /* vtotal */
	15284 /* pixel_clk / 10000 */
};

static int lt8911_i2c_write(struct i2c_client *client,
                               uint8_t reg, uint8_t val)
{
	int ret = -1;
	int retries = 0;
	uint8_t buf[2] = { reg, val };
	struct i2c_msg msg = {
		.flags = !I2C_M_RD,
		.addr = client->addr,
		.len = 2,
		.buf = buf,
	};

	while (retries < 5) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret == 1)
			return 0;
		retries++;
	}

	DBG_FUNC("%s: write addr 0x%02x error! ret = %d\n",
		__func__, reg, ret);
	return ret;
}

static int lt8911_i2c_read(struct i2c_client *client, uint8_t reg)
{
	int ret = -1;
	int retries = 0;
	uint8_t buf[2] = { reg, 0 };
	struct i2c_msg msgs[2];

	msgs[0].flags = client->flags;
	msgs[0].addr = client->addr;
	msgs[0].len = 1;
	msgs[0].buf = &buf[0];

	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].addr = client->addr;
	msgs[1].len = 1;
	msgs[1].buf = &buf[1];

	while (retries < 5) {
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret == 2)
			return buf[1];
		retries++;
	}

	DBG_FUNC("%s: read addr 0x%02x error! ret = %d\n",
		__func__, reg, ret);
	return ret;
}

static void lt8911_reset(struct i2c_mipi_dsi *md)
{
	gpio_set_value(md->reset_pin, 0);
	msleep(md->rst_delay_ms);
	gpio_set_value(md->reset_pin, 1);
	msleep(md->rst_delay_ms);
}

static void lt8911exb_cfg_set_mipi_timing(struct i2c_mipi_dsi *md)
{
	struct i2c_client *client = md->client;

	/* lt8911exb MIPI video timing configuration */
	lt8911_i2c_write(client, 0xff, 0xd0);
	lt8911_i2c_write(client, 0x0d, (u8)(mipi_timing[vtotal] / 256));
	lt8911_i2c_write(client, 0x0e, (u8)(mipi_timing[vtotal] % 256));
	lt8911_i2c_write(client, 0x0f, (u8)(mipi_timing[vact] / 256));
	lt8911_i2c_write(client, 0x10, (u8)(mipi_timing[vact] % 256));
	lt8911_i2c_write(client, 0x11, (u8)(mipi_timing[htotal] / 256));
	lt8911_i2c_write(client, 0x12, (u8)(mipi_timing[htotal] % 256));
	lt8911_i2c_write(client, 0x13, (u8)(mipi_timing[hact] / 256));
	lt8911_i2c_write(client, 0x14, (u8)(mipi_timing[hact] % 256));
	lt8911_i2c_write(client, 0x15, (u8)(mipi_timing[vs] % 256));
	lt8911_i2c_write(client, 0x16, (u8)(mipi_timing[hs] % 256));
	lt8911_i2c_write(client, 0x17, (u8)(mipi_timing[vfp] / 256));
	lt8911_i2c_write(client, 0x18, (u8)(mipi_timing[vfp] % 256));
	lt8911_i2c_write(client, 0x19, (u8)(mipi_timing[hfp] / 256));
	lt8911_i2c_write(client, 0x1a, (u8)(mipi_timing[hfp] % 256));
}

static void lt8911exb_cfg_set_edp_timing(struct i2c_mipi_dsi *md)
{
	struct i2c_client *client = md->client;

	/* lt8911exb eDP video timing configuration */
	lt8911_i2c_write(client, 0xff, 0xa8);
	lt8911_i2c_write(client, 0x2d, 0x88);
	lt8911_i2c_write(client, 0x05,
		(u8)(mipi_timing[htotal] / 256));
	lt8911_i2c_write(client, 0x06,
		(u8)(mipi_timing[htotal] % 256));
	lt8911_i2c_write(client, 0x07,
		(u8)((mipi_timing[hs] + mipi_timing[hbp]) / 256));
	lt8911_i2c_write(client, 0x08,
		(u8)((mipi_timing[hs] + mipi_timing[hbp]) % 256));
	lt8911_i2c_write(client, 0x09,
		(u8)(mipi_timing[hs] / 256));
	lt8911_i2c_write(client, 0x0a,
		(u8)(mipi_timing[hs] % 256));
	lt8911_i2c_write(client, 0x0b,
		(u8)(mipi_timing[hact] / 256));
	lt8911_i2c_write(client, 0x0c,
		(u8)(mipi_timing[hact] % 256));
	lt8911_i2c_write(client, 0x0d,
		(u8)(mipi_timing[vtotal] / 256));
	lt8911_i2c_write(client, 0x0e,
		(u8)(mipi_timing[vtotal] % 256));
	lt8911_i2c_write(client, 0x11,
		(u8)((mipi_timing[vs] + mipi_timing[vbp]) / 256));
	lt8911_i2c_write(client, 0x12,
		(u8)((mipi_timing[vs] + mipi_timing[vbp]) % 256));
	lt8911_i2c_write(client, 0x14,
		(u8)(mipi_timing[vs] % 256));
	lt8911_i2c_write(client, 0x15,
		(u8)(mipi_timing[vact] / 256));
	lt8911_i2c_write(client, 0x16,
		(u8)(mipi_timing[vact] % 256));
}

static void lt8911exb_cfg_init_regs(struct i2c_mipi_dsi *md)
{
	u32 val = 0;
	u8 i, pcr_pll_postdiv, pcr_m;
	struct i2c_client *client = md->client;
	u8 swing_ds1[13][2] = {
		{ 0x83, 0x00 }, /* 27.8 mA */
		{ 0x82, 0xe0 }, /* 26.2 mA */
		{ 0x82, 0xc0 }, /* 24.6 mA */
		{ 0x82, 0xa0 }, /* 23.0 mA */
		{ 0x82, 0x80 }, /* 21.4 mA */
		{ 0x82, 0x40 }, /* 18.2 mA */
		{ 0x82, 0x20 }, /* 16.6 mA */
		{ 0x82, 0x00 }, /* 15.0 mA */
		{ 0x81, 0x00 }, /* 12.8 mA */
		{ 0x80, 0xe0 }, /* 11.2 mA */
		{ 0x80, 0xc0 }, /* 9.6 mA */
		{ 0x80, 0xa0 }, /* 8 mA */
		{ 0x80, 0x80 }  /* 6 mA */
	};

	/* initialization */
	lt8911_i2c_write(client, 0xff, 0x81);
	lt8911_i2c_write(client, 0x49, 0xff);
	lt8911_i2c_write(client, 0xff, 0x82);
	lt8911_i2c_write(client, 0x5a, 0x0e);

	/* MIPI Rx analog */
	lt8911_i2c_write(client, 0xff, 0x82);
	lt8911_i2c_write(client, 0x32, 0x51);
	lt8911_i2c_write(client, 0x35, 0x22);
	lt8911_i2c_write(client, 0x4c, 0x0c);
	lt8911_i2c_write(client, 0x4d, 0x00);

	lt8911_i2c_write(client, 0x3a, 0x77);
	lt8911_i2c_write(client, 0x3b, 0x77);

	/* dessc_pcr pll analog */
	lt8911_i2c_write(client, 0xff, 0x82);
	lt8911_i2c_write(client, 0x6a, 0x40);
	lt8911_i2c_write(client, 0x6b, 0x40);

	if (mipi_timing[pclk_10khz] < 8800) {
		/* 0x44: pre-div = 2, pixel_clk = 44~88MHz */
		lt8911_i2c_write(client, 0x6e, 0x82);
		pcr_pll_postdiv = 0x08;
	} else {
		/* 0x40: pre-div = 1, pixel_clk = 88~176MHz */
		lt8911_i2c_write(client, 0x6e, 0x81);
		pcr_pll_postdiv = 0x04;
	}
	pcr_m = (u8)(mipi_timing[pclk_10khz] * pcr_pll_postdiv / 25 / 100);

	/* dessc pll digital */
	lt8911_i2c_write(client, 0xff, 0x85);
	lt8911_i2c_write(client, 0xa9, 0x31);
	lt8911_i2c_write(client, 0xaa, 0x17);
	lt8911_i2c_write(client, 0xab, 0xba);
	lt8911_i2c_write(client, 0xac, 0xe1);
	lt8911_i2c_write(client, 0xad, 0x47);
	lt8911_i2c_write(client, 0xae, 0x01);
	lt8911_i2c_write(client, 0xae, 0x11);

	/* digital top */
	lt8911_i2c_write(client, 0xff, 0x85);
	lt8911_i2c_write(client, 0xc0, 0x01);/* select mipi rx */

	if (md->edp_depth == 6)
		val = 0xd0; /* enable dither */
	else if (md->edp_depth == 8)
		val = 0x00; /* disable dither */
	lt8911_i2c_write(client, 0xb0, val);

	/* MIPI Rx digital */
	lt8911_i2c_write(client, 0xff, 0xd0);
	/* 0: 4 lane; 1: 1 lane; 2: 2 lane; 3: 3 lane */
	lt8911_i2c_write(client, 0x00, md->mipi_lane_cnt % 4);
	lt8911_i2c_write(client, 0x02, 0x08);
	lt8911_i2c_write(client, 0x08, 0x00);
	lt8911_i2c_write(client, 0x0a, 0x12);/* pcr mode */
	lt8911_i2c_write(client, 0x0c, 0x40);

	lt8911_i2c_write(client, 0x1c, 0x3a);
	lt8911_i2c_write(client, 0x31, 0x0a);

	lt8911_i2c_write(client, 0x3f, 0x10);
	lt8911_i2c_write(client, 0x40, 0x20);
	lt8911_i2c_write(client, 0x41, 0x30);

#ifdef TEST_PATTERN
	lt8911_i2c_write(client, 0x26, pcr_m | 0x80);
#else
	lt8911_i2c_write(client, 0x26, pcr_m);
#endif

	lt8911_i2c_write(client, 0x27, 0x28);
	lt8911_i2c_write(client, 0x28, 0xf8);

	lt8911_i2c_write(client, 0xff, 0x81);/* pcr reset */
	lt8911_i2c_write(client, 0x03, 0x7b);
	lt8911_i2c_write(client, 0x03, 0xff);

	/* Tx PLL 2.7GHz */
	lt8911_i2c_write(client, 0xff, 0x87);
	lt8911_i2c_write(client, 0x19, 0x31);
	lt8911_i2c_write(client, 0xff, 0x82);
	lt8911_i2c_write(client, 0x02, 0x42);
	lt8911_i2c_write(client, 0x03, 0x00);
	lt8911_i2c_write(client, 0x03, 0x01);
	lt8911_i2c_write(client, 0xff, 0x81);
	lt8911_i2c_write(client, 0x09, 0xfc);
	lt8911_i2c_write(client, 0x09, 0xfd);
	lt8911_i2c_write(client, 0xff, 0x87);
	lt8911_i2c_write(client, 0x0c, 0x11);

	for (i = 0; i < 5; i++) {
		msleep(5);
		if (lt8911_i2c_read(client, 0x37) & 0x02) {
			DBG_FUNC("%s: lt8911exb tx pll locked\n",
				__func__);
			break;
		}

		DBG_FUNC("%s: lt8911exb tx pll unlocked\n", __func__);
		lt8911_i2c_write(client, 0xff, 0x81);
		lt8911_i2c_write(client, 0x09, 0xfc);
		lt8911_i2c_write(client, 0x09, 0xfd);
		lt8911_i2c_write(client, 0xff, 0x87);
		lt8911_i2c_write(client, 0x0c, 0x10);
		lt8911_i2c_write(client, 0x0c, 0x11);
	}

	/* Tx PHY */
	lt8911_i2c_write(client, 0xff, 0x82);
	lt8911_i2c_write(client, 0x11, 0x00);
	lt8911_i2c_write(client, 0x13, 0x10);
	lt8911_i2c_write(client, 0x14, 0x0c);
	lt8911_i2c_write(client, 0x14, 0x08);
	lt8911_i2c_write(client, 0x13, 0x20);
	lt8911_i2c_write(client, 0xff, 0x82);
	lt8911_i2c_write(client, 0x0e, 0x25);
	lt8911_i2c_write(client, 0x12, 0xff);

	/* eDP tx digital */
	lt8911_i2c_write(client, 0xff, 0xa8);

#ifdef TEST_PATTERN
	/* bit[2:0]: test panttern image mode */
	lt8911_i2c_write(client, 0x24, 0x50);
	/* bit[6:4]: test pattern color */
	lt8911_i2c_write(client, 0x25, 0x70);
	/* 0x50: pattern; 0x10: mipi video */
	lt8911_i2c_write(client, 0x27, 0x50);
#else
	/* 0x50: pattern; 0x10: mipi video */
	lt8911_i2c_write(client, 0x27, 0x10);
#endif

	if (md->edp_depth == 6)
		val = 0x00;
	else if (md->edp_depth == 8)
		val = 0x10;
	lt8911_i2c_write(client, 0x17, val);
	lt8911_i2c_write(client, 0x18, val << 1);

	lt8911_i2c_write(client, 0xff, 0xa0);
	lt8911_i2c_write(client, 0x00, 0x08);
	lt8911_i2c_write(client, 0x01, 0x00);

	/* set eDP drive strength */
	lt8911_i2c_write(client, 0xff, 0x82);
	/* lane 0 tap0 */
	lt8911_i2c_write(client, 0x22, swing_ds1[0][0]);
	lt8911_i2c_write(client, 0x23, swing_ds1[0][1]);
	/* lane 0 tap1 */
	lt8911_i2c_write(client, 0x24, 0x80);
	lt8911_i2c_write(client, 0x25, 0x00);
	/* lane 1 tap0 */
	lt8911_i2c_write(client, 0x26, swing_ds1[0][0]);
	lt8911_i2c_write(client, 0x27, swing_ds1[0][1]);
	/* lane 1 tap1 */
	lt8911_i2c_write(client, 0x28, 0x80);
	lt8911_i2c_write(client, 0x29, 0x00);
}

/*
 * MIPI signal from SoC should be ready before
 * configuring below video check setting
 */
static void lt8911exb_dbg_check_mipi_timing(struct i2c_mipi_dsi *md)
{
	u32 val = 0;
	struct i2c_client *client = md->client;

	/* MIPI byte clk check */
	lt8911_i2c_write(client, 0xff, 0x85);
	/* FM select byte clk */
	lt8911_i2c_write(client, 0x1d, 0x00);
	lt8911_i2c_write(client, 0x40, 0xf7);
	lt8911_i2c_write(client, 0x41, 0x30);
	/* eDP scramble mode; video chech from mipi */
	lt8911_i2c_write(client, 0xa1, 0x02);
	/* 0xf0: close scramble; 0xD0: open scramble */
	//lt8911_i2c_write(client, 0x17, 0xf0);

	/* video check reset */
	lt8911_i2c_write(client, 0xff, 0x81);
	lt8911_i2c_write(client, 0x09, 0x7d);
	lt8911_i2c_write(client, 0x09, 0xfd);

	lt8911_i2c_write(client, 0xff, 0x85);
	//msleep(200);
	msleep(10);
	if (lt8911_i2c_read(client, 0x50) == 0x03) {
		val = lt8911_i2c_read(client, 0x4d);
		val = (val << 8) + lt8911_i2c_read(client, 0x4e);
		val = (val << 8) + lt8911_i2c_read(client, 0x4f);
		/* MIPI clk = val * 1000 */
		DBG_FUNC("%s: video check: mipi clk = %d\n",
			__func__, val);
	} else {
		DBG_FUNC("%s: video check: mipi clk unstable",
			__func__);
	}

	/* MIPI Vtotal check */
	val = lt8911_i2c_read(client, 0x76);
	val = (val << 8) + lt8911_i2c_read(client, 0x77);
	DBG_FUNC("%s: video check: Vtotal = %d\n",
		__func__, val);

	/* MIPI word count check */
	lt8911_i2c_write(client, 0xff, 0xd0);
	val = lt8911_i2c_read(client, 0x82);
	val = (val << 8) + lt8911_i2c_read(client, 0x83);
	val = val / 3;
	DBG_FUNC("%s: video check: Hact(word counter) = %d\n",
		__func__, val);

	/* MIPI Vact check */
	val = lt8911_i2c_read(client, 0x85);
	val = (val << 8) + lt8911_i2c_read(client, 0x86);
	DBG_FUNC("%s: video check: Vact = %d\n",
		__func__, val);
}

static void lt8911exb_link_train_start(struct i2c_mipi_dsi *md)
{
	struct i2c_client *client = md->client;

	/* lt8911exb link training */
	lt8911_i2c_write(client, 0xff, 0x85);
	/* eDP scramble mode */
	lt8911_i2c_write(client, 0xa1, 0x02);

	/* AUX setup */
	lt8911_i2c_write(client, 0xff, 0xac);
	/* soft link training */
	lt8911_i2c_write(client, 0x00, 0x60);
	lt8911_i2c_write(client, 0xff, 0xa6);
	lt8911_i2c_write(client, 0x2a, 0x00);

	lt8911_i2c_write(client, 0xff, 0x81);
	lt8911_i2c_write(client, 0x07, 0xfe);
	lt8911_i2c_write(client, 0x07, 0xff);
	lt8911_i2c_write(client, 0x0a, 0xfc);
	lt8911_i2c_write(client, 0x0a, 0xfe);

	/* link training */
	lt8911_i2c_write(client, 0xff, 0x85);
	lt8911_i2c_write(client, 0x1a, md->edp_lane_cnt);
	//lt8911_i2c_write(client, 0x13, 0xd1);
	lt8911_i2c_write(client, 0xff, 0xac);
	lt8911_i2c_write(client, 0x00, 0x64);
	lt8911_i2c_write(client, 0x01, 0x0a);
	lt8911_i2c_write(client, 0x0c, 0x85);
	lt8911_i2c_write(client, 0x0c, 0xc5);
}

static void lt8911exb_link_train_get_result(struct i2c_mipi_dsi *md)
{
	u32 i, val;
	struct i2c_client *client = md->client;

	lt8911_i2c_write(client, 0xff, 0xac);
	for (i = 0; i < 10; i++) {
		val = lt8911_i2c_read(client, 0x82);
		if (val & 0x20) {
			if ((val & 0x1f) == 0x1e)
				DBG_FUNC("%s: link training succeeded\n",
					__func__);
			else
				DBG_FUNC("%s: link training failed\n",
					__func__);

			DBG_FUNC("%s: panel link rate: %d\n", __func__,
				lt8911_i2c_read(client, 0x83));
			DBG_FUNC("%s: panel link count: %d\n", __func__,
				lt8911_i2c_read(client, 0x84));
			break;
		}
		DBG_FUNC("%s: link training ongoing...\n", __func__);
		msleep(100);
	}
}

/* panel_funcs */
static int panel_prepare(struct drm_panel *panel)
{
	int ret = 0;
	struct i2c_mipi_dsi *md = panel_to_md(panel);
	struct i2c_client *client = md->client;

	DBG_FUNC("lt8911exb enter\n");

	if(g_is_std_suspend){
		DBG_FUNC("lt8911exb prepare under std mode, do not prepare\n");
		return 0;
	}

	if(md->client == NULL){
		DBG_FUNC("lt8911exb i2c client still not ready\n");
		return 0;
	}

	ret = regulator_enable(md->vspn3v3);
	if(ret){
		goto fail;
	}

	ret = regulator_enable(md->hsvcc);
	if(ret){
		goto fail;
	}

	//msleep(200);
	msleep(10);

	lt8911_reset(md);

	lt8911_i2c_write(client, 0xff, 0x81); /* 0x81: register bank */
	lt8911_i2c_write(client, 0x08, 0x7f);

	DBG_FUNC("%s: lt8911exb chip ID: 0x%02x-0x%02x-0x%02x\n",
		__func__, lt8911_i2c_read(client, 0x00),
		lt8911_i2c_read(client, 0x01),
		lt8911_i2c_read(client, 0x02));

	return 0;

fail:
	gpio_set_value(md->reset_pin, 0);
	regulator_disable(md->hsvcc);
	regulator_disable(md->vspn3v3);
	return ret;
}

static int panel_unprepare(struct drm_panel *panel)
{
	int ret = 0;
	struct i2c_mipi_dsi *md = panel_to_md(panel);

	DBG_FUNC("panel_unprepare enter\n");
	gpio_set_value(md->reset_pin, 0);

	regulator_disable(md->hsvcc);
	regulator_disable(md->vspn3v3);

	return ret;
}

static int panel_enable(struct drm_panel *panel)
{
	int ret = 0;
	struct i2c_mipi_dsi *md = panel_to_md(panel);

	DBG_FUNC("panel_enable enter\n");

	if(g_is_std_suspend){
		DBG_FUNC("lt8911exb enable under std mode, do not enable\n");
		return 0;
	}

	gpio_set_value(md->backlight_pin, 1);

	lt8911exb_cfg_set_mipi_timing(md);
	lt8911exb_cfg_set_edp_timing(md);
	lt8911exb_cfg_init_regs(md);
	lt8911exb_dbg_check_mipi_timing(md);
	lt8911exb_link_train_start(md);
	lt8911exb_link_train_get_result(md);

	return ret;
}

static int panel_disable(struct drm_panel *panel)
{
	int ret = 0;
	struct i2c_mipi_dsi *md = panel_to_md(panel);

	DBG_FUNC("panel_disable enter\n");

	gpio_set_value(md->backlight_pin, 0);

	return ret;
}

static int panel_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct i2c_mipi_dsi *md = panel_to_md(panel);
	const struct drm_display_mode *m = md->desc->display_mode;
	struct drm_display_mode *mode;

	DBG_FUNC("panel_get_modes enter\n");

	mode = drm_mode_duplicate(connector->dev, m);
	if (!mode) {
		/*
		dev_err(pinfo->base.dev, "failed to add mode %ux%u@%u\n",
			m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
		*/
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
	.prepare = panel_prepare,
	.unprepare = panel_unprepare,
	.enable = panel_enable,
	.disable = panel_disable,
	.get_modes = panel_get_modes,
};

/* backlight */
static int backlight_update(struct backlight_device *bd)
{
	struct i2c_mipi_dsi *md = bl_get_data(bd);
	int brightness = bd->props.brightness;

	if (bd->props.power != FB_BLANK_UNBLANK ||
		bd->props.fb_blank != FB_BLANK_UNBLANK ||
		(bd->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))) {
			brightness = 0;
		}

	md->brightness = brightness;

	return 0;
}

static const struct backlight_ops backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status	= backlight_update,
};

static int lt8911_pm_notify(struct notifier_block *notify_block,
			     unsigned long mode, void *unused)
{
	struct i2c_mipi_dsi *ctx = container_of(
		notify_block, struct i2c_mipi_dsi, pm_notify);

	DBG_FUNC("pm_notify: mode (%ld)\n", mode);

	switch (mode) {
	case PM_HIBERNATION_PREPARE:
		DBG_FUNC("pm_notify PM_HIBERNATION_PREPARE\n");
		g_is_std_suspend = true;
		break;
	case PM_POST_HIBERNATION:
		DBG_FUNC("pm_notify PM_HIBERNATION_PREPARE\n");
		g_is_std_suspend = false;
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

/**
static int backlight_init(struct i2c_mipi_dsi *md)
{
	struct device *dev = &md->client->dev;
	struct backlight_properties props;
	struct backlight_device *bd;

	printk(KERN_ERR "=====Function %s line %d\n", __FUNCTION__, __LINE__);

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = 255;
	bd = devm_backlight_device_register(dev, dev_name(dev),
					dev, md, &backlight_ops,
					&props);
	if (IS_ERR(bd)) {
		dev_err(dev, "failed to register backlight\n");
		return PTR_ERR(bd);
	}

	bd->props.brightness = 255;
	backlight_update_status(bd);

	return 0;
}
*/

static int i2c_md_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct i2c_mipi_dsi *md = &g_lt8911_mipi_dsi;

	DBG_FUNC("start");

	i2c_set_clientdata(client, md);
	mutex_init(&md->mutex);
	md->client = client;

	return 0;
}

static void i2c_md_remove(struct i2c_client *i2c)
{
	struct i2c_mipi_dsi *md = i2c_get_clientdata(i2c);

	DBG_FUNC();

	mipi_dsi_detach(md->dsi);
	drm_panel_remove(&md->panel);

	return;
}

static void i2c_md_shutdown(struct i2c_client *i2c)
{
	struct i2c_mipi_dsi *md = i2c_get_clientdata(i2c);

	DBG_FUNC();

	mipi_dsi_detach(md->dsi);
	drm_panel_remove(&md->panel);
}

static int lt8911_parse_dt(struct i2c_mipi_dsi *md)
{
	int ret = -1;
	struct mipi_dsi_device *dsi = md->dsi;
	struct device_node *np = dsi->dev.of_node;

	md->hsvcc =  devm_regulator_get(&dsi->dev, "hsvcc");
	if (IS_ERR(md->hsvcc))
		return dev_err_probe(&dsi->dev, PTR_ERR(md->hsvcc),
				"Failed to request hsvcc regulator\n");

	md->vspn3v3 =  devm_regulator_get(&dsi->dev, "vspn3v3");
	if (IS_ERR(md->vspn3v3))
		return dev_err_probe(&dsi->dev, PTR_ERR(md->vspn3v3),
				"Failed to request vspn3v3 regulator\n");

	md->backlight_pin = of_get_named_gpio(np,
	                                 "lt8911,backlight-gpio",
	                                 0);
	if (!gpio_is_valid(md->backlight_pin)) {
		DBG_FUNC("%s: backlight-gpio is invalid\n", __func__);
		return -EINVAL;
	}

	ret = devm_gpio_request_one(&dsi->dev,
	                            md->backlight_pin,
	                            GPIOF_DIR_OUT, NULL);
	if (ret) {
		DBG_FUNC("%s: failed to request backlight gpio\n",
			__func__);
		return ret;
	}
	gpio_set_value(md->backlight_pin, 0);
	DBG_FUNC("%s: succeed to init backlight gpio\n", __func__);

	md->irq_pin = of_get_named_gpio(np,
	                           "lt8911,irq-gpio", 0);
	if (!gpio_is_valid(md->irq_pin)) {
		DBG_FUNC("%s: irq-gpio is invalid\n", __func__);
		return -EINVAL;
	}

	ret = devm_gpio_request_one(&dsi->dev,
	                            md->irq_pin,
	                            GPIOF_DIR_IN, NULL);
	if (ret) {
		DBG_FUNC("%s: failed to request irq gpio\n",
			__func__);
		return ret;
	}
	DBG_FUNC("%s: succeed to init irq gpio\n", __func__);

	ret = of_property_read_u32(np, "lt8911,rst-delay-ms",
	                           &md->rst_delay_ms);
	if (ret < 0) {
		DBG_FUNC("%s: no rst-delay-ms property in dts\n",
			__func__);
		md->rst_delay_ms = 100;
	}

	md->reset_pin = of_get_named_gpio(np,
		                        "lt8911,reset-gpio", 0);
	if (!gpio_is_valid(md->reset_pin)) {
		DBG_FUNC("%s: reset-gpio is invalid\n", __func__);
		return -EINVAL;
	}

	ret = devm_gpio_request_one(&dsi->dev,
	                            md->reset_pin,
	                            GPIOF_DIR_OUT, NULL);
	if (ret) {
		DBG_FUNC("%s: failed to request reset gpio\n",
			__func__);
		return ret;
	}
	gpio_set_value(md->reset_pin, 0);
	DBG_FUNC("%s: succeed to init reset gpio\n", __func__);

	if (of_property_read_u32(np, "lt8911,edp-lane-cnt",
	                         &md->edp_lane_cnt)) {
		DBG_FUNC("%s: miss edp-lane-cnt property in dts\n",
			__func__);
		md->edp_lane_cnt = 2; /* default value */
	}

	if (of_property_read_u32(np, "lt8911,mipi-lane-cnt",
	                         &md->mipi_lane_cnt)) {
		DBG_FUNC("%s: miss mipi-lane-cnt property in dts\n",
			__func__);
		md->mipi_lane_cnt = 4;
	}

	/*
	 * eDP panel color depth:
	 *     6 bit: 262K colors
	 *     8 bit: 16.7M colors
	 */
	if (of_property_read_u32(np, "lt8911,edp-depth",
	                         &md->edp_depth)) {
		DBG_FUNC("%s: miss edp-depth property in dts\n",
			__func__);
		md->edp_depth = 8;
	}
	
	return ret;
}

static const struct of_device_id i2c_md_of_ids[] = {
	{
		.compatible = "i2c,lt8911",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, i2c_md_of_ids);

#ifdef CONFIG_PM_SLEEP

static int edpi2c_suspend(struct device *dev)
{
	return 0;
}

static int edpi2c_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops edpi2c_pm_ops = {
    SET_LATE_SYSTEM_SLEEP_PM_OPS(edpi2c_suspend,
				 edpi2c_resume)
};

#define EDPI2C_PM_OPS &edpi2c_pm_ops

#else

#define EDPI2C_PM_OPS NULL

#endif

static struct i2c_driver i2c_md_driver = {
	.driver = {
		.name = "i2c_mipi_dsi",
		.pm = EDPI2C_PM_OPS,
		.of_match_table = i2c_md_of_ids,
	},
	.probe = i2c_md_probe,
	.remove = i2c_md_remove,
	.shutdown = i2c_md_shutdown,
};

module_i2c_driver(i2c_md_driver);

static int lt8911_dsi_probe(struct mipi_dsi_device *dsi)
{
	int ret;
	struct i2c_mipi_dsi *ctx;

	ctx = &g_lt8911_mipi_dsi;

	if(ctx == NULL){
		return -ENOMEM;
	}

	if(ctx->client == NULL){
		return -EPROBE_DEFER;
	}

	g_is_std_suspend = false;
	
	ctx->dsi    = dsi;
	ctx->desc   = &lt8911_panel_data;
	ret = lt8911_parse_dt(ctx); 
	if (ret) {
		DBG_FUNC("%s: failed to parse device tree\n", __func__);
		return ret;
	}

	dsi->mode_flags = ctx->desc->mode_flags;
	dsi->format     = ctx->desc->format;
	dsi->lanes      = ctx->desc->lanes;

	mipi_dsi_set_drvdata(dsi, ctx);

	//ctx->panel_data->set_dsi(ctx->dsi);
	drm_panel_init(&ctx->panel, &dsi->dev, &panel_funcs, DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	//backlight_init(ctx);

	if (IS_ENABLED(CONFIG_PM))
		ctx->pm_notify.notifier_call = lt8911_pm_notify;

	ret = register_pm_notifier(&ctx->pm_notify);
	if (ret)
		DBG_FUNC("register_pm_notifier failed: %d\n", ret);

	ret = mipi_dsi_attach(dsi);
	if (ret  < 0)
	{
		drm_panel_remove(&ctx->panel);
	}

	return ret;
}

static void lt8911_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct i2c_mipi_dsi *ctx = mipi_dsi_get_drvdata(dsi);

	unregister_pm_notifier(&ctx->pm_notify);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return;
}

static void lt8911_dsi_shutdown(struct mipi_dsi_device *dsi)
{
	return;
}

static const struct of_device_id lt8911_of_match[] = {
	{.compatible = "i2c_dsi,lt8911", },
	{ }
};
MODULE_DEVICE_TABLE(of, lt8911_of_match);

static struct mipi_dsi_driver lt8911_dsi_driver = {
	.probe		= lt8911_dsi_probe,
	.remove		= lt8911_dsi_remove,
	.shutdown   = lt8911_dsi_shutdown,
	.driver = {
		.name		= "panel-lt8911",
		.of_match_table	= lt8911_of_match,
	},
};
module_mipi_dsi_driver(lt8911_dsi_driver);

MODULE_DESCRIPTION("LT8911 Controller Driver");
MODULE_LICENSE("GPL v2");
