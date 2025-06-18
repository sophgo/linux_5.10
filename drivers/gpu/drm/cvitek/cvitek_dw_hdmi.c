#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/component.h>

#include <drm/drm_of.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dsc.h>
#include <drm/drm_edid.h>
#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_print.h>

#include <uapi/linux/videodev2.h>

#include <video/videomode.h>
#include <drm/bridge/dw_hdmi.h>
#include "cvitek_vo_sys_reg.h"
#include "cvitek_drm.h"
#include "cvitek_disp.h"
#include "cvitek_mipipll_cfg.h"

struct cvitek_hdmi {
	struct device *dev;
	struct drm_encoder encoder;
	struct drm_device *drm_dev;
	struct dw_hdmi_plat_data *plat_data;
	struct dw_hdmi *hdmi;
	u8 id;
	struct videomode vm;

	u8 force_output;
	u32 max_tmdsclk;
	unsigned long input_bus_format;
	unsigned long output_bus_format;
	unsigned long enc_out_encoding;
	int color_changed;

	struct drm_property *hdmi_output_property;
	struct drm_property *quant_range;
	struct drm_property *output_hdmi_dvi;
	struct drm_property *output_type_capacity;

	unsigned int colordepth;
	unsigned int colorimetry;
	unsigned int hdmi_quant_range;
	enum cvitek_if_color_format hdmi_output;
};

#define to_cvitek_hdmi(x)	container_of(x, struct cvitek_hdmi, x)

static const struct drm_prop_enum_list drm_hdmi_output_enum_list[] = {
	{ CVITEK_IF_FORMAT_RGB888, "rgb888" },
	{ CVITEK_IF_FORMAT_YCBCR444, "ycbcr444" },
	{ CVITEK_IF_FORMAT_YCBCR422, "ycbcr422" },
};

static const struct drm_prop_enum_list quant_range_enum_list[] = {
	{ HDMI_QUANTIZATION_RANGE_DEFAULT, "default" },
	{ HDMI_QUANTIZATION_RANGE_LIMITED, "limit" },
	{ HDMI_QUANTIZATION_RANGE_FULL, "full" },
};

static const struct drm_prop_enum_list output_hdmi_dvi_enum_list[] = {
	{ 0, "auto" },
	{ 1, "force_hdmi" },
	{ 2, "force_dvi" },
};

static const struct drm_prop_enum_list output_type_cap_list[] = {
	{ 0, "DVI" },
	{ 1, "HDMI" },
};

const struct dw_hdmi_plat_data cvitek_hdmi_drv_data = {
	.phy_name = "phy_model_316",
	.phy_force_vendor = false,
	.ycbcr_420_allowed = false,
	.use_drm_infoframe = true,
};

