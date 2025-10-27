// SPDX-License-Identifier: GPL-2.0
/*
 * gc8613 driver
 *
 * Copyright (C) 2024 Sophon Co., Ltd.
 *
 */
#include <linux/clk.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>

#include <linux/comm_cif.h>
#include <linux/sns_v4l2_uapi.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#include "gc8613.h"

/* I2C per write of bits */
#define REG_VALUE_08BIT		1
#define REG_VALUE_16BIT		2
#define REG_VALUE_24BIT		3

/* Chip ID */
#define GC8613_CHIP_ID_ADDR_H	0x03f0
#define GC8613_CHIP_ID_ADDR_L	0x03f1
#define GC8613_CHIP_ID			0x8613

/*Sensor type for isp middleware*/
#define GC8613_SNS_TYPE_SDR V4L2_GCORE_GC8613_MIPI_8M_60FPS_10BIT
#define GC8613_SNS_TYPE_WDR V4L2_SNS_TYPE_BUTT

static const enum mipi_wdr_mode_e gc8613_wdr_mode = MIPI_WDR_MODE_NONE;

int gc8613_count;
static int force_bus[MAX_SENSOR_DEVICE] = {[0 ... (MAX_SENSOR_DEVICE - 1)] = -1};
module_param_array(force_bus, int, &gc8613_count, 0644);

static int gc8613_probe_index;
static const unsigned short gc8613_i2c_list[] = {0x31};
static int gc8613_bus_map[MAX_SENSOR_DEVICE] = {1, 2, -1, -1, -1, -1};

struct gc8613_reg_list {
	u32 num_of_regs;
	const struct gc8613_reg *regs;
};

/* Mode : resolution and related config&values */
struct gc8613_mode {
	u32 max_width;
	u32 max_height;
	u32 width;
	u32 height;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 mipi_wdr_mode;
	u32 sns_type;
	char *sns_type_name;
	struct v4l2_fract max_fps;
	sns_sync_info_t gc8613_sync_info;
	struct gc8613_reg_list reg_list;
};

/* Mode configs */
static struct gc8613_mode supported_modes[] = {
	{
		.max_width = 3840,
		.max_height = 2160,
		.width = 3840,
		.height = 2160,
		.exp_def = 1,
		.hts_def = 0x0278,
		.vts_def = 0x1224,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2160p60_10bit_regs),
			.regs = mode_2160p60_10bit_regs,
		},
	},
};

struct gc8613 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct i2c_client *client;
	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;
	/* Current mode */
	struct gc8613_mode *cur_mode;
	/* Mutex for serialized access */
	struct mutex mutex;
	/* Streaming on/off */
	bool streaming;
	/*dtsi config*/
	struct clk       *xvclk;
	struct gpio_desc *power_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	struct pinctrl   *pinctrl;

	unsigned int lane_num;
	unsigned int module_index;
};

#define to_gc8613(_sd)	container_of(_sd, struct gc8613, sd)

/* Read registers up to 4 at a time */
static int gc8613_read_reg(struct gc8613 *gc8613, u16 reg, u32 len,
			    u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&gc8613->sd);
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	int ret;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);

	if (len > 4)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

/* Write registers up to 4 at a time */
static int gc8613_write_reg(struct gc8613 *gc8613, u16 reg, u32 len,
			     u32 __val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&gc8613->sd);
	int buf_i, val_i;
	u8 buf[6], *val_p;
	__be32 val;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val = cpu_to_be32(__val);
	val_p = (u8 *)&val;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

/* Write a list of registers */
static int gc8613_write_regs(struct gc8613 *gc8613,
			      const struct gc8613_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&gc8613->sd);
	int ret;
	u32 i;

	for (i = 0; i < len; i++) {
		ret = gc8613_write_reg(gc8613, regs[i].address, 1,
					regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev, "Failed to write reg 0x%4.4x. error=%d\n",
					    regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

/* Open sub-device */
static int gc8613_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc8613 *gc8613 = to_gc8613(sd);
	struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_get_try_format(sd, fh->pad, 0);

	mutex_lock(&gc8613->mutex);

	/* Initialize try_fmt */
	try_fmt->width = gc8613->cur_mode->width;
	try_fmt->height = gc8613->cur_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	/* No crop or compose */
	mutex_unlock(&gc8613->mutex);

	return 0;
}

static int gc8613_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc8613 *gc8613 = container_of(ctrl->handler,
					       struct gc8613, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&gc8613->sd);

	pm_runtime_put(&client->dev);

	return 0;
}

