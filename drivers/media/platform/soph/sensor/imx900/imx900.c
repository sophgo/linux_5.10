// SPDX-License-Identifier: GPL-2.0
/*
 * imx900 driver
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
#include <linux/compat.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/comm_cif.h>
#include <linux/sns_v4l2_uapi.h>
#include "imx900.h"
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

/* I2C per write of bits */
#define REG_VALUE_08BIT		1
#define REG_VALUE_16BIT		2
#define REG_VALUE_24BIT		3

/* Sensor Information */
#define IMX900_INFO_ADDR_H	0x3817
#define IMX900_INFO_ADDR_L	0x3816
#define IMX900_AQR 			900
#define IMX900_AMR 			1924

/*Sensor type for isp middleware*/
#define IMX900_SNS_TYPE_SDR V4L2_SONY_IMX900_MIPI_3M_70FPS_12BIT
#define IMX900_SNS_MONO_TYPE_SDR V4L2_SONY_IMX900_MONO_MIPI_3M_70FPS_12BIT

static const enum mipi_wdr_mode_e imx900_wdr_mode = MIPI_WDR_MODE_VC;

int imx900_count;
static int force_bus[MAX_SENSOR_DEVICE] = {[0 ... (MAX_SENSOR_DEVICE - 1)] = -1};
module_param_array(force_bus, int, &imx900_count, 0644);

static int imx900_probe_index;
static const unsigned short imx900_i2c_list[] = {0x36, 0x37};
static const int imx900_bus_map[MAX_SENSOR_DEVICE] = {3, 3};

struct imx900_reg_list {
	u32 num_of_regs;
	const struct imx900_reg *regs;
};

/* Mode : resolution and related config&values */
struct imx900_mode {
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
	sns_sync_info_t imx900_sync_info;
	struct imx900_reg_list reg_list;
};

/* Mode configs */
static struct imx900_mode supported_modes[] = {
	{
		.max_width = 2064,
		.max_height = 1552,
		.width = 2048,
		.height = 1536,
		.exp_def = 0x2000,
		.hts_def = 0x262,
		.vts_def = 0x699,
		.mipi_wdr_mode = MIPI_WDR_MODE_NONE,
		.sns_type = V4L2_SONY_IMX900_MIPI_3M_70FPS_12BIT,
		.sns_type_name = "V4L2_SONY_IMX900_MIPI_3M_70FPS_12BIT",
		.max_fps = {
			.numerator = 10000,
			.denominator = 700000,
		},
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2064x1552_regs),
			.regs = mode_2064x1552_regs,
		},
	},
	{
		.max_width = 2064,
		.max_height = 1552,
		.width = 2048,
		.height = 1536,
		.exp_def = 0x2000,
		.hts_def = 0x262,
		.vts_def = 0x699,
		.mipi_wdr_mode = MIPI_WDR_MODE_NONE,
		.sns_type = V4L2_SONY_IMX900_MONO_MIPI_3M_70FPS_12BIT,
		.sns_type_name = "V4L2_SONY_IMX900_MONO_MIPI_3M_70FPS_12BIT",
		.max_fps = {
			.numerator = 10000,
			.denominator = 700000,
		},
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mono_mode_2064x1552_regs),
			.regs = mono_mode_2064x1552_regs,
		},
	},
};

struct imx900 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct i2c_client *client;
	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;
	/* Current mode */
	struct imx900_mode *cur_mode;
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

#define to_imx900(_sd)	container_of(_sd, struct imx900, sd)

/* Read registers up to 4 at a time */
static int imx900_read_reg(struct imx900 *imx900, u16 reg, u32 len,
			    u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx900->sd);
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
static int imx900_write_reg(struct imx900 *imx900, u16 reg, u32 len,
			     u32 __val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx900->sd);
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
static int imx900_write_regs(struct imx900 *imx900,
			      const struct imx900_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx900->sd);
	int ret;
	u32 i, val;

	for (i = 0; i < len; i++) {
		ret = imx900_write_reg(imx900, regs[i].address, 1,
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
static int imx900_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx900 *imx900 = to_imx900(sd);
	struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_get_try_format(sd, fh->pad, 0);

	mutex_lock(&imx900->mutex);

	/* Initialize try_fmt */
	try_fmt->width = imx900->cur_mode->width;
	try_fmt->height = imx900->cur_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	/* No crop or compose */
	mutex_unlock(&imx900->mutex);

	return 0;
}

static int imx900_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx900 *imx900 = container_of(ctrl->handler,
					       struct imx900, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx900->sd);

	pm_runtime_put(&client->dev);

	return 0;
}

