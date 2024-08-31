/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <linux/workqueue.h>
#include <linux/time.h>
#include <linux/fb.h>
#include <linux/notifier.h>

/* test mode: display color stripe */
//#define TEST_PATTERN

#if 1
#define	LT8911EXB_DBG(x...)   printk(KERN_ERR x)
#else
#define	LT8911EXB_DBG(x...)
#endif

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

struct lt8911exb {
	struct i2c_client *client;
	struct notifier_block nb;
	struct delayed_work rst_check;

	int backlight_pin;
	int irq_pin;
	int reset_pin;
	int enable_pin;
	int rst_delay_ms;

	u32 edp_lane_cnt;
	u32 mipi_lane_cnt;
	u32 edp_depth;

	bool screen_on;
	bool init_done;
};

static int lt8911exb_i2c_write(struct i2c_client *client,
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

	LT8911EXB_DBG("%s: write addr 0x%02x error! ret = %d\n",
		__func__, reg, ret);
	return ret;
}

static int lt8911exb_i2c_read(struct i2c_client *client, uint8_t reg)
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

	LT8911EXB_DBG("%s: read addr 0x%02x error! ret = %d\n",
		__func__, reg, ret);
	return ret;
}

static void lt8911exb_reset(struct lt8911exb *lt8911exb)
{
	gpio_set_value(lt8911exb->reset_pin, 0);
	msleep(lt8911exb->rst_delay_ms);
	gpio_set_value(lt8911exb->reset_pin, 1);
	msleep(lt8911exb->rst_delay_ms);
}

static void lt8911exb_cfg_set_mipi_timing(struct lt8911exb *lt8911exb)
{
	struct i2c_client *client = lt8911exb->client;

	/* lt8911exb MIPI video timing configuration */
	lt8911exb_i2c_write(client, 0xff, 0xd0);
	lt8911exb_i2c_write(client, 0x0d, (u8)(mipi_timing[vtotal] / 256));
	lt8911exb_i2c_write(client, 0x0e, (u8)(mipi_timing[vtotal] % 256));
	lt8911exb_i2c_write(client, 0x0f, (u8)(mipi_timing[vact] / 256));
	lt8911exb_i2c_write(client, 0x10, (u8)(mipi_timing[vact] % 256));
	lt8911exb_i2c_write(client, 0x11, (u8)(mipi_timing[htotal] / 256));
	lt8911exb_i2c_write(client, 0x12, (u8)(mipi_timing[htotal] % 256));
	lt8911exb_i2c_write(client, 0x13, (u8)(mipi_timing[hact] / 256));
	lt8911exb_i2c_write(client, 0x14, (u8)(mipi_timing[hact] % 256));
	lt8911exb_i2c_write(client, 0x15, (u8)(mipi_timing[vs] % 256));
	lt8911exb_i2c_write(client, 0x16, (u8)(mipi_timing[hs] % 256));
	lt8911exb_i2c_write(client, 0x17, (u8)(mipi_timing[vfp] / 256));
	lt8911exb_i2c_write(client, 0x18, (u8)(mipi_timing[vfp] % 256));
	lt8911exb_i2c_write(client, 0x19, (u8)(mipi_timing[hfp] / 256));
	lt8911exb_i2c_write(client, 0x1a, (u8)(mipi_timing[hfp] % 256));
}

