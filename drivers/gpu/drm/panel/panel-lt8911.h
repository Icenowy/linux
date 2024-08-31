#ifndef __MIPI_DSI_H__
#define __MIPI_DSI_H__


#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pm.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>

#include <video/mipi_display.h>


#if 0
#define DBG_FUNC(format, x...)		printk(KERN_ERR "[DSI]%s:" format"\n", __func__, ##x)
#define DBG_PRINT(format, x...)		printk(KERN_ERR "[DSI]" format"\n", ##x)
#else
#define DBG_FUNC(format, x...)
#define DBG_PRINT(format, x...)
#endif

#define DSI_DRIVER_NAME		        "i2c_mipi_dsi"

#define DSI_DCS_WRITE(dsi, seq...)		\
	{									\
		int ret = 0;					\
		const u8 d[] = { seq };			\
		ret = mipi_dsi_dcs_write_buffer(dsi, d, ARRAY_SIZE(d));	\
		if (ret < 0)		\
			return ret;		\
	}

struct panel_data {
	struct drm_display_mode *display_mode;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
};

struct i2c_mipi_dsi {
	struct i2c_client *client;
	struct mutex mutex;

	struct regulator	*hsvcc;
	struct regulator	*vspn3v3;

	struct notifier_block pm_notify;

	// panel
	struct drm_panel panel;
	struct panel_data *desc;

	// dsi
	struct mipi_dsi_device *dsi;

	int backlight_pin;
	int irq_pin;
	int reset_pin;
	int enable_pin;
	int rst_delay_ms;

	u32 edp_lane_cnt;
	u32 mipi_lane_cnt;
	u32 edp_depth;

	// backlight
	int brightness;
};

#define panel_to_md(_p)	container_of(_p, struct i2c_mipi_dsi, panel)

#endif /*End of header guard macro */
