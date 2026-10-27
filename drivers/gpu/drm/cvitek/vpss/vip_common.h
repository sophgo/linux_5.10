#ifndef _COMMON_VIP_H_
#define _COMMON_VIP_H_

#if !defined(ENV_CVITEST) && !defined(ENV_EMU)
#include <linux/types.h>
#endif
#include "cmdq.h"

//#define ISP_DUMP_TOOL
//#define VIP_PERFORMANCE_TEST

//#define FLOW_AFTER_ECO

#define BASE_OFFSET 0x1000000000
#define GOP_FRAME_OFFSET 0x200000

#ifndef MIN
#define MIN(a, b) (((a) < (b))?(a):(b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b))?(a):(b))
#endif
#define CLIP(x, min, max) MAX(MIN(x, max), min)
#define VIP_ALIGN(x) (((x) + 0xF) & ~0xF)   // for 16byte alignment
#define UPPER(x,y) (((x) + ((1 << (y)) - 1)) >> (y))   // for alignment
#ifndef ALIGN
#define ALIGN(x,a) (((x) + ((a) - 1)) & ~((a) - 1))
#endif

#if defined(ENV_CVITEST) || defined(ENV_EMU)
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))
#endif
#ifndef BIT
#define BIT(nr)         (UINT32_C(1) << (nr))
#endif
#endif

#define R_IDX 0
#define G_IDX 1
#define B_IDX 2

union vip_sys_reset {
	struct {
		u32 resv_b0 : 1;
		u32 isp_top : 1;
		u32 img_d : 1;
		u32 img_v : 1;
		u32 sc_top : 1;
		u32 sc_d : 1;
		u32 sc_v1 : 1;
		u32 sc_v2 : 1;
		u32 resv_b8 : 1;
		u32 disp : 1;
		u32 bt : 1;
		u32 dsi_mac : 1;
		u32 csi_mac0 : 1;
		u32 csi_mac1 : 1;
		u32 ldc : 1;
		u32 clk_div : 1;
		u32 rsv_b16 : 1;
		u32 isp_top_apb : 1;
		u32 sc_top_apb : 1;
		u32 ldc_top_apb : 1;
		u32 dsi_mac_apb : 1;
		u32 csi_mac0_apb : 1;
		u32 csi_mac1_apb : 1;
		u32 dsi_phy_apb : 1;
		u32 csi_phy0_apb : 1;
		u32 rsv_b25 : 1;
		u32 dsi_phy : 1;
		u32 csi_phy0 : 1;
		u32 rsv_b28 : 1;
		u32 csi_be : 1;
	} b;
	u32 raw;
};

union vip_sys_isp_clk {
	struct {
		u32 rsv : 18;
		u32 clk_isp_top_en : 1;
		u32 clk_axi_isp_en : 1;
		u32 clk_csi_mac0_en : 1;
		u32 clk_csi_mac1_en : 1;
	} b;
	u32 raw;
};
union vip_sys_clk_ctrl0{
	struct {
		u32 bt_src_sel : 2;
		u32 lvds0_src_sel : 1;
		u32 lvds1_src_sel : 1;
		u32 disp_sel_bt_div1 : 1;
		u32 disp_div_cnt: 3;
		u32 rsv1 : 1;
		u32 dsi_mac_src_sel : 1;
		u32 csi0_rx_src_sel : 1;
		u32 csi1_rx_src_sel : 1;
		u32 vi0_clk_src_sel : 1;
		u32 vi1_clk_src_sel : 1;
		u32 vi2_clk_src_sel : 1;
		u32 rsv2 : 1;
		u32 clk_disp_div_up_w1t : 1;
	} b;
	u32 raw;
};
union vip_sys_clk {
	struct {
		u32 sc_top : 1;
		u32 isp_top : 1;
		u32 ldc_top : 1;
		u32 rev_b3 : 1;
		u32 vip_sys : 1;
		u32 csi_phy : 1;
		u32 dsi_phy : 1;
		u32 rev_b7 : 1;
		u32 csi_mac0 : 1;
		u32 csi_mac1 : 1;
		u32 rsv2 : 2;
		u32 sc_x2p_busy_en : 1;
		u32 isp_x2p_busy_en : 1;
		u32 ldc_x2p_busy_en : 1;
		u32 rev_b15 : 1;
		u32 auto_sc_top : 1;
		u32 auto_isp_top : 1;
		u32 auto_ldc : 1;
		u32 rev_b19 : 1;
		u32 auto_vip_sys : 1;
		u32 auto_csi_phy : 1;
		u32 auto_dsi_phy : 1;
		u32 rev_b23 : 1;
		u32 auto_csi_mac0 : 1;
		u32 auto_csi_mac1 : 1;
	} b;
	u32 raw;
};

union vip_sys_intr {
	struct {
		u32 sc : 1;
		u32 rsv1 : 15;
		u32 isp : 1;
		u32 rsv2 : 7;
		u32 dwa : 1;
		u32 rsv3 : 3;
		u32 rot : 1;
		u32 csi_mac0 : 1;
		u32 csi_mac1 : 1;
	} b;
	u32 raw;
};

struct vip_size {
    u16 w;
    u16 h;
};

struct vip_rect {
    u16 x;
    u16 y;
    u16 w;
    u16 h;
};
/********************************************************************
 *   APIs to replace bmtest's standard APIs
 ********************************************************************/
#if defined(ENV_CVITEST) || defined(ENV_EMU)
extern int cli_readline(char *prompt, char *cmdbuf);
#define uart_getc bm_getc
#undef uartlog
#define uartlog bm_uartlog
#define bm_get_num(str, var) \
	{ \
		uartlog(str); \
		scanf("%d", &tmp); \
		var = tmp; \
	}

void bm_scanf(const char *fmt, ...);
int bm_getc(void);
void bm_uartlog(const char *fmt, ...);
#endif
u8 generate_cmdset(union cmdq_set *set, u32 addr, u8 num, u32 pattern, u8 op,
		bool is_end);
int reg_compare(u32 addr, u8 count, u32 pattern);

void vip_set_base_addr(void *base);
union vip_sys_clk vip_get_clk_lp(void);
void vip_set_clk_lp(union vip_sys_clk clk);
union vip_sys_reset vip_get_reset(void);
void vip_set_reset(union vip_sys_reset reset);
union vip_sys_intr vip_get_intr_status(void);
union vip_sys_intr vip_get_intr_mask(void);
void vip_set_intr_mask(union vip_sys_intr intr);
union vip_sys_clk_ctrl0 vip_get_clk_ctrl0(void);
void vip_set_clk_ctrl0(union vip_sys_clk_ctrl0 clk);

/**
 * vip_toggle_reset - enable/disable reset specified in mask. Lock protected.
 *
 * @param mask: resets want to be toggled.
 */
void vip_toggle_reset(union vip_sys_reset mask);

#endif