static const struct v4l2_ctrl_ops imx900_ctrl_ops = {
	.s_ctrl = imx900_set_ctrl,
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

static int enum_frame_size(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx900 *imx900 = to_imx900(sd);

	fse->min_width = imx900->cur_mode->width;
	fse->max_width = imx900->cur_mode->max_width;
	fse->min_height = imx900->cur_mode->height;
	fse->max_height = imx900->cur_mode->max_height;

	return 0;
}

static void update_pad_format(const struct imx900_mode *mode, struct v4l2_subdev_format *fmt)
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
	struct imx900 *imx900 = to_imx900(sd);
	struct v4l2_mbus_framefmt *framefmt;
	int ret = 0;

	mutex_lock(&imx900->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		fmt->format = *framefmt;
		ret = -ENOTTY;
	} else {
		update_pad_format(imx900->cur_mode, fmt);
	}
	mutex_unlock(&imx900->mutex);

	return ret;
}

static int set_pad_format(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx900 *imx900 = to_imx900(sd);
	struct imx900_mode *mode;
	struct v4l2_mbus_framefmt *framefmt;

	mutex_lock(&imx900->mutex);

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
		imx900->cur_mode = mode;
	}

	mutex_unlock(&imx900->mutex);

	return 0;
}

static void imx900_standby(struct imx900 *imx900)
{
	imx900_write_reg(imx900, 0x3000, REG_VALUE_08BIT, 0x01);
}

static void imx900_restart(struct imx900 *imx900)
{
	imx900_write_reg(imx900, 0x3000, REG_VALUE_08BIT, 0x00);
}

/* Start streaming */
static int start_streaming(struct imx900 *imx900)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx900->sd);
	const struct imx900_reg_list *reg_list;
	const sns_sync_info_t *sync_info;
	int ret;

	reg_list = &imx900->cur_mode->reg_list;

	ret = imx900_write_regs(imx900, reg_list->regs, reg_list->num_of_regs);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	sync_info = &imx900->cur_mode->imx900_sync_info;

	if (sync_info->num_of_regs > 0) {
		ret = imx900_write_regs(imx900, (struct imx900_reg *)sync_info->regs,
					sync_info->num_of_regs);
		if (ret) {
			dev_err(&client->dev, "%s failed to set default\n", __func__);
			return ret;
		}
	}

	usleep_range(100 * 1000, 200 * 1000);
	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(imx900->sd.ctrl_handler);
	if (ret)
		return ret;

	dev_info(&client->dev, "wdr_mode(%d) reg setting done\n", imx900->cur_mode->mipi_wdr_mode);

	return ret;
}

/* Stop streaming */
static int stop_streaming(struct imx900 *imx900)
{
	imx900_standby(imx900);
	return 0;
}

static int set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx900 *imx900 = to_imx900(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&imx900->mutex);
	if (imx900->streaming == enable) {
		mutex_unlock(&imx900->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		//reset sensor
		if (!IS_ERR(imx900->reset_gpio)) {
			gpiod_set_value_cansleep(imx900->reset_gpio, 0);
			msleep(100);
			gpiod_set_value_cansleep(imx900->reset_gpio, 1);
			msleep(100);
		}

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = start_streaming(imx900);
		if (ret)
			goto err_rpm_put;
	} else {
		stop_streaming(imx900);
		pm_runtime_put(&client->dev);
	}

	imx900->streaming = enable;
	mutex_unlock(&imx900->mutex);

	dev_info(&client->dev, "set stream(%d) success\n", enable);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&imx900->mutex);

	return ret;
}

static int __maybe_unused suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx900 *imx900 = to_imx900(sd);

	if (imx900->streaming)
		stop_streaming(imx900);

	return 0;
}

static int __maybe_unused resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx900 *imx900 = to_imx900(sd);
	int ret;

	if (imx900->streaming) {
		ret = start_streaming(imx900);
		if (ret)
			goto error;
	}

	return 0;

error:
	stop_streaming(imx900);
	imx900->streaming = false;
	return ret;
}

