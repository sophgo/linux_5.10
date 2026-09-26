// SPDX-License-Identifier: GPL-2.0
/*
 * tp2825 driver
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
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/gpio/consumer.h>

#include <linux/comm_cif.h>
#include <linux/comm_vi.h>
#include <linux/sns_v4l2_uapi.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#include "tp2825.h"

/* I2C per write of bits */
#define REG_VALUE_08BIT		1
#define REG_VALUE_16BIT		2
#define REG_VALUE_24BIT		3
#define PAGE_SELECT 0XFF
/* Chip ID */
#define TP2825_CHIP_ID_ADDR_L		0xff
#define TP2825_CHIP_ID_ADDR_H		0xfe
#define TP2825_CHIP_ID			0x2825

static int tp2825_count;

static int force_bus[MAX_SENSOR_DEVICE] = {[0 ... (MAX_SENSOR_DEVICE - 1)] = 0xFF};
module_param_array(force_bus, int, &tp2825_count, 0644);

static int force_i2caddr[MAX_SENSOR_DEVICE] = {[0 ... (MAX_SENSOR_DEVICE - 1)] = 0xFF};
module_param_array(force_i2caddr, int, &tp2825_count, 0644);

static int tp2825_probe_index;

static unsigned short tp2825_i2caddr_map[] = {0x45, -1, -1, -1, -1, -1, -1, -1};
static int tp2825_bus_map[MAX_SENSOR_DEVICE] = {2, -1, -1, -1, -1, -1, -1, -1};
static int tp2825_type_map[MAX_SENSOR_DEVICE] = {
	V4L2_TECHPOINT_TP2825_1M_30FPS_8BIT,
	// V4L2_TECHPOINT_TP2825_2M_30FPS_8BIT,
	// V4L2_TECHPOINT_TP2825_2M_50FPS_8BIT,
	// V4L2_TECHPOINT_TP2825_2M_60FPS_8BIT,
	// V4L2_TECHPOINT_TP2825_4M_30FPS_8BIT,
	// V4L2_TECHPOINT_TP2825_4M_25FPS_8BIT,
};

struct tp2825_yuv_format {
	vi_isp_yuv_scene_e	yuv_scene_mode;
	vi_intf_mode_e	inf_mode;
	vi_work_mode_e	mux_mode;
	vi_yuv_data_seq_e data_seq;
};
struct tp2825_reg_list {
	u32 num_of_regs;
	const struct tp2825_reg *regs;
};

/* Mode : resolution and related config&values */
struct tp2825_mode {
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
	sns_sync_info_t tp2825_sync_info;
	struct tp2825_yuv_format yuv_format;
	struct tp2825_reg_list reg_list;
};

/* Mode configs */
static struct tp2825_mode supported_modes[] = {
	// 720P
	{
		.max_width  = 1280,
		.max_height = 720,
		.width  = 1280,
		.height = 720,
		.sns_type = V4L2_TECHPOINT_TP2825_1M_30FPS_8BIT,
		.sns_type_name = "V4L2_TECHPOINT_TP2825_1M_30FPS_8BIT",
		.max_fps = {
			.numerator   = 10000,
			.denominator = 300000,
		},
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1280x720_30p_regs),
			.regs = mode_1280x720_30p_regs,
		}
	},
	// // 1080P
	// {
	// 	.max_width  = 1920,
	// 	.max_height = 1080,
	// 	.width  = 1920,
	// 	.height = 1080,
	// 	.sns_type = V4L2_TECHPOINT_TP2825_2M_60FPS_8BIT,
	// 	.sns_type_name = "V4L2_TECHPOINT_TP2825_2M_60FPS_8BIT",
	// 	.max_fps = {
	// 		.numerator   = 10000,
	// 		.denominator = 600000,
	// 	},
	// 	.reg_list = {
	// 		.num_of_regs = ARRAY_SIZE(mode_1920x1080_60p_regs),
	// 		.regs = mode_1920x1080_60p_regs,
	// 	}
	// },
	// 2k
	// {
	// 	.max_width = 2560,
	// 	.max_height = 1440,
	// 	.width = 2560,
	// 	.height = 1440,
	// 	.sns_type = V4L2_TECHPOINT_TP2825_4M_25FPS_8BIT,
	// 	.sns_type_name = "V4L2_TECHPOINT_TP2825_4M_25FPS_8BIT",
	// 	.max_fps = {
	// 		.numerator = 10000,
	// 		.denominator = 250000,
	// 	},
	// 	.reg_list = {
	// 		.num_of_regs = ARRAY_SIZE(mode_2560x1440_25p_regs),
	// 		.regs = mode_2560x1440_25p_regs,
	// 	}
	// }
};