static const struct v4l2_ctrl_ops gc8613_ctrl_ops = {
	.s_ctrl = gc8613_set_ctrl,
};

static int g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
			 struct v4l2_mbus_config *config)
{
	config->type = V4L2_MBUS_CSI2_DPHY;
	return 0;
}

static int enum_mbus_code(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_mbus_code_enum *code)
{
	/* Only one bayer order(GRBG) is supported */
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int enum_frame_interval(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_frame_interval_enum *fie)
{
	struct gc8613 *gc8613 = to_gc8613(sd);

	fie->width  = gc8613->cur_mode->width;
	fie->height = gc8613->cur_mode->height;

	fie->interval.numerator   = gc8613->cur_mode->max_fps.numerator;
	fie->interval.denominator = gc8613->cur_mode->max_fps.denominator;

	return 0;
}

static int enum_frame_size(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc8613 *gc8613 = to_gc8613(sd);

	fse->min_width = gc8613->cur_mode->width;
	fse->max_width = gc8613->cur_mode->max_width;
	fse->min_height = gc8613->cur_mode->height;
	fse->max_height = gc8613->cur_mode->max_height;

	return 0;
}

static void update_pad_format(const struct gc8613_mode *mode, struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = MEDIA_BUS_FMT_SGRBG10_1X10;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int get_pad_format(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc8613 *gc8613 = to_gc8613(sd);
	struct v4l2_mbus_framefmt *framefmt;
	int ret = 0;

	mutex_lock(&gc8613->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		fmt->format = *framefmt;
		ret = -ENOTTY;
	} else {
		update_pad_format(gc8613->cur_mode, fmt);
	}
	mutex_unlock(&gc8613->mutex);

	return ret;
}

static int set_pad_format(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc8613 *gc8613 = to_gc8613(sd);
	struct gc8613_mode *mode;
	struct v4l2_mbus_framefmt *framefmt;

	mutex_lock(&gc8613->mutex);

	/* Only one raw bayer(GRBG) order is supported */
	if (fmt->format.code != MEDIA_BUS_FMT_SGRBG10_1X10)
		fmt->format.code = MEDIA_BUS_FMT_SGRBG10_1X10;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);
	update_pad_format(mode, fmt);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*framefmt = fmt->format;
	} else {
		gc8613->cur_mode = mode;
	}

	mutex_unlock(&gc8613->mutex);

	return 0;
}

static void gc8613_standby(struct gc8613 *gc8613)
{
	gc8613_write_reg(gc8613, 0x0100, REG_VALUE_08BIT, 0x00);
	gc8613_write_reg(gc8613, 0x03d1, REG_VALUE_08BIT, 0x3d);
	gc8613_write_reg(gc8613, 0x061c, REG_VALUE_08BIT, 0x10);
	gc8613_write_reg(gc8613, 0x0a34, REG_VALUE_08BIT, 0x00);
	gc8613_write_reg(gc8613, 0x031c, REG_VALUE_08BIT, 0x8d);
	gc8613_write_reg(gc8613, 0x0a38, REG_VALUE_08BIT, 0x00);
	gc8613_write_reg(gc8613, 0x03d1, REG_VALUE_08BIT, 0x38);
}

static void gc8613_restart(struct gc8613 *gc8613)
{
	gc8613_write_reg(gc8613, 0x0a38, REG_VALUE_08BIT, 0x01);
	gc8613_write_reg(gc8613, 0x061c, REG_VALUE_08BIT, 0x50);
	gc8613_write_reg(gc8613, 0x0a34, REG_VALUE_08BIT, 0x40);
	gc8613_write_reg(gc8613, 0x031c, REG_VALUE_08BIT, 0xce);
	gc8613_write_reg(gc8613, 0x0100, REG_VALUE_08BIT, 0x09);
	usleep_range(20 * 1000, 20 * 1000);
}

