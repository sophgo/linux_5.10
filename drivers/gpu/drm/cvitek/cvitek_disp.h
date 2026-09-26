#ifndef __CVITEK_DISP_H__
#define __CVITEK_DISP_H__

#ifdef CONFIG_ARCH_CV84X6
#include <linux/ktime.h>
#include <linux/atomic.h>
#endif
#include "cvitek_drm_debugfs.h"

#define to_cvitek_crtc(crtc) \
	container_of(crtc, struct cvitek_crtc, base)

#define to_cvitek_plane(plane) \
	container_of(plane, struct cvitek_plane, base)

#define CVITEK_MAX_PLANE		4
#define DISP_MAX_GOP_OW_INST 	8
#define DISP_MAX_GOP_FB_INST 	2
#define DISP_COVER_0     		0
#define DISP_COVER_1     		1
#define DISP_COVER_2     		2
#define DISP_COVER_3     		3

#ifdef CONFIG_ARCH_CV84X6
/*
 * reg_disp_sel (bit[2:0]):
 *   0: from DMA
 *   1: from scaler_disp
 *   2: from 32bit DMA
 */
#define DISP_SEL_SHIFT                      0
#define DISP_SEL_MASK                       (0x7 << DISP_SEL_SHIFT)
#define DISP_SEL_DMA                        0
#define DISP_SEL_VPSS_ONLINE                1
#define DISP_SEL_DMA_32BIT                  2

/*
 * reg_fmt_sel_32b (bit[11:8]): disp source from 32bit DMA:。
 */
#define DISP_FMT_SEL_32B_SHIFT              8
#define DISP_FMT_SEL_32B_MASK               (0xF << DISP_FMT_SEL_32B_SHIFT)
#define DISP_FMT_SEL_32B_YUV420_PLANAR      0x0  /* YUV420 planar */
#define DISP_FMT_SEL_32B_NV21               0x1  /* NV21 {V,U} */
#define DISP_FMT_SEL_32B_NV12               0x2  /* NV12 {U,V} */
#define DISP_FMT_SEL_32B_X_2101010_PACKED   0x3  /* X###2101010 packet {X,#,#,#} */
#define DISP_FMT_SEL_32B_2101010_X_PACKED   0x4  /* ###X2101010 packet {#,#,#,X} */
#define DISP_FMT_SEL_32B_X_8888_PACKED      0x6  /* X###8888 packed {X,#,#,#} */
#define DISP_FMT_SEL_32B_8888_X_PACKED      0x7  /* ###X8888 packed {#,#,#,X} */

/*
 * reg_fmt_sel (bit[15:12]): disp source from DMA:
 *   4'h0 YUV420 planar   4'h1 YUV422 planar   4'h2 RGB888 planar
 *   4'h3 RGB {R,G,B}     4'h4 RGB {B,G,R}     4'h5 Y only
 *   4'h8 NV21 {V,U}      4'h9 NV12 {U,V}      4'ha YUV422-SP1{V,U}
 *   4'hb YUV422-SP2{U,V} 4'hc YUV2-1{U,Y,V,Y} 4'hd YUV2-2{V,Y,U,Y}
 *   4'he YUV2-3{Y,U,Y,V} 4'hf YUV2-4{Y,V,Y,U}
 */
#define DISP_FMT_SEL_SHIFT                  12
#define DISP_FMT_SEL_MASK                   (0xF << DISP_FMT_SEL_SHIFT)
#define DISP_CFG_FMT_SEL_MASK               (DISP_SEL_MASK | DISP_FMT_SEL_32B_MASK | DISP_FMT_SEL_MASK)

/*
 * REG_8B10B_MAP.reg_8b10b_map (bit[3:0]): 8bit -> 10bit data map.
 */