/* Verify chip ID */
static int imx900_identify_module(struct imx900 *imx900)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx900->sd);
	int ret;
	u32 val1 = 0, val2 = 0;
	u16 SensorIdentify = 0;

	imx900_write_reg(imx900, 0x3000, REG_VALUE_08BIT, 0x00);/* STANDBY */

	usleep_range(12 * 1000, 12 * 1000);

	ret = imx900_read_reg(imx900, IMX900_INFO_ADDR_H,
			       REG_VALUE_08BIT, &val1);

	dev_info(&client->dev, "read id:0x%x, ret:%d", val1, ret);

	ret = imx900_read_reg(imx900, IMX900_INFO_ADDR_L,
			       REG_VALUE_08BIT, &val2);

	SensorIdentify = (((val1 & 0xFF) << 8) | (val2 & 0xFF)) >> 5;

	if (SensorIdentify == IMX900_AQR) {
		dev_info(&client->dev, "This sensor is IMX900_AQR");
	} else if (SensorIdentify == IMX900_AMR) {
		dev_info(&client->dev, "This sensor is IMX900_AMR");
	} else {
		dev_err(&client->dev, "imx900 mismatch, read identify is (%x)\n", SensorIdentify);
		return -EIO;
	}

	return 0;
}

static void imx900_mirror_flip(struct imx900 *imx900, int orient)
{
	int Filp = 0;
	int Mirror = 0;

	pr_info("set mirror_flip:%d", orient);

	switch (orient) {
	case 0:
		break;
	case 1:
		Mirror = 1;
		break;
	case 2:
		Filp = 1;
		break;
	case 3:
		Mirror = 1;
		Filp = 1;
		break;
	default:
		return;
	}

	// imx900_write_reg(imx900, 0x3020, REG_VALUE_08BIT, Mirror);
	// imx900_write_reg(imx900, 0x3021, REG_VALUE_08BIT, Filp);
}

static int imx900_update_link_menu(struct imx900 *imx900)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx900->sd);
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_ctrl *ctrl;
	int wdr_index = SNS_CFG_TYPE_WDR_MODE;
	int id = imx900->module_index;
	int ret;
	int i;

	imx900_link_cif_menu[id][wdr_index] = imx900->cur_mode->mipi_wdr_mode;

	dev_info(&client->dev, "update mipi_mode:%lld", imx900_link_cif_menu[id][wdr_index]);

	ctrl_hdlr = imx900->sd.ctrl_handler;
	v4l2_ctrl_handler_free(ctrl_hdlr);

	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret) {
		dev_err(&client->dev, "%s ctrl handler init failed (%d)\n",
			__func__, ret);
		return ret;
	}

	for (i = 0; i < SNS_CFG_TYPE_MAX; i++) {
		ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx900_ctrl_ops, V4L2_CID_LINK_FREQ,
					      imx900_link_cif_menu[id][i], 0,
					       (const s64 *)imx900_link_cif_menu[id]);

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
static int imx900_get_info_from_dts(struct imx900 *imx900, int index_id)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx900->sd);
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
		imx900_link_cif_menu[index_id][SNS_CFG_TYPE_DATA_LANE0 + i] =
			i >= num_lanes ? -1 : lane[i];
		imx900_link_cif_menu[index_id][SNS_CFG_TYPE_PN_SWAP0 + i] =
			i >= num_lanes_swap ? 0 : lane_swap[i];
	}
	imx900_link_cif_menu[index_id][SNS_CFG_TYPE_MIPI_DEV] = mipi_dev;
	imx900_link_cif_menu[index_id][SNS_CFG_TYPE_MCLK_NUM] = mclk_num;
	imx900_link_cif_menu[index_id][SNS_CFG_TYPE_WDR_MODE] = wdr_mode;
	imx900_link_cif_menu[index_id][SNS_CFG_TYPE_DPHY_SETTLE] = hs_settle;
	imx900_link_cif_menu[index_id][SNS_CFG_TYPE_DPHY_EN] = dphy_enable;
	imx900_link_cif_menu[index_id][SNS_CFG_TYPE_PHY_MODE] = cif_mode;
	//type_mode
	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (!strcmp(type_name, supported_modes[i].sns_type_name)) {
			imx900->cur_mode = devm_kzalloc(&client->dev,
						 sizeof(struct imx900_mode), GFP_KERNEL);
			memcpy(imx900->cur_mode, &supported_modes[i], sizeof(struct imx900_mode));
		}
	}

	imx900->power_gpio = devm_gpiod_get(&client->dev,
			"power", GPIOD_OUT_LOW);
	if (IS_ERR(imx900->power_gpio))
		dev_err(&client->dev, "failed to get power-gpios\n");
	else
		gpiod_set_value_cansleep(imx900->power_gpio, 1);

	imx900->reset_gpio = devm_gpiod_get(&client->dev,
			"reset", GPIOD_OUT_HIGH);
	if (IS_ERR(imx900->reset_gpio))
		dev_err(&client->dev, "failed to get reset_gpio\n");

	return 0;
}