static void lt8911exb_cfg_set_edp_timing(struct lt8911exb *lt8911exb)
{
	struct i2c_client *client = lt8911exb->client;

	/* lt8911exb eDP video timing configuration */
	lt8911exb_i2c_write(client, 0xff, 0xa8);
	lt8911exb_i2c_write(client, 0x2d, 0x88);
	lt8911exb_i2c_write(client, 0x05,
		(u8)(mipi_timing[htotal] / 256));
	lt8911exb_i2c_write(client, 0x06,
		(u8)(mipi_timing[htotal] % 256));
	lt8911exb_i2c_write(client, 0x07,
		(u8)((mipi_timing[hs] + mipi_timing[hbp]) / 256));
	lt8911exb_i2c_write(client, 0x08,
		(u8)((mipi_timing[hs] + mipi_timing[hbp]) % 256));
	lt8911exb_i2c_write(client, 0x09,
		(u8)(mipi_timing[hs] / 256));
	lt8911exb_i2c_write(client, 0x0a,
		(u8)(mipi_timing[hs] % 256));
	lt8911exb_i2c_write(client, 0x0b,
		(u8)(mipi_timing[hact] / 256));
	lt8911exb_i2c_write(client, 0x0c,
		(u8)(mipi_timing[hact] % 256));
	lt8911exb_i2c_write(client, 0x0d,
		(u8)(mipi_timing[vtotal] / 256));
	lt8911exb_i2c_write(client, 0x0e,
		(u8)(mipi_timing[vtotal] % 256));
	lt8911exb_i2c_write(client, 0x11,
		(u8)((mipi_timing[vs] + mipi_timing[vbp]) / 256));
	lt8911exb_i2c_write(client, 0x12,
		(u8)((mipi_timing[vs] + mipi_timing[vbp]) % 256));
	lt8911exb_i2c_write(client, 0x14,
		(u8)(mipi_timing[vs] % 256));
	lt8911exb_i2c_write(client, 0x15,
		(u8)(mipi_timing[vact] / 256));
	lt8911exb_i2c_write(client, 0x16,
		(u8)(mipi_timing[vact] % 256));
}

