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

    Notes:
    1. IIC address of LT8911EXB:
     A) If LT8911EXBEXB's number 31 (S_ADR) is low, the I2C address of LT8911EXBEXB is 0x52,
	and bit0 is the read-write mark bit.
	If it is a Linux system, the I2C address of LT8911EXBEXB is 0x29, and bit7 is
	the read and write flag bit.
     B) If LT8911EXBEXB's number 31 (S_ADR) is high, then LT8911EXBEXB's I2C address is 0x5a,
	and bit0 is the read-write mark bit.
	If it is a Linux system, the I2C address of LT8911EXBEXB is 0x2d, and bit7 is
	the read and write flag bit.
    2. The IIC rate should not exceed 100KHz.
    3. To ensure that MIPI signal is given to LT8911EXBEXB, then initialize LT8911EXBEXB.
    4. The front-end master control GPIO is required to reply to LT8911EXBEXB. Before
      the register, the LT8911EXBEXB is reset.
      Use GPIO to lower LT8911EXBEXB's reset foot 100ms, then pull up and maintain 100ms.
    5. LT8911EXBEXB MIPI input signal requirements:
     A) MIPI DSI
     B) Video mode
     C) non-burst mode (continue mode) - (CLK of MIPI is continuous).
     D) sync event

    Author: shenhaibo
    Version: 1.1.0
    Release Date: 2019/3/6
*/

#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/input/mt.h>
#include <linux/slab.h>
#include "lontium-lt8911exb.h"

/*******************************************************
    Function:
    Write data to the i2c slave device.
    Input:
    client: i2c device.
    buf[0]: write start address.
    buf[1~len-1]: data buffer
    len: LT8911EXB_ADDR_LENGTH + write bytes count
    Output:
    numbers of i2c_msgs to transfer:
	0: succeed, otherwise: failed
 *********************************************************/
int lt8911exb_i2c_write(struct i2c_client *client, u8 * buf, int len)
{
	unsigned int pos = 0, transfer_length = 0;
	u8 address = buf[0];
	unsigned char put_buf[64];
	int retry, ret = 0;
	struct i2c_msg msg = {
		.addr = client->addr,
				.flags = !I2C_M_RD,
	};

	if (likely(len < sizeof(put_buf))) {
		/* code optimize,use stack memory */
		msg.buf = &put_buf[0];
	} else {
		msg.buf = kmalloc(len > I2C_MAX_TRANSFER_SIZE
				  ? I2C_MAX_TRANSFER_SIZE : len, GFP_KERNEL);

		if (!msg.buf)
			return -ENOMEM;
	}

	len -= LT8911EXB_ADDR_LENGTH;

	while (pos != len) {
		if (unlikely
				(len - pos > I2C_MAX_TRANSFER_SIZE - LT8911EXB_ADDR_LENGTH))
			transfer_length =
					I2C_MAX_TRANSFER_SIZE - LT8911EXB_ADDR_LENGTH;
		else
			transfer_length = len - pos;

		msg.buf[0] = address;
		msg.len = transfer_length + LT8911EXB_ADDR_LENGTH;
		memcpy(&msg.buf[LT8911EXB_ADDR_LENGTH],
		       &buf[LT8911EXB_ADDR_LENGTH + pos], transfer_length);

		for (retry = 0; retry < RETRY_MAX_TIMES; retry++) {
			if (likely(i2c_transfer(client->adapter, &msg, 1) == 1)) {
				pos += transfer_length;
				address += transfer_length;
				break;
			}

			dev_info(&client->dev, "I2C write retry[%d]\n",
				 retry + 1);
			udelay(2000);
		}

		if (unlikely(retry == RETRY_MAX_TIMES)) {
			dev_err(&client->dev,
				"I2c write failed,dev:%02x,reg:%02x,size:%u\n",
				client->addr, address, len);
			ret = -EAGAIN;
			goto write_exit;
		}
	}

write_exit:

	if (len + LT8911EXB_ADDR_LENGTH >= sizeof(put_buf))
		kfree(msg.buf);

	return ret;
}

/*******************************************************
    Function:
    Read data from the i2c slave device.
    Input:
    client: i2c device.
    buf[0]: read start address.
    buf[1~len-1]: data buffer
    len: LT8911EXB_ADDR_LENGTH + read bytes count
    Output:
    numbers of i2c_msgs to transfer:
	0: succeed, otherwise: failed
 *********************************************************/