static struct tp2825_yuv_format yuv_format[] = {
	{
		.yuv_scene_mode = VI_ISP_YUV_SCENE_BYPASS,
		.inf_mode = VI_MODE_MIPI_YUV422,
		.mux_mode = VI_WORK_MODE_1MULTIPLEX,
		.data_seq = VI_DATA_SEQ_YUYV,
	},
};

struct tp2825 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct i2c_client *client;
	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;
	/* Current mode */
	struct tp2825_mode *cur_mode;
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

#define to_tp2825(_sd)	container_of(_sd, struct tp2825, sd)

/* Read registers up to 4 at a time */
static int tp2825_read_reg(struct tp2825 *tp2825, u8 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&tp2825->sd);
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	int ret;
	__be32 data_be = 0;
	u8 reg_addr_be = reg;

	if (len > 5)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags =  I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

/* Write registers up to 4 at a time */
static int tp2825_write_reg(struct tp2825 *tp2825, u16 reg, u32 len, u32 __val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&tp2825->sd);
	int buf_i, val_i;
	u8 buf[6], *val_p;
	__be32 val;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg;

	val = cpu_to_be32(__val);
	val_p = (u8 *)&val;
	buf_i = 1;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 1) != len + 1)
		return -EIO;

	return 0;
}

/* Write a list of registers */
static int tp2825_write_regs(struct tp2825 *tp2825, const struct tp2825_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&tp2825->sd);
	int ret;
	u32 i;

	for (i = 0; i < len; i++) {
		ret = tp2825_write_reg(tp2825, regs[i].address, 1,
					regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev, "Failed to write reg 0x%4.4x. error=%d\n",
					    regs[i].address, ret);

			return ret;
		} else {
			dev_info(&client->dev, "Write reg 0x%4.4x = 0x%2.2x\n",
				regs[i].address, regs[i].val);
		}
	}

	return 0;
}

/* Open sub-device */
static int tp2825_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct tp2825 *tp2825 = to_tp2825(sd);
	struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_get_try_format(sd, fh->pad, 0);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int module_id = tp2825->module_index;

	mutex_lock(&tp2825->mutex);

	/* Initialize try_fmt */
	try_fmt->width = tp2825->cur_mode->width;
	try_fmt->height = tp2825->cur_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_VUY8_1X24;
	try_fmt->field = V4L2_FIELD_NONE;

	/* No crop or compose */
	mutex_unlock(&tp2825->mutex);

	return 0;
}

static int tp2825_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tp2825 *tp2825 = container_of(ctrl->handler,
					       struct tp2825, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&tp2825->sd);

	pm_runtime_put(&client->dev);

	return 0;
}

static const struct v4l2_ctrl_ops tp2825_ctrl_ops = {
	.s_ctrl = tp2825_set_ctrl,
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

	code->code = MEDIA_BUS_FMT_VUY8_1X24;

	return 0;
}

static int enum_frame_interval(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_frame_interval_enum *fie)
{
	struct tp2825 *tp2825 = to_tp2825(sd);

	fie->width  = tp2825->cur_mode->width;
	fie->height = tp2825->cur_mode->height;

	fie->interval.numerator   = tp2825->cur_mode->max_fps.numerator;
	fie->interval.denominator = tp2825->cur_mode->max_fps.denominator;

	return 0;
}