static void lt8911exb_cfg_init_regs(struct lt8911exb *lt8911exb)
{
	u32 val = 0;
	u8 i, pcr_pll_postdiv, pcr_m;
	struct i2c_client *client = lt8911exb->client;
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
	lt8911exb_i2c_write(client, 0xff, 0x81);
	lt8911exb_i2c_write(client, 0x49, 0xff);
	lt8911exb_i2c_write(client, 0xff, 0x82);
	lt8911exb_i2c_write(client, 0x5a, 0x0e);

	/* MIPI Rx analog */
	lt8911exb_i2c_write(client, 0xff, 0x82);
	lt8911exb_i2c_write(client, 0x32, 0x51);
	lt8911exb_i2c_write(client, 0x35, 0x22);
	lt8911exb_i2c_write(client, 0x4c, 0x0c);
	lt8911exb_i2c_write(client, 0x4d, 0x00);

	lt8911exb_i2c_write(client, 0x3a, 0x77);
	lt8911exb_i2c_write(client, 0x3b, 0x77);

	/* dessc_pcr pll analog */
	lt8911exb_i2c_write(client, 0xff, 0x82);
	lt8911exb_i2c_write(client, 0x6a, 0x40);
	lt8911exb_i2c_write(client, 0x6b, 0x40);

	if (mipi_timing[pclk_10khz] < 8800) {
		/* 0x44: pre-div = 2, pixel_clk = 44~88MHz */
		lt8911exb_i2c_write(client, 0x6e, 0x82);
		pcr_pll_postdiv = 0x08;
	} else {
		/* 0x40: pre-div = 1, pixel_clk = 88~176MHz */
		lt8911exb_i2c_write(client, 0x6e, 0x81);
		pcr_pll_postdiv = 0x04;
	}
	pcr_m = (u8)(mipi_timing[pclk_10khz] * pcr_pll_postdiv / 25 / 100);

	/* dessc pll digital */
	lt8911exb_i2c_write(client, 0xff, 0x85);
	lt8911exb_i2c_write(client, 0xa9, 0x31);
	lt8911exb_i2c_write(client, 0xaa, 0x17);
	lt8911exb_i2c_write(client, 0xab, 0xba);
	lt8911exb_i2c_write(client, 0xac, 0xe1);
	lt8911exb_i2c_write(client, 0xad, 0x47);
	lt8911exb_i2c_write(client, 0xae, 0x01);
	lt8911exb_i2c_write(client, 0xae, 0x11);

	/* digital top */
	lt8911exb_i2c_write(client, 0xff, 0x85);
	lt8911exb_i2c_write(client, 0xc0, 0x01);/* select mipi rx */

	if (lt8911exb->edp_depth == 6)
		val = 0xd0; /* enable dither */
	else if (lt8911exb->edp_depth == 8)
		val = 0x00; /* disable dither */
	lt8911exb_i2c_write(client, 0xb0, val);

	/* MIPI Rx digital */
	lt8911exb_i2c_write(client, 0xff, 0xd0);
	/* 0: 4 lane; 1: 1 lane; 2: 2 lane; 3: 3 lane */
	lt8911exb_i2c_write(client, 0x00, lt8911exb->mipi_lane_cnt % 4);
	lt8911exb_i2c_write(client, 0x02, 0x08);
	lt8911exb_i2c_write(client, 0x08, 0x00);
	lt8911exb_i2c_write(client, 0x0a, 0x12);/* pcr mode */
	lt8911exb_i2c_write(client, 0x0c, 0x40);

	lt8911exb_i2c_write(client, 0x1c, 0x3a);
	lt8911exb_i2c_write(client, 0x31, 0x0a);

	lt8911exb_i2c_write(client, 0x3f, 0x10);
	lt8911exb_i2c_write(client, 0x40, 0x20);
	lt8911exb_i2c_write(client, 0x41, 0x30);

#ifdef TEST_PATTERN
	lt8911exb_i2c_write(client, 0x26, pcr_m | 0x80);
#else
	lt8911exb_i2c_write(client, 0x26, pcr_m);
#endif

	lt8911exb_i2c_write(client, 0x27, 0x28);
	lt8911exb_i2c_write(client, 0x28, 0xf8);

	lt8911exb_i2c_write(client, 0xff, 0x81);/* pcr reset */
	lt8911exb_i2c_write(client, 0x03, 0x7b);
	lt8911exb_i2c_write(client, 0x03, 0xff);

	/* Tx PLL 2.7GHz */
	lt8911exb_i2c_write(client, 0xff, 0x87);
	lt8911exb_i2c_write(client, 0x19, 0x31);
	lt8911exb_i2c_write(client, 0xff, 0x82);
	lt8911exb_i2c_write(client, 0x02, 0x42);
	lt8911exb_i2c_write(client, 0x03, 0x00);
	lt8911exb_i2c_write(client, 0x03, 0x01);
	lt8911exb_i2c_write(client, 0xff, 0x81);
	lt8911exb_i2c_write(client, 0x09, 0xfc);
	lt8911exb_i2c_write(client, 0x09, 0xfd);
	lt8911exb_i2c_write(client, 0xff, 0x87);
	lt8911exb_i2c_write(client, 0x0c, 0x11);

	for (i = 0; i < 5; i++) {
		msleep(5);
		if (lt8911exb_i2c_read(client, 0x37) & 0x02) {
			LT8911EXB_DBG("%s: lt8911exb tx pll locked\n",
				__func__);
			break;
		}

		LT8911EXB_DBG("%s: lt8911exb tx pll unlocked\n", __func__);
		lt8911exb_i2c_write(client, 0xff, 0x81);
		lt8911exb_i2c_write(client, 0x09, 0xfc);
		lt8911exb_i2c_write(client, 0x09, 0xfd);
		lt8911exb_i2c_write(client, 0xff, 0x87);
		lt8911exb_i2c_write(client, 0x0c, 0x10);
		lt8911exb_i2c_write(client, 0x0c, 0x11);
	}

	/* Tx PHY */
	lt8911exb_i2c_write(client, 0xff, 0x82);
	lt8911exb_i2c_write(client, 0x11, 0x00);
	lt8911exb_i2c_write(client, 0x13, 0x10);
	lt8911exb_i2c_write(client, 0x14, 0x0c);
	lt8911exb_i2c_write(client, 0x14, 0x08);
	lt8911exb_i2c_write(client, 0x13, 0x20);
	lt8911exb_i2c_write(client, 0xff, 0x82);
	lt8911exb_i2c_write(client, 0x0e, 0x25);
	lt8911exb_i2c_write(client, 0x12, 0xff);

	/* eDP tx digital */
	lt8911exb_i2c_write(client, 0xff, 0xa8);

#ifdef TEST_PATTERN
	/* bit[2:0]: test panttern image mode */
	lt8911exb_i2c_write(client, 0x24, 0x50);
	/* bit[6:4]: test pattern color */
	lt8911exb_i2c_write(client, 0x25, 0x70);
	/* 0x50: pattern; 0x10: mipi video */
	lt8911exb_i2c_write(client, 0x27, 0x50);
#else
	/* 0x50: pattern; 0x10: mipi video */
	lt8911exb_i2c_write(client, 0x27, 0x10);
#endif

	if (lt8911exb->edp_depth == 6)
		val = 0x00;
	else if (lt8911exb->edp_depth == 8)
		val = 0x10;
	lt8911exb_i2c_write(client, 0x17, val);
	lt8911exb_i2c_write(client, 0x18, val << 1);

	lt8911exb_i2c_write(client, 0xff, 0xa0);
	lt8911exb_i2c_write(client, 0x00, 0x08);
	lt8911exb_i2c_write(client, 0x01, 0x00);

	/* set eDP drive strength */
	lt8911exb_i2c_write(client, 0xff, 0x82);
	/* lane 0 tap0 */
	lt8911exb_i2c_write(client, 0x22, swing_ds1[0][0]);
	lt8911exb_i2c_write(client, 0x23, swing_ds1[0][1]);
	/* lane 0 tap1 */
	lt8911exb_i2c_write(client, 0x24, 0x80);
	lt8911exb_i2c_write(client, 0x25, 0x00);
	/* lane 1 tap0 */
	lt8911exb_i2c_write(client, 0x26, swing_ds1[0][0]);
	lt8911exb_i2c_write(client, 0x27, swing_ds1[0][1]);
	/* lane 1 tap1 */
	lt8911exb_i2c_write(client, 0x28, 0x80);
	lt8911exb_i2c_write(client, 0x29, 0x00);
}