int lt8911exb_i2c_read(struct i2c_client *client, u8 * buf, int len)
{
	unsigned int transfer_length = 0;
	unsigned int pos = 0;
	u8 address = buf[0];
	unsigned char get_buf[64], addr_buf[2];
	int retry, ret = 0;
	struct i2c_msg msgs[] = {
	{
		.addr = client->addr,
				.flags = !I2C_M_RD,
				.buf = &addr_buf[0],
				.len = LT8911EXB_ADDR_LENGTH,
	}, {
		.addr = client->addr,
				.flags = I2C_M_RD,
	}
};

	len -= LT8911EXB_ADDR_LENGTH;

	if (likely(len < sizeof(get_buf))) {
		/* code optimize, use stack memory */
		msgs[1].buf = &get_buf[0];
	} else {
		msgs[1].buf = kzalloc(len > I2C_MAX_TRANSFER_SIZE
				      ? I2C_MAX_TRANSFER_SIZE : len,
				      GFP_KERNEL);

		if (!msgs[1].buf)
			return -ENOMEM;
	}

	while (pos != len) {
		if (unlikely(len - pos > I2C_MAX_TRANSFER_SIZE))
			transfer_length = I2C_MAX_TRANSFER_SIZE;
		else
			transfer_length = len - pos;

		msgs[0].buf[0] = address;
		msgs[1].len = transfer_length;

		for (retry = 0; retry < RETRY_MAX_TIMES; retry++) {
			if (likely(i2c_transfer(client->adapter, msgs, 2) == 2)) {
				memcpy(&buf[LT8911EXB_ADDR_LENGTH + pos],
						msgs[1].buf, transfer_length);
				pos += transfer_length;
				address += transfer_length;
				break;
			}

			dev_info(&client->dev, "I2c read retry[%d]:0x%x\n",
				 retry + 1, address);
			udelay(2000);
		}

		if (unlikely(retry == RETRY_MAX_TIMES)) {
			dev_err(&client->dev,
				"I2c read failed,dev:%02x,reg:%02x,size:%u\n",
				client->addr, address, len);
			ret = -EAGAIN;
			goto read_exit;
		}
	}

read_exit:

	if (len >= sizeof(get_buf))
		kfree(msgs[1].buf);

	return ret;
}

int lt8911exb_write(struct i2c_client *client, u8 addr, u8 data)
{
	u8 buf[2] = { addr, data };
	int ret = -1;

	ret = lt8911exb_i2c_write(client, buf, 2);

	return ret;
}

u8 lt8911exb_read(struct i2c_client *client, u8 addr)
{
	u8 buf[2] = { addr };
	int ret = -1;

	ret = lt8911exb_i2c_read(client, buf, 2);

	if (ret == 0) {
		return buf[1];
	} else {
		return 0;
	}
}

void lt8911exb_reset_guitar(struct i2c_client *client)
{
	struct lt8911exb_data *lt8911exb = i2c_get_clientdata(client);

	dev_info(&client->dev, "Guitar reset");

	if (!gpio_is_valid(lt8911exb->rst_gpio)) {
		dev_warn(&client->dev, "reset failed no valid reset gpio");
		return;
	} else {
		gpio_direction_output(lt8911exb->rst_gpio, 0);
		usleep_range(100 * 1000, 150 * 1000);	//150ms
		gpio_direction_output(lt8911exb->rst_gpio, 1);
		usleep_range(100 * 1000, 150 * 1000);	//150ms
		gpio_direction_input(lt8911exb->rst_gpio);
	}
}

