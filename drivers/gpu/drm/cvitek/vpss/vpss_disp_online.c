/*
 * vpss_disp_online.c
 *
 * Ported from the bare-metal VPSS test (testcase_vpss.c) so that the
 * vpss_disp_online_single() flow can be driven from within the Linux 5.10
 * DRM driver module (soph_drm), reusing the scaler (scaler.c) logic.
 *
 * The VGOP/GOP path is intentionally omitted. Register access is routed
 * through vpss_compat.h, which translates the bare-metal absolute physical
 * addresses to offsets against the DRM-mapped vpss0_base.
 */

#include "vpss_compat.h"
#include "vip_common.h"
#include "scaler.h"
#include "scaler_reg.h"

#include <stdarg.h>

/*
 * VO_SYS register base (phys 0x67000000), mapped by the DRM driver
 * (cvitek_vo_sys_reg.c). REG_VO_SYS_AXI_SW is at vo_sys_f_base + 0x0C
 * (phys 0x6700000C). This lives OUTSIDE the vpss0 (0x67110000) window,
 * so it must be accessed via vo_sys_f_base, NOT the vpss_compat.h macros.
 */
extern void __iomem *vo_sys_f_base;
#define REG_VO_SYS_AXI_SW_OFFSET 0x0C

/*
 * Logging wrapper for the ported bare-metal code (see vpss_compat.h).
 *
 * Intentionally NOT annotated with __printf() so the compiler skips
 * format-string checking on the many %ld/%lx-on-u64 call sites, avoiding
 * -Wformat / -Werror build breaks. Emitted at pr_debug() level.
 */
void vpss_uartlog(const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_debug("%pV", &vaf);
	va_end(args);
}

#define SINGLE_PACKED

#ifndef BOARD_PALLADIUM
#define DEFAULT_W 800
#define DEFAULT_H 600
#define WAIT_TIME 20000
#else
#define DEFAULT_W 800
#define DEFAULT_H 600
#define WAIT_TIME 600
#endif

/****************************************************************************
 * Config structures (mirrored from testcase_vpss.c)
 ****************************************************************************/
struct cvi_img_cfg {
	enum sclr_input in;   // 0(ISP), 1(dwa), 2(share), 3(DRAM)
	enum sclr_format fmt_in;
	enum sclr_csc csc;
	struct sclr_mem mem;
	bool dup2fancy_enable;
};

struct cvi_sc_cfg {
	struct sclr_rect crop;
	struct sclr_size dst;
	enum sclr_format fmt_out;
	enum sclr_out_mode out_mode;
	enum sclr_odma_datatype datatype;
	enum sclr_csc csc;
	enum sclr_flip_mode flip;
	struct sclr_mem mem;
	enum sclr_algorithm algorithm;
	struct sclr_convertto_formula convert_to_cfg;
	struct sclr_quant_formula quant_form;
	union {
		enum sclr_hsv_rounding hsv_round;
		enum sclr_quant_rounding quant_round;
	};
};

struct cvi_vpss_cfg {
	u8 inst;
	bool tile_enable;
	bool v_tile_enable;
	struct cvi_img_cfg img_cfg;
	struct cvi_sc_cfg sc_cfg;
};

/* struct cvi_vip_fmt
 * @fourcc: v4l2 fourcc code.
 * @fmt: sclr driver's relative format.
 * @buffers: number of buffers.
 * @bit_depth: number of bits per pixel per plane.
 * @plane_sub_h: plane horizontal subsample.
 * @plane_sub_v: plane vertical subsample.
 */
struct cvi_vip_fmt {
	u32 fourcc;			/* v4l2 format id */
	enum sclr_format fmt;
	u8	buffers;
	u32 bit_depth[3];
	u8 plane_sub_h;
	u8 plane_sub_v;
};