#define DISP_8B10B_MAP_SHIFT                0
#define DISP_8B10B_MAP_MASK                 (0xF << DISP_8B10B_MAP_SHIFT)
#define DISP_8B10B_MAP_LOW                  0x0  /* [2'b0, data]        (default) */
#define DISP_8B10B_MAP_MID                  0x1  /* [1'b0, data, 1'b0] */
#define DISP_8B10B_MAP_HIGH                 0x2  /* [data, 2'b0] */
#define DISP_8B10B_MAP_REPL_HI              0x3  /* [data, data[7:6]] */
#define DISP_8B10B_MAP_REPL_LO              0x4  /* [data, data[1:0]] */

/*
 * REG_DISP_FIFO / REG_Y_TH (0x30) fields:
 *   reg_fifo_rd_th_y   (bit[8:0])  : read y threshold; issue a read request when buffer data drops below this
 *   reg_fifo_pr_th_y   (bit[23:16]): read y high priority threshold
 *   reg_pck_lb_serial  (bit31)     : whether the line buffer is serial for packed formats
 *                                    0=not serial, 1=serial (must enable for packed RGB)
 *
 * BW fail optimization (4K high bandwidth): the FIFO max watermark is affected
 * by reg_pck_lb_serial; must satisfy rd_th_y <= watermark cap - burstIn
 * (reg_burst_ln, currently 0xF=15):
 *   - packed and serial enabled:   cap 384 -> rd_th_y = 384 - 15 = 369 (0x171)
 *   - non-packed (serial disabled): cap 144 -> rd_th_y = 144 - 15 = 129 (0x081)
 */
#define DISP_FIFO_RD_TH_Y_SHIFT             0
#define DISP_FIFO_RD_TH_Y_MASK              (0x1FF << DISP_FIFO_RD_TH_Y_SHIFT)
#define DISP_FIFO_PR_TH_Y_SHIFT             16
#define DISP_FIFO_PR_TH_Y_MASK              (0xFF << DISP_FIFO_PR_TH_Y_SHIFT)
#define DISP_FIFO_PCK_LB_SERIAL             (1U << 31)

#define DISP_FIFO_RD_TH_Y_PCK               511  /* 384 - burstIn(16), serial enable */
#define DISP_FIFO_RD_TH_Y_NONPCK            128  /* 144 - burstIn(16), serial disable */
#define DISP_FIFO_PR_TH_Y_DEFAULT           0xFF

/*
 * REG_DISP_FIFO_C / REG_C_TH (0x34) fields:
 *   reg_fifo_rd_th_c   (bit[7:0])  : read c threshold; issue a read request when buffer data drops below this
 *   reg_fifo_pr_th_c   (bit[15:8]) : read c high priority threshold
 */
#define DISP_FIFO_RD_TH_C_SHIFT             0
#define DISP_FIFO_RD_TH_C_MASK              (0xFF << DISP_FIFO_RD_TH_C_SHIFT)
#define DISP_FIFO_PR_TH_C_SHIFT             8
#define DISP_FIFO_PR_TH_C_MASK              (0xFF << DISP_FIFO_PR_TH_C_SHIFT)

#define DISP_FIFO_RD_TH_C(v)                (((v) & 0xFF) << DISP_FIFO_RD_TH_C_SHIFT)
#define DISP_FIFO_PR_TH_C(v)                (((v) & 0xFF) << DISP_FIFO_PR_TH_C_SHIFT)
#define DISP_FIFO_C_TH_MAX                  (DISP_FIFO_RD_TH_C(0xFF) | DISP_FIFO_PR_TH_C(0xFF))

/*
 * REG_AXI_CFG (0x58) fields:
 *   reg_os_max    (bit[3:0])  : max number of outstanding transactions
 *   reg_burst_ln  (bit[11:8]) : burst length
 *   reg_64b_align (bit16)     : read address 64-byte align enable
 *                               (only effective when reg_burst_ln is 3/7/11/15)
 */