static int lt8911exb_parse_dt(struct device *dev, struct lt8911exb_data *pdata)
{
	int ret = 0;
	struct device_node *np = dev->of_node;

	pdata->pwr_gpio = of_get_named_gpio(np, "power-gpio", 0);

	if (!gpio_is_valid(pdata->pwr_gpio)) {
		dev_warn(dev, "No valid pwr gpio");
	}

	pdata->rst_gpio = of_get_named_gpio(np, "reset-gpio", 0);

	if (!gpio_is_valid(pdata->rst_gpio)) {
		dev_warn(dev, "No valid rst gpio");
	}

	ret = of_property_read_u32(np, "lontium,test", &pdata->test);
	if (ret) {
		dev_err(dev, "Parse test failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,mipi_lane", &pdata->mipi_lane);
	if (ret) {
		dev_err(dev, "Parse mipi_lane failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,lane_cnt", &pdata->lane_cnt);
	if (ret) {
		dev_err(dev, "Parse lane_cnt failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,color", &pdata->color);
	if (ret) {
		dev_err(dev, "Parse color failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,lane_cnt", &pdata->lane_cnt);
	if (ret) {
		dev_err(dev, "Parse lane_cnt failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,hact", &pdata->hact);
	if (ret) {
		dev_err(dev, "Parse hact failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,hact", &pdata->hact);
	if (ret) {
		dev_err(dev, "Parse hact failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,vact", &pdata->vact);
	if (ret) {
		dev_err(dev, "Parse vact failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,hbp", &pdata->hbp);
	if (ret) {
		dev_err(dev, "Parse hbp failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,hfp", &pdata->hfp);
	if (ret) {
		dev_err(dev, "Parse hfp failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,hs", &pdata->hs);
	if (ret) {
		dev_err(dev, "Parse hs failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,vbp", &pdata->vbp);
	if (ret) {
		dev_err(dev, "Parse vbp failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,vfp", &pdata->vfp);
	if (ret) {
		dev_err(dev, "Parse vfp failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,vs", &pdata->vs);
	if (ret) {
		dev_err(dev, "Parse vs failed");
		return -1;
	}

	ret = of_property_read_u32(np, "lontium,pclk", &pdata->pclk);
	if (ret) {
		dev_err(dev, "Parse pclk failed");
		return -1;
	} else {

		pdata->pclk = pdata->pclk / 10000;	// 10khz
	}

	pdata->htotal = pdata->hact + pdata->hbp + pdata->hfp + pdata->hs;
	pdata->vtotal = pdata->vact + pdata->vbp + pdata->vfp + pdata->vs;

	return 0;
}

static int lt8911exb_request_io_port(struct lt8911exb_data *lt8911exb)
{
	int ret = 0;

	if (gpio_is_valid(lt8911exb->pwr_gpio)) {
		ret = gpio_request(lt8911exb->pwr_gpio, "lt8911exb_pwr");

		if (ret < 0) {
			dev_warn(&lt8911exb->client->dev,
				 "Failed to request GPIO:%d, ERRNO:%d\n",
				 (s32) lt8911exb->pwr_gpio, ret);
		} else {
			gpio_direction_input(lt8911exb->pwr_gpio);
			dev_info(&lt8911exb->client->dev, "Success request pwr-gpio\n");
		}
	}

	if (gpio_is_valid(lt8911exb->rst_gpio)) {
		ret = gpio_request(lt8911exb->rst_gpio, "lt8911exb_rst");

		if (ret < 0) {
			dev_err(&lt8911exb->client->dev,
				"Failed to request GPIO:%d, ERRNO:%d\n",
				(s32) lt8911exb->rst_gpio, ret);

			if (gpio_is_valid(lt8911exb->pwr_gpio))
				gpio_free(lt8911exb->pwr_gpio);
		} else {
			gpio_direction_input(lt8911exb->rst_gpio);
			dev_info(&lt8911exb->client->dev, "Success request rst-gpio\n");
		}
	}

	return 0;
}

static int lt8911exb_i2c_test(struct i2c_client *client)
{
	u8 retry = 0;
	u8 chip_id_h = 0, chip_id_m = 0, chip_id_l = 0;
	int ret = -EAGAIN;

	while (retry++ < 3) {
		ret = lt8911exb_write(client, 0xff, 0x81);

		if (ret < 0) {
			dev_err(&client->dev,
				"LT8911EXB i2c test write addr:0xff failed\n");
			continue;
		}

		ret = lt8911exb_write(client, 0x08, 0x7f);

		if (ret < 0) {
			dev_err(&client->dev,
				"LT8911EXB i2c test write addr:0x08 failed\n");
			continue;
		}

		chip_id_l = lt8911exb_read(client, 0x00);
		chip_id_m = lt8911exb_read(client, 0x01);
		chip_id_h = lt8911exb_read(client, 0x02);
		// LT8911EXB i2c test success chipid: 0xe0517
		dev_info(&client->dev,
			 "LT8911EXB i2c test success chipid: 0x%x%x%x\n",
			 chip_id_h, chip_id_m, chip_id_l);

		//        if (chip_id_h == 0 && chip_id_l == 0) {
		//            dev_err(&client->dev, "LT8911EXB i2c test failed time %d\n", retry);
		//            continue;
		//        }

		ret = 0;
		break;
	}

	return ret;
}

void lt8911exb_dpcd_write(struct i2c_client *client, u32 address, u8 Data)
{
	u8 address_h = 0x0f & (address >> 16);
	u8 address_m = 0xff & (address >> 8);
	u8 address_l = 0xff & address;
	u8 ret = 0x00;

	lt8911exb_write(client, 0xff, 0xa6);
	lt8911exb_write(client, 0x2b, (0x80 | address_h));
	lt8911exb_write(client, 0x2b, address_m);	//addr[15:8]
	lt8911exb_write(client, 0x2b, address_l);	//addr[7:0]
	lt8911exb_write(client, 0x2b, 0x00);	//data lenth
	lt8911exb_write(client, 0x2b, Data);	//data
	lt8911exb_write(client, 0x2c, 0x00);	//start Aux read edid

	mdelay(20);
	ret = lt8911exb_read(client, 0x25);

	if ((ret & 0x0f) == 0x0c) {
		return;
	}
}

u8 lt8911exb_dpcd_read(struct i2c_client *client, u32 address)
{
	u8 read_cnt = 0x03;
	u8 dpcd_value = 0x00;
	u8 address_h = 0x0f & (address >> 16);
	u8 address_m = 0xff & (address >> 8);
	u8 address_l = 0xff & address;

	lt8911exb_write(client, 0xff, 0x80);
	lt8911exb_write(client, 0x62, 0xbd);
	lt8911exb_write(client, 0x62, 0xbf);	//ECO(AUX reset)

	lt8911exb_write(client, 0x36, 0x00);
	lt8911exb_write(client, 0x30, 0x8f);	//0x91
	lt8911exb_write(client, 0x33, address_l);
	lt8911exb_write(client, 0x34, address_m);
	lt8911exb_write(client, 0x35, address_h);
	lt8911exb_write(client, 0x36, 0x20);

	mdelay(2);		//The necessary

	if (lt8911exb_read(client, 0x39) == 0x01) {
		dpcd_value = lt8911exb_read(client, 0x38);
	} else {
		while ((lt8911exb_read(client, 0x39) != 0x01) && (read_cnt > 0)) {
			lt8911exb_write(client, 0x36, 0x00);
			lt8911exb_write(client, 0x36, 0x20);
			read_cnt--;
			mdelay(2);
		}

		if (lt8911exb_read(client, 0x39) == 0x01) {
			dpcd_value = lt8911exb_read(client, 0x38);
		}
	}

	return dpcd_value;
}

void lt8911exb_mipi_video_timing(struct i2c_client *client)
{
	struct lt8911exb_data *lt8911exb = i2c_get_clientdata(client);

	lt8911exb_write(client, 0xff, 0xd0);
	lt8911exb_write(client, 0x0d, (u8) (lt8911exb->vtotal / 256));
	lt8911exb_write(client, 0x0e, (u8) (lt8911exb->vtotal % 256));	//vtotal
	lt8911exb_write(client, 0x0f, (u8) (lt8911exb->vact / 256));
	lt8911exb_write(client, 0x10, (u8) (lt8911exb->vact % 256));	//vactive
	lt8911exb_write(client, 0x11, (u8) (lt8911exb->htotal / 256));
	lt8911exb_write(client, 0x12, (u8) (lt8911exb->htotal % 256));	//htotal
	lt8911exb_write(client, 0x13, (u8) (lt8911exb->hact / 256));
	lt8911exb_write(client, 0x14, (u8) (lt8911exb->hact % 256));	//hactive
	lt8911exb_write(client, 0x15, (u8) (lt8911exb->vs % 256));	//vsa
	lt8911exb_write(client, 0x16, (u8) (lt8911exb->hs % 256));	//hsa
	lt8911exb_write(client, 0x17, (u8) (lt8911exb->vfp / 256));
	lt8911exb_write(client, 0x18, (u8) (lt8911exb->vfp % 256));	//vfp
	lt8911exb_write(client, 0x19, (u8) (lt8911exb->hfp / 256));
	lt8911exb_write(client, 0x1a, (u8) (lt8911exb->hfp % 256));	//hfp
}

void lt8911exb_edp_video_cfg(struct i2c_client *client)
{
	struct lt8911exb_data *lt8911exb = i2c_get_clientdata(client);

	lt8911exb_write(client, 0xff, 0xa8);
	lt8911exb_write(client, 0x2d, 0x88);	// MSA from register
	lt8911exb_write(client, 0x05, (u8) (lt8911exb->htotal / 256));
	lt8911exb_write(client, 0x06, (u8) (lt8911exb->htotal % 256));
	lt8911exb_write(client, 0x07,
			(u8) ((lt8911exb->hs + lt8911exb->hbp) / 256));
	lt8911exb_write(client, 0x08,
			(u8) ((lt8911exb->hs + lt8911exb->hbp) % 256));
	lt8911exb_write(client, 0x09, (u8) (lt8911exb->hs / 256));
	lt8911exb_write(client, 0x0a, (u8) (lt8911exb->hs % 256));
	lt8911exb_write(client, 0x0b, (u8) (lt8911exb->hact / 256));
	lt8911exb_write(client, 0x0c, (u8) (lt8911exb->hact % 256));
	lt8911exb_write(client, 0x0d, (u8) (lt8911exb->vtotal / 256));
	lt8911exb_write(client, 0x0e, (u8) (lt8911exb->vtotal % 256));
	lt8911exb_write(client, 0x11,
			(u8) ((lt8911exb->vs + lt8911exb->vbp) / 256));
	lt8911exb_write(client, 0x12,
			(u8) ((lt8911exb->vs + lt8911exb->vbp) % 256));
	lt8911exb_write(client, 0x14, (u8) (lt8911exb->vs % 256));
	lt8911exb_write(client, 0x15, (u8) (lt8911exb->vact / 256));
	lt8911exb_write(client, 0x16, (u8) (lt8911exb->vact % 256));

	// lvdv de only mode to regerate h/v sync
	lt8911exb_write(client, 0xff, 0xd8);
	lt8911exb_write(client, 0x20, (u8) (lt8911exb->hfp / 256));
	lt8911exb_write(client, 0x21, (u8) (lt8911exb->hfp % 256));
	lt8911exb_write(client, 0x22, (u8) (lt8911exb->hs / 256));
	lt8911exb_write(client, 0x23, (u8) (lt8911exb->hs % 256));
	lt8911exb_write(client, 0x24, (u8) (lt8911exb->htotal / 256));
	lt8911exb_write(client, 0x25, (u8) (lt8911exb->htotal % 256));
	lt8911exb_write(client, 0x26, (u8) (lt8911exb->vfp % 256));
	lt8911exb_write(client, 0x27, (u8) (lt8911exb->vs % 256));

	// de-sscpll to generate pixel clock for pattern
	u8 dessc_m;
	dessc_m = (lt8911exb->pclk * 4) / (25 * 1000);
	lt8911exb_write(client, 0xff, 0x85);
	lt8911exb_write(client, 0xaa, dessc_m);
}

void lt8911exb_setup(struct i2c_client *client)
{
	u8 i;
	u8 pcr_pll_postdiv;
	u8 pcr_m;
	u16 temp16;
	struct lt8911exb_data *lt8911exb = i2c_get_clientdata(client);

	/* init */
	lt8911exb_write(client, 0xff, 0x81);
	lt8911exb_write(client, 0x08, 0x7f);    //i2c over aux issue
	lt8911exb_write(client, 0x49, 0xff);	//enable 0x87xx

	lt8911exb_write(client, 0xff, 0x82);	//GPIO test output
	lt8911exb_write(client, 0x5a, 0x0e);

	/* power consumption */
	lt8911exb_write(client, 0xff, 0x81);
	lt8911exb_write(client, 0x05, 0x06);
	lt8911exb_write(client, 0x43, 0x00);
	lt8911exb_write(client, 0x44, 0x1f);
	lt8911exb_write(client, 0x45, 0xf7);
	lt8911exb_write(client, 0x46, 0xf6);
	lt8911exb_write(client, 0x49, 0x7f);

	lt8911exb_write(client, 0xff, 0x82);
	lt8911exb_write(client, 0x12, 0x33);

	/* mipi Rx analog */
	lt8911exb_write(client, 0xff, 0x82);
	lt8911exb_write(client, 0x32, 0x51);
	lt8911exb_write(client, 0x35, 0x62);	//EQ current 0x42
	lt8911exb_write(client, 0x3a, 0x33); // 0x77:EQ 12.5db ,0x33:EQ 6.5db
	lt8911exb_write(client, 0x3b, 0x33); // 0x77:EQ 12.5db ,0x33:EQ 6.5db
	lt8911exb_write(client, 0x4c, 0x0c);
	lt8911exb_write(client, 0x4d, 0x00);

	/* dessc_pcr  pll analog */
	lt8911exb_write(client, 0xff, 0x82);
	lt8911exb_write(client, 0x6a, 0x43); // final setting 0x40
	lt8911exb_write(client, 0x6b, 0x40); // 0x44:pre-div = 2, 0x40: pre-div = 1

	temp16 = lt8911exb->pclk;

	if (lt8911exb->pclk < 8800) {
		lt8911exb_write(client, 0x6e, 0x82);	//0x44:pre-div = 2 ,pixel_clk=44~ 88MHz
		pcr_pll_postdiv = 0x08;
	} else {
		lt8911exb_write(client, 0x6e, 0x81);	//0x40:pre-div = 1, pixel_clk =88~176MHz
		pcr_pll_postdiv = 0x04;
	}

	pcr_m = (u8) (temp16 * pcr_pll_postdiv / 25 / 100);

	/* dessc pll digital */
	lt8911exb_write(client, 0xff, 0x85);
	lt8911exb_write(client, 0xa9, 0x31);
	lt8911exb_write(client, 0xaa, 0x17);
	lt8911exb_write(client, 0xab, 0xba);
	lt8911exb_write(client, 0xac, 0xe1);
	lt8911exb_write(client, 0xad, 0x47);
	lt8911exb_write(client, 0xae, 0x01);
	lt8911exb_write(client, 0xae, 0x11);

	/* Digital Top */
	lt8911exb_write(client, 0xff, 0x85);
	lt8911exb_write(client, 0xc0, 0x01);	//select mipi Rx
#ifdef _6bit_
	lt8911exb_write(client, 0xb0, 0xd0);	//enable dither
#else
	lt8911exb_write(client, 0xb0, 0x00);	// disable dither
#endif

	/* mipi Rx Digital */
	lt8911exb_write(client, 0xff, 0xd0);
	//lt8911exb_write(client, 0x00, lt8911exb->mipi_lane % 4);	// 0: 4 Lane / 1: 1 Lane / 2 : 2 Lane / 3: 3 Lane
	lt8911exb_write(client, 0x02, 0x08);	//settle
	lt8911exb_write(client, 0x08, 0x00);

	lt8911exb_write(client, 0x0c, 0x80); // fifo position
	lt8911exb_write(client, 0x1c, 0x80); // fifo position
	lt8911exb_write(client, 0x24, 0x70); // pcr mode (de hs vs)

	lt8911exb_write(client, 0x31, 0x0a); // M down limit

	// stage1 hs mode */
	lt8911exb_write(client, 0x25, 0x90); // line limit
	lt8911exb_write(client, 0x2a, 0x3a); // step in limit
	lt8911exb_write(client, 0x21, 0x4f); // hs_step
	lt8911exb_write(client, 0x22, 0xff);

	// stage2 de mode */
	lt8911exb_write(client, 0x0a, 0x02); // de adjust pre line
	lt8911exb_write(client, 0x38, 0x02); // de threshold 1
	lt8911exb_write(client, 0x39, 0x04); // de threshold 2
	lt8911exb_write(client, 0x3a, 0x08); // de threshold 3
	lt8911exb_write(client, 0x3b, 0x10); // de threshold 4

	lt8911exb_write(client, 0x3f, 0x02); // de_step 1
	lt8911exb_write(client, 0x40, 0x04); // de_step 2
	lt8911exb_write(client, 0x41, 0x08); // de_step 3
	lt8911exb_write(client, 0x42, 0x10); // de_step 4

	// stage 2 hs mode */
	lt8911exb_write(client, 0x1e, 0x01); // hs threshold
	lt8911exb_write(client, 0x23, 0xf0); // hs step

	lt8911exb_write(client, 0x2b, 0x80); // stable out // V1.8 20200417

	if (lt8911exb->test) {
		lt8911exb_write(client, 0x26, (pcr_m | 0x80));
	} else {
		lt8911exb_write(client, 0x26, pcr_m);
	}

	lt8911exb_mipi_video_timing(client);	//defualt setting is 1080P

	lt8911exb_write(client, 0xff, 0x81);	//PCR reset
	lt8911exb_write(client, 0x03, 0x7b);
	lt8911exb_write(client, 0x03, 0xff);

	/* Txpll 2.7G */
	lt8911exb_write(client, 0xff, 0x87);
	lt8911exb_write(client, 0x19, 0x31);
	lt8911exb_write(client, 0x1a, 0x1b);
	lt8911exb_write(client, 0xff, 0x82);
	lt8911exb_write(client, 0x02, 0x42);
	lt8911exb_write(client, 0x03, 0x00);
	lt8911exb_write(client, 0x03, 0x01);
	lt8911exb_write(client, 0x0a, 0x1b);
	lt8911exb_write(client, 0x04, 0x2a);
	lt8911exb_write(client, 0xff, 0x81);
	lt8911exb_write(client, 0x09, 0xfc);
	lt8911exb_write(client, 0x09, 0xfd);
	lt8911exb_write(client, 0xff, 0x87);
	lt8911exb_write(client, 0x0c, 0x11);

	for (i = 0; i < 5; i++) {	//Check Tx PLL
		mdelay(5);

		if (lt8911exb_read(client, 0x37) & 0x02) {
			dev_info(&client->dev, "LT8911 tx pll locked");
			lt8911exb_write(client, 0xff, 0x87);
			lt8911exb_write(client, 0x1a, 0x36);
			lt8911exb_write(client, 0xff, 0x82);
			lt8911exb_write(client, 0x0a, 0x36);
			lt8911exb_write(client, 0x04, 0x3a);
			break;
		} else {
			dev_info(&client->dev, "LT8911 tx pll unlocked");
			lt8911exb_write(client, 0xff, 0x81);
			lt8911exb_write(client, 0x09, 0xfc);
			lt8911exb_write(client, 0x09, 0xfd);
			lt8911exb_write(client, 0xff, 0x87);
			lt8911exb_write(client, 0x0c, 0x10);
			lt8911exb_write(client, 0x0c, 0x11);
		}
	}

	/* tx phy */
	lt8911exb_write(client, 0xff, 0x82);
	lt8911exb_write(client, 0x11, 0x00);
	lt8911exb_write(client, 0x13, 0x10);
	lt8911exb_write(client, 0x14, 0x0c);
	lt8911exb_write(client, 0x14, 0x08);
	lt8911exb_write(client, 0x13, 0x20);
	lt8911exb_write(client, 0xff, 0x82);
	lt8911exb_write(client, 0x0e, 0x25);
	//lt8911exb_write(client, 0x12, 0xff);
	lt8911exb_write(client, 0xff, 0x80);
	lt8911exb_write(client, 0x40, 0x22);

	/*eDP Tx Digital */
	lt8911exb_write(client, 0xff, 0xa8);
	if (lt8911exb->test) {
		lt8911exb_write(client, 0x24, 0x52);	// bit2 ~ bit 0 : test panttern image mode
		//lt8911exb_write(client, 0x25, 0x70);	// bit6 ~ bit 4 : test Pattern color
		lt8911exb_write(client, 0x27, 0x50);	//0x50:Pattern; 0x10:mipi video
	} else {
		lt8911exb_write(client, 0x27, 0x10);	//0x50:Pattern; 0x10:mipi video
	}

	if (lt8911exb->color) {
		//8bit
		//lt8911exb_write(client, 0x17, 0x10);
		//lt8911exb_write(client, 0x18, 0x20);
	} else {
		//6bit
		lt8911exb_write(client, 0x17, 0x00);
		lt8911exb_write(client, 0x18, 0x00);
	}

	lt8911exb_write(client, 0xff, 0xa0); // nvid = 0x080000;
	lt8911exb_write(client, 0x00, 0x08);
	lt8911exb_write(client, 0x01, 0x80);
}

/* mipi should be ready before configuring below video check setting*/
void lt8911exb_video_check(struct i2c_client *client)
{
	u32 ret = 0x00;

	/* mipi byte clk check */
	lt8911exb_write(client, 0xff, 0x85);
	lt8911exb_write(client, 0x1d, 0x00);	//FM select byte clk
	lt8911exb_write(client, 0x40, 0xf7);
	lt8911exb_write(client, 0x41, 0x30);
	lt8911exb_write(client, 0xa1, 0x02);	//video chech from mipi

	//  lt8911exb_write(client, 0x17, 0xf0 ); //0xf0:Close scramble; 0xD0 : Open scramble

	lt8911exb_write(client, 0xff, 0x81);	//video check rst
	lt8911exb_write(client, 0x09, 0x7d);
	lt8911exb_write(client, 0x09, 0xfd);

	lt8911exb_write(client, 0xff, 0x85);
	mdelay(100);

	if (lt8911exb_read(client, 0x50) == 0x03) {
		ret = lt8911exb_read(client, 0x4d);
		ret = ret * 256 + lt8911exb_read(client, 0x4e);
		ret = ret * 256 + lt8911exb_read(client, 0x4f);

		dev_info(&client->dev, "video check: mipi clk = %d", ret);	//mipi clk = ret * 1000
	} else {
		dev_info(&client->dev, "video check: mipi clk unstable");
	}

	/* mipi vtotal check */
	ret = lt8911exb_read(client, 0x76);
	ret = ret * 256 + lt8911exb_read(client, 0x77);

	dev_info(&client->dev, "video check: Vtotal = %d", ret);

	/* mipi word count check */
	lt8911exb_write(client, 0xff, 0xd0);
	ret = lt8911exb_read(client, 0x82);
	ret = ret * 256 + lt8911exb_read(client, 0x83);
	ret = ret / 3;

	dev_info(&client->dev, "video check: Hact(word counter) = %d", ret);

	/* mipi Vact check */
	ret = lt8911exb_read(client, 0x85);
	ret = ret * 256 + lt8911exb_read(client, 0x86);

	dev_info(&client->dev, "video check: Vact = %d", ret);
	dev_info(&client->dev, "lane0 settle: 0x%02x", lt8911exb_read(client, 0x88));
	dev_info(&client->dev, "lane1 settle: 0x%02x", lt8911exb_read(client, 0x8a));
	dev_info(&client->dev, "lane2 settle: 0x%02x", lt8911exb_read(client, 0x8c));
	dev_info(&client->dev, "lane3 settle: 0x%02x", lt8911exb_read(client, 0x8e));

	dev_info(&client->dev, "lane0 sot: 0x%02x", lt8911exb_read(client, 0x89));
	dev_info(&client->dev, "lane1 sot: 0x%02x", lt8911exb_read(client, 0x8b));
	dev_info(&client->dev, "lane2 sot: 0x%02x", lt8911exb_read(client, 0x8d));
	dev_info(&client->dev, "lane3 sot: 0x%02x", lt8911exb_read(client, 0x8f));
}

#define SCRAMBLE_MODE 0x00 // 0x80:edp, 0x00:dp
#define MSA_SW_MODE 0x80 // MSA from register
#define MSA_HW_MODE 0x00 // MSA from video check
#define EDP_IDLE_PTN_ON 0x04
#define EDP_IDLE_PTN_OFF 0x00
#define LANE_CNT 2

void lt8911exb_link_train(struct i2c_client *client)
{
	// struct lt8911exb_data *lt8911exb = i2c_get_clientdata(client);

	if (SCRAMBLE_MODE == 0x80) {
		lt8911exb_dpcd_write(client, 0x10a, 0x01);
	}

	lt8911exb_write(client, 0xff, 0xa6);
	lt8911exb_write(client, 0x2a, 0x00);

	lt8911exb_write(client, 0xff, 0x81);
	lt8911exb_write(client, 0x07, 0xfe);
	lt8911exb_write(client, 0x07, 0xff);
	lt8911exb_write(client, 0x0a, 0xfc);
	lt8911exb_write(client, 0x0a, 0xfe);

	// link train
	lt8911exb_write(client, 0xff, 0xa8);
	lt8911exb_write(client, 0x2d, MSA_SW_MODE | EDP_IDLE_PTN_OFF); // edp output video

	lt8911exb_write(client, 0xff, 0x85);
	lt8911exb_write(client, 0x17, 0xc0);
	lt8911exb_write(client, 0x1a, LANE_CNT);
	lt8911exb_write(client, 0xa1, (SCRAMBLE_MODE | 0x03)); // scramble mode

	lt8911exb_write(client, 0xff, 0xac);
	lt8911exb_write(client, 0x00, 0x60);
	lt8911exb_write(client, 0x01, 0x0a);
	lt8911exb_write(client, 0x0c, 0x05);
	lt8911exb_write(client, 0x0c, 0x45);
	dev_info(&client->dev, "link train: hardware linktrain start");
	mdelay(500);
}

void lt8911exb_main(struct i2c_client *client)
{
	u16 reg;
	u16 vtotal;

	struct lt8911exb_data *lt8911exb = i2c_get_clientdata(client);
	vtotal = lt8911exb->vtotal;
	lt8911exb_write(client, 0xff, 0x85);
	lt8911exb_write(client, 0xa1, 0x02); // video chech from mipi
	lt8911exb_write(client, 0xff, 0x81); // video check rst
	lt8911exb_write(client, 0x09, 0x7d);
	lt8911exb_write(client, 0x09, 0xfd);
	mdelay(100);

	lt8911exb_write(client, 0xff, 0x85);
	reg = lt8911exb_read(client, 0x76);
	reg = reg * 256 + lt8911exb_read(client, 0x77);

	if ((reg <= (vtotal + 3)) && (reg >= (vtotal - 3))) {
		mdelay(1000);
		lt8911exb_write(client, 0xff, 0x81); // pcr reset
		lt8911exb_write(client, 0x03, 0x7b);
		lt8911exb_write(client, 0x03, 0xff);
		mdelay(100);
		lt8911exb_write(client, 0xff, 0xa8);
		lt8911exb_write(client, 0x2d, 0x88); // edp disable idle pattern;
		dev_info(&client->dev, "PCR reset0");
		lt8911exb_write(client, 0xff, 0xd0);
		if ((lt8911exb_read(client, 0x84) & 0x40) == 0x00) {
			lt8911exb_write(client, 0xff, 0x81); // pcr reset
			lt8911exb_write(client, 0x03, 0x7b);
			lt8911exb_write(client, 0x03, 0xff);
			mdelay(500);
			dev_info(&client->dev, "PCR reset1");
		}
		lt8911exb_write(client, 0xff, 0xd0);
	}
}

void lt8911exb_config(struct i2c_client *client)
{
	lt8911exb_edp_video_cfg(client);
	lt8911exb_setup(client);
	lt8911exb_link_train(client);
	lt8911exb_video_check(client);	//just for Check MIPI Input
	lt8911exb_main(client);
}

static int lt8911exb_probe(struct i2c_client *client)
{
	int ret = -1;
	struct lt8911exb_data *lt8911exb;

	/* do NOT remove these logs */
	dev_info(&client->dev, "LT8911EXB Driver Version: %s\n",
		 LT8911EXB_DRIVER_VERSION);
	dev_info(&client->dev, "LT8911EXB I2C Address: 0x%02x\n", client->addr);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "Failed check I2C functionality");
		return -ENODEV;
	}

	lt8911exb = devm_kzalloc(&client->dev, sizeof(*lt8911exb), GFP_KERNEL);

	if (lt8911exb == NULL) {
		dev_err(&client->dev, "Failed alloc lt8911exb memory");
		return -ENOMEM;
	}

	if (client->dev.of_node) {
		ret = lt8911exb_parse_dt(&client->dev, lt8911exb);

		if (ret < 0) {
			dev_err(&client->dev, "Failed parse dlt8911exb\n");
			goto exit_free_client_data;
		}
	}

	lt8911exb->client = client;
	i2c_set_clientdata(client, lt8911exb);

	ret = lt8911exb_request_io_port(lt8911exb);

	if (ret < 0) {
		dev_err(&client->dev, "Failed request IO port\n");
		goto exit_free_client_data;
	}
	//    lt8911exb_reset_guitar(client);

	ret = lt8911exb_i2c_test(client);

	if (ret < 0) {
		dev_err(&client->dev, "Failed communicate with IC use I2C\n");
		goto exit_free_io_port;
	}

	lt8911exb_config(client);
	dev_info(&client->dev, "LT8911EXB setup finish.\n");

	return 0;

exit_free_io_port:

	if (gpio_is_valid(lt8911exb->rst_gpio))
		gpio_free(lt8911exb->rst_gpio);

	if (gpio_is_valid(lt8911exb->pwr_gpio))
		gpio_free(lt8911exb->pwr_gpio);

exit_free_client_data:
	devm_kfree(&client->dev, lt8911exb);
	i2c_set_clientdata(client, NULL);

	return ret;
}

static void lt8911exb_remove(struct i2c_client *client)
{
	struct lt8911exb_data *lt8911exb = i2c_get_clientdata(client);

	if (gpio_is_valid(lt8911exb->rst_gpio))
		gpio_free(lt8911exb->rst_gpio);

	if (gpio_is_valid(lt8911exb->pwr_gpio))
		gpio_free(lt8911exb->pwr_gpio);

	dev_info(&client->dev, "lt8911exb driver removed");
	i2c_set_clientdata(client, NULL);

	devm_kfree(&client->dev, lt8911exb);
}

static const struct of_device_id lt8911exb_match_table[] = {
{.compatible = "lontium,lt8911exb",},
{},
};

static const struct i2c_device_id lt8911exb_device_id[] = {
{LT8911EXB_I2C_NAME, 0},
{}
};

static struct i2c_driver lt8911exb_driver = {
	.probe = lt8911exb_probe,
	.remove = lt8911exb_remove,
	.id_table = lt8911exb_device_id,
	.driver = {
		.name = LT8911EXB_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = lt8911exb_match_table,
	},
};

static int __init lt8911exb_init(void)
{
	s32 ret;

	pr_info("Lontium LT8911EXB driver installing....\n");
	ret = i2c_add_driver(&lt8911exb_driver);

	return ret;
}

static void __exit lt8911exb_exit(void)
{
	pr_info("Lontium LT8911EXB driver exited\n");
	i2c_del_driver(&lt8911exb_driver);
}

fs_initcall(lt8911exb_init);
module_exit(lt8911exb_exit);

MODULE_DESCRIPTION("Lontium LT8911EXB Driver");
MODULE_LICENSE("GPL v2");