static const struct cvi_vip_fmt cvi_vip_formats[] = {
	{ .fmt = SCL_FMT_YUV420, .bit_depth = { 8, 2, 2 }, .buffers = 3, .plane_sub_h = 2, .plane_sub_v = 2, },
	{ .fmt = SCL_FMT_YUV422, .bit_depth = { 8, 4, 4 }, .buffers = 3, .plane_sub_h = 2, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_YUV444, .bit_depth = { 8, 8, 8 }, .buffers = 3, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_NV12, .bit_depth = { 8, 4, 0 }, .buffers = 2, .plane_sub_h = 1, .plane_sub_v = 2, },
	{ .fmt = SCL_FMT_NV21, .bit_depth = { 8, 4, 0 }, .buffers = 2, .plane_sub_h = 1, .plane_sub_v = 2, },
	{ .fmt = SCL_FMT_YUV422SP2, .bit_depth = { 8, 8, 0 }, .buffers = 2, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_YUV422SP1, .bit_depth = { 8, 8, 0 }, .buffers = 2, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_YUYV, .bit_depth = { 16 }, .buffers = 1, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_YVYU, .bit_depth = { 16 }, .buffers = 1, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_UYVY, .bit_depth = { 16 }, .buffers = 1, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_VYUY, .bit_depth = { 16 }, .buffers = 1, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_RGB_PLANAR, .bit_depth = { 8, 8, 8 }, .buffers = 3, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_BF16, .bit_depth = { 8, 8, 8 }, .buffers = 3, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_RGB_PACKED, .bit_depth = { 24 }, .buffers = 1, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_BGR_PACKED, .bit_depth = { 24 }, .buffers = 1, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_Y_ONLY, .bit_depth = { 8 }, .buffers = 1, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_BGR_PACKED, .bit_depth = { 24 }, .buffers = 1, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_RGB_PLANAR, .bit_depth = { 8, 8, 8 }, .buffers = 3, .plane_sub_h = 1, .plane_sub_v = 1, },
	{ .fmt = SCL_FMT_YUV444_PACKED, .bit_depth = { 24 }, .buffers = 1, .plane_sub_h = 1, .plane_sub_v = 1, },
};

/****************************************************************************
 * Static globals (mirrored from testcase_vpss.c)
 ****************************************************************************/
static bool is_init = false;

static u64 src_addr_scl[4][3] = {
	{BASE_OFFSET + 0x008000000, BASE_OFFSET + 0x00c000000, BASE_OFFSET + 0x010000000},  //fbd0-2
	{BASE_OFFSET + 0x014000000, BASE_OFFSET + 0x018000000, BASE_OFFSET + 0x01c000000},  //fbd3
	{BASE_OFFSET + 0x020000000, BASE_OFFSET + 0x024000000, BASE_OFFSET + 0x028000000},  //gop
	{BASE_OFFSET + 0x02c000000, BASE_OFFSET + 0x030000000, BASE_OFFSET + 0x034000000},
};

static u64 dst_addr_scl[4][3] = {
	{BASE_OFFSET + 0x038000000, BASE_OFFSET + 0x03c000000, BASE_OFFSET + 0x040000000},
	{BASE_OFFSET + 0x044000000, BASE_OFFSET + 0x048000000, BASE_OFFSET + 0x04c000000},
	{BASE_OFFSET + 0x050000000, BASE_OFFSET + 0x054000000, BASE_OFFSET + 0x058000000},
	{BASE_OFFSET + 0x05c000000, BASE_OFFSET + 0x060000000, BASE_OFFSET + 0x064000000},
};

//gop_addr 0x102000000
static u64 gop_addr = BASE_OFFSET + 0x02000000;
static u32 vpss_enable_mask;

/****************************************************************************
 * Helpers (mirrored from testcase_vpss.c)
 ****************************************************************************/
static void _get_bytesperline(enum sclr_format sc_fmt, u16 width, u16 *bytesperline)
{
	const struct cvi_vip_fmt *fmt = NULL;
	int k, p;

	memset(bytesperline, 0, sizeof(bytesperline[0] * 3));
	for (k = 0; k < ARRAY_SIZE(cvi_vip_formats); k++) {
		fmt = &cvi_vip_formats[k];
		if (fmt->fmt == sc_fmt)
			break;
	}

	for (p = 0; p < fmt->buffers; p++) {
		/* Calculate the minimum supported bytesperline value */
		if (p == 0)
			bytesperline[p] = (width * fmt->bit_depth[p]) >> 3;
		else {
			bytesperline[p] = bytesperline[0] / fmt->plane_sub_h;
			if (bytesperline[p] * fmt->plane_sub_h < bytesperline[0])
				bytesperline[p] += 1;
		}
		if (bytesperline[p] < 16) {
			bytesperline[p] = 16;
			if (p != 0)
				bytesperline[0] = bytesperline[p] * fmt->plane_sub_h;
		}
	}
	uartlog("fmt(%d) num_planes(%d) pitch(%d %d)\n", sc_fmt, fmt->buffers, bytesperline[0], bytesperline[1]);
}