static int enum_frame_size(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_frame_size_enum *fse)
{
	struct tp2825 *tp2825 = to_tp2825(sd);

	fse->min_width = tp2825->cur_mode->width;
	fse->max_width = tp2825->cur_mode->max_width;
	fse->min_height = tp2825->cur_mode->height;
	fse->max_height = tp2825->cur_mode->max_height;

	return 0;
}

static void update_pad_format(const struct tp2825_mode *mode, struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = MEDIA_BUS_FMT_VUY8_1X24;
	fmt->format.field = V4L2_FIELD_NONE;

	//set yuv format
	fmt->format.reserved[YUV_SCENCE_MODE] = mode->yuv_format.yuv_scene_mode;
	fmt->format.reserved[YUV_INF_MODE] = mode->yuv_format.inf_mode;
	fmt->format.reserved[YUV_MUX_MODE] = mode->yuv_format.mux_mode;
	fmt->format.reserved[YUV_DATA_SEQ] = mode->yuv_format.data_seq;
}

static int get_pad_format(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct tp2825 *tp2825 = to_tp2825(sd);
	struct v4l2_mbus_framefmt *framefmt;
	int ret = 0;

	mutex_lock(&tp2825->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		fmt->format = *framefmt;
		ret = -ENOTTY;
	} else {
		update_pad_format(tp2825->cur_mode, fmt);
	}
	mutex_unlock(&tp2825->mutex);

	return ret;
}

static int set_pad_format(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct tp2825 *tp2825 = to_tp2825(sd);
	struct tp2825_mode *mode;
	struct v4l2_mbus_framefmt *framefmt;

	mutex_lock(&tp2825->mutex);

	/* Only one raw bayer(GRBG) order is supported */
	if (fmt->format.code != MEDIA_BUS_FMT_VUY8_1X24)
		fmt->format.code = MEDIA_BUS_FMT_VUY8_1X24;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);
	update_pad_format(mode, fmt);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*framefmt = fmt->format;
	} else {
		tp2825->cur_mode = mode;
	}

	mutex_unlock(&tp2825->mutex);

	return 0;
}

static void tp2825_standby(struct tp2825 *tp2825)
{
	return;
}

/* Start streaming */
static int start_streaming(struct tp2825 *tp2825)
{
	struct i2c_client *client = v4l2_get_subdevdata(&tp2825->sd);
	const struct tp2825_reg_list *reg_list;
	const sns_sync_info_t *sync_info;
	int module_id = tp2825->module_index;
	int ret = 0;
	u32 val;

	reg_list = &tp2825->cur_mode->reg_list;

	dev_info(&client->dev, "1 channal out regs start to write\n");
	ret = tp2825_write_regs(tp2825, reg_list->regs, reg_list->num_of_regs);

	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	sync_info = &tp2825->cur_mode->tp2825_sync_info;
	if (sync_info->num_of_regs > 0) {
		ret = tp2825_write_regs(tp2825, (struct tp2825_reg *)sync_info->regs,
					sync_info->num_of_regs);
		if (ret) {
			dev_err(&client->dev, "%s failed to set default\n", __func__);
			return ret;
		}
	}

	usleep_range(100 * 1000, 200 * 1000);
	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(tp2825->sd.ctrl_handler);
	if (ret)
		return ret;

	dev_info(&client->dev, "wdr_mode(%d) reg setting done\n", tp2825->cur_mode->mipi_wdr_mode);

	return ret;
}

/* Stop streaming */
static int stop_streaming(struct tp2825 *tp2825)
{
	tp2825_standby(tp2825);
	return 0;
}