#define DISP_AXI_CFG_OS_MAX_SHIFT           0
#define DISP_AXI_CFG_OS_MAX_MASK            (0xF << DISP_AXI_CFG_OS_MAX_SHIFT)
#define DISP_AXI_CFG_BURST_LN_SHIFT         8
#define DISP_AXI_CFG_BURST_LN_MASK          (0xF << DISP_AXI_CFG_BURST_LN_SHIFT)
#define DISP_AXI_CFG_64B_ALIGN              (1U << 16)

#define DISP_AXI_CFG_OS_MAX(v)              (((v) & 0xF) << DISP_AXI_CFG_OS_MAX_SHIFT)
#define DISP_AXI_CFG_BURST_LN(v)            (((v) & 0xF) << DISP_AXI_CFG_BURST_LN_SHIFT)
#endif /* CONFIG_ARCH_CV84X6 */

/* cvitek-format translate table */
struct cvitek_format {
	u32 pixel_format;
	u32 hw_format;
};

enum DISP_FORMAT_E {
	DISP_FORMAT_YUV_PLANAR_420 = 0,
	DISP_FORMAT_YUV_PLANAR_422 = 1,
	DISP_FORMAT_YUV_PLANAR_444 = 2,

	DISP_FORMAT_RGB_888 = 4,
	DISP_FORMAT_BGR_888 = 3,

	DISP_FORMAT_NV21 = 9,
	DISP_FORMAT_NV12 = 8,
	DISP_FORMAT_NV16 = 10,
	DISP_FORMAT_NV61 = 11,

	DISP_FORMAT_UYVY = 15,
	DISP_FORMAT_VYUY = 14,
	DISP_FORMAT_YUYV = 13,
	DISP_FORMAT_YVYU = 12,

	DISP_FORMAT_XRGB_8888 = 254,
	DISP_FORMAT_UNSUPPORT = 255,
};

enum VGOP_FORMAT_E {
	VGOP_FORMAT_ARGB8888 = 0,
	VGOP_FORMAT_ARGB4444 = 4,
	VGOP_FORMAT_ARGB1555 = 5,
	VGOP_FORMAT_UNSUPPORT
};

static const struct cvitek_format disp_formats[] = {
	{ DRM_FORMAT_XRGB8888, DISP_FORMAT_XRGB_8888 },
	{ DRM_FORMAT_RGB888, DISP_FORMAT_RGB_888 },
	{ DRM_FORMAT_BGR888, DISP_FORMAT_BGR_888 },
	{ DRM_FORMAT_YUV420, DISP_FORMAT_YUV_PLANAR_420 },
	{ DRM_FORMAT_YUV422, DISP_FORMAT_YUV_PLANAR_422 },
	{ DRM_FORMAT_NV12, DISP_FORMAT_NV12 },
	{ DRM_FORMAT_NV21, DISP_FORMAT_NV21 },
	{ DRM_FORMAT_NV16, DISP_FORMAT_NV16 },
	{ DRM_FORMAT_NV61, DISP_FORMAT_NV61 },
	{ DRM_FORMAT_YUYV, DISP_FORMAT_YUYV },
	{ DRM_FORMAT_YVYU, DISP_FORMAT_YVYU },
	{ DRM_FORMAT_UYVY, DISP_FORMAT_UYVY },
	{ DRM_FORMAT_VYUY, DISP_FORMAT_VYUY },
};

static const struct cvitek_format vgop_formats[] = {
	{ DRM_FORMAT_ARGB8888, VGOP_FORMAT_ARGB8888 },
	{ DRM_FORMAT_ARGB4444, VGOP_FORMAT_ARGB4444 },
	{ DRM_FORMAT_ARGB1555, VGOP_FORMAT_ARGB1555 },
};

static const u32 primary_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888, DRM_FORMAT_BGR888, DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV422, DRM_FORMAT_NV12, DRM_FORMAT_NV21,
	DRM_FORMAT_NV16, DRM_FORMAT_NV61, DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU, DRM_FORMAT_UYVY, DRM_FORMAT_VYUY
};