static long imx900_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx900 *imx900 = to_imx900(sd);
	long ret = 0;

	switch (cmd) {
	case SNS_V4L2_GET_TYPE:
	{
		int type = 0;
		struct i2c_client *client = v4l2_get_subdevdata(&imx900->sd);

		if (imx900->cur_mode->mipi_wdr_mode == MIPI_WDR_MODE_NONE) {//linear
			type = IMX900_SNS_TYPE_SDR;
			if (client->addr == 0x1b) // dts sensor1
				type = IMX900_SNS_MONO_TYPE_SDR;
		} else {
		}
		memcpy(arg, &type, sizeof(int));
		break;
	}

	case SNS_V4L2_SET_MIRROR_FLIP:
	{
		int orient = 0;

		memcpy(&orient, arg, sizeof(int));
		imx900_mirror_flip(imx900, orient);
		break;
	}

	case SNS_V4L2_GET_I2C_INFO:
	{
		struct i2c_client *client = v4l2_get_subdevdata(&imx900->sd);
		sns_i2c_info_t i2c_info;

		i2c_info.i2c_addr =  client->addr;
		i2c_info.i2c_idx  =  client->adapter->i2c_idx;
		memcpy(arg, &i2c_info, sizeof(sns_i2c_info_t));
		break;
	}

	case SNS_V4L2_SET_HDR_ON:
	{
		int hdr_on = 0;
		struct i2c_client *client = v4l2_get_subdevdata(&imx900->sd);

		memcpy(&hdr_on, arg, sizeof(int));

		if (hdr_on) {
			dev_warn(&client->dev, "Not support HDR!\n");
		}
		else {
			memcpy(imx900->cur_mode, &supported_modes[0], sizeof(struct imx900_mode));
		}

		imx900_update_link_menu(imx900);
		break;
	}

	case SNS_V4L2_SET_SNS_SYNC_INFO:
	{
		memcpy(&imx900->cur_mode->imx900_sync_info, arg, sizeof(sns_sync_info_t));
		break;
	}

	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long imx900_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	long ret;

	switch (cmd) {
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static const struct v4l2_subdev_core_ops imx900_core_ops = {
	.ioctl = imx900_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = imx900_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops imx900_video_ops = {
	.s_stream = set_stream,
};

static const struct v4l2_subdev_pad_ops imx900_pad_ops = {
	.enum_mbus_code = enum_mbus_code,
	.get_fmt = get_pad_format,
	.set_fmt = set_pad_format,
	.enum_frame_size = enum_frame_size,
	.get_mbus_config = g_mbus_config,
};

static const struct v4l2_subdev_ops imx900_subdev_ops = {
	.core	= &imx900_core_ops,
	.video  = &imx900_video_ops,
	.pad    = &imx900_pad_ops,
};

static const struct media_entity_operations imx900_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops imx900_internal_ops = {
	.open = imx900_open,
};

/* Initialize control handlers */
static int imx900_init_controls(struct imx900 *imx900, int index_id)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx900->sd);
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_ctrl *ctrl;
	int ret;
	int i;

	ctrl_hdlr = &imx900->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret) {
		dev_err(&client->dev, "%s ctrl handler init failed (%d)\n",
			__func__, ret);
		return ret;
	}

	imx900_get_info_from_dts(imx900, index_id);

	mutex_init(&imx900->mutex);
	ctrl_hdlr->lock = &imx900->mutex;
	for (i = 0; i < SNS_CFG_TYPE_MAX; i++) {
		if (i == SNS_CFG_TYPE_WDR_MODE)
			imx900->cur_mode->mipi_wdr_mode = imx900_link_cif_menu[index_id][i];

		ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx900_ctrl_ops, V4L2_CID_LINK_FREQ,
					      imx900_link_cif_menu[index_id][i], 0,
					       (const s64 *)imx900_link_cif_menu[index_id]);

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

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx900_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx900->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&imx900->mutex);

	return ret;
}

static void imx900_free_controls(struct imx900 *imx900)
{
	v4l2_ctrl_handler_free(imx900->sd.ctrl_handler);
	mutex_destroy(&imx900->mutex);
}