/* Start streaming */
static int start_streaming(struct gc8613 *gc8613)
{
	struct i2c_client *client = v4l2_get_subdevdata(&gc8613->sd);
	const struct gc8613_reg_list *reg_list;
	const sns_sync_info_t *sync_info;
	int ret;

	reg_list = &gc8613->cur_mode->reg_list;

	ret = gc8613_write_regs(gc8613, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	sync_info = &gc8613->cur_mode->gc8613_sync_info;

	if (sync_info->num_of_regs > 0) {
		ret = gc8613_write_regs(gc8613, (struct gc8613_reg *)sync_info->regs,
					sync_info->num_of_regs);
		if (ret) {
			dev_err(&client->dev, "%s failed to set default\n", __func__);
			return ret;
		}
	}


	usleep_range(100 * 1000, 200 * 1000);
	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(gc8613->sd.ctrl_handler);
	if (ret)
		return ret;

	dev_info(&client->dev, "wdr_mode(%d) reg setting done\n", gc8613->cur_mode->mipi_wdr_mode);

	return ret;
}

/* Stop streaming */
static int stop_streaming(struct gc8613 *gc8613)
{
	gc8613_standby(gc8613);
	return 0;
}

static int set_stream(struct v4l2_subdev *sd, int enable)
{
	struct gc8613 *gc8613 = to_gc8613(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&gc8613->mutex);
	if (gc8613->streaming == enable) {
		mutex_unlock(&gc8613->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		//reset sensor
		if (!IS_ERR(gc8613->reset_gpio)) {
			gpiod_set_value_cansleep(gc8613->reset_gpio, 0);
			msleep(100);
			gpiod_set_value_cansleep(gc8613->reset_gpio, 1);
			msleep(100);
		}

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = start_streaming(gc8613);
		if (ret)
			goto err_rpm_put;
	} else {
		stop_streaming(gc8613);
		pm_runtime_put(&client->dev);
	}

	gc8613->streaming = enable;
	mutex_unlock(&gc8613->mutex);

	dev_info(&client->dev, "In sensor, set stream(%d) success\n", enable);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&gc8613->mutex);

	return ret;
}

static int __maybe_unused suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc8613 *gc8613 = to_gc8613(sd);

	if (gc8613->streaming)
		stop_streaming(gc8613);

	return 0;
}

static int __maybe_unused resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc8613 *gc8613 = to_gc8613(sd);
	int ret;

	if (gc8613->streaming) {
		ret = start_streaming(gc8613);
		if (ret)
			goto error;
	}

	return 0;

error:
	stop_streaming(gc8613);
	gc8613->streaming = false;
	return ret;
}

/* Verify chip ID */
static int gc8613_identify_module(struct gc8613 *gc8613)
{
	struct i2c_client *client = v4l2_get_subdevdata(&gc8613->sd);
	int ret;
	int val1, val2;
	int read_data = 0;

	ret = gc8613_read_reg(gc8613, GC8613_CHIP_ID_ADDR_H,
			       REG_VALUE_08BIT, &val1);
	dev_info(&client->dev, "read id:0x%x, ret:%d", val1, ret);
	if (ret)
		return ret;

	ret = gc8613_read_reg(gc8613, GC8613_CHIP_ID_ADDR_L,
			       REG_VALUE_08BIT, &val2);

	read_data = ((val1 & 0xFF) << 8) | (val2 & 0xFF);

	if (read_data != GC8613_CHIP_ID) {
		dev_err(&client->dev, "chip id(%x) mismatch, read(%x)\n",
			GC8613_CHIP_ID, read_data);
		return -EIO;
	}

	return 0;
}