/*
 * MIPI signal from SoC should be ready before
 * configuring below video check setting
 */
static void lt8911exb_dbg_check_mipi_timing(struct lt8911exb *lt8911exb)
{
	u32 val = 0;
	struct i2c_client *client = lt8911exb->client;

	/* MIPI byte clk check */
	lt8911exb_i2c_write(client, 0xff, 0x85);
	/* FM select byte clk */
	lt8911exb_i2c_write(client, 0x1d, 0x00);
	lt8911exb_i2c_write(client, 0x40, 0xf7);
	lt8911exb_i2c_write(client, 0x41, 0x30);
	/* eDP scramble mode; video chech from mipi */
	lt8911exb_i2c_write(client, 0xa1, 0x02);
	/* 0xf0: close scramble; 0xD0: open scramble */
	//lt8911exb_i2c_write(client, 0x17, 0xf0);

	/* video check reset */
	lt8911exb_i2c_write(client, 0xff, 0x81);
	lt8911exb_i2c_write(client, 0x09, 0x7d);
	lt8911exb_i2c_write(client, 0x09, 0xfd);

	lt8911exb_i2c_write(client, 0xff, 0x85);
	msleep(200);
	if (lt8911exb_i2c_read(client, 0x50) == 0x03) {
		val = lt8911exb_i2c_read(client, 0x4d);
		val = (val << 8) + lt8911exb_i2c_read(client, 0x4e);
		val = (val << 8) + lt8911exb_i2c_read(client, 0x4f);
		/* MIPI clk = val * 1000 */
		LT8911EXB_DBG("%s: video check: mipi clk = %d\n",
			__func__, val);
	} else {
		LT8911EXB_DBG("%s: video check: mipi clk unstable",
			__func__);
	}

	/* MIPI Vtotal check */
	val = lt8911exb_i2c_read(client, 0x76);
	val = (val << 8) + lt8911exb_i2c_read(client, 0x77);
	LT8911EXB_DBG("%s: video check: Vtotal = %d\n",
		__func__, val);

	/* MIPI word count check */
	lt8911exb_i2c_write(client, 0xff, 0xd0);
	val = lt8911exb_i2c_read(client, 0x82);
	val = (val << 8) + lt8911exb_i2c_read(client, 0x83);
	val = val / 3;
	LT8911EXB_DBG("%s: video check: Hact(word counter) = %d\n",
		__func__, val);

	/* MIPI Vact check */
	val = lt8911exb_i2c_read(client, 0x85);
	val = (val << 8) + lt8911exb_i2c_read(client, 0x86);
	LT8911EXB_DBG("%s: video check: Vact = %d\n",
		__func__, val);
}

