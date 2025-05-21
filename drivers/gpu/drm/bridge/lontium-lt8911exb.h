/*
    Lontium LT8911EXB MIPI to EDP driver

    Copyright  (C)  2016 - 2017 Topband. Ltd.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be a reference
    to you, when you are integrating the Lontium's LT8911EXB IC into your system,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    Author: shenhaibo
    Version: 1.1.0
    Release Date: 2019/3/6
*/

#ifndef _LONTIUM_LT8911EXB_H_
#define _LONTIUM_LT8911EXB_H_

#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/major.h>
#include <linux/kdev_t.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/usb.h>
#include <linux/power_supply.h>

#define LT8911EXB_I2C_NAME      "lt8911exb"
#define LT8911EXB_DRIVER_VERSION  "1.0.0"

#define LT8911EXB_ADDR_LENGTH      1
#define I2C_MAX_TRANSFER_SIZE   255
#define RETRY_MAX_TIMES         3

struct lt8911exb_data {
	struct i2c_client *client;
	int pwr_gpio;
	int rst_gpio;

	int hact;
	int vact;
	int hbp;
	int hfp;
	int hs;
	int vbp;
	int vfp;
	int vs;
	int pclk;
	int htotal;
	int vtotal;

	int lane_cnt;
	int mipi_lane;
	int color;		//Color Depth 0:6bit 1:8bit
	int test;
};

#endif /*_LONTIUM_LT8911EXB_H_*/