static const u32 overlay_formats[] = {
	DRM_FORMAT_ARGB8888, DRM_FORMAT_ARGB4444, DRM_FORMAT_ARGB1555
};

struct disp_size {
	u16 w;
	u16 h;
};

struct disp_point {
	u16 x;
	u16 y;
};

struct disp_rect {
	u16 x;
	u16 y;
	u16 w;
	u16 h;
};

struct disp_csc_matrix {
	u16 coef[3][3];
	u8 sub[3];
	u8 add[3];
};

#ifdef CONFIG_ARCH_CV84X6
union disp_intr_status {
	struct {
		u32 tgen_frame_str : 1;
		u32 tgen_frame_done : 1;
		u32 odma_frame_done : 1;
		u32 rdma_frame_done : 1;
		u32 disp_bw_fail : 1;
		u32 odma_fifo_fail : 1;
		u32 tgen_wrap_fifo_full : 1;
	} b;
	u32 raw;
};
#else
union disp_intr {
	struct {
		u32 fifo_full_error : 1;
		u32 up_1t_lite : 1;
		u32 up_1t : 1;
		u32 disp_frame_done : 1;
		u32 all_interrupt : 1;
	} b;
	u32 raw;
};
#endif /* CONFIG_ARCH_CV84X6 */


enum disp_csc {
	DISP_CSC_NONE,
	DISP_CSC_601_LIMIT_YUV2RGB,
	DISP_CSC_601_FULL_YUV2RGB,
	DISP_CSC_709_LIMIT_YUV2RGB,
	DISP_CSC_709_FULL_YUV2RGB,
	DISP_CSC_601_LIMIT_RGB2YUV,
	DISP_CSC_601_FULL_RGB2YUV,
	DISP_CSC_709_LIMIT_RGB2YUV,
	DISP_CSC_709_FULL_RGB2YUV,
	DISP_CSC_MAX,
};

enum disp_drop_mode {
	DISP_DROP_MODE_DITHER = 1,
	DISP_DROP_MODE_ROUNDING = 2,
	DISP_DROP_MODE_DROP = 3,
	DISP_DROP_MODE_MAX,
};

struct disp_mem {
	u64 addr0;
	u64 addr1;
	u64 addr2;
	u16 pitch_y;
	u16 pitch_c;
	u16 start_x;
	u16 start_y;
	u16 width;
	u16 height;
};

struct disp_gop_ow_cfg {
	enum VGOP_FORMAT_E fmt;
	struct disp_point start;
	struct disp_point end;
	u64 addr;
	u16 pitch;
	u16 crop_pixels;
	struct disp_size mem_size;
	struct disp_size img_size;
};

struct disp_gop_fb_cfg {
	union {
		struct {
			u32 width	: 7;
			u32 resv_b7	: 1;
			u32 pix_thr	: 5;
			u32 sample_rate	: 2;
			u32 resv_b15	: 1;
			u32 fb_num	: 5;
			u32 resv_b21	: 3;
			u32 attach_ow	: 3;
			u32 resv_b27	: 1;
			u32 enable	: 1;
		} b;
		u32 raw;
	} fb_ctrl;
	u32 init_st;
};

struct disp_cover_cfg {
	union {
		struct {
			u32 x       : 16;
			u32 y       : 15;
			u32 enable  : 1;
		} b;
		u32 raw;
	} start;
	struct disp_size img_size;
	union {
		struct {
			u32 cover_color_r   : 8;
			u32 cover_color_g   : 8;
			u32 cover_color_b   : 8;
			u32 resv            : 8;
		} b;
		u32 raw;
	} color;
};

struct disp_gop_odec_cfg {
	union {
		struct {
			u32 odec_en: 1;
			u32 odec_int_en: 1;
			u32 odec_int_clr: 1;
			u32 odec_dbg_ridx: 4;
			u32 odec_done: 1;
			u32 resev: 4;
			u32 odec_attached_idx: 3;
			u32 odec_int_vec: 8;
		} b;
		u32 raw;
	} odec_ctrl;
	u32 odec_debug;
};