static void default_src_mem(u16 w, u16 h, enum sclr_format fmt, u8 src_idx,
		struct sclr_mem *mem)
{
	u16 pitch[3];

	_get_bytesperline(fmt, w, pitch);

	mem->width = w;
	mem->height = h;
	mem->start_x = 0;
	mem->start_y = 0;
	mem->pitch_y = pitch[0];
	mem->pitch_c = pitch[1];

#ifdef SINGLE_PACKED
	{
		const struct cvi_vip_fmt *vip_fmt = NULL;
		int k;

		for (k = 0; k < ARRAY_SIZE(cvi_vip_formats); k++) {
			vip_fmt = &cvi_vip_formats[k];
			if (vip_fmt->fmt == fmt)
				break;
		}
		mem->addr0 = src_addr_scl[src_idx][R_IDX];
		mem->addr1 = mem->addr0 + pitch[0] * h;
		mem->addr2 = mem->addr1 + (pitch[1] * h / vip_fmt->plane_sub_v);
	}
#else
	mem->addr0 = src_addr_scl[src_idx][R_IDX];
	mem->addr1 = src_addr_scl[src_idx][G_IDX];
	mem->addr2 = src_addr_scl[src_idx][B_IDX];
#endif
}

static void default_dst_mem(u16 w, u16 h, enum sclr_format fmt, u8 dst_idx,
		struct sclr_mem *mem, u16 data_size)
{
	u16 pitch[3];

	_get_bytesperline(fmt, w, pitch);
	mem->width = w;
	mem->height = h;
	mem->start_x = 0;
	mem->start_y = 0;
	mem->pitch_y = pitch[0] * data_size;
	mem->pitch_c = pitch[1] * data_size;

#ifdef SINGLE_PACKED
	{
		const struct cvi_vip_fmt *vip_fmt = NULL;
		int k;

		for (k = 0; k < ARRAY_SIZE(cvi_vip_formats); k++) {
			vip_fmt = &cvi_vip_formats[k];
			if (vip_fmt->fmt == fmt)
				break;
		}
		mem->addr0 = dst_addr_scl[dst_idx][R_IDX];
		mem->addr1 = mem->addr0 + pitch[0] * data_size * h;
		mem->addr2 = mem->addr1 + (pitch[1] * data_size * h / vip_fmt->plane_sub_v);
	}
#else
	mem->addr0 = dst_addr_scl[dst_idx][R_IDX];
	mem->addr1 = dst_addr_scl[dst_idx][G_IDX];
	mem->addr2 = dst_addr_scl[dst_idx][B_IDX];
#endif
}

static void show_dump_mem(enum sclr_format fmt, struct sclr_mem *mem)
{
	const struct cvi_vip_fmt *vip_fmt = NULL;
	u16 stride0 = mem->pitch_y;
	u16 stride1 = mem->pitch_c;
	u16 h = mem->height;
	u32 len;
	u64 start_addr, end_addr;
	u64 addr[3] = {mem->addr0, mem->addr1, mem->addr2};
	int k, p;

	for (k = 0; k < ARRAY_SIZE(cvi_vip_formats); k++) {
		vip_fmt = &cvi_vip_formats[k];
		if (vip_fmt->fmt == fmt)
			break;
	}

#ifdef SINGLE_PACKED
	start_addr = (addr[0] - 0x100000000) / 4;
	end_addr = addr[0];

	for (p = 0; p < vip_fmt->buffers; p++) {
		len = (p == 0) ? (stride0 * h) : (stride1 * h);
		len = (p == 0) ? len : (len / vip_fmt->plane_sub_v);
		end_addr += len;
	}
	end_addr = (end_addr - 0x100000000) / 4 - 1;
	uartlog("pld dump addr, single packed : start(0x%lx) end (0x%lx).\n", start_addr, end_addr);
#else
	for (p = 0; p < vip_fmt->buffers; p++) {
		len = (p == 0) ? (stride0 * h) : (stride1 * h);
		len = len / vip_fmt->plane_sub_v;
		start_addr = (addr[p] - 0x100000000) / 4;
		end_addr = ((addr[p] + len) - 0x100000000) / 4;
		uartlog("pld dump addr,planar%d: start(0x%lx) end (0x%lx).\n", p, start_addr, end_addr);
	}
#endif
}