static int set_stream(struct v4l2_subdev *sd, int enable)
{
	struct tp2825 *tp2825 = to_tp2825(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&tp2825->mutex);
	if (tp2825->streaming == enable) {
		mutex_unlock(&tp2825->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		if (!IS_ERR(tp2825->reset_gpio)) {
			gpiod_set_value_cansleep(tp2825->reset_gpio, 0);
			msleep(100);
			gpiod_set_value_cansleep(tp2825->reset_gpio, 1);
			msleep(100);
		}

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = start_streaming(tp2825);
		if (ret)
			goto err_rpm_put;
	} else {
		stop_streaming(tp2825);
		pm_runtime_put(&client->dev);
	}

	tp2825->streaming = enable;
	mutex_unlock(&tp2825->mutex);

	dev_info(&client->dev, "In sensor, set stream(%d) success\n", enable);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&tp2825->mutex);

	return ret;
}

static int __maybe_unused suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tp2825 *tp2825 = to_tp2825(sd);

	if (tp2825->streaming)
		stop_streaming(tp2825);

	return 0;
}

static int __maybe_unused resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tp2825 *tp2825 = to_tp2825(sd);
	int ret;

	if (tp2825->streaming) {
		ret = start_streaming(tp2825);
		if (ret)
			goto error;
	}

	return 0;

error:
	stop_streaming(tp2825);
	tp2825->streaming = false;
	return ret;
}

/* Verify chip ID */
static int tp2825_identify_module(struct tp2825 *tp2825)
{
	int ret;
	int val1, val2;
	int read_data = 0;
	struct i2c_client *client = v4l2_get_subdevdata(&tp2825->sd);


	tp2825_write_reg(tp2825, PAGE_SELECT, 1, 0x00);  //select page0

	ret = tp2825_read_reg(tp2825, TP2825_CHIP_ID_ADDR_L,
			       REG_VALUE_08BIT, &val1);
	ret = tp2825_read_reg(tp2825, TP2825_CHIP_ID_ADDR_H,
			       REG_VALUE_08BIT, &val2);
	read_data = (val1 & 0xFF) | ((val2 & 0xFF) << 8);

	if (ret)
		return ret;

	if (read_data != TP2825_CHIP_ID) {
		dev_err(&client->dev, "chip id(%x) mismatch, read(%x)\n",
			TP2825_CHIP_ID, read_data);
		return -EIO;
	}

	return 0;
}