struct disp_gop_cfg {
	union {
		struct {
			u32 ow0_en : 1;
			u32 ow1_en : 1;
			u32 ow2_en : 1;
			u32 ow3_en : 1;
			u32 ow4_en : 1;
			u32 ow5_en : 1;
			u32 ow6_en : 1;
			u32 ow7_en : 1;
			u32 hscl_en: 1;
			u32 vscl_en: 1;
			u32 colorkey_en : 1;
			u32 resv   : 1;
			u32 burst  : 4;
			u32 resv_b16 : 15;
			u32 sw_rst : 1;
		} b;
		u32 raw;
	} gop_ctrl;

	union {
		struct {
			u32 hi_thr	: 6;
			u32 resv_b6	: 2;
			u32 lo_thr	: 6;
			u32 resv_b14	: 2;
			u32 fb_init	: 1;
			u32 lo_thr_inv	: 1;
			u32 resv_b18	: 2;
			u32 detect_fnum	: 6;
		} b;
		u32 raw;
	} fb_ctrl;

	u32 colorkey;       // RGB888
	u16 font_fg_color;  // ARGB4444
	u16 font_bg_color;  // ARGB4444
	u8  ow_fb_id;

	struct disp_gop_ow_cfg ow_cfg[DISP_MAX_GOP_OW_INST];
	struct disp_gop_fb_cfg fb_cfg[DISP_MAX_GOP_FB_INST];
	struct disp_gop_odec_cfg odec_cfg;
};

enum disp_pattern_mode {
	DISP_PAT_OFF = 0,
	DISP_PAT_SNOW,
	DISP_PAT_AUTO,
	DISP_PAT_RED,
	DISP_PAT_GREEN,
	DISP_PAT_BLUE,
	DISP_PAT_COLORBAR,
	DISP_PAT_GRAY_GRAD_H,
	DISP_PAT_GRAY_GRAD_V,
	DISP_PAT_BLACK,
	DISP_PAT_MAX,
};

enum disp_pat_color {
	DISP_PAT_COLOR_WHITE,
	DISP_PAT_COLOR_RED,
	DISP_PAT_COLOR_GREEN,
	DISP_PAT_COLOR_BLUE,
	DISP_PAT_COLOR_CYAN,
	DISP_PAT_COLOR_MAGENTA,
	DISP_PAT_COLOR_YELLOW,
	DISP_PAT_COLOR_BAR,
	DISP_PAT_COLOR_USR,
	DISP_PAT_COLOR_MAX
};

enum disp_pat_type {
	DISP_PAT_TYPE_FULL,
	DISP_PAT_TYPE_H_GRAD,
	DISP_PAT_TYPE_V_GRAD,
	DISP_PAT_TYPE_AUTO,
	DISP_PAT_TYPE_SNOW,
	DISP_PAT_TYPE_OFF,
	DISP_PAT_TYPE_MAX
};

struct disp_pattern {
	enum disp_pat_type type;
	enum disp_pat_color color;
	u16 rgb[3];
};

struct disp_cfg {
	bool disp_from_sc;  // 0(DRAM), 1(scaler_d)
	bool cache_mode;
	bool sync_ext;
	bool tgen_en;
	enum DISP_FORMAT_E fmt;
	enum disp_csc in_csc;
	enum disp_csc out_csc;
	u8 burst;       // 0~15
	u8 out_bit;     // 6/8/10-bit
	enum disp_drop_mode drop_mode;
	struct disp_mem mem;
	struct disp_gop_cfg gop_cfg[CVITEK_MAX_PLANE - 1];
};

