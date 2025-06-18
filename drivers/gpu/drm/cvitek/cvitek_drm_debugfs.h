#ifndef CVITEK_DRM_DEBUGFS_H
#define CVITEK_DRM_DEBUGFS_H

struct disp_dump_info {
	bool primary_xr24;
	bool overlay;
	bool yuv_format;
	u8 hsub;
	u8 vsub;
	u32 primary_fmt;
	u32 pitches[3];
	u32 width;
	u32 height;
	u64 cma_object_vaddr[3];
};

enum disp_dump_status {
	DUMP_DISABLE = 0,
	DUMP_KEEP,
	DUMP_ONCE,
	DUMP_MULTI,
};

struct debugfs_disp_state {
	struct dentry *debugfs;
	struct drm_info_list *debugfs_files;

	/**
	 * @disp_dump_status the status of disp dump control
	 * @disp_dump_times control the dump times
	 * @frme_count the frame of dump buf
	 */
	enum disp_dump_status disp_dump_status;
	int disp_dump_times;
	int frame_count;

	struct disp_dump_info dump_info;
};

struct cvitek_drm_debugfs_format {
	u32 pixel_format;
	char image_format[30];
};

#if defined(CONFIG_CVITEK_DRM_DEBUG)
int cvitek_drm_add_dump_buffer(struct drm_crtc *crtc, struct dentry *root);
int cvitek_drm_dump_plane_buffer(struct disp_dump_info *dump_info, int frame_count);
#else
static inline int
cvitek_drm_add_dump_buffer(struct drm_crtc *crtc, struct dentry *root)
{
	return 0;
}

static inline int
cvitek_drm_dump_plane_buffer(struct disp_dump_info *dump_info, int frame_count)
{
	return 0;
}
#endif

#endif