static int tp2825_get_info_from_dts(struct tp2825 *tp2825, int index_id)
{
	struct i2c_client *client = v4l2_get_subdevdata(&tp2825->sd);
	struct device_node *np = client->dev.of_node;
	u32 i, ret, len, num_lanes, num_lanes_swap;
	u32 lane[LANE_MAX_NUM] = {0}, lane_swap[LANE_MAX_NUM] = {0};
	u32 mipi_dev, mclk_num, wdr_mode, hs_settle, cif_mode;
	u32	dphy_enable, rst_gpio, rst_acvive;
	struct property *prop;

	prop = of_find_property(np, "lanes", &len);
	if (!prop) {
		dev_dbg(&client->dev, "not set lanes, using default\n");
		return -1;
	}

	num_lanes = len / sizeof(u32);

	ret = of_property_read_u32_array(np, "lanes",
				lane, num_lanes);
	if (ret) {
		dev_dbg(&client->dev, "failed to to lanes\n");
		return -1;
	}

	prop = of_find_property(np, "lanes-swap", &len);
	if (!prop) {
		dev_dbg(&client->dev, "not set lanes-swap, using default\n");
		return -1;
	}

	num_lanes_swap = len / sizeof(u32);

	ret = of_property_read_u32_array(np, "lanes-swap",
				lane_swap, num_lanes_swap);
	if (ret) {
		dev_dbg(&client->dev, "failed to lanes-swap, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "mipi-dev", &mipi_dev);
	if (ret) {
		dev_dbg(&client->dev, "failed to to mipi-dev, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "mclk-num", &mclk_num);
	if (ret) {
		dev_dbg(&client->dev, "failed to mclk-num, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "wdr-mode", &wdr_mode);
	if (ret) {
		dev_dbg(&client->dev, "failed to wdr-mode, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "hs-settle", &hs_settle);
	if (ret) {
		dev_dbg(&client->dev, "failed to hs-settle, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "dphy-enable", &dphy_enable);
	if (ret) {
		dev_dbg(&client->dev, "failed to dphy-enable, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "cif-mode", &cif_mode);
	if (ret) {
		dev_dbg(&client->dev, "failed to cif-mode, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "sns_rest_pin", &rst_gpio);
	if (ret) {
		dev_dbg(&client->dev, "failed to cif-mode, using default\n");
		return -1;
	}

	ret = of_property_read_u32(np, "rst_acvive", &rst_acvive);
	if (ret) {
		dev_dbg(&client->dev, "failed to cif-mode, using default\n");
		return -1;
	}

	for (i = 0; i < LANE_MAX_NUM; i++) {
		tp2825_link_bt1120_cif_menu[index_id][SNS_CFG_TYPE_DATA_LANE0 + i] =
			i >= num_lanes ? -1 : lane[i];
		tp2825_link_bt1120_cif_menu[index_id][SNS_CFG_TYPE_PN_SWAP0 + i] =
			i >= num_lanes_swap ? 0 : lane_swap[i];
	}
	tp2825_link_bt1120_cif_menu[index_id][SNS_CFG_TYPE_MIPI_DEV] = mipi_dev;
	tp2825_link_bt1120_cif_menu[index_id][SNS_CFG_TYPE_MCLK_NUM] = mclk_num;
	tp2825_link_bt1120_cif_menu[index_id][SNS_CFG_TYPE_WDR_MODE] = wdr_mode;
	tp2825_link_bt1120_cif_menu[index_id][SNS_CFG_TYPE_DPHY_SETTLE] = hs_settle;
	tp2825_link_bt1120_cif_menu[index_id][SNS_CFG_TYPE_DPHY_EN] = dphy_enable;
	tp2825_link_bt1120_cif_menu[index_id][SNS_CFG_TYPE_PHY_MODE] = cif_mode;

	return 0;
}

static long tp2825_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct tp2825 *tp2825 = to_tp2825(sd);
	long ret = 0;

	switch (cmd) {
	case SNS_V4L2_GET_TYPE:
	{
		int type = 0;
		int index = tp2825->module_index;

		if (index < 0 || index >= MAX_SENSOR_DEVICE) {
			pr_info("invalid module_index:%d\n", index);
		}

		type = tp2825_type_map[index];
		memcpy(arg, &type, sizeof(int));

		break;
	}

	case SNS_V4L2_GET_I2C_INFO:
	{
		struct i2c_client *client = v4l2_get_subdevdata(&tp2825->sd);
		sns_i2c_info_t i2c_info;

		i2c_info.i2c_addr =  client->addr;
		i2c_info.i2c_idx  =  client->adapter->i2c_idx;
		memcpy(arg, &i2c_info, sizeof(sns_i2c_info_t));
		break;
	}

	case SNS_V4L2_SET_SNS_SYNC_INFO:
	{
		memcpy(&tp2825->cur_mode->tp2825_sync_info, arg, sizeof(sns_sync_info_t));
		break;
	}

	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long tp2825_compat_ioctl32(struct v4l2_subdev *sd,
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

static const struct v4l2_subdev_core_ops tp2825_core_ops = {
	.ioctl = tp2825_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = tp2825_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops tp2825_video_ops = {
	.s_stream = set_stream,
};

static const struct v4l2_subdev_pad_ops tp2825_pad_ops = {
	.enum_mbus_code = enum_mbus_code,
	.get_fmt = get_pad_format,
	.set_fmt = set_pad_format,
	.enum_frame_size = enum_frame_size,
	.enum_frame_interval = enum_frame_interval,
	.get_mbus_config = g_mbus_config,
};

static const struct v4l2_subdev_ops tp2825_subdev_ops = {
	.core	= &tp2825_core_ops,
	.video  = &tp2825_video_ops,
	.pad    = &tp2825_pad_ops,
};

static const struct media_entity_operations tp2825_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops tp2825_internal_ops = {
	.open = tp2825_open,
};

/* Initialize control handlers */
static int tp2825_init_controls(struct tp2825 *tp2825, int index_id)
{
	struct i2c_client *client = v4l2_get_subdevdata(&tp2825->sd);
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_ctrl *ctrl;
	int ret;
	int i;

	ctrl_hdlr = &tp2825->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret) {
		dev_err(&client->dev, "%s ctrl handler init failed (%d)\n",
			__func__, ret);
		return ret;
	}

	tp2825_get_info_from_dts(tp2825, index_id);

	mutex_init(&tp2825->mutex);
	ctrl_hdlr->lock = &tp2825->mutex;
	for (i = 0; i < DVP_SNS_CFG_TYPE_MAX; i++) {
		// if (i == DVP_SNS_CFG_TYPE_WDR_MODE)
		// 	tp2825->cur_mode->mipi_wdr_mode = tp2825_link_bt1120_cif_menu[index_id][i];

		ctrl = v4l2_ctrl_new_int_menu(ctrl_hdlr, &tp2825_ctrl_ops, V4L2_CID_LINK_FREQ,
					      tp2825_link_bt1120_cif_menu[index_id][i], 0,
					       (const s64 *)tp2825_link_bt1120_cif_menu[index_id]);

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

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &tp2825_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	tp2825->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&tp2825->mutex);

	return ret;
}

static void tp2825_free_controls(struct tp2825 *tp2825)
{
	v4l2_ctrl_handler_free(tp2825->sd.ctrl_handler);
	mutex_destroy(&tp2825->mutex);
}

static int tp2825_probe(struct i2c_client *client,
			 const struct i2c_device_id *devid)
{
	struct tp2825 *tp2825;
	struct v4l2_subdev *sd;
	struct device *dev = &client->dev;
	int index_id = tp2825_probe_index;
	u32 bus_id, i2c_addr, use_defualt = 1;
	int ret = -1;

	dev_info(dev, "probe id[%d] start\n", tp2825_probe_index);

	tp2825_probe_index++;

	if (index_id >= MAX_SENSOR_DEVICE || index_id < 0) {
		dev_info(dev, "invalid devid(%d)\n", index_id);
		return ret;
	}

	tp2825 = devm_kzalloc(&client->dev, sizeof(*tp2825), GFP_KERNEL);
	if (!tp2825)
		return -ENOMEM;

	sd = &tp2825->sd;

	if (!of_property_read_u32(client->dev.of_node, "reg-addr", &i2c_addr) &&
		!of_property_read_u32(client->dev.of_node, "bus-id", &bus_id) &&
		!tp2825_count) {
		printk("tp2825_probe reg = %x\n", i2c_addr);
		printk("tp2825_probe bus-id = %x\n", bus_id);
		client->addr = i2c_addr;
		client->adapter = i2c_get_adapter(bus_id);
		tp2825->client = client;
		v4l2_i2c_subdev_init(sd, client, &tp2825_subdev_ops);
		/* Check module identity */
		ret = tp2825_identify_module(tp2825);
		if (ret) {
			dev_info(dev, "id[%d] bus[%d] i2c_addr[0x%x] no sensor found,use default\n",
				index_id, bus_id, client->addr);
			use_defualt = 1;
		} else {
			dev_info(dev, "id[%d] bus[%d] i2c_addr[0x%x] sensor found\n",
				 index_id, bus_id, client->addr);
			use_defualt = 0;
		}
	}
	if (use_defualt) {
		if (force_bus[index_id] == 0xFF)
			bus_id = tp2825_bus_map[index_id];
		else
			bus_id = force_bus[index_id];

		if (bus_id < 0 || bus_id > MAX_I2C_BUS_NUM)
			return ret;

		if (force_i2caddr[index_id] == 0xFF && use_defualt) {
			if (index_id >= ARRAY_SIZE(tp2825_i2caddr_map))
				client->addr = tp2825_i2caddr_map[index_id - 1];
			else
				client->addr = tp2825_i2caddr_map[index_id];
		} else
			client->addr = force_i2caddr[index_id];

		client->adapter = i2c_get_adapter(bus_id);
		tp2825->client = client;
		v4l2_i2c_subdev_init(sd, client, &tp2825_subdev_ops);

	/* Check module identity */
		ret = tp2825_identify_module(tp2825);
		if (ret) {
			dev_info(dev, "id[%d] bus[%d] i2c_addr[0x%x] no sensor found\n",
				 index_id, bus_id, client->addr);
			return ret;
		} else
			dev_info(dev, "id[%d] bus[%d] i2c_addr[0x%x] sensor found\n",
				 index_id, bus_id, client->addr);
		}
	tp2825->module_index = index_id;

	tp2825->cur_mode = devm_kzalloc(&client->dev,
					sizeof(struct tp2825_mode), GFP_KERNEL);
	memcpy(tp2825->cur_mode, &supported_modes[0], sizeof(struct tp2825_mode));

	memset(&tp2825->cur_mode->tp2825_sync_info, 0, sizeof(sns_sync_info_t));

	mutex_init(&tp2825->mutex);

	ret = tp2825_init_controls(tp2825, index_id);
	if (ret)
		return ret;

	/* Initialize subdev */
	sd->internal_ops = &tp2825_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->entity.ops = &tp2825_subdev_entity_ops;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	tp2825->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 1, &tp2825->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init pads:%d\n", ret);
		goto error_handler_free;
	}

	snprintf(sd->name, sizeof(sd->name), "cam%d_%s %s",
		 tp2825->module_index, "tp2825", dev_name(sd->dev));

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
	media_entity_cleanup(&tp2825->sd.entity);

error_handler_free:
	tp2825_free_controls(tp2825);
	dev_err(&client->dev, "%s failed:%d\n", __func__, ret);

	return ret;
}

static int tp2825_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tp2825 *tp2825 = to_tp2825(sd);

	tp2825_probe_index = 0;
	pr_info("== tp2825_remove_index = %d ==\n", tp2825_probe_index);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	tp2825_free_controls(tp2825);

	pm_runtime_set_suspended(&client->dev);
	pm_runtime_disable(&client->dev);
	pm_runtime_suspend(&client->dev);

	dev_info(&client->dev, "sensor_%d remove success\n", tp2825_probe_index);
	return 0;
}

static const struct of_device_id tp2825_of_match[] = {
	{ .compatible = "v4l2,sensor0" },
	{ .compatible = "v4l2,sensor1" },
	{ .compatible = "v4l2,sensor2" },
	{ .compatible = "v4l2,sensor3" },
	{ .compatible = "v4l2,sensor4" },
	{ .compatible = "v4l2,sensor5" },
	{ .compatible = "v4l2,sensor6" },
	{ .compatible = "v4l2,sensor7" },
	{ .compatible = "v4l2,sensor8" },
	{ .compatible = "v4l2,sensor9" },
	{ .compatible = "v4l2,sensor10" },
	{ .compatible = "v4l2,sensor11" },
	{},
};
MODULE_DEVICE_TABLE(of, tp2825_of_match);

static const struct dev_pm_ops tp2825_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(suspend, resume)
};

static struct i2c_driver tp2825_i2c_driver = {
	.driver = {
		.name = "tp2825",
		.pm = &tp2825_pm_ops,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tp2825_of_match),
	},
	.probe    = tp2825_probe,
	.remove   = tp2825_remove,
};

static int __init sensor_mod_init(void)
{
	pr_info("== tp2825 mod add ==\n");

	return i2c_add_driver(&tp2825_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	pr_info("== tp2825 mod rmmod ==\n");
	i2c_del_driver(&tp2825_i2c_driver);
}

module_init(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("tp2825 sensor driver");
MODULE_LICENSE("GPL v2");