struct disp_timing {
	u16 width;
	u16 height;
	bool vsync_pol;
	bool hsync_pol;
	u16 htotal;
	u16 vtotal;
	u16 vsync_start;
	u16 vsync_end;
	u16 hsync_start;
	u16 hsync_end;
	u16 vfde_start;
	u16 vfde_end;
	u16 vmde_start;
	u16 vmde_end;
	u16 hfde_start;
	u16 hfde_end;
	u16 hmde_start;
	u16 hmde_end;
};

union disp_dbg_status {
	struct {
		u32 bw_fail     : 1;
		u32 bw_fail_clr : 1;
		u32 osd_bw_fail : 1;
		u32 osd_bw_fail_clr : 1;
		u32 err_fwr_y   : 1;
		u32 err_fwr_u   : 1;
		u32 err_fwr_v   : 1;
		u32 err_fwr_clr : 1;
		u32 err_erd_y   : 1;
		u32 err_erd_u   : 1;
		u32 err_erd_v   : 1;
		u32 err_erd_clr : 1;
		u32 lb_full_y   : 1;
		u32 lb_full_u   : 1;
		u32 lb_full_v   : 1;
		u32 resv1       : 1;
		u32 lb_empty_y  : 1;
		u32 lb_empty_u  : 1;
		u32 lb_empty_v  : 1;
		u32 resv2       : 13;
	} b;
	u32 raw;
};

struct disp_hw_ctx {
	u32 disp_id;
	u16 bg_rgb[3];
	int irq;
	struct clk *disp_clk;
	struct disp_cfg disp_cfg;
	struct disp_timing disp_timing;
	bool disp_vgop_status[CVITEK_MAX_PLANE];
	spinlock_t disp_mask_spinlock;
	bool primary_formats_xr24;
	struct disp_cover_cfg cover_cfg[CVITEK_MAX_COVER_NUM];
	struct debugfs_disp_state debugfs_disp_state[CVITEK_MAX_CRTC];
#ifdef CONFIG_ARCH_CV84X6
	bool disp_rdma_on;
	bool rdma_trig_done;
	/*
	 * Flag that the previous RDMA frame is done (rdma_frame_done received).
	 * RDMA trigger cadence: set on rdma_frame_done -> the next
	 * tgen_frame_done triggers the next RDMA only if this is set, then clears
	 * it. Ensures RDMA re-triggers for a frame only after the previous frame
	 * has been fetched and the vblank cadence point is reached, avoiding
	 * bw_fail due to data starvation.
	 */
	bool rdma_done_pending;
	/*
	 * DRM framework vblank enable state. This SoC's continuous RDMA refresh
	 * relies on the frame-done interrupt re-triggering in the irq handler, so
	 * the interrupt must not be disabled together with vblank. Use this flag
	 * to control whether vblank is reported to DRM; the interrupt itself stays
	 * always on.
	 */
	bool vblank_enabled;
	/*
	 * Whether the VO_SYS/VPSS online pipeline has already run once (hot state).
	 * Used to decide whether a VO_SYS IP reset is needed on a repeated modeset:
	 *   - cold start (first time): hardware initial values are clean, no reset needed;
	 *   - hot reconfigure (already ran): residual pipeline state from last time,
	 *     an IP reset is required before reconfiguring, otherwise the VPSS scaler
	 *     and DISP timing get row-alignment/size mismatch -> image distortion.
	 * Once set, this flag is NOT cleared on atomic_disable (unlike rdma_trig_done),
	 * so a second modeset at the same resolution can still trigger an IP reset.
	 */
	bool vo_sys_inited;
	u32 current_disp_sel;
	bool disp_sel_changed;
	u32 current_fmt;
	bool fmt_changed;
	int bw_restoration;         /* bw_fail recovery state: 1=waiting for hardware recover, software refill stopped */

	/* interrupt-storm protection */
	unsigned long irq_window_start;
	u32 irq_count;
	bool irq_disabled_by_storm;

	/* odma writeback debug */
	bool odma_enabled;
	void *odma_wb_vaddr;
	dma_addr_t odma_wb_paddr;
	u32 odma_wb_size;
	struct work_struct odma_work;
	u32 odma_dump_cnt;   /* frames dumped so far; stop after reaching the cap (ODMA_DUMP_MAX) */