static void lt8911exb_link_train_start(struct lt8911exb *lt8911exb)
{
	struct i2c_client *client = lt8911exb->client;

	/* lt8911exb link training */
	lt8911exb_i2c_write(client, 0xff, 0x85);
	/* eDP scramble mode */
	lt8911exb_i2c_write(client, 0xa1, 0x02);

	/* AUX setup */
	lt8911exb_i2c_write(client, 0xff, 0xac);
	/* soft link training */
	lt8911exb_i2c_write(client, 0x00, 0x60);
	lt8911exb_i2c_write(client, 0xff, 0xa6);
	lt8911exb_i2c_write(client, 0x2a, 0x00);

	lt8911exb_i2c_write(client, 0xff, 0x81);
	lt8911exb_i2c_write(client, 0x07, 0xfe);
	lt8911exb_i2c_write(client, 0x07, 0xff);
	lt8911exb_i2c_write(client, 0x0a, 0xfc);
	lt8911exb_i2c_write(client, 0x0a, 0xfe);

	/* link training */
	lt8911exb_i2c_write(client, 0xff, 0x85);
	lt8911exb_i2c_write(client, 0x1a, lt8911exb->edp_lane_cnt);
	//lt8911exb_i2c_write(client, 0x13, 0xd1);
	lt8911exb_i2c_write(client, 0xff, 0xac);
	lt8911exb_i2c_write(client, 0x00, 0x64);
	lt8911exb_i2c_write(client, 0x01, 0x0a);
	lt8911exb_i2c_write(client, 0x0c, 0x85);
	lt8911exb_i2c_write(client, 0x0c, 0xc5);
}

static void lt8911exb_link_train_get_result(struct lt8911exb *lt8911exb)
{
	u32 i, val;
	struct i2c_client *client = lt8911exb->client;

	lt8911exb_i2c_write(client, 0xff, 0xac);
	for (i = 0; i < 10; i++) {
		val = lt8911exb_i2c_read(client, 0x82);
		if (val & 0x20) {
			if ((val & 0x1f) == 0x1e)
				LT8911EXB_DBG("%s: link training succeeded\n",
					__func__);
			else
				LT8911EXB_DBG("%s: link training failed\n",
					__func__);

			LT8911EXB_DBG("%s: panel link rate: %d\n", __func__,
				lt8911exb_i2c_read(client, 0x83));
			LT8911EXB_DBG("%s: panel link count: %d\n", __func__,
				lt8911exb_i2c_read(client, 0x84));
			break;
		}
		LT8911EXB_DBG("%s: link training ongoing...\n", __func__);
		msleep(100);
	}
}

static void lt8911exb_chip_init(struct lt8911exb *lt8911exb)
{
	lt8911exb_cfg_set_mipi_timing(lt8911exb);
	lt8911exb_cfg_set_edp_timing(lt8911exb);
	lt8911exb_cfg_init_regs(lt8911exb);
	lt8911exb_dbg_check_mipi_timing(lt8911exb);
	lt8911exb_link_train_start(lt8911exb);
	lt8911exb_link_train_get_result(lt8911exb);
}

/* return value: 0x77 is normal, others are abnormal */
static int lt8911exb_dpcd0202_check(struct i2c_client *client)
{
	u8 val = 0, dpcd_val = 0x77;

	/* AUX operater init */
	lt8911exb_i2c_write(client, 0xff, 0xac);
	/* soft link training */
	lt8911exb_i2c_write(client, 0x00, 0x20);
	lt8911exb_i2c_write(client, 0xff, 0xa6);
	lt8911exb_i2c_write(client, 0x2a, 0x01);

	/* read DPCD 0x0202 */
	lt8911exb_i2c_write(client, 0xff, 0xa6);
	lt8911exb_i2c_write(client, 0x2b, 0x90); /* command */
	lt8911exb_i2c_write(client, 0x2b, 0x02); /* addr[15:8] */
	lt8911exb_i2c_write(client, 0x2b, 0x02); /* addr[7:0] */
	lt8911exb_i2c_write(client, 0x2b, 0x00); /* data lenth */
	lt8911exb_i2c_write(client, 0x2c, 0x00); /* start Aux to read EDID */

	msleep(20); /* more than 10ms */

	val = lt8911exb_i2c_read(client, 0x25);
	if ((val & 0x0f) == 0x0c) {
		if (lt8911exb_i2c_read(client, 0x39) == 0x22) {
			/* first reading is necessary! */
			lt8911exb_i2c_read(client, 0x2b);
			dpcd_val = lt8911exb_i2c_read(client, 0x2b);
		} else {
			dpcd_val = 0xff;
		}
	}

	return dpcd_val;
}

