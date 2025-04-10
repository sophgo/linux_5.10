#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_of.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_fourcc.h>

#include <linux/file.h>

#include "cvitek_drm.h"
#include "cvitek_drm_debugfs.h"
#include "cvitek_disp.h"

#define DUMP_BUF_PATH		"/tmp"

static const struct cvitek_drm_debugfs_format cvitek_drm_debugfs_format[] = {
	{ DRM_FORMAT_XRGB8888, 	"XRGB_8888" },
	{ DRM_FORMAT_RGB888, 	"RGB_888" },
	{ DRM_FORMAT_BGR888, 	"BGR_888" },
	{ DRM_FORMAT_YUV420, 	"YUV_PLANAR_420" },
	{ DRM_FORMAT_YUV422, 	"YUV_PLANAR_422" },
	{ DRM_FORMAT_NV12, 		"NV12" },
	{ DRM_FORMAT_NV21, 		"NV21" },
	{ DRM_FORMAT_NV16, 		"NV16" },
	{ DRM_FORMAT_NV61, 		"NV61" },
	{ DRM_FORMAT_YUYV, 		"YUYV" },
	{ DRM_FORMAT_YVYU, 		"YVYU" },
	{ DRM_FORMAT_UYVY, 		"UYVY" },
	{ DRM_FORMAT_VYUY, 		"VYUY" },
};

/* convert from fourcc format to disp format */
static char* cvitek_drm_debugfs_get_format(u32 pixel_format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cvitek_drm_debugfs_format); i++)
		if (cvitek_drm_debugfs_format[i].pixel_format == pixel_format)
			return cvitek_drm_debugfs_format[i].image_format;

	/* not found */
	DRM_ERROR("Not found pixel format!!fourcc_format= %d\n",
		  pixel_format);
	return NULL;
}

static int temp_pow(int sum, int n)
{
	int i;
	int temp = sum;

	if (n < 1)
		return 1;
	for (i = 1; i < n ; i++)
		sum *= temp;
	return sum;
}

int cvitek_drm_dump_plane_buffer(struct disp_dump_info *dump_info, int frame_count)
{
	const char *ptr;
	char file_name[100];
	size_t size, uv_size = 0;
	struct file *file;
	loff_t pos = 0;
	char *format;
	int flags;
	u16 virtual_height;

	if (dump_info->primary_xr24) {
		format = "XRGB8888";
	} else if (dump_info->overlay) {
		format = "ARGB8888";
	 } else {
		format = cvitek_drm_debugfs_get_format(dump_info->primary_fmt);
		if (!format)
			format = "UNKNOWN";
	}

	snprintf(file_name, 100, "%s/video%d_%d_%s_num_%d.%s", DUMP_BUF_PATH,
			dump_info->width, dump_info->height, format, frame_count,
			"bin");

	if (!dump_info->primary_xr24 && !dump_info->overlay &&
		(dump_info->primary_fmt != DRM_FORMAT_XRGB8888 &&
		dump_info->primary_fmt != DRM_FORMAT_RGB888 &&
		dump_info->primary_fmt != DRM_FORMAT_BGR888)) {
		size = ((dump_info->pitches[0] * 2 / dump_info->hsub / dump_info->vsub) +
				dump_info->pitches[0]) * dump_info->height;
	} else {
		size = dump_info->pitches[0] * dump_info->height;
	}

	ptr = file_name;
	flags = O_RDWR | O_CREAT | O_APPEND;
	file = filp_open(ptr, flags, 0644);
	if (!IS_ERR(file)) {
		kernel_write(file, dump_info->cma_object_vaddr[0], size, &pos);
		DRM_INFO("dump file name is:%s\n", file_name);
		fput(file);
	} else {
		DRM_ERROR("open %s failed\n", ptr);
	}

	return 0;
}

static int cvitek_drm_dump_buffer_show(struct seq_file *m, void *data)
{
	seq_puts(m, "  echo dump    > dump to dump one frame\n");
	seq_puts(m, "  echo dumpon  > dump to start disp keep dumping\n");
	seq_puts(m, "  echo dumpoff > dump to stop keep dumping\n");
	seq_puts(m, "  echo dumpn   > dump n is the number of dump times\n");
	seq_puts(m, "  dump path is /tmp\n");

	return 0;
}

static int cvitek_drm_dump_buffer_open(struct inode *inode, struct file *file)
{
	struct drm_crtc *crtc = inode->i_private;

	return single_open(file, cvitek_drm_dump_buffer_show, crtc);
}

static ssize_t
cvitek_drm_dump_buffer_write(struct file *file, const char __user *ubuf,
			       size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_crtc *crtc = m->private;
	char buf[14] = {};
	int dump_times = 0;
	int i = 0;
	struct cvitek_crtc *cvitek_crtc = to_cvitek_crtc(crtc);
	struct disp_hw_ctx *ctx = cvitek_crtc->hw_ctx;
	u8 disp_id = ctx->disp_id;

	if (len > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len - 1] = '\0';

	ctx->debugfs_disp_state[disp_id].frame_count = 0;
	ctx->debugfs_disp_state[disp_id].disp_dump_times = 0;

	if (strncmp(buf, "dumpon", 6) == 0) {
		ctx->debugfs_disp_state[disp_id].disp_dump_status = DUMP_KEEP;
		DRM_INFO("keep dumping\n");
	} else if (strncmp(buf, "dumpoff", 7) == 0) {
		ctx->debugfs_disp_state[disp_id].disp_dump_status = DUMP_DISABLE;
		DRM_INFO("close keep dumping\n");
	} else if (strncmp(buf, "dump", 4) == 0) {
		if (isdigit(buf[4])) {
			for (i = 4; i < strlen(buf); i++) {
				dump_times += temp_pow(10, (strlen(buf)
						       - i - 1))
						       * (buf[i] - '0');
			}
			ctx->debugfs_disp_state[disp_id].disp_dump_status = DUMP_MULTI;
			ctx->debugfs_disp_state[disp_id].disp_dump_times = dump_times;
			DRM_INFO("dump multi frame, dump times:%d\n", dump_times);
		} else {
			ctx->debugfs_disp_state[disp_id].disp_dump_status = DUMP_ONCE;
			DRM_INFO("dump one frame\n");
		}
	} else {
		return -EINVAL;
	}

	return len;
}

static const struct file_operations cvitek_drm_dump_buffer_fops = {
	.owner = THIS_MODULE,
	.open = cvitek_drm_dump_buffer_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = cvitek_drm_dump_buffer_write,
};

int cvitek_drm_add_dump_buffer(struct drm_crtc *crtc, struct dentry *root)
{
	struct dentry *ent;
	struct cvitek_crtc *cvitek_crtc = to_cvitek_crtc(crtc);
	struct disp_hw_ctx *ctx = cvitek_crtc->hw_ctx;
	u8 disp_id = ctx->disp_id;

	ctx->debugfs_disp_state[disp_id].disp_dump_status = DUMP_DISABLE;
	ctx->debugfs_disp_state[disp_id].disp_dump_times = 0;
	ctx->debugfs_disp_state[disp_id].frame_count = 0;
	ent = debugfs_create_file("dump", 0644, root,
				  crtc, &cvitek_drm_dump_buffer_fops);
	if (!ent) {
		DRM_ERROR("create disp dump err\n");
		debugfs_remove_recursive(root);
	}

	return 0;
}