static const struct of_device_id dw_hdmi_cvitek_dt_ids[] = {
	{ .compatible = "cvitek,dw_hdmi",
	  .data = &cvitek_hdmi_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, dw_hdmi_cvitek_dt_ids);

static unsigned long
dw_hdmi_cvitek_get_input_bus_format(void *data)
{
	struct cvitek_hdmi *hdmi = (struct cvitek_hdmi *)data;

	return hdmi->input_bus_format;
}

static unsigned long
dw_hdmi_cvitek_get_output_bus_format(void *data)
{
	struct cvitek_hdmi *hdmi = (struct cvitek_hdmi *)data;

	return hdmi->output_bus_format;
}

static unsigned long
dw_hdmi_cvitek_get_enc_in_encoding(void *data)
{
	struct cvitek_hdmi *hdmi = (struct cvitek_hdmi *)data;

	return hdmi->enc_out_encoding;
}

static unsigned long
dw_hdmi_cvitek_get_enc_out_encoding(void *data)
{
	struct cvitek_hdmi *hdmi = (struct cvitek_hdmi *)data;

	return hdmi->enc_out_encoding;
}

static unsigned long
dw_hdmi_cvitek_get_quant_range(void *data)
{
	struct cvitek_hdmi *hdmi = (struct cvitek_hdmi *)data;

	return hdmi->hdmi_quant_range;
}

static bool
dw_hdmi_cvitek_get_color_changed(void *data)
{
	struct cvitek_hdmi *hdmi = (struct cvitek_hdmi *)data;
	bool ret = false;

	if (hdmi->color_changed)
		ret = true;
	hdmi->color_changed = 0;

	return ret;
}

static void dw_hdmi_cvitek_encoder_disable(struct drm_encoder *encoder)
{
}

static void
dw_hdmi_cvitek_select_output(struct drm_connector_state *conn_state,
							struct drm_crtc_state *crtc_state,
							struct cvitek_hdmi *hdmi,
							unsigned long *enc_out_encoding)
{
	struct drm_display_info *info = &conn_state->connector->display_info;
	struct drm_display_mode mode;
	u32 vic;
	bool sink_is_hdmi = true;

	switch(hdmi->hdmi_output) {
	case CVITEK_IF_FORMAT_YCBCR444:
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			hdmi->output_bus_format = MEDIA_BUS_FMT_YUV8_1X24;
		break;
	case CVITEK_IF_FORMAT_YCBCR422:
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY8_1X16;
		break;
	case CVITEK_IF_FORMAT_RGB888:
	default:
		hdmi->output_bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	}

	sink_is_hdmi = dw_hdmi_get_output_whether_hdmi(hdmi->hdmi);
	if (!sink_is_hdmi)
		hdmi->output_bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	drm_mode_copy(&mode, &crtc_state->mode);
	vic = drm_match_cea_mode(&mode);
	if ((vic == 6) || (vic == 7) || (vic == 21) || (vic == 22) ||
		 (vic == 2) || (vic == 3) || (vic == 17) || (vic == 18))
		*enc_out_encoding = V4L2_YCBCR_ENC_601;
	else
		*enc_out_encoding = V4L2_YCBCR_ENC_709;
}

static bool
dw_hdmi_cvitek_check_color(struct drm_connector_state *conn_state,
			     struct cvitek_hdmi *hdmi)
{
	struct drm_crtc_state *crtc_state = conn_state->crtc->state;
	unsigned long output_bus_format = hdmi->output_bus_format;
	unsigned long enc_out_encoding = hdmi->enc_out_encoding;

	dw_hdmi_cvitek_select_output(conn_state, crtc_state,
								hdmi, &hdmi->enc_out_encoding);

	if (output_bus_format != hdmi->output_bus_format ||
	    enc_out_encoding != hdmi->enc_out_encoding)
		return true;
	else
		return false;
}

static void
dw_hdmi_cvitek_attach_properties(struct drm_connector *connector,
				   unsigned int color, int version,
				   void *data)
{
	struct cvitek_hdmi *hdmi = (struct cvitek_hdmi *)data;
	struct drm_property *prop;
	struct cvitek_drm_private *private = connector->dev->dev_private;

	if (color == MEDIA_BUS_FMT_YUV8_1X24)
		hdmi->hdmi_output = CVITEK_IF_FORMAT_YCBCR444;
	else if (color == MEDIA_BUS_FMT_YUV8_1X24)
		hdmi->hdmi_output = CVITEK_IF_FORMAT_YCBCR422;
	else if (color == MEDIA_BUS_FMT_RGB888_1X24)
		hdmi->hdmi_output = CVITEK_IF_FORMAT_RGB888;

	hdmi->colordepth = 8;

	hdmi->input_bus_format = color;
	hdmi->output_bus_format = color;

	prop = drm_property_create_enum(connector->dev, 0, CVITEK_IF_PROP_COLOR_FORMAT_OUT,
					drm_hdmi_output_enum_list,
					ARRAY_SIZE(drm_hdmi_output_enum_list));
	if (prop) {
		hdmi->hdmi_output_property = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = drm_property_create_enum(connector->dev, 0, CVITEK_IF_PROP_OUTPUT_HDMI_DVI,
					output_hdmi_dvi_enum_list,
					ARRAY_SIZE(output_hdmi_dvi_enum_list));
	if (prop) {
		hdmi->output_hdmi_dvi = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = drm_property_create_enum(connector->dev, 0, CVITEK_IF_PROP_OUTPUT_TYPE_CAPS,
					output_type_cap_list,
					ARRAY_SIZE(output_type_cap_list));
	if (prop) {
		hdmi->output_type_capacity = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = drm_property_create_enum(connector->dev, 0, CVITEK_IF_PROP_HDMI_QUANT_RANGE,
					quant_range_enum_list,
					ARRAY_SIZE(quant_range_enum_list));
	if (prop) {
		hdmi->quant_range = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}
}

static int
dw_hdmi_cvitek_set_property(struct drm_connector *connector,
			      struct drm_connector_state *state,
			      struct drm_property *property,
			      u64 val,
			      void *data)
{
	struct cvitek_hdmi *hdmi = (struct cvitek_hdmi *)data;
	struct drm_mode_config *config = &connector->dev->mode_config;

	if (property == hdmi->hdmi_output_property) {
		hdmi->hdmi_output = val;
		if (!state->crtc)
			return 0;
		if (dw_hdmi_cvitek_check_color(state, hdmi))
			hdmi->color_changed++;
		return 0;
	} else if (property == hdmi->quant_range) {
		u64 quant_range = hdmi->hdmi_quant_range;

		hdmi->hdmi_quant_range = val;
		if (quant_range != hdmi->hdmi_quant_range)
			dw_hdmi_set_quant_range(hdmi->hdmi);
		return 0;
	} else if (property == hdmi->output_hdmi_dvi) {
		if (hdmi->force_output != val)

			hdmi->color_changed++;
		hdmi->force_output = val;
		dw_hdmi_set_output_type(hdmi->hdmi, val);
		return 0;
	} else if (property == hdmi->output_type_capacity) {
		return 0;
	}

	DRM_ERROR("Unknown property [PROP:%d:%s]\n",
		  property->base.id, property->name);

	return -EINVAL;
}

static int
dw_hdmi_cvitek_get_property(struct drm_connector *connector,
			      const struct drm_connector_state *state,
			      struct drm_property *property,
			      u64 *val,
			      void *data)
{
	struct cvitek_hdmi *hdmi = (struct cvitek_hdmi *)data;
	struct drm_mode_config *config = &connector->dev->mode_config;

	if (property == hdmi->hdmi_output_property) {
		*val = hdmi->hdmi_output;
		return 0;
	} else if (property == hdmi->quant_range) {
		*val = hdmi->hdmi_quant_range;
		return 0;
	} else if (property == hdmi->output_hdmi_dvi) {
		*val = hdmi->force_output;
		return 0;
	} else if (property == hdmi->output_type_capacity) {
		*val = dw_hdmi_get_output_type_cap(hdmi->hdmi);
		return 0;
	} else if (property == connector->max_bpc_property) {
		/* we only support 8bit now */
		*val = 8;
		return 0;
	}

	DRM_ERROR("Unknown property [PROP:%d:%s]\n",
		  property->base.id, property->name);

	return -EINVAL;
}

static const struct dw_hdmi_property_ops dw_hdmi_cvitek_property_ops = {
	.attach_properties	= dw_hdmi_cvitek_attach_properties,
	.set_property		= dw_hdmi_cvitek_set_property,
	.get_property		= dw_hdmi_cvitek_get_property,
};

static void dw_hdmi_cvitek_encoder_mode_set(struct drm_encoder *encoder,
					      struct drm_display_mode *mode,
					      struct drm_display_mode *adj_mode)
{
	struct cvitek_hdmi *hdmi = to_cvitek_hdmi(encoder);

	drm_display_mode_to_videomode(adj_mode, &hdmi->vm);
}

static void dw_hdmi_cvitek_encoder_commit(struct drm_encoder *encoder)
{
}

void mipipll_clk_set(u32 ClkKhz)
{
	_reg_write(REG_DSI_PHY_POWER_DOWN_CFG(1), 0x0);
	mipi_dphy_set_pll(1, ClkKhz, 4, 24);
}

static void dw_hdmi_cvitek_encoder_enable(struct drm_encoder *encoder)
{
	struct cvitek_hdmi *hdmi = to_cvitek_hdmi(encoder);
	struct drm_crtc *crtc = encoder->crtc;

	if (WARN_ON(!crtc || !crtc->state))
		return;

	mipipll_clk_set(hdmi->vm.pixelclock / 1000);
}

static int
dw_hdmi_cvitek_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	return 0;
}

static const struct drm_encoder_helper_funcs dw_hdmi_cvitek_encoder_helper_funcs = {
	.mode_set	= dw_hdmi_cvitek_encoder_mode_set,
	.enable     = dw_hdmi_cvitek_encoder_enable,
	.disable    = dw_hdmi_cvitek_encoder_disable,
	.atomic_check = dw_hdmi_cvitek_encoder_atomic_check,
	.commit    = dw_hdmi_cvitek_encoder_commit,
};

static int dw_hdmi_cvitek_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_hdmi_plat_data *plat_data;
	struct drm_device *drm = data;
	struct drm_encoder *encoder = NULL;
	struct cvitek_hdmi *hdmi = NULL;
	int ret = 0;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdmi = platform_get_drvdata(pdev);
	if (!hdmi)
		return -ENOMEM;

	plat_data = hdmi->plat_data;
	if (!plat_data)
		return -ENOMEM;

	hdmi->drm_dev = drm;
	plat_data->phy_data = hdmi;

	plat_data->get_input_bus_format =
		dw_hdmi_cvitek_get_input_bus_format;
	plat_data->get_output_bus_format =
		dw_hdmi_cvitek_get_output_bus_format;
	plat_data->get_enc_in_encoding =
		dw_hdmi_cvitek_get_enc_in_encoding;
	plat_data->get_enc_out_encoding =
		dw_hdmi_cvitek_get_enc_out_encoding;
	plat_data->get_quant_range =
		dw_hdmi_cvitek_get_quant_range;
	plat_data->get_color_changed =
		dw_hdmi_cvitek_get_color_changed;

	plat_data->property_ops = &dw_hdmi_cvitek_property_ops;

	encoder = &hdmi->encoder;
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	drm_encoder_helper_add(encoder, &dw_hdmi_cvitek_encoder_helper_funcs);
	ret = drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_TMDS);
	if (ret) {
		DRM_ERROR("failed to init hdmi encoder\n");
		return ret;
	}

	mipipll_clk_set(25200);
	hdmi->hdmi = dw_hdmi_bind(pdev, encoder, plat_data);
	/*
	 * If dw_hdmi_bind() fails we'll never call dw_hdmi_unbind(),
	 * which would have called the encoder cleanup.  Do it manually.
	 */
	if (IS_ERR(hdmi->hdmi)) {
		ret = PTR_ERR(hdmi->hdmi);
		drm_encoder_cleanup(encoder);
	}

	return ret;
}

static void dw_hdmi_cvitek_unbind(struct device *dev, struct device *master,
				    void *data)
{
	struct cvitek_hdmi *hdmi = dev_get_drvdata(dev);

	dw_hdmi_unbind(hdmi->hdmi);
}

static const struct component_ops dw_hdmi_cvitek_ops = {
	.bind	= dw_hdmi_cvitek_bind,
	.unbind	= dw_hdmi_cvitek_unbind,
};

static int dw_hdmi_cvitek_probe(struct platform_device *pdev)
{
	struct cvitek_hdmi *hdmi;
	const struct of_device_id *match;
	struct dw_hdmi_plat_data *plat_data;
	int id;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	id = of_alias_get_id(pdev->dev.of_node, "hdmi");
	if (id < 0)
		id = 0;

	hdmi->id = id;
	hdmi->dev = &pdev->dev;

	match = of_match_node(dw_hdmi_cvitek_dt_ids, pdev->dev.of_node);

	plat_data = devm_kmemdup(&pdev->dev, match->data,
				 sizeof(*plat_data), GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;

	plat_data->id = hdmi->id;
	hdmi->plat_data = plat_data;

	platform_set_drvdata(pdev, hdmi);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	return component_add(&pdev->dev, &dw_hdmi_cvitek_ops);
}

static int dw_hdmi_cvitek_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dw_hdmi_cvitek_ops);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static void dw_hdmi_cvitek_shutdown(struct platform_device *pdev)
{
	struct cvitek_hdmi *hdmi = dev_get_drvdata(&pdev->dev);

	if (!hdmi)
		return;

	dw_hdmi_suspend(hdmi->hdmi);
	pm_runtime_put_sync(&pdev->dev);
}

static int dw_hdmi_cvitek_suspend(struct device *dev)
{
	struct cvitek_hdmi *hdmi = dev_get_drvdata(dev);

	dw_hdmi_suspend(hdmi->hdmi);
	pm_runtime_put_sync(dev);

	return 0;
}

static int dw_hdmi_cvitek_resume(struct device *dev)
{
	struct cvitek_hdmi *hdmi = dev_get_drvdata(dev);

	dw_hdmi_resume(hdmi->hdmi);
	pm_runtime_get_sync(dev);

	return 0;
}

static const struct dev_pm_ops dw_hdmi_cvitek_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(dw_hdmi_cvitek_suspend, dw_hdmi_cvitek_resume)
};

struct platform_driver dw_hdmi_cvitek_pltfm_driver = {
	.probe  = dw_hdmi_cvitek_probe,
	.remove = dw_hdmi_cvitek_remove,
	.shutdown = dw_hdmi_cvitek_shutdown,
	.driver = {
		.name = "dw_hdmi_cvitek",
		.pm = &dw_hdmi_cvitek_pm,
		.of_match_table = dw_hdmi_cvitek_dt_ids,
	},
};