static void is_odd_yuv(struct cvi_vpss_cfg *cfg)
{
	if (cfg->img_cfg.fmt_in == SCL_FMT_YUV420 || cfg->img_cfg.fmt_in == SCL_FMT_YUV422)
		if (cfg->img_cfg.mem.width % 2 == 1) {
			cfg->img_cfg.mem.width -= 1;
			cfg->sc_cfg.crop.w -= 1;
		}
	if (cfg->sc_cfg.fmt_out == SCL_FMT_YUV420 || cfg->sc_cfg.fmt_out == SCL_FMT_YUV422)
		if (cfg->sc_cfg.dst.w % 2 == 1) {
			cfg->sc_cfg.dst.w -= 1;
			cfg->sc_cfg.mem.width -= 1;
		}

	if (cfg->img_cfg.fmt_in == SCL_FMT_YUV420 || cfg->img_cfg.fmt_in == SCL_FMT_NV12 || cfg->img_cfg.fmt_in == SCL_FMT_NV21)
		if (cfg->img_cfg.mem.height % 2 == 1) {
			cfg->img_cfg.mem.height -= 1;
			cfg->sc_cfg.crop.h -= 1;
		}
	if (cfg->sc_cfg.fmt_out == SCL_FMT_YUV420 || cfg->sc_cfg.fmt_out == SCL_FMT_NV12 || cfg->sc_cfg.fmt_out == SCL_FMT_NV21)
		if (cfg->sc_cfg.dst.h % 2 == 1) {
			cfg->sc_cfg.dst.h -= 1;
			cfg->sc_cfg.mem.height -= 1;
		}
}