	/*
	 * Polling-refill thread (refill_poll=1): pinned to CPU7 + SCHED_FIFO,
	 * disables all disp interrupts, busy-polls the 0x200 status register to
	 * refill, eliminating bw_fail caused by interrupt latency.
	 */
	struct task_struct *refill_thread;
	bool     refill_active;          /* set true after enable so the thread starts refilling; set false on bw_fail */
	struct drm_crtc *refill_crtc;    /* filled in by private_init, used by handle_vblank inside the thread */
	u64      refill_cnt;             /* number of successful refills */
	/* bw_fail stop site: record one set when stopping, print afterwards */
	ktime_t  refill_fail_ts;
	u32      refill_fail_hvcnt;
	u32      refill_fail_rdma_cnt;
	u32      refill_fail_tgen_cnt;
	u32      refill_fail_status;
	u32      refill_fail_axi;         /* AXI_CFG(0x58) at bw_fail */
	u32      refill_fail_fifo;        /* FIFO(0x30) at bw_fail */
	u32      refill_fail_inten;       /* INT_ENABLE(0x204) at bw_fail */
	u32      refill_fail_rdma_st;     /* RDMA_ST(0x270) at bw_fail */
	bool     refill_fail_dumped;     /* site printed only once; also serves as "has bw_fail happened" flag */
	/* v3 wide-window polling: phase anchor + done->TRIG latency stats */
	ktime_t  refill_last_done;       /* time of the most recent refill (phase anchor) */
	bool     refill_anchor_valid;    /* set true after the first frame; only then use predictive sleep */
	u64      refill_trig_cnt;        /* number of done->TRIG latency samples */
	u32      refill_trig_min_us;     /* done->TRIG latency min */
	u32      refill_trig_max_us;     /* done->TRIG latency max */
	u64      refill_trig_sum_us;     /* done->TRIG latency sum (for avg) */
	unsigned int poll_snap_seq;      /* bw_fail site snapshot sequence number */
#endif /* CONFIG_ARCH_CV84X6 */
};

struct cvitek_crtc {
	struct drm_crtc base;
	struct drm_pending_vblank_event *event;
	void *hw_ctx;
};

struct cvitek_plane {
	struct drm_plane base;
	void *hw_ctx;
	u32 ch;

	/*plane property*/
	struct drm_property *osd_en_prop;
	struct drm_property *osd_colorkey_en_prop;
	struct drm_property *osd_hscl_prop;
	struct drm_property *osd_vscl_prop;
	struct drm_property *osd_colorkey_prop;
	struct drm_property *ow_fb_id_prop;
};

struct cvitek_disp {
	struct drm_device *drm;
	struct cvitek_crtc ccrtc;
	struct cvitek_plane planes[CVITEK_MAX_PLANE];
	void *hw_ctx;
};

#define to_cvitek_disp(cvitek_crtc) \
	container_of(cvitek_crtc, struct cvitek_disp, cvitek_crtc)

/* display controller init/cleanup ops */
struct disp_match_data {
	u32 crtc_id;
	u32 num_planes;
	u32 prim_plane;
	char irq_name[32];
	const u32 *primary_formats;
	u32 primary_formats_cnt;
	const u32 *overlay_formats;
	u32 overlay_formats_cnt;
	u32 gamma_lut_size;

	const struct drm_crtc_helper_funcs *crtc_helper_funcs;
	const struct drm_crtc_funcs *crtc_funcs;
	const struct drm_plane_helper_funcs *plane_helper_funcs;
	const struct drm_plane_funcs  *plane_funcs;

	void *(*alloc_hw_ctx)(struct platform_device *pdev);
	void (*cleanup_hw_ctx)(struct platform_device *pdev,
					void *hw_ctx);
};

#endif /* __CVITEK_DISP_H__ */
