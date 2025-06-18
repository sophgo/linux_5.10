#ifndef __CVITEK_DRM_H__
#define __CVITEK_DRM_H__

struct cvitek_drm {
	struct drm_device drm;
};

#define CVITEK_OUT_MODE_AAAA					0
#define CVITEK_MAX_CRTC							2
#define CVITEK_MAX_LAYER						4
#define CVITEK_MAX_COVER_NUM					4

#define CVITEK_IF_PROP_COLOR_FORMAT_OUT			"color_format_out"
#define CVITEK_IF_PROP_OUTPUT_HDMI_DVI			"output_hdmi_dvi"
#define CVITEK_IF_PROP_OUTPUT_TYPE_CAPS 		"output_type_capacity"
#define CVITEK_IF_PROP_HDMI_QUANT_RANGE			"hdmi_quant_range"

enum cvitek_if_color_depth {
	CVITEK_IF_DEPTH_8,
	CVITEK_IF_DEPTH_10,
	CVITEK_IF_DEPTH_MAX,
};

enum cvitek_if_color_format {
	CVITEK_IF_FORMAT_RGB888,
	CVITEK_IF_FORMAT_YCBCR444,
	CVITEK_IF_FORMAT_YCBCR422,
	CVITEK_IF_FORMAT_MAX,
};

extern struct platform_driver cvitek_disp_driver;
extern struct platform_driver cvitek_dsi_driver;
extern struct platform_driver dw_hdmi_cvitek_pltfm_driver;
extern struct platform_driver cvitek_lvds_driver;

/*
 * Cvitek drm private crtc funcs.
 * @debugfs_init: init crtc debugfs.
 * @debugfs_dump: debugfs to dump crtc and plane state.
 * @regs_dump: dump disp current register config.
 */
struct cvitek_crtc_funcs {
	int (*debugfs_init)(struct drm_minor *minor, struct drm_crtc *crtc);
	int (*debugfs_dump)(struct drm_crtc *crtc, struct seq_file *s);
	void (*regs_dump)(struct drm_crtc *crtc, struct seq_file *s);
};

struct cvitek_crtc_cover_prop {
	struct drm_property *cover_en_prop[CVITEK_MAX_COVER_NUM];
	struct drm_property *cover_x_prop[CVITEK_MAX_COVER_NUM];
	struct drm_property *cover_y_prop[CVITEK_MAX_COVER_NUM];
	struct drm_property *cover_w_prop[CVITEK_MAX_COVER_NUM];
	struct drm_property *cover_h_prop[CVITEK_MAX_COVER_NUM];
	struct drm_property *cover_rgb_prop[CVITEK_MAX_COVER_NUM];
};

struct cvitek_drm_private {
	/* private crtc prop */
	struct drm_property *bg_color_prop[CVITEK_MAX_CRTC];
	struct cvitek_crtc_cover_prop cover_prop[CVITEK_MAX_CRTC];
	const struct cvitek_crtc_funcs *crtc_funcs[CVITEK_MAX_CRTC];
};

int cvitek_register_crtc_funcs(struct drm_crtc *crtc,
				 const struct cvitek_crtc_funcs *crtc_funcs);
void cvitek_unregister_crtc_funcs(struct drm_crtc *crtc);

#endif /* __CVITEK_DRM_H__ */