static int basic(struct cvi_vpss_cfg *cfg)
{
	int ret = 0;
	struct sclr_img_cfg img = {0};
	struct sclr_core_cfg core_cfg = {0};
	struct sclr_odma_cfg odma_cfg = {0};
	u8 inst = cfg->inst;

	is_odd_yuv(cfg);
	//setup img

	img.burst = 15;
	img.outstanding = 15;
	img.fifo_y = 96;//130;
	img.fifo_c = (cfg->img_cfg.fmt_in == SCL_FMT_YUV420 || cfg->img_cfg.fmt_in == SCL_FMT_YUV422) ? 48 : 96;//65 : 130;
	img.src = cfg->img_cfg.in;
	img.dup2fancy_enable = cfg->img_cfg.dup2fancy_enable;
	img.csc = cfg->img_cfg.csc;
	img.fmt = cfg->img_cfg.fmt_in;
	img.mem = cfg->img_cfg.mem;

	if (!IS_YUV_FMT(img.fmt)) {
		img.csc = SCL_CSC_NONE;
	}
	sclr_img_checksum_en(inst, 1);
	sclr_img_set_cfg(inst, &img);
	uartlog("\n----vpss(%d) img_in----\n", inst);
	uartlog("in_fmt(%d), rect(%d %d %d %d)\n", img.fmt, img.mem.start_x, img.mem.start_y, img.mem.width, img.mem.height);
	uartlog("vpss(%d) input mem addr(%lx %lx %lx).\n", inst, img.mem.addr0, img.mem.addr1, img.mem.addr2);

	//setup sc core
	core_cfg.sc.src.w = cfg->img_cfg.mem.width;
	core_cfg.sc.src.h = cfg->img_cfg.mem.height;
	core_cfg.sc.crop = cfg->sc_cfg.crop;
	core_cfg.sc.dst = cfg->sc_cfg.dst;
	core_cfg.sc.algorithm = cfg->sc_cfg.algorithm;
	core_cfg.sc.tile_enable = cfg->tile_enable;
	core_cfg.sc.v_tile_enable = cfg->v_tile_enable;
	core_cfg.sc.mir_enable = 1;
	sclr_core_set_cfg(inst, &core_cfg);
	sclr_core_checksum_en(inst, 1);
	sclr_set_cfg(inst, false, true, true, false);
	uartlog("\n--sc core--\n");
	uartlog("crop(%d %d %d %d)\n", core_cfg.sc.crop.x, core_cfg.sc.crop.y, core_cfg.sc.crop.w, core_cfg.sc.crop.h);
	uartlog("size(%d %d)\n", core_cfg.sc.dst.w, core_cfg.sc.dst.h);

	// setup odma
	uartlog("\n--odma--\n");
	if ((cfg->sc_cfg.out_mode == SCL_OUT_CSC && IS_YUV_FMT(cfg->sc_cfg.fmt_out)) ||
			(cfg->sc_cfg.out_mode != SCL_OUT_CSC && !IS_YUV_FMT(cfg->sc_cfg.fmt_out))
		) {
			odma_cfg.csc_cfg.datatype = cfg->sc_cfg.datatype;
			odma_cfg.csc_cfg.mode = cfg->sc_cfg.out_mode;
			odma_cfg.csc_cfg.csc_type = cfg->sc_cfg.csc;
		// use yuv format if output-mode is csc, o/w should go rgb format.
	} else {
		uartlog("Invalid parameter, fmt(%d) should be rgb if mode(%d) is not csc.\n",
			cfg->sc_cfg.fmt_out, cfg->sc_cfg.out_mode);
		ret = -1;
	}

	if (cfg->sc_cfg.out_mode == SCL_OUT_QUANT) {
		odma_cfg.csc_cfg.quant_form = cfg->sc_cfg.quant_form;
		odma_cfg.csc_cfg.quant_round = cfg->sc_cfg.quant_round;
	} else if (cfg->sc_cfg.out_mode == SCL_OUT_CONVERT_TO)
		odma_cfg.csc_cfg.convert_to_cfg = cfg->sc_cfg.convert_to_cfg;

	odma_cfg.csc_cfg.work_on_border = true;
	odma_cfg.flip = cfg->sc_cfg.flip;
	odma_cfg.burst = 7;
	odma_cfg.fmt = cfg->sc_cfg.fmt_out;
	odma_cfg.mem = cfg->sc_cfg.mem;
	sclr_odma_set_cfg(inst, &odma_cfg);

	uartlog("output mem addr(0x%lx, 0x%lx, 0x%lx).\n",
		odma_cfg.mem.addr0, odma_cfg.mem.addr1, odma_cfg.mem.addr2);
	uartlog("odma_size(%d %d)\n", odma_cfg.mem.width, odma_cfg.mem.height);
	show_dump_mem(odma_cfg.fmt, &odma_cfg.mem);

	return ret;
}

/*
 * vpss_online_set_interrupt - stub.
 *
 * TODO: hook up request_irq() for the VPSS/scaler frame-done interrupts in the
 * kernel driver (the bare-metal version registered irq_handler_scl via
 * request_irq for each enabled instance). For now the disp-online path does
 * not depend on interrupt completion, so this is a no-op.
 */
static int vpss_online_set_interrupt(void)
{
	return 0;
}