static int imx900_probe(struct i2c_client *client,
			 const struct i2c_device_id *devid)
{
	struct imx900 *imx900;
	struct v4l2_subdev *sd;
	struct device *dev = &client->dev;
	int index_id = imx900_probe_index;
	int addr_num = sizeof(imx900_i2c_list) / sizeof(unsigned short);
	u32 bus_id, i2c_addr, use_defualt = 1;
	int ret = -1;
	int i;

	dev_info(dev, "probe id[%d] start , client_addr = 0x%x\n", imx900_probe_index, client->addr);

	if (index_id >= MAX_SENSOR_DEVICE || index_id < 0) {
		dev_info(dev, "invalid devid(%d)\n", index_id);
		return ret;
	}

	imx900 = devm_kzalloc(&client->dev, sizeof(*imx900), GFP_KERNEL);
	if (!imx900)
		return -ENOMEM;

	sd = &imx900->sd;

	if (!of_property_read_u32(client->dev.of_node, "reg-addr", &i2c_addr) &&
		!of_property_read_u32(client->dev.of_node, "bus-id", &bus_id) &&
		!imx900_count) {
		client->addr = i2c_addr;
		client->adapter = i2c_get_adapter(bus_id);
		imx900->client = client;
		v4l2_i2c_subdev_init(sd, client, &imx900_subdev_ops);
		/* Check module identity */
		ret = imx900_identify_module(imx900);
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

	if (force_bus[index_id] < 0 && use_defualt)
		bus_id = imx900_bus_map[index_id];
	else
		bus_id = force_bus[index_id];

	if (bus_id < 0 || bus_id > MAX_I2C_BUS_NUM)
		return ret;

	client->addr = imx900_i2c_list[imx900_probe_index++];
	client->adapter = i2c_get_adapter(bus_id);
	imx900->client = client;
	v4l2_i2c_subdev_init(sd, client, &imx900_subdev_ops);

	/* Check module identity */
	ret = imx900_identify_module(imx900);
	if (ret) {
		dev_info(dev, "id[%d] bus[%d] i2c_addr[%d][0x%x] no sensor found\n",
				index_id, bus_id, i, client->addr);

		if (i == addr_num - 1)
			return ret;

	} else {
		dev_info(dev, "id[%d] bus[%d] i2c_addr[0x%x] sensor found\n",
				index_id, bus_id, client->addr);
	}

	imx900->module_index = index_id;


	imx900->cur_mode = devm_kzalloc(&client->dev,
						sizeof(struct imx900_mode), GFP_KERNEL);
	memcpy(imx900->cur_mode, &supported_modes[0], sizeof(struct imx900_mode));

	memset(&imx900->cur_mode->imx900_sync_info, 0, sizeof(sns_sync_info_t));

	mutex_init(&imx900->mutex);

	ret = imx900_init_controls(imx900, index_id);
	if (ret)
		return ret;

	/* Initialize subdev */
	sd->internal_ops = &imx900_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->entity.ops = &imx900_subdev_entity_ops;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx900->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 1, &imx900->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init pads:%d\n", ret);
		goto error_handler_free;
	}

	snprintf(sd->name, sizeof(sd->name), "cam%d_%s %s",
		 imx900->module_index, "imx900", dev_name(sd->dev));

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
	media_entity_cleanup(&imx900->sd.entity);

error_handler_free:
	imx900_free_controls(imx900);
	dev_err(&client->dev, "%s failed:%d\n", __func__, ret);

	return ret;
}

static int imx900_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx900 *imx900 = to_imx900(sd);

	imx900_probe_index = 0;
	pr_info("== imx900_remove_index = %d ==\n", imx900_probe_index);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imx900_free_controls(imx900);

	pm_runtime_set_suspended(&client->dev);
	pm_runtime_disable(&client->dev);
	pm_runtime_suspend(&client->dev);

	dev_info(&client->dev, "sensor_%d remove success\n", imx900_probe_index);

	return 0;
}

static const struct of_device_id imx900_of_match[] = {
	{ .compatible = "v4l2,sensor0" },
	{ .compatible = "v4l2,sensor1" },
	{},
};
MODULE_DEVICE_TABLE(of, imx900_of_match);

static const struct dev_pm_ops imx900_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(suspend, resume)
};

static struct i2c_driver imx900_i2c_driver = {
	.driver = {
		.name = "imx900",
		.pm = &imx900_pm_ops,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(imx900_of_match),
	},
	.probe    = imx900_probe,
	.remove   = imx900_remove,
};

static int __init sensor_mod_init(void)
{
	pr_info("== imx900 mod add ==\n");

	return i2c_add_driver(&imx900_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	pr_info("== imx900 mod rmmod ==\n");
	i2c_del_driver(&imx900_i2c_driver);
}

module_init(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("imx900 sensor driver");
MODULE_LICENSE("GPL v2");