static void lt8911exb_rst_check_work_cb(struct work_struct *work)
{
	struct delayed_work *dwork =
		container_of(work, struct delayed_work, work);
	struct lt8911exb *lt8911exb =
		container_of(dwork, struct lt8911exb, rst_check);
	struct i2c_client *client = lt8911exb->client;

	if (!lt8911exb->init_done) {
		lt8911exb->init_done = true;
		lt8911exb_chip_init(lt8911exb);
	}

	if (lt8911exb->screen_on) {
		if (gpio_get_value(lt8911exb->backlight_pin) == 0)
			gpio_set_value(lt8911exb->backlight_pin, 1);
	}

	if (lt8911exb_dpcd0202_check(client) != 0x77) {
		msleep(50);
		if (lt8911exb_dpcd0202_check(client) != 0x77) {
			LT8911EXB_DBG("%s: line = %d\n", __func__, __LINE__);
			/* AUX setup */
			lt8911exb_i2c_write(client, 0xff, 0xac);

			/* soft link train */
			lt8911exb_i2c_write(client, 0x00, 0x60);
			lt8911exb_i2c_write(client, 0xff, 0xa6);
			lt8911exb_i2c_write(client, 0x2a, 0x00);

			lt8911exb_i2c_write(client, 0xff, 0x81);
			lt8911exb_i2c_write(client, 0x07, 0xfe);
			lt8911exb_i2c_write(client, 0x07, 0xff);
			lt8911exb_i2c_write(client, 0x0a, 0xfc);
			lt8911exb_i2c_write(client, 0x0a, 0xfe);

			/* link train */
			lt8911exb_i2c_write(client, 0xff, 0x85);
			lt8911exb_i2c_write(client, 0x1a,
			                    lt8911exb->edp_lane_cnt);

			lt8911exb_i2c_write(client, 0xff, 0xac);
			lt8911exb_i2c_write(client, 0x00, 0x64);
			lt8911exb_i2c_write(client, 0x01, 0x0a);
			lt8911exb_i2c_write(client, 0x0c, 0x85);
			lt8911exb_i2c_write(client, 0x0c, 0xc5);
		}
	}

	schedule_delayed_work(&lt8911exb->rst_check,
	                      msecs_to_jiffies(2000));
}

static void lt8911exb_suspend(struct lt8911exb *lt8911exb)
{
	gpio_set_value(lt8911exb->backlight_pin, 0);
	cancel_delayed_work_sync(&lt8911exb->rst_check);

	gpio_set_value(lt8911exb->reset_pin, 0);
//	gpio_set_value(lt8911exb->enable_pin, 0);
}

static void lt8911exb_resume(struct lt8911exb *lt8911exb)
{
//	gpio_set_value(lt8911exb->enable_pin, 1);
	lt8911exb_reset(lt8911exb);
	lt8911exb_chip_init(lt8911exb);

	schedule_delayed_work(&lt8911exb->rst_check,
	                      msecs_to_jiffies(500));
}