int vpss_disp_online_single(enum vpss_dev inst, int w, int h, enum sclr_format fmt_in, u64 src_addr, bool fbd_enable, bool vgop_enable)
{
	int ret = 0;
	struct cvi_vpss_cfg cfg;
	struct sclr_fbd_cfg fbd_cfg;
	struct sclr_rect fbd_crop;

	memset(&cfg, 0, sizeof(struct cvi_vpss_cfg));
	memset(&fbd_cfg, 0, sizeof(struct sclr_fbd_cfg));

	if (is_init == false) {
		ret = vpss_online_set_interrupt();
		is_init = true;
	}

	if (fbd_enable) {
		int height;

		fbd_crop.x = 0;
		fbd_crop.y = 0;
		fbd_crop.w = w;
		fbd_crop.h = h;

		height = ((h + 15) >> 4) << 2;
		fbd_cfg.crop = fbd_crop;
		fbd_cfg.stride_y = ALIGN(ALIGN(w, 16) << 2, 32);
		fbd_cfg.stride_c = ALIGN(ALIGN(w >> 1, 16) << 2, 32);
		fbd_cfg.height_y = ALIGN(h, 16);
		fbd_cfg.height_c = ALIGN(h >> 1, 8);
		fbd_cfg.offset_base_y = src_addr_scl[0][R_IDX] + (((fbd_crop.x >> 8) * height) << 5) +
					((fbd_crop.y >> 2) << 5);
		fbd_cfg.offset_base_c = src_addr_scl[0][G_IDX] + (((fbd_crop.x >> 9) * height) << 5) +
					((fbd_crop.y >> 2) << 5);
		fbd_cfg.comp_base_y = src_addr_scl[0][B_IDX] + (fbd_crop.y >> 2) * fbd_cfg.stride_y;
		fbd_cfg.comp_base_c = src_addr_scl[1][R_IDX] + (fbd_crop.y >> 2) * ALIGN(ALIGN(((w + 1) >> 1), 16) << 2, 32);
		fbd_cfg.out_mode_y = 0;
		fbd_cfg.out_mode_c = 4;
		fbd_cfg.enable = 1;
		fbd_cfg.endian = 0;
		sclr_set_fbd(inst, &fbd_cfg);
		uartlog("vpss(%d) fbd set done\n", inst);
	}

	cfg.inst = inst;
	cfg.tile_enable = false;
	cfg.img_cfg.in = SCL_INPUT_MEM;
	cfg.img_cfg.fmt_in = fmt_in;
	cfg.img_cfg.csc = IS_YUV_FMT(fmt_in) ? SCL_CSC_709_FULL_YUV2RGB : SCL_CSC_NONE;
	cfg.img_cfg.dup2fancy_enable = false;
	src_addr_scl[0][R_IDX] = src_addr;
	default_src_mem(w, h, cfg.img_cfg.fmt_in, 0, &cfg.img_cfg.mem);

	cfg.sc_cfg.crop.x = 0;
	cfg.sc_cfg.crop.y = 0;
	cfg.sc_cfg.crop.w = w;
	cfg.sc_cfg.crop.h = h;
	cfg.sc_cfg.dst.w = w;
	cfg.sc_cfg.dst.h = h;
	cfg.sc_cfg.fmt_out = SCL_FMT_RGB_PACKED;  //  fmt_in
	cfg.sc_cfg.out_mode = IS_YUV_FMT(fmt_in) ? SCL_OUT_CSC : SCL_OUT_DISABLE; // disp online
	cfg.sc_cfg.csc = IS_YUV_FMT(fmt_in) ? SCL_CSC_709_FULL_RGB2YUV : SCL_CSC_NONE;
	cfg.sc_cfg.flip = SCL_FLIP_NO;
	cfg.sc_cfg.algorithm = SCL_BICUBIC;
	default_dst_mem(w, h, cfg.sc_cfg.fmt_out, 0, &cfg.sc_cfg.mem, 1);

	ret = basic(&cfg);

	// _reg_write(0x67111460, 0x10821082); // disp_online

	vpss_enable_mask = BIT(inst);
	sclr_set_cfg(inst, false, (!vgop_enable), true, true); // disp online
	sclr_top_set_cfg(inst, true, fbd_enable);

	_reg_write(REG_SCL_TOP_SW_VS_EN(inst), BIT(0) | BIT(3));

	_reg_write(REG_SCL_TOP_STITCHING_PATH(inst), BIT(0));

	// _reg_write(0x67111464, 0xf); // disp online

	/*
	 * 0x6700000C = REG_VO_SYS_AXI_SW (VO_SYS domain, not vpss0). Access via
	 * vo_sys_f_base mapped by the DRM driver; going through the vpss0
	 * translation macros would compute a negative (out-of-range) offset.
	 */
	// if (vo_sys_f_base)
	// 	writel(0x0, vo_sys_f_base + REG_VO_SYS_AXI_SW_OFFSET); // disp online

	sclr_multi_start(inst); // disp online
	return ret;
}