static void gc8613_mirror_flip(struct gc8613 *gc8613, int orient)
{
	int u8Mirror = 0, u8Flip = 0;
	int mirror_addr = 0x0063;
	int flip_addr   = 0x022c;

	pr_info("set mirror_flip:%d", orient);

	switch (orient) {
	case 0:
		u8Mirror = 0;
		u8Flip = 0;
		break;
	case 1:
		u8Mirror = 0x05;
		u8Flip = 0;
		break;
	case 2:
		u8Mirror = 0x02;
		u8Flip = 0x01;
		break;
	case 3:
		u8Mirror = 0x05;
		u8Flip = 0x01;
		break;
	default:
		return;
	}

	gc8613_write_reg(gc8613, mirror_addr, REG_VALUE_08BIT, u8Mirror);
	gc8613_write_reg(gc8613, flip_addr, REG_VALUE_08BIT, u8Flip);
}

static int gc8613_update_link_menu(struct gc8613 *gc8613)
{
	struct i2c_client *client = v4l2_get_subdevdata(&gc8613->sd);
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_ctrl *ctrl;
	int wdr_index = SNS_CFG_TYPE_WDR_MODE;
	int id = gc8613->module_index;
	int ret;
	int i;

	gc8613_link_cif_menu[id][wdr_index] = gc8613->cur_mode->mipi_wdr_mode;

	dev_info(&client->dev, "update mipi_mode:%lld", gc8613_link_cif_menu[id][wdr_index]);

	ctrl_hdlr = gc8613->sd.ctrl_handler;
	v4l2_ctrl_handler_free(ctrl_hdlr);

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret) {
		dev_err(&client->dev, "%s ctrl handler init failed (%d)\n",
			__func__, ret);
		return ret;
	}

	for (i = 0; i < SNS_CFG_TYPE_MAX; i++) {
		ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr, &gc8613_ctrl_ops, V4L2_CID_LINK_FREQ,
					      gc8613_link_cif_menu[id][i], 0,
					       (const s64 *)gc8613_link_cif_menu[id]);

		if (ctrl)
			ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	}

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s new int menu failed (%d)\n",
			__func__, ret);
		goto error;
	}

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static int gc8613_get_info_from_dts(struct gc8613 *gc8613, int index_id)
{
	struct i2c_client *client = v4l2_get_subdevdata(&gc8613->sd);
	struct device_node *np = client->dev.of_node;
	u32 i, ret, len, num_lanes, num_lanes_swap;
	u32 lane[LANE_MAX_NUM] = {0}, lane_swap[LANE_MAX_NUM] = {0};
	u32 mipi_dev, mclk_num, wdr_mode, hs_settle, cif_mode;
	u32	dphy_enable;
	const char *type_name;
	struct property *prop;

	prop = of_find_property(np, "lanes", &len);
	if (!prop) {
		dev_err(&client->dev, "not set lanes, using default\n");
		return -1;
	}

	num_lanes = len / sizeof(u32);

	ret = of_property_read_u32_array(np, "lanes",
				lane, num_lanes);
	if (ret) {
		dev_err(&client->dev, "failed to lanes\n");
		return -1;
	}

	prop = of_find_property(np, "lanes-swap", &len);
	if (!prop) {
		dev_err(&client->dev, "not set lanes-swap, using default\n");
		return -1;
	}

	num_lanes_swap = len / sizeof(u32);

	ret = of_property_read_u32_array(np, "lanes-swap",
				lane_swap, num_lanes_swap);
	if (ret) {
		dev_err(&client->dev, "failed to lanes-swap, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "mipi-dev", &mipi_dev);
	if (ret) {
		dev_err(&client->dev, "failed to mipi-dev, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "mclk-num", &mclk_num);
	if (ret) {
		dev_err(&client->dev, "failed to mclk-num, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "wdr-mode", &wdr_mode);
	if (ret) {
		dev_err(&client->dev, "failed to wdr-mode, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "hs-settle", &hs_settle);
	if (ret) {
		dev_err(&client->dev, "failed to hs-settle, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "dphy-enable", &dphy_enable);
	if (ret) {
		dev_err(&client->dev, "failed to dphy-enable, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "cif-mode", &cif_mode);
	if (ret) {
		dev_err(&client->dev, "failed to cif-mode, using default\n");
		return -1;
	}

	ret = of_property_read_string(np, "sns-type", &type_name);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read sns-type property\n");
		return -1;
	}

	for (i = 0; i < LANE_MAX_NUM; i++) {
		gc8613_link_cif_menu[index_id][SNS_CFG_TYPE_DATA_LANE0 + i] =
			i >= num_lanes ? -1 : lane[i];
		gc8613_link_cif_menu[index_id][SNS_CFG_TYPE_PN_SWAP0 + i] =
			i >= num_lanes_swap ? 0 : lane_swap[i];
	}
	gc8613_link_cif_menu[index_id][SNS_CFG_TYPE_MIPI_DEV] = mipi_dev;
	gc8613_link_cif_menu[index_id][SNS_CFG_TYPE_MCLK_NUM] = mclk_num;
	gc8613_link_cif_menu[index_id][SNS_CFG_TYPE_WDR_MODE] = wdr_mode;
	gc8613_link_cif_menu[index_id][SNS_CFG_TYPE_DPHY_SETTLE] = hs_settle;
	gc8613_link_cif_menu[index_id][SNS_CFG_TYPE_DPHY_EN] = dphy_enable;
	gc8613_link_cif_menu[index_id][SNS_CFG_TYPE_PHY_MODE] = cif_mode;
	//type_mode
	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (!strcmp(type_name, supported_modes[i].sns_type_name)) {
			gc8613->cur_mode = devm_kzalloc(&client->dev,
						 sizeof(struct gc8613_mode), GFP_KERNEL);
			memcpy(gc8613->cur_mode, &supported_modes[i], sizeof(struct gc8613_mode));
		}
	}

	gc8613->power_gpio = devm_gpiod_get(&client->dev,
			"power", GPIOD_OUT_LOW);
	if (IS_ERR(gc8613->power_gpio))
		dev_err(&client->dev, "failed to get power-gpios\n");
	else
		gpiod_set_value_cansleep(gc8613->power_gpio, 1);

	gc8613->reset_gpio = devm_gpiod_get(&client->dev,
			"reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gc8613->reset_gpio))
		dev_err(&client->dev, "failed to get reset_gpio\n");

	return 0;
}

static long gc8613_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc8613 *gc8613 = to_gc8613(sd);
	long ret = 0;

	switch (cmd) {
	case SNS_V4L2_GET_TYPE:
	{
		int type = 0;

		if (gc8613->cur_mode->mipi_wdr_mode == MIPI_WDR_MODE_NONE) {//linear
			type = GC8613_SNS_TYPE_SDR;
		} else {//wdr
			type = GC8613_SNS_TYPE_WDR;
		}
		memcpy(arg, &type, sizeof(int));
		break;
	}

	case SNS_V4L2_SET_MIRROR_FLIP:
	{
		int orient = 0;

		memcpy(&orient, arg, sizeof(int));
		gc8613_mirror_flip(gc8613, orient);
		break;
	}

	case SNS_V4L2_GET_I2C_INFO:
	{
		struct i2c_client *client = v4l2_get_subdevdata(&gc8613->sd);
		sns_i2c_info_t i2c_info;

		i2c_info.i2c_addr =  client->addr;
		i2c_info.i2c_idx  =  client->adapter->i2c_idx;
		memcpy(arg, &i2c_info, sizeof(sns_i2c_info_t));
		break;
	}

	case SNS_V4L2_SET_HDR_ON:
	{
		int hdr_on = 0;
		struct i2c_client *client = v4l2_get_subdevdata(&gc8613->sd);

		memcpy(&hdr_on, arg, sizeof(int));

		if (hdr_on)
			dev_warn(&client->dev, "Not support HDR!\n");
		else
			memcpy(gc8613->cur_mode, &supported_modes[0], sizeof(struct gc8613_mode));

		gc8613_update_link_menu(gc8613);
		break;
	}

	case SNS_V4L2_SET_SNS_SYNC_INFO:
	{
		memcpy(&gc8613->cur_mode->gc8613_sync_info, arg, sizeof(sns_sync_info_t));
		break;
	}

	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc8613_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	long ret;

	switch (cmd) {
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static const struct v4l2_subdev_core_ops gc8613_core_ops = {
	.ioctl = gc8613_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc8613_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc8613_video_ops = {
	.s_stream = set_stream,
};

static const struct v4l2_subdev_pad_ops gc8613_pad_ops = {
	.enum_mbus_code = enum_mbus_code,
	.get_fmt = get_pad_format,
	.set_fmt = set_pad_format,
	.enum_frame_size = enum_frame_size,
	.enum_frame_interval = enum_frame_interval,
	.get_mbus_config = g_mbus_config,
};

static const struct v4l2_subdev_ops gc8613_subdev_ops = {
	.core	= &gc8613_core_ops,
	.video  = &gc8613_video_ops,
	.pad    = &gc8613_pad_ops,
};

static const struct media_entity_operations gc8613_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops gc8613_internal_ops = {
	.open = gc8613_open,
};

/* Initialize control handlers */
static int gc8613_init_controls(struct gc8613 *gc8613, int index_id)
{
	struct i2c_client *client = v4l2_get_subdevdata(&gc8613->sd);
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_ctrl *ctrl;
	int ret;
	int i;

	ctrl_hdlr = &gc8613->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret) {
		dev_err(&client->dev, "%s ctrl handler init failed (%d)\n",
			__func__, ret);
		return ret;
	}

	gc8613_get_info_from_dts(gc8613, index_id);

	mutex_init(&gc8613->mutex);
	ctrl_hdlr->lock = &gc8613->mutex;
	for (i = 0; i < SNS_CFG_TYPE_MAX; i++) {
		if (i == SNS_CFG_TYPE_WDR_MODE)
			gc8613->cur_mode->mipi_wdr_mode = gc8613_link_cif_menu[index_id][i];

		ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr, &gc8613_ctrl_ops, V4L2_CID_LINK_FREQ,
					      gc8613_link_cif_menu[index_id][i], 0,
					       (const s64 *)gc8613_link_cif_menu[index_id]);

		if (ctrl)
			ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	}

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s new std menu failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &gc8613_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	gc8613->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&gc8613->mutex);

	return ret;
}

static void gc8613_free_controls(struct gc8613 *gc8613)
{
	v4l2_ctrl_handler_free(gc8613->sd.ctrl_handler);
	mutex_destroy(&gc8613->mutex);
}

static int gc8613_probe(struct i2c_client *client,
			 const struct i2c_device_id *devid)
{
	struct gc8613 *gc8613;
	struct v4l2_subdev *sd;
	struct device *dev = &client->dev;
	int index_id = gc8613_probe_index;
	int addr_num = sizeof(gc8613_i2c_list) / sizeof(unsigned short);
	u32 bus_id, i2c_addr, use_defualt = 1;
	int ret = -1;
	int i;

	dev_info(dev, "probe id[%d] start\n", gc8613_probe_index);

	gc8613_probe_index++;

	if (index_id >= MAX_SENSOR_DEVICE || index_id < 0) {
		dev_info(dev, "invalid devid(%d)\n", index_id);
		return ret;
	}

	gc8613 = devm_kzalloc(&client->dev, sizeof(*gc8613), GFP_KERNEL);
	if (!gc8613)
		return -ENOMEM;

	sd = &gc8613->sd;

	if (!of_property_read_u32(client->dev.of_node, "reg-addr", &i2c_addr) &&
		!of_property_read_u32(client->dev.of_node, "bus-id", &bus_id) &&
		!gc8613_count) {
		client->addr = i2c_addr;
		client->adapter = i2c_get_adapter(bus_id);
		gc8613->client = client;
		v4l2_i2c_subdev_init(sd, client, &gc8613_subdev_ops);
		/* Check module identity */
		ret = gc8613_identify_module(gc8613);
		if (ret) {
			dev_info(dev, "id[%d] bus[%d] i2c_addr[%d][0x%x] no sensor found,use default\n",
				index_id, bus_id, i, client->addr);
			use_defualt = 1;
		} else {
			dev_info(dev, "id[%d] bus[%d] i2c_addr[0x%x] sensor found\n",
				index_id, bus_id, client->addr);
			use_defualt = 0;
		}
	}

	for (i = 0; i < addr_num && use_defualt; i++) {
		if (force_bus[index_id] < 0)
			bus_id = gc8613_bus_map[index_id];
		else
			bus_id = force_bus[index_id];

		if (bus_id < 0 || bus_id > MAX_I2C_BUS_NUM)
			return ret;

		client->addr = gc8613_i2c_list[i];
		client->adapter = i2c_get_adapter(bus_id);
		gc8613->client = client;
		v4l2_i2c_subdev_init(sd, client, &gc8613_subdev_ops);

		/* Check module identity */
		ret = gc8613_identify_module(gc8613);
		if (ret) {
			dev_info(dev, "id[%d] bus[%d] i2c_addr[%d][0x%x] no sensor found\n",
				 index_id, bus_id, i, client->addr);

			if (i == addr_num - 1)
				return ret;

			continue;
		} else {
			dev_info(dev, "id[%d] bus[%d] i2c_addr[0x%x] sensor found\n",
				 index_id, bus_id, client->addr);
			break;
		}
	}

	gc8613->module_index = index_id;


	gc8613->cur_mode = devm_kzalloc(&client->dev,
						sizeof(struct gc8613_mode), GFP_KERNEL);
	memcpy(gc8613->cur_mode, &supported_modes[0], sizeof(struct gc8613_mode));


	memset(&gc8613->cur_mode->gc8613_sync_info, 0, sizeof(sns_sync_info_t));

	mutex_init(&gc8613->mutex);

	ret = gc8613_init_controls(gc8613, index_id);
	if (ret)
		return ret;

	/* Initialize subdev */
	sd->internal_ops = &gc8613_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->entity.ops = &gc8613_subdev_entity_ops;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	gc8613->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 1, &gc8613->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init pads:%d\n", ret);
		goto error_handler_free;
	}

	snprintf(sd->name, sizeof(sd->name), "cam%d_%s %s",
		 gc8613->module_index, "gc8613", dev_name(sd->dev));

	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret < 0) {
		dev_err(&client->dev, "failed to async subdev:%d\n", ret);
		goto error_media_entity;
	}

	/*
	 * Device is already turned on by i2c-core with ACPI domain PM.
	 * Enable runtime PM and turn off the device.
	 */
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	dev_info(dev, "sensor_%d probe success\n", index_id);

	return 0;

error_media_entity:
	media_entity_cleanup(&gc8613->sd.entity);

error_handler_free:
	gc8613_free_controls(gc8613);
	dev_err(&client->dev, "%s failed:%d\n", __func__, ret);

	return ret;
}

static int gc8613_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc8613 *gc8613 = to_gc8613(sd);

	gc8613_probe_index = 0;
	pr_info("== gc8613_remove_index = %d ==\n", gc8613_probe_index);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	gc8613_free_controls(gc8613);

	pm_runtime_set_suspended(&client->dev);
	pm_runtime_disable(&client->dev);
	pm_runtime_suspend(&client->dev);

	dev_info(&client->dev, "sensor_%d remove success\n", gc8613_probe_index);
	return 0;
}

static const struct of_device_id gc8613_of_match[] = {
	{ .compatible = "v4l2,sensor0" },
	{ .compatible = "v4l2,sensor1" },
	{ .compatible = "v4l2,sensor2" },
	{ .compatible = "v4l2,sensor3" },
	{ .compatible = "v4l2,sensor4" },
	{ .compatible = "v4l2,sensor5" },
	{},
};
MODULE_DEVICE_TABLE(of, gc8613_of_match);

static const struct dev_pm_ops gc8613_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(suspend, resume)
};

static struct i2c_driver gc8613_i2c_driver = {
	.driver = {
		.name = "gc8613",
		.pm = &gc8613_pm_ops,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(gc8613_of_match),
	},
	.probe    = gc8613_probe,
	.remove   = gc8613_remove,
};

static int __init sensor_mod_init(void)
{
	pr_info("== gc8613 mod add ==\n");

	return i2c_add_driver(&gc8613_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	pr_info("== gc8613 mod rmmod ==\n");
	i2c_del_driver(&gc8613_i2c_driver);
}

module_init(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("gc8613 sensor driver");
MODULE_LICENSE("GPL v2");