static int lt8911exb_fb_notifier_cb(struct notifier_block *self,
                      unsigned long action, void *ptr)
{
	struct fb_event *event = ptr;
	struct lt8911exb *lt8911exb =
		container_of(self, struct lt8911exb, nb);

	switch (action) {
	case FB_EARLY_EVENT_BLANK:
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			break;
		default:
			if (lt8911exb->screen_on) {
				LT8911EXB_DBG("%s: screen off\n", __func__);
				lt8911exb->screen_on = false;
				lt8911exb_suspend(lt8911exb);
			}
			break;
		}
		break;
	case FB_EVENT_BLANK:
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			if (!lt8911exb->screen_on) {
				LT8911EXB_DBG("%s: screen on\n", __func__);
				lt8911exb->screen_on = true;
				lt8911exb_resume(lt8911exb);
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int lt8911exb_parse_dt(struct lt8911exb *lt8911exb)
{
	int ret = -1;
	struct i2c_client *client = lt8911exb->client;
	struct device_node *np = client->dev.of_node;

	lt8911exb->backlight_pin = of_get_named_gpio_flags(np,
	                                 "lt8911exb,backlight-gpio",
	                                 0, NULL);
	if (!gpio_is_valid(lt8911exb->backlight_pin)) {
		LT8911EXB_DBG("%s: backlight-gpio is invalid\n", __func__);
		return -EINVAL;
	}

	ret = devm_gpio_request_one(&client->dev,
	                            lt8911exb->backlight_pin,
	                            GPIOF_DIR_OUT, NULL);
	if (ret) {
		LT8911EXB_DBG("%s: failed to request backlight gpio\n",
			__func__);
		return ret;
	}
	gpio_set_value(lt8911exb->backlight_pin, 0);
	LT8911EXB_DBG("%s: succeed to init backlight gpio\n", __func__);

	lt8911exb->irq_pin = of_get_named_gpio_flags(np,
	                           "lt8911exb,irq-gpio", 0, NULL);
	if (!gpio_is_valid(lt8911exb->irq_pin)) {
		LT8911EXB_DBG("%s: irq-gpio is invalid\n", __func__);
		return -EINVAL;
	}

	ret = devm_gpio_request_one(&client->dev,
	                            lt8911exb->irq_pin,
	                            GPIOF_DIR_IN, NULL);
	if (ret) {
		LT8911EXB_DBG("%s: failed to request irq gpio\n",
			__func__);
		return ret;
	}
	LT8911EXB_DBG("%s: succeed to init irq gpio\n", __func__);

	/*
	 * TODO:
	 * request threaded irq handler
	 */
/*
	lt8911exb->enable_pin = of_get_named_gpio_flags(np,
	                           "lt8911exb,enable-gpio", 0, NULL);
	if (!gpio_is_valid(lt8911exb->enable_pin)) {
		LT8911EXB_DBG("%s: enable_pin is invalid\n", __func__);
		return -EINVAL;
	}

	ret = devm_gpio_request_one(&client->dev,
	                            lt8911exb->enable_pin,
	                            GPIOF_DIR_OUT, "edp2_enable_pin");
	if (ret) {
		LT8911EXB_DBG("%s: failed to request enable gpio\n",
			__func__);
		return ret;
	}
	LT8911EXB_DBG("%s: succeed to init enable gpio\n", __func__);
	gpio_set_value(lt8911exb->enable_pin, 1);
*/
	ret = of_property_read_u32(np, "lt8911exb,rst-delay-ms",
	                           &lt8911exb->rst_delay_ms);
	if (ret < 0) {
		LT8911EXB_DBG("%s: no rst-delay-ms property in dts\n",
			__func__);
		lt8911exb->rst_delay_ms = 100;
	}

	lt8911exb->reset_pin = of_get_named_gpio_flags(np,
		                        "lt8911exb,reset-gpio", 0, NULL);
	if (!gpio_is_valid(lt8911exb->reset_pin)) {
		LT8911EXB_DBG("%s: reset-gpio is invalid\n", __func__);
		return -EINVAL;
	}

	ret = devm_gpio_request_one(&client->dev,
	                            lt8911exb->reset_pin,
	                            GPIOF_DIR_OUT, NULL);
	if (ret) {
		LT8911EXB_DBG("%s: failed to request reset gpio\n",
			__func__);
		return ret;
	}
	gpio_set_value(lt8911exb->reset_pin, 0);
	LT8911EXB_DBG("%s: succeed to init reset gpio\n", __func__);

	if (of_property_read_u32(np, "lt8911exb,edp-lane-cnt",
	                         &lt8911exb->edp_lane_cnt)) {
		LT8911EXB_DBG("%s: miss edp-lane-cnt property in dts\n",
			__func__);
		lt8911exb->edp_lane_cnt = 2; /* default value */
	}

	if (of_property_read_u32(np, "lt8911exb,mipi-lane-cnt",
	                         &lt8911exb->mipi_lane_cnt)) {
		LT8911EXB_DBG("%s: miss mipi-lane-cnt property in dts\n",
			__func__);
		lt8911exb->mipi_lane_cnt = 4;
	}

	/*
	 * eDP panel color depth:
	 *     6 bit: 262K colors
	 *     8 bit: 16.7M colors
	 */
	if (of_property_read_u32(np, "lt8911exb,edp-depth",
	                         &lt8911exb->edp_depth)) {
		LT8911EXB_DBG("%s: miss edp-depth property in dts\n",
			__func__);
		lt8911exb->edp_depth = 8;
	}

	return ret;
}

static int lt8911exb_i2c_probe(struct i2c_client *client,
                               const struct i2c_device_id *id)
{
	int ret = -1;
	struct lt8911exb *lt8911exb;

	LT8911EXB_DBG("%s: start\n", __func__);

	lt8911exb = devm_kzalloc(&client->dev, sizeof(struct lt8911exb),
	                         GFP_KERNEL);
	if (!lt8911exb) {
		LT8911EXB_DBG("%s: failed to allocate lt8911exb data\n",
			__func__);
		return -ENOMEM;
	}

	i2c_set_clientdata(client, lt8911exb);
	lt8911exb->client = client;
	lt8911exb->screen_on = true;
	lt8911exb->init_done = false;

	ret = lt8911exb_parse_dt(lt8911exb);
	if (ret) {
		LT8911EXB_DBG("%s: failed to parse device tree\n", __func__);
		return ret;
	}

	lt8911exb_reset(lt8911exb);

	/* read chip ID */
	lt8911exb_i2c_write(client, 0xff, 0x81); /* 0x81: register bank */
	lt8911exb_i2c_write(client, 0x08, 0x7f);
	LT8911EXB_DBG("%s: lt8911exb chip ID: 0x%02x-0x%02x-0x%02x\n",
		__func__, lt8911exb_i2c_read(client, 0x00),
		lt8911exb_i2c_read(client, 0x01),
		lt8911exb_i2c_read(client, 0x02));

	lt8911exb->nb.notifier_call = lt8911exb_fb_notifier_cb;
	ret = fb_register_client(&lt8911exb->nb);
	if (ret) {
		LT8911EXB_DBG("%s: failed to register lt8911exb fb notifier\n",
			__func__);
		return ret;
	}

	INIT_DELAYED_WORK(&lt8911exb->rst_check,
	                  lt8911exb_rst_check_work_cb);
	schedule_delayed_work(&lt8911exb->rst_check,
	                      msecs_to_jiffies(3200));

	LT8911EXB_DBG("%s: end\n", __func__);
	return 0;
}

static void lt8911exb_i2c_shutdown(struct i2c_client *client)
{
	struct lt8911exb *lt8911exb = i2c_get_clientdata(client);

	gpio_set_value(lt8911exb->reset_pin, 0);
}

static struct of_device_id lt8911exb_dt_ids[] = {
	{ .compatible = "lontium,lt8911exb" },
	{ }
};

static struct i2c_device_id lt8911exb_id[] = {
	{ "lt8911exb", 0 },
	{ }
};

struct i2c_driver lt8911exb_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "lt8911exb",
		.of_match_table = of_match_ptr(lt8911exb_dt_ids),
	},
	.id_table = lt8911exb_id,
	.probe = lt8911exb_i2c_probe,
	.shutdown = lt8911exb_i2c_shutdown,
};

int __init lt8911exb_init(void)
{
	return i2c_add_driver(&lt8911exb_driver);
}
void __exit lt8911exb_exit(void)
{
	i2c_del_driver(&lt8911exb_driver);
}
late_initcall(lt8911exb_init);
module_exit(lt8911exb_exit);

MODULE_AUTHOR("edward@techvision.com.cn");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Lontium LT8911EXB mipi2edp bridge chip driver");
