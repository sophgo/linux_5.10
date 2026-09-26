#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include "aks_phy.h"
#include "aks_phy_fw.h"
#include "cvitek_vo_sys_reg.h"

#define AKS_PHY_DEBUG_LOG 0


#if !AKS_PHY_DEBUG_LOG
#undef dev_info
#define dev_info(dev, fmt, ...) no_printk(fmt, ##__VA_ARGS__)
#endif

const struct hdmi_phy_mode phy_modes[] = {
	/* pixel_clk ratio sel freq_ctrl frac_en frac_quot frac_rem bwc   tx_div tx_rate rx_rate vco_t cdr_th vco_band */
	{ 31500,    0, 0, 403, 1,  6553,  2, 14244, 2, 3, 5, 1260, 16, 0},  /* 640x480@72Hz, CMPLL0, vendor testcase proto53 (not in DTD) */
	{ 13500,    0, 1, 518, 1, 13107,  1, 30612, 6, 2, 4, 1080, 14, 0},  /* SD 480i/576i pixel-repeat, cfg HDMI_1/c22 */
	{ 13513,    0, 1, 518, 1, 30094,  0, 30612, 6, 2, 4, 1081, 14, 0},  /* SD 480i/576i @59.94 pixel-repeat, cfg HDMI_1/c23 */
	{ 25175,    0, 1, 483, 1, 11686,  1, 14228, 6, 3, 5, 1007, 13, 0},  /* 640x480@59.94 (VGA), cfg HDMI_1/c2 */
	{ 25200,    0, 1, 483, 1, 11686,  1, 14228, 6, 3, 5, 1007, 13, 0},  /* 640x480@60 (VGA), cfg HDMI_1/c3 */
	{ 27000,    0, 1, 518, 1, 13107,  1, 30612, 6, 3, 5, 1080, 14, 0},  /* 720x480p/576p @50/60, cfg HDMI_1/c4 */
	{ 27027,    0, 1, 518, 1, 13107,  1, 30612, 6, 3, 5, 1080, 14, 0},  /* 720x480p @59.94, cfg HDMI_1/c5 */
	{ 54000,    0, 1, 518, 1, 13107,  1, 30612, 6, 2, 4, 1080, 14, 0},  /* 1440x480i / 2880 @50/60, cfg HDMI_1/c22 */
	{ 54054,    0, 1, 518, 1, 30094,  0, 30612, 6, 2, 4, 1081, 14, 0},  /* 1440x480i @59.94, cfg HDMI_1/c23 */
	{ 59341,    0, 1, 569, 1, 21965,  1, 46996, 6, 2, 4, 1187, 15, 0},  /* 1080i-var @59.34, cfg HDMI_1/c25 */
	{ 59400,    0, 1, 570, 1,  7864,  1, 46996, 6, 2, 4, 1188, 15, 0},  /* 1080i-var @59.4, cfg HDMI_1/c26 */
	{ 72000,    0, 0, 460, 1, 26214,  1, 14276, 2, 2, 4, 1440, 19, 0},  /* 1920x1080i@50 (VIC39), CMPLL0, cfg HDMI_1/c34 */
	{ 74176,    0, 1, 474, 1, 23765,  2, 14228, 2, 2, 4, 1484, 20, 0},  /* 720p60 / 1080i60 @59.94, cfg HDMI_1/c36 */
	{ 74250,    0, 1, 475, 1,  6553,  2, 14228, 2, 2, 4, 1485, 20, 0},  /* 1280x720@60 / 1080i60, cfg HDMI_1/c37 (vendor proto83) */
	{108000,    0, 1, 518, 1, 13107,  1, 30612, 4, 2, 3, 1080, 14, 0},  /* 2880 @50/60, cfg HDMI_2/c10 */
	{108100,    0, 1, 518, 1, 30094,  0, 30612, 4, 2, 3, 1081, 14, 0},  /* 2880 @59.94, cfg HDMI_2/c11 */
	{108108,    0, 1, 518, 1, 30094,  0, 30612, 4, 2, 3, 1081, 14, 0},  /* 2880 @60, cfg HDMI_2/c11 */
	{148352,    0, 1, 474, 1, 23765,  2, 14228, 2, 1, 3, 1484, 20, 0},  /* 1920x1080p@59.94, cfg HDMI_2/c37 */
	{148500,    0, 1, 475, 1,  6553,  2, 14228, 2, 1, 3, 1485, 20, 0},  /* 1920x1080p@60, cfg HDMI_2/c38 (vendor proto140) */
	{198000,    0, 1, 633, 1, 19660,  2, 63396, 2, 1, 2,  990, 12, 1},  /* 2560x1440@50, cfg HDMI_2/c68 (vendor proto170) */
	{241500,    0, 1, 579, 1, 39322,  2, 46996, 4, 1, 2, 1208, 16, 0},  /* 2560x1440@60 (CVT-RB, 241.5MHz), derived from HDMI_3 c28(243MHz) layout */
	{296703,    0, 1, 474, 1, 23765,  2, 14228, 2, 0, 2, 1484, 20, 0},  /* 3840x2160@29.97, cfg HDMI_3/c53 */
	{297000,    0, 1, 475, 1,  6553,  2, 14228, 2, 0, 2, 1485, 20, 0},  /* 3840x2160@30 (VIC95), cfg HDMI_3/c54 */
	{594000,    1, 1, 475, 1,  6553,  2, 14228, 1, 0, 1, 1485, 20, 0},  /* 3840x2160@60 (VIC97), cfg HDMI_6/c31 (vendor proto333) */
};

struct aks_phy *aks_phy_instance;
EXPORT_SYMBOL(aks_phy_instance);

const struct hdmi_phy_mode *aks_phy_find_mode(u32 pixel_clk_khz, u8 tmds_ratio)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(phy_modes); i++) {
		if (phy_modes[i].pixel_clk_khz == pixel_clk_khz &&
		    phy_modes[i].tmds_ratio == tmds_ratio)
			return &phy_modes[i];
	}
	return NULL;
}

struct aks_phy *aks_phy_get_instance(void)
{
	return aks_phy_instance;
}

static void aks_top_write(struct aks_phy *phy, u32 offset, u32 val)
{
	writel(val, phy->top_regs + offset);
}

static u32 aks_top_read(struct aks_phy *phy, u32 offset)
{
	return readl(phy->top_regs + offset);
}

static void aks_top_write_mask(struct aks_phy *phy, u32 offset, u32 mask, u32 val)
{
	u32 tmp = readl(phy->top_regs + offset);
	tmp &= ~mask;
	tmp |= (val & mask);
	writel(tmp, phy->top_regs + offset);
}

static void aks_icp_write(struct aks_phy *phy, u32 offset, u32 val)
{
	writel(val, phy->icp_base + offset);
}

static u32 aks_icp_read(struct aks_phy *phy, u32 offset)
{
	return readl(phy->icp_base + offset);
}

static void aks_icp_write_mask(struct aks_phy *phy, u32 offset, u32 mask, u32 val)
{
	u32 tmp = readl(phy->icp_base + offset);

	tmp &= ~mask;
	tmp |= (val & mask);
	writel(tmp, phy->icp_base + offset);
}

static void aks_pcs_write(struct aks_phy *phy, u32 offset, u32 val)
{
	writel(val, phy->pcs_base + offset);
}

static u32 aks_pcs_read(struct aks_phy *phy, u32 offset)
{
	return readl(phy->pcs_base + offset);
}

static void aks_icp_write_lane(struct aks_phy *phy, u16 block_base, int lane, u16 word_off, u32 val)
{
	u32 byte_off = icp_lane_offset(block_base + word_off, lane);
	aks_icp_write(phy, byte_off, val);
}

static void aks_lcfg_write_lane(struct aks_phy *phy, u16 word_addr, int lane, u32 val)
{
	u32 byte_off = icp_lane_offset(word_addr, lane);
	aks_icp_write(phy, byte_off, val);
}

static int aks_sram_init_done_wait(struct aks_phy *phy)
{
	u32 val;
	int timeout = SRAM_INIT_TIMEOUT_MS;

	while (timeout--) {
		val = aks_top_read(phy, TOP_PHY_SRAM_CTRL);
		if (val & SRAM_INIT_DONE)
			return 0;
		usleep_range(1000, 2000);
	}

	dev_err(phy->dev, "sram_init_done timeout\n");
	return -ETIMEDOUT;
}

static int aks_sram_load_fw(struct aks_phy *phy, const u16 *fw_data, int fw_size)
{
	int i;
	u32 addr_offset;

	if (fw_size > SRAM_SIZE_WORDS) {
		dev_err(phy->dev, "fw size %d exceeds SRAM capacity %d\n",
			fw_size, SRAM_SIZE_WORDS);
		return -EINVAL;
	}

	for (i = 0; i < fw_size; i++) {
		addr_offset = i << 2;
		aks_icp_write(phy, HDMI_ICP_SRAM_OFF + addr_offset, fw_data[i]);
	}

	dev_info(phy->dev, "SRAM fw loaded: %d words\n", fw_size);
	return 0;
}

static void aks_phy_ate_disable_fw(struct aks_phy *phy)
{
	int lane;

	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++) {
		aks_lcfg_write_lane(phy, PPU_LCFG_MCU_JMP_BANK, lane, 0x0);
		aks_lcfg_write_lane(phy, PPU_LCFG_MCU_OVRD_CTL, lane,
			BIT(14) | BIT(12));
	}

	aks_icp_write(phy, ICP_PPUAOFG_ATE_CONFIG0 * 4, BIT(0));
}

static void aks_phy_analog_config(struct aks_phy *phy)
{
	u32 byte_off;

	byte_off = ICP_SUP_DIG_IN_TX_VBOOST_LVL * 4;
	aks_icp_write(phy, byte_off, BIT(3) | TX_VBOOST_LVL);

	byte_off = ICP_SUP_DIG_IN_BG_CTL * 4;
	aks_icp_write(phy, byte_off,
		BIT(7) | (RX_VREF_CTRL << 2));
}

static void aks_phy_cmpll0_config(struct aks_phy *phy, const struct hdmi_phy_mode *mode)
{
	bool cmpll0_active = (mode->cmpll_sel == 0);
	u32 byte_off;

	byte_off = ICP_SUP_DIG_IN_CMPLL0_WORD_CLK_EN * 4;
	aks_icp_write(phy, byte_off, BIT(1) | (cmpll0_active ? BIT(0) : 0));

	byte_off = ICP_SUP_DIG_IN_CMPLL0_CTL * 4;
	aks_icp_write(phy, byte_off,
		BIT(7) |
		(cmpll0_active ? CMPLL0_W_CLK_SEL << 5 : 0) |
		(cmpll0_active ? mode->tx_clk_div << 2 : 0) |
		(cmpll0_active ? CMPLL0_REF_DIV : 0));

	byte_off = ICP_PPUPSFG_CMPLLC_3_SR_0 * 4;
	aks_icp_write(phy, byte_off,
		BIT(12) |
		(cmpll0_active ? mode->freq_ctrl << 2 : 0) |
		BIT(1) |
		(cmpll0_active && mode->frac_en ? BIT(0) : 0));

	byte_off = ICP_SUP_DIG_IN_CMPLL0_DIV_FREQ_CTL * 4;
	aks_icp_write(phy, byte_off, BIT(8));

	byte_off = ICP_PPUPSFG_CMPLL_FRAC_2_SR_0_SPLIT0 * 4;
	aks_icp_write(phy, byte_off, cmpll0_active ? mode->frac_quot : 0);
	byte_off = ICP_PPUPSFG_CMPLL_FRAC_2_SR_0_SPLIT1 * 4;
	aks_icp_write(phy, byte_off, BIT(0));

	byte_off = ICP_PPUPSFG_CMPLL_FRAC_3_SR_0_SPLIT0 * 4;
	aks_icp_write(phy, byte_off, cmpll0_active ? mode->frac_rem : 0);
	byte_off = ICP_PPUPSFG_CMPLL_FRAC_3_SR_0_SPLIT1 * 4;
	aks_icp_write(phy, byte_off, BIT(0));

	byte_off = ICP_SUP_DIG_IN_CMPLL0_SSC * 4;
	aks_icp_write(phy, byte_off, BIT(2));

	byte_off = ICP_SUP_DIG_IN_CMPLL0_DIV_CLK_EN * 4;
	aks_icp_write(phy, byte_off, BIT(1));

	byte_off = ICP_SUP_DIG_IN_CMPLL0_WORD_CLK_EN * 4;
	aks_icp_write(phy, byte_off, BIT(1) | (cmpll0_active ? BIT(0) : 0));

	aks_icp_write_mask(phy, ICP_PPUPSFG_CMPLLC_2_SR * 4,
		BIT(3) | BIT(2), BIT(3));

	aks_icp_write_mask(phy, ICP_PPUPSFG_CMPLL_SSC_2_SR * 4,
		BIT(5) | BIT(4) | BIT(3),
		BIT(5) | (CMPLL0_REF_DIV << 3));
}

static void aks_phy_cmpll1_config(struct aks_phy *phy, const struct hdmi_phy_mode *mode)
{
	bool cmpll1_active = (mode->cmpll_sel == 1);
	u32 byte_off;

	aks_icp_write_mask(phy, ICP_PPUPSFG_CMPLL_SSC_2_SR * 4,
		BIT(2) | BIT(1) | BIT(0),
		BIT(2) | CMPLL1_REF_DIV);

	byte_off = ICP_SUP_DIG_IN_CMPLL1_WORD_CLK_EN * 4;
	aks_icp_write(phy, byte_off, BIT(1) | (cmpll1_active ? BIT(0) : 0));

	byte_off = ICP_SUP_DIG_IN_CMPLL1_CTL * 4;
	aks_icp_write(phy, byte_off,
		BIT(7) |
		(cmpll1_active ? CMPLL1_W_CLK_SEL << 5 : 0) |
		(cmpll1_active ? mode->tx_clk_div << 2 : 0) |
		(cmpll1_active ? CMPLL1_REF_DIV : 0));

	byte_off = ICP_PPUPSFG_CMPLLC_3_SR_1 * 4;
	aks_icp_write(phy, byte_off,
		BIT(12) |
		(cmpll1_active ? mode->freq_ctrl << 2 : 0) |
		BIT(1) |
		(cmpll1_active && mode->frac_en ? BIT(0) : 0));

	byte_off = ICP_PPUPSFG_CMPLL_FRAC_2_SR_1_SPLIT0 * 4;
	aks_icp_write(phy, byte_off, cmpll1_active ? mode->frac_quot : 0);
	byte_off = ICP_PPUPSFG_CMPLL_FRAC_2_SR_1_SPLIT1 * 4;
	aks_icp_write(phy, byte_off, BIT(0));

	byte_off = ICP_PPUPSFG_CMPLL_FRAC_3_SR_1_SPLIT0 * 4;
	aks_icp_write(phy, byte_off, cmpll1_active ? mode->frac_rem : 0);
	byte_off = ICP_PPUPSFG_CMPLL_FRAC_3_SR_1_SPLIT1 * 4;
	aks_icp_write(phy, byte_off, BIT(0));

	byte_off = ICP_SUP_DIG_IN_CMPLL1_SSC * 4;
	aks_icp_write(phy, byte_off, BIT(2));

	byte_off = ICP_SUP_DIG_IN_CMPLL1_SSC_PEAK_15_0 * 4;
	aks_icp_write(phy, byte_off, 0);

	byte_off = ICP_SUP_DIG_IN_CMPLL1_SSC_PEAK_19_16 * 4;
	aks_icp_write(phy, byte_off, 0);

	byte_off = ICP_SUP_DIG_IN_CMPLL1_SSC_STEP_SIZE_15_0 * 4;
	aks_icp_write(phy, byte_off, 0);

	byte_off = ICP_SUP_DIG_IN_CMPLL1_SSC_STEP_SIZE_20_16 * 4;
	aks_icp_write(phy, byte_off, 0);

	byte_off = ICP_SUP_DIG_IN_CMPLL1_DIV_FREQ_CTL * 4;
	aks_icp_write(phy, byte_off, BIT(8));

	byte_off = ICP_SUP_DIG_IN_CMPLL1_DIV_CLK_EN * 4;
	aks_icp_write(phy, byte_off, BIT(1));

	byte_off = ICP_SUP_DIG_IN_CMPLL1_WORD_CLK_EN * 4;
	aks_icp_write(phy, byte_off, BIT(1) | (cmpll1_active ? BIT(0) : 0));

	aks_icp_write_mask(phy, ICP_PPUPSFG_CMPLLC_2_SR * 4,
		BIT(1) | BIT(0), BIT(1));
}

#if 0
static void aks_phy_cmpll_force_en_override(struct aks_phy *phy)
{
	u32 byte_off;

	byte_off = ICP_PPUAOFG_CMPLLC * 4;
	aks_icp_write(phy, byte_off, BIT(7) | BIT(5));
}
#endif

static void aks_phy_rx_eq_config(struct aks_phy *phy, const struct hdmi_phy_mode *mode)
{
	int lane;

	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++) {

		aks_lcfg_write_lane(phy, PPU_LCFG_RX_EQ_INFO_4_SR, lane,
			(RX_VCO_CAL_CFG_R << 2) | BIT(9));

		aks_lcfg_write_lane(phy, PPU_LCFG_RX_EQ_INFO_1_SR, lane,
			BIT(15) | (mode->rx_vco_cal_cfg_t << 2) | BIT(1) | RX_DFE_BYPASS);

		aks_lcfg_write_lane(phy, PPU_LCFG_RX_EQ_INFO_5_SR, lane,
			mode->rx_cdr_detect_th | BIT(5));
	}
}

static void aks_phy_rx_cdr_vco_band_config(struct aks_phy *phy, const struct hdmi_phy_mode *mode)
{
	int lane;

	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++) {
		u32 byte_off = icp_lane_offset(ICP_LANE_OVRD_RX_ANA_VCO_OVRD0, lane);

		aks_icp_write_mask(phy, byte_off,
			RX_CDR_VCO_BAND_ATOP | RX_CDR_VCO_BAND_ATOP_OVRD_EN,
			(mode->rx_cdr_vco_band ? RX_CDR_VCO_BAND_ATOP : 0) |
			RX_CDR_VCO_BAND_ATOP_OVRD_EN);
	}
}

static void aks_phy_bwc_config(struct aks_phy *phy, const struct hdmi_phy_mode *mode)
{
	u32 byte_off;
	bool cmpll0_active = (mode->cmpll_sel == 0);
	bool cmpll1_active = (mode->cmpll_sel == 1);

	byte_off = ICP_PPUPSFG_CMPLLC_1_SR_0_SPLIT0 * 4;
	aks_icp_write(phy, byte_off, cmpll0_active ? mode->bwc : 0);
	byte_off = ICP_PPUPSFG_CMPLLC_1_SR_0_SPLIT1 * 4;
	aks_icp_write(phy, byte_off, BIT(0));

	byte_off = ICP_PPUPSFG_CMPLLC_1_SR_1_SPLIT0 * 4;
	aks_icp_write(phy, byte_off, cmpll1_active ? mode->bwc : 0);
	byte_off = ICP_PPUPSFG_CMPLLC_1_SR_1_SPLIT1 * 4;
	aks_icp_write(phy, byte_off, BIT(0));
}

static void aks_phy_tx_lane_config(struct aks_phy *phy, const struct hdmi_phy_mode *mode)
{
	int lane;

	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++) {

		aks_lcfg_write_lane(phy, PPU_LCFG_TX_MISC_0_SR, lane,
			mode->tx_rate | BIT(3) |
			(mode->cmpll_sel << 4) | BIT(5) |
			(TX_IBOOST_CODE << 6) | BIT(10) |
			(TX_IBOOST_EN << 11) | BIT(12));

		aks_lcfg_write_lane(phy, PPU_LCFG_TX_MISC_1_SR, lane,
			(TX_WIDTH << 8) | BIT(10) |
			(TX_TERM_SEL << 11) | BIT(14));

		aks_icp_write_lane(phy, ICP_LANE_OVRD_TX_PPU_EQ_OVRD0, lane, 0,
			TX_CTRL_MAIN | BIT(6));
		aks_icp_write_lane(phy, ICP_LANE_OVRD_TX_PPU_EQ_OVRD1, lane, 0,
			TX_CTRL_PRE | BIT(6) |
			(TX_CTRL_POST << 7) | BIT(13));
	}

}

static void aks_phy_rx_lane_config(struct aks_phy *phy, const struct hdmi_phy_mode *mode)
{
	int lane;

	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++) {

		aks_lcfg_write_lane(phy, PPU_LCFG_RX_MISC_INFO_0_SR, lane,
			(RX_WIDTH << 0) | BIT(2));
		aks_lcfg_write_lane(phy, PPU_LCFG_RX_EQ_INFO_5_SR_PROC0, lane,
			(mode->rx_rate << 6) | BIT(9));
	}
}

static void aks_phy_rx_cdr_override(struct aks_phy *phy)
{
	int lane;

	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++) {
		u32 byte_off = icp_lane_offset(ICP_LANE_OVRD_RX_PPU_CDR_OVRD1, lane);

		aks_icp_write_mask(phy, byte_off,
			BIT(3) | BIT(2) | BIT(1) | BIT(0),
			BIT(3) | BIT(1));
	}
}

static void aks_phy_xcvr_mode(struct aks_phy *phy)
{
	int lane;

	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++) {
		u32 byte_off = icp_lane_offset(ICP_PPUAOFG_MUX_REV_TRANS_CTRL, lane);
		aks_icp_write(phy, byte_off, BIT(1) | (1 << 0));
	}
}

static void aks_pcs_psfg_phy_param_config(struct aks_phy *phy, const struct hdmi_phy_mode *mode)
{
	u32 w_clk_div = mode->cmpll_sel ? CMPLL1_W_CLK_DIV : CMPLL0_W_CLK_DIV;
	u32 word_div2_en = (w_clk_div >> 1) & 1;
	u32 val = (mode->cmpll_sel << 4) | (mode->tx_rate << 1) | word_div2_en;
	aks_pcs_write(phy, pcs_psfg_byte_off(PCS_PSFG_PHY_PARAM_CFG), val);
}

static void aks_pcs_psfg_lane_clk_ctrl_config(struct aks_phy *phy, u8 color_depth, u8 pixel_rep)
{
	u32 val;
	if (pixel_rep == 0)
		return;
	val = aks_pcs_read(phy, pcs_psfg_byte_off(PCS_PSFG_LANE_CLK_CTRL_SETTING));
	val &= ~GENMASK(5, 2);
	val |= (1u << 2);
	aks_pcs_write(phy, pcs_psfg_byte_off(PCS_PSFG_LANE_CLK_CTRL_SETTING), val);
}

static void aks_pcs_lane_hmdi_ratio_config(struct aks_phy *phy, u8 tmds_ratio)
{
	int lane;
	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++)
		aks_pcs_write(phy, pcs_lane_byte_off(PCS_LANE_HDMI_RATIO_OVRD, lane), tmds_ratio);
}

static int aks_pcs_wait_phystatus(struct aks_phy *phy)
{
	int lane;
	int timeout;
	u32 val;

	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++) {
		timeout = ICP_TIMEOUT_MS * 10;
		do {
			val = aks_pcs_read(phy,
				pcs_lane_byte_off(PCS_LANE_CTRL_IN_OUT, lane));
			if (!(val & BIT(12)))
				break;
			usleep_range(100, 200);
		} while (--timeout);
		if (!timeout) {
			dev_err(phy->dev, "lane%d phystatus de-assert timeout\n", lane);
			return -ETIMEDOUT;
		}
	}
	return 0;
}

static void aks_phy_dump_regs(struct aks_phy *phy)
{
	static const struct { const char *name; u32 off; } top_regs[] = {
		{ "TOP_PHY_RESET",     TOP_PHY_RESET },
		{ "TOP_PIPE_TX0_CTRL", TOP_PIPE_TX0_CTRL },
		{ "TOP_PIPE_TX1_CTRL", TOP_PIPE_TX1_CTRL },
		{ "TOP_PIPE_TX2_CTRL", TOP_PIPE_TX2_CTRL },
		{ "TOP_PIPE_TX3_CTRL", TOP_PIPE_TX3_CTRL },
		{ "TOP_PHY_MODE",      TOP_PHY_MODE },
		{ "TOP_PHY_SRAM_CTRL", TOP_PHY_SRAM_CTRL },
		{ "TOP_REF_CLK_CTRL",  TOP_REF_CLK_CTRL },
		{ "TOP_PHY_CMP_CTRL",  TOP_PHY_CMP_CTRL },
	};
	static const struct { const char *name; u16 woff; } icp_regs[] = {
		{ "ICP_SUP_DIG_IN_REF_CLK_CTL",            ICP_SUP_DIG_IN_REF_CLK_CTL },
		{ "ICP_SUP_DIG_IN_CMPLL0_WORD_CLK_EN",     ICP_SUP_DIG_IN_CMPLL0_WORD_CLK_EN },
		{ "ICP_SUP_DIG_IN_CMPLL0_DIV_CLK_EN",      ICP_SUP_DIG_IN_CMPLL0_DIV_CLK_EN },
		{ "ICP_SUP_DIG_IN_CMPLL0_CTL",             ICP_SUP_DIG_IN_CMPLL0_CTL },
		{ "ICP_SUP_DIG_IN_CMPLL0_DIV_FREQ_CTL",    ICP_SUP_DIG_IN_CMPLL0_DIV_FREQ_CTL },
		{ "ICP_SUP_DIG_IN_CMPLL0_SSC",             ICP_SUP_DIG_IN_CMPLL0_SSC },
		{ "ICP_PPUPSFG_CMPLLC_1_SR_0_SPLIT0",      ICP_PPUPSFG_CMPLLC_1_SR_0_SPLIT0 },
		{ "ICP_PPUPSFG_CMPLLC_1_SR_0_SPLIT1",      ICP_PPUPSFG_CMPLLC_1_SR_0_SPLIT1 },
		{ "ICP_PPUPSFG_CMPLLC_1_SR_1_SPLIT0",      ICP_PPUPSFG_CMPLLC_1_SR_1_SPLIT0 },
		{ "ICP_PPUPSFG_CMPLLC_1_SR_1_SPLIT1",      ICP_PPUPSFG_CMPLLC_1_SR_1_SPLIT1 },
		{ "ICP_PPUPSFG_CMPLLC_2_SR",               ICP_PPUPSFG_CMPLLC_2_SR },
		{ "ICP_PPUPSFG_CMPLLC_3_SR_0",             ICP_PPUPSFG_CMPLLC_3_SR_0 },
		{ "ICP_PPUPSFG_CMPLLC_3_SR_1",             ICP_PPUPSFG_CMPLLC_3_SR_1 },
		{ "ICP_PPUPSFG_CMPLL_FRAC_2_SR_0_SPLIT0",  ICP_PPUPSFG_CMPLL_FRAC_2_SR_0_SPLIT0 },
		{ "ICP_PPUPSFG_CMPLL_FRAC_2_SR_0_SPLIT1",  ICP_PPUPSFG_CMPLL_FRAC_2_SR_0_SPLIT1 },
		{ "ICP_PPUPSFG_CMPLL_FRAC_2_SR_1_SPLIT0",  ICP_PPUPSFG_CMPLL_FRAC_2_SR_1_SPLIT0 },
		{ "ICP_PPUPSFG_CMPLL_FRAC_2_SR_1_SPLIT1",  ICP_PPUPSFG_CMPLL_FRAC_2_SR_1_SPLIT1 },
		{ "ICP_PPUPSFG_CMPLL_FRAC_3_SR_0_SPLIT0",  ICP_PPUPSFG_CMPLL_FRAC_3_SR_0_SPLIT0 },
		{ "ICP_PPUPSFG_CMPLL_FRAC_3_SR_0_SPLIT1",  ICP_PPUPSFG_CMPLL_FRAC_3_SR_0_SPLIT1 },
		{ "ICP_PPUPSFG_CMPLL_FRAC_3_SR_1_SPLIT0",  ICP_PPUPSFG_CMPLL_FRAC_3_SR_1_SPLIT0 },
		{ "ICP_PPUPSFG_CMPLL_FRAC_3_SR_1_SPLIT1",  ICP_PPUPSFG_CMPLL_FRAC_3_SR_1_SPLIT1 },
		{ "ICP_PPUPSFG_CMPLL_SSC_2_SR",            ICP_PPUPSFG_CMPLL_SSC_2_SR },
		{ "ICP_SUP_DIG_IN_CMPLL1_WORD_CLK_EN",     ICP_SUP_DIG_IN_CMPLL1_WORD_CLK_EN },
		{ "ICP_SUP_DIG_IN_CMPLL1_DIV_CLK_EN",      ICP_SUP_DIG_IN_CMPLL1_DIV_CLK_EN },
		{ "ICP_SUP_DIG_IN_CMPLL1_CTL",             ICP_SUP_DIG_IN_CMPLL1_CTL },
		{ "ICP_SUP_DIG_IN_CMPLL1_DIV_FREQ_CTL",    ICP_SUP_DIG_IN_CMPLL1_DIV_FREQ_CTL },
		{ "ICP_SUP_DIG_IN_CMPLL1_SSC",             ICP_SUP_DIG_IN_CMPLL1_SSC },
		{ "ICP_SUP_DIG_IN_CMPLL1_SSC_STEP_SIZE_20_16", ICP_SUP_DIG_IN_CMPLL1_SSC_STEP_SIZE_20_16 },
		{ "ICP_SUP_DIG_IN_CMPLL1_SSC_STEP_SIZE_15_0",  ICP_SUP_DIG_IN_CMPLL1_SSC_STEP_SIZE_15_0 },
		{ "ICP_SUP_DIG_IN_CMPLL1_SSC_PEAK_19_16",  ICP_SUP_DIG_IN_CMPLL1_SSC_PEAK_19_16 },
		{ "ICP_SUP_DIG_IN_CMPLL1_SSC_PEAK_15_0",   ICP_SUP_DIG_IN_CMPLL1_SSC_PEAK_15_0 },
		{ "ICP_SUP_DIG_IN_BG_CTL",                 ICP_SUP_DIG_IN_BG_CTL },
		{ "ICP_SUP_DIG_IN_TX_VBOOST_LVL",          ICP_SUP_DIG_IN_TX_VBOOST_LVL },
		{ "ICP_PPUAOFG_RESCAL_INFO0_RO",           ICP_PPUAOFG_RESCAL_INFO0_RO },
	};
	static const struct { const char *name; u16 woff; } icp_lane_regs[] = {
		{ "PPU_LCFG_RX_EQ_INFO_4_SR",       PPU_LCFG_RX_EQ_INFO_4_SR },
		{ "PPU_LCFG_RX_EQ_INFO_1_SR",       PPU_LCFG_RX_EQ_INFO_1_SR },
		{ "PPU_LCFG_RX_EQ_INFO_5_SR",       PPU_LCFG_RX_EQ_INFO_5_SR },
		{ "PPU_LCFG_RX_EQ_INFO_5_SR_PROC0", PPU_LCFG_RX_EQ_INFO_5_SR_PROC0 },
		{ "PPU_LCFG_RX_MISC_INFO_0_SR",     PPU_LCFG_RX_MISC_INFO_0_SR },
		{ "PPU_LCFG_TX_MISC_0_SR",          PPU_LCFG_TX_MISC_0_SR },
		{ "PPU_LCFG_TX_MISC_1_SR",          PPU_LCFG_TX_MISC_1_SR },
		{ "PPU_LCFG_TXRX_TERM_SEL_OVRD_RO", PPU_LCFG_TXRX_TERM_SEL_OVRD_RO },
		{ "ICP_LANE_TX_PPU_GP_MON0",        ICP_LANE_TX_PPU_GP_MON0 },
		{ "ICP_LANE_TX_PPU_GP_MON1",        ICP_LANE_TX_PPU_GP_MON1 },
		{ "ICP_LANE_TX_PPU_GP_MON2",        ICP_LANE_TX_PPU_GP_MON2 },
		{ "ICP_LANE_ANA_TX_VBOOST",         ICP_LANE_ANA_TX_VBOOST },
		{ "ICP_LANE_OVRD_TX_PPU_EQ_OVRD0",  ICP_LANE_OVRD_TX_PPU_EQ_OVRD0 },
		{ "ICP_LANE_OVRD_TX_PPU_EQ_OVRD1",  ICP_LANE_OVRD_TX_PPU_EQ_OVRD1 },
		{ "ICP_LANE_OVRD_RX_PPU_CDR_OVRD1", ICP_LANE_OVRD_RX_PPU_CDR_OVRD1 },
		{ "ICP_LANE_TX_PPU_GP_OUT_MON0",    ICP_LANE_TX_PPU_GP_OUT_MON0 },
	};
	static const struct { const char *name; u32 woff; } pcs_psfg_regs[] = {
		{ "PCS_PSFG_PHY_PARAM_CFG",          PCS_PSFG_PHY_PARAM_CFG },
		{ "PCS_PSFG_LANE_CLK_CTRL_SETTING",  PCS_PSFG_LANE_CLK_CTRL_SETTING },
	};
	static const struct { const char *name; u32 woff; } pcs_lane_regs[] = {
		{ "PCS_LANE_HDMI_RATIO_OVRD", PCS_LANE_HDMI_RATIO_OVRD },
		{ "PCS_LANE_CTRL_IN_OUT",     PCS_LANE_CTRL_IN_OUT },
	};
	int i, lane;
	u32 off;
	char nb[56];

	dev_dbg(phy->dev, "[AKS PHY] ===== reg dump (phy_status ok) =====\n");
	dev_dbg(phy->dev, "[AKS PHY] %-48s %-10s   %s\n",
		"NAME", "REG_OFF", "VALUE");

	/*
	 * REG_OFF is the register manual "Address Offset" (word offset for
	 * ICP/PCS, byte offset for TOP wrapper) so values line up directly
	 * with the datasheet without recomputing (base + woff*4).
	 */
	for (i = 0; i < ARRAY_SIZE(top_regs); i++)
		dev_dbg(phy->dev, "[AKS PHY] %-48s 0x%06x = 0x%08x\n",
			top_regs[i].name,
			top_regs[i].off,
			aks_top_read(phy, top_regs[i].off));

	for (i = 0; i < ARRAY_SIZE(icp_regs); i++) {
		off = icp_regs[i].woff * 4;
		dev_dbg(phy->dev, "[AKS PHY] %-48s 0x%06x = 0x%08x\n",
			icp_regs[i].name,
			icp_regs[i].woff,
			aks_icp_read(phy, off));
	}

	for (i = 0; i < ARRAY_SIZE(icp_lane_regs); i++) {
		for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++) {
			off = icp_lane_offset(icp_lane_regs[i].woff, lane);
			snprintf(nb, sizeof(nb), "%s lane%d",
				 icp_lane_regs[i].name, lane);
			dev_dbg(phy->dev, "[AKS PHY] %-48s 0x%06x = 0x%08x\n",
				nb,
				icp_lane_regs[i].woff + lane * 0x200,
				aks_icp_read(phy, off));
		}
	}

	for (i = 0; i < ARRAY_SIZE(pcs_psfg_regs); i++) {
		off = pcs_psfg_byte_off(pcs_psfg_regs[i].woff);
		dev_dbg(phy->dev, "[AKS PHY] %-48s 0x%06x = 0x%08x\n",
			pcs_psfg_regs[i].name,
			pcs_psfg_regs[i].woff,
			aks_pcs_read(phy, off));
	}

	for (i = 0; i < ARRAY_SIZE(pcs_lane_regs); i++) {
		for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++) {
			off = pcs_lane_byte_off(pcs_lane_regs[i].woff, lane);
			snprintf(nb, sizeof(nb), "%s lane%d",
				 pcs_lane_regs[i].name, lane);
			dev_dbg(phy->dev, "[AKS PHY] %-48s 0x%06x = 0x%08x\n",
				nb,
				pcs_lane_regs[i].woff + lane * PCS_LANE_WORD_STRIDE,
				aks_pcs_read(phy, off));
		}
	}

	dev_dbg(phy->dev, "[AKS PHY] ===== reg dump end =====\n");
}

static void dw_hdmi_cvitek_platform_init(void)
{
	void __iomem *p;
	u32 val;

	/* HDMI pad pinmux: 0x28104304 = 0x3838 */
	p = ioremap(0x28104304, 4);
	if (p) {
		writel(0x3838, p);
		iounmap(p);
	}

	/* clk/reset enable: set bit4 of 0x27012004 and 0x27012000 */
	p = ioremap(0x27012000, 8);
	if (p) {
		val = readl(p + 0x4);
		writel(val | 0x10, p + 0x4);

		val = readl(p + 0x0);
		writel(val | 0x10, p + 0x0);
		iounmap(p);
	}
}


int aks_phy_probe_init(struct aks_phy *phy)
{
	int ret;
	u32 val;

	dev_info(phy->dev, "aks_phy_probe_init: enter, initialized=%d\n",
		 phy->initialized);

	if (phy->initialized) {
		dev_info(phy->dev, "aks_phy_probe_init: already initialized, skip\n");
		return 0;
	}

	mutex_lock(&phy->lock);

	dev_info(phy->dev, "aks_phy_probe_init: platform init start\n");
	dw_hdmi_cvitek_platform_init();
	dev_info(phy->dev, "aks_phy_probe_init: platform init done\n");

	dev_info(phy->dev, "aks_phy_probe_init: config TOP_PHY_MODE, pipe tx ctrl\n");
	aks_top_write_mask(phy, TOP_PHY_MODE, GENMASK(4, 0), BIT(4));

	aks_top_write_mask(phy, TOP_PIPE_TX0_CTRL, BIT(10) | GENMASK(9, 6), 0 | (3 << 6));
	aks_top_write_mask(phy, TOP_PIPE_TX1_CTRL, BIT(10) | GENMASK(9, 6), 0 | (3 << 6));
	aks_top_write_mask(phy, TOP_PIPE_TX2_CTRL, BIT(10) | GENMASK(9, 6), 0 | (3 << 6));
	aks_top_write_mask(phy, TOP_PIPE_TX3_CTRL, BIT(10) | GENMASK(9, 6), 0 | (3 << 6));

	aks_top_write_mask(phy, TOP_REF_CLK_CTRL, BIT(0), 0);
	aks_top_write_mask(phy, TOP_PHY_CMP_CTRL, GENMASK(5, 0), BIT(4));

	aks_top_write_mask(phy, TOP_PHY_RESET, BIT(1) | BIT(0), 0);

	usleep_range(15, 20);

	dev_info(phy->dev, "aks_phy_probe_init: wait SRAM init done\n");
	ret = aks_sram_init_done_wait(phy);
	if (ret) {
		dev_err(phy->dev, "aks_phy_probe_init: SRAM init done wait failed\n");
		goto unlock;
	}
	dev_info(phy->dev, "aks_phy_probe_init: SRAM init done OK\n");

	usleep_range(2500, 3000);

	val = SRAM_INIT_BYP;
	aks_top_write_mask(phy, TOP_PHY_SRAM_CTRL,
			   SRAM_INIT_BYP | SRAM_STARTUP_BYP,
			   val);

	dev_info(phy->dev, "aks_phy_probe_init: load SRAM fw, size=%d\n",
		 AKS_PHY_FW_SIZE);
	ret = aks_sram_load_fw(phy, aks_phy_fw_data, AKS_PHY_FW_SIZE);
	if (ret) {
		dev_err(phy->dev, "aks_phy_probe_init: SRAM fw load failed\n");
		goto unlock;
	}
	dev_info(phy->dev, "aks_phy_probe_init: SRAM fw load OK\n");

	/* [vendor power-up seq] ld done + reset release + cal + tx_ack moved to aks_phy_configure (must run after per-mode config) */
	phy->initialized = true;
	dev_info(phy->dev, "AKS PHY probe init done (SRAM loaded)\n");

unlock:
	mutex_unlock(&phy->lock);
	return ret;
}

int aks_phy_configure(struct aks_phy *phy, u32 pixel_clk_khz, u8 tmds_ratio)
{
	int ret;
	int lane;
	const struct hdmi_phy_mode *mode;
	int tx_timeout;
	u32 tx_val;
	u32 reg_0x14, reg_0x20;

	dev_info(phy->dev, "aks_phy_configure: enter pixel_clk=%d tmds_ratio=%d\n",
		 pixel_clk_khz, tmds_ratio);

	mode = aks_phy_find_mode(pixel_clk_khz, tmds_ratio);
	if (!mode) {
		dev_err(phy->dev, "aks_phy_configure: unsupported mode: %d kHz ratio %d\n",
			pixel_clk_khz, tmds_ratio);
		return -EINVAL;
	}
	dev_info(phy->dev, "aks_phy_configure: found mode (pixel_clk=%d)\n",
		 mode->pixel_clk_khz);

	if (!phy->initialized) {
		dev_err(phy->dev, "aks_phy_configure: PHY not initialized, call probe_init first\n");
		return -EINVAL;
	}

	mutex_lock(&phy->lock);

	dev_info(phy->dev, "aks_phy_configure: start config cmpll0\n");
	aks_phy_cmpll0_config(phy, mode);
	dev_info(phy->dev, "aks_phy_configure: cmpll0 config done\n");

	dev_info(phy->dev, "aks_phy_configure: start config cmpll1\n");
	aks_phy_cmpll1_config(phy, mode);
	dev_info(phy->dev, "aks_phy_configure: cmpll1 config done\n");

	dev_info(phy->dev, "aks_phy_configure: start rx_eq/rx_cdr config\n");
	aks_phy_rx_eq_config(phy, mode);
	aks_phy_rx_cdr_vco_band_config(phy, mode);
	dev_info(phy->dev, "aks_phy_configure: rx_eq/rx_cdr config done\n");

	dev_info(phy->dev, "aks_phy_configure: start bwc config\n");
	aks_phy_bwc_config(phy, mode);
	dev_info(phy->dev, "aks_phy_configure: bwc config done\n");

	dev_info(phy->dev, "aks_phy_configure: start analog config\n");
	aks_phy_analog_config(phy);
	dev_info(phy->dev, "aks_phy_configure: analog config done\n");

	dev_info(phy->dev, "aks_phy_configure: start lane config\n");
	aks_phy_rx_lane_config(phy, mode);
	aks_phy_tx_lane_config(phy, mode);
	dev_info(phy->dev, "aks_phy_configure: lane config done\n");

	aks_phy_rx_cdr_override(phy);

	dev_info(phy->dev, "aks_phy_configure: start pcs config\n");
	aks_pcs_psfg_phy_param_config(phy, mode);
	aks_pcs_psfg_lane_clk_ctrl_config(phy, 0, 0);
	aks_pcs_lane_hmdi_ratio_config(phy, mode->tmds_ratio);
	dev_info(phy->dev, "aks_phy_configure: pcs config done\n");

	/* [vendor power-up seq] config written above (before ld done); ld done -> release reset(P3) -> cal -> wait tx_ack -> P0 */
	usleep_range(20000, 25000);

	dev_info(phy->dev, "[RES_DBG] before write: 0x22=0x%08x\n",
		aks_icp_read(phy, ICP_SUP_RES_CFG * 4));

	aks_icp_write_mask(phy, ICP_SUP_RES_CFG * 4,
		SUP_RES_TERM_CTRL_MASK,
		SUP_RES_TERM_40_7OHM << SUP_RES_TERM_CTRL_SHIFT);
	dev_info(phy->dev, "[RES_DBG] after cal_en: 0x22=0x%08x\n",
		aks_icp_read(phy, ICP_SUP_RES_CFG * 4));

	dev_info(phy->dev, "aks_phy_configure: set SRAM_LOAD_DONE\n");
	aks_top_write_mask(phy, TOP_PHY_SRAM_CTRL, SRAM_LOAD_DONE, SRAM_LOAD_DONE);

	usleep_range(21000, 25000);

	dev_info(phy->dev, "[RES_DBG] after LOAD_DONE: 0x22=0x%08x\n",
		aks_icp_read(phy, ICP_SUP_RES_CFG * 4));

	dev_info(phy->dev, "aks_phy_configure: release lane reset_n\n");
	/* release lane reset_n only, keep powerdown at P3 */
	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++)
		aks_top_write_mask(phy, TOP_PIPE_TX0_CTRL + lane * 4, BIT(10), BIT(10));

	dev_info(phy->dev, "aks_phy_configure: trigger per-lane cal\n");

	/* fw running now: trigger per-lane cal (ACC_TX_CAL_S bit10, ACC_RX_CAL_S bit0) */
	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++)
		aks_icp_write_mask(phy, icp_lane_offset(ICP_LANE_TX_PPU_GP_OUT_MON0, lane), BIT(10), BIT(10));

	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++)
		aks_icp_write_mask(phy, icp_lane_offset(ICP_LANE_TX_PPU_GP_OUT_MON0, lane), BIT(0), BIT(0));

	msleep(100);

	dev_info(phy->dev, "[RES_DBG] after fw cal: 0x22=0x%08x\n",
		aks_icp_read(phy, ICP_SUP_RES_CFG * 4));
	dev_info(phy->dev, "[RES_DBG] rescal_info(0x18b)=0x%08x rx_stat(0x2b)=0x%08x txdn_stat(0x2c)=0x%08x txup_stat(0x2d)=0x%08x fsm(0x2f)=0x%08x\n",
		aks_icp_read(phy, ICP_PPUAOFG_RESCAL_INFO0_RO * 4),
		aks_icp_read(phy, 0x2b * 4),
		aks_icp_read(phy, 0x2c * 4),
		aks_icp_read(phy, 0x2d * 4),
		aks_icp_read(phy, 0x2f * 4));

	/* wait each lane cal/init done (tx_ack = MON0 bit0 de-assert) BEFORE switching to P0 */
	dev_info(phy->dev, "aks_phy_configure: wait lane cal/init done\n");
	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++) {
		tx_timeout = ICP_TIMEOUT_MS * 10;
		dev_info(phy->dev, "aks_phy_configure: wait lane%d tx_ack\n", lane);
		do {
			tx_val = aks_icp_read(phy,
				icp_lane_offset(ICP_LANE_TX_PPU_GP_OUT_MON0, lane));
			if (!(tx_val & BIT(0)))
				break;
			usleep_range(100, 200);
		} while (--tx_timeout);

		if (!tx_timeout) {
			dev_err(phy->dev, "aks_phy_configure: lane%d cal/init (tx_ack) timeout before P0\n", lane);
			ret = -ETIMEDOUT;
			goto unlock;
		}
		dev_info(phy->dev, "aks_phy_configure: lane%d tx_ack OK\n", lane);
	}

	/* all lanes done: powerdown P3 -> P0 */
	dev_info(phy->dev, "aks_phy_configure: all lanes cal done, switch to P0\n");
	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++)
		aks_top_write_mask(phy, TOP_PIPE_TX0_CTRL + lane * 4, GENMASK(9, 6), 0);

	/*
	 * After P3->P0, allow settling time for PLL/PHY power-up before polling PCS phystatus / PLL lock.
	 */
	usleep_range(20000, 25000);

	dev_info(phy->dev, "aks_phy_configure: wait PCS PHY status ready\n");
	ret = aks_pcs_wait_phystatus(phy);
	if (ret) {
		dev_err(phy->dev, "aks_phy_configure: PCS PHY status wait failed\n");
		goto unlock;
	}

	dev_info(phy->dev, "aks_phy_configure: PCS PHY status ready\n");

	/* verify tx/iboost params took effect (per lane) */
	{
		u32 vboost_dig = aks_icp_read(phy, ICP_SUP_DIG_IN_TX_VBOOST_LVL * 4);

		dev_info(phy->dev,
			"[TX_DBG] vboost_dig(0x58)=0x%04x (ovrd_en=%d lvl=%d)\n",
			vboost_dig, !!(vboost_dig & BIT(3)), vboost_dig & 0x7);
	}
	for (lane = 0; lane < AKS_PHY_NUM_LANES; lane++) {
		u32 misc0 = aks_icp_read(phy, icp_lane_offset(PPU_LCFG_TX_MISC_0_SR, lane));
		u32 mon0  = aks_icp_read(phy, icp_lane_offset(ICP_LANE_TX_PPU_GP_MON0, lane));
		u32 mon1  = aks_icp_read(phy, icp_lane_offset(ICP_LANE_TX_PPU_GP_MON1, lane));
		u32 mon2  = aks_icp_read(phy, icp_lane_offset(ICP_LANE_TX_PPU_GP_MON2, lane));

		dev_info(phy->dev,
			"[TX_DBG] lane%d MISC0=0x%04x (iboost_en_ovrd=%d en=%d code_ovrd=%d code=%d) MON0=0x%04x (rate=%d pstate=%d clk_rdy=%d) MON1=0x%04x (main=%d) MON2=0x%04x (post=%d pre=%d)\n",
			lane, misc0,
			!!(misc0 & BIT(12)), !!(misc0 & BIT(11)),
			!!(misc0 & BIT(10)), (misc0 >> 6) & 0xf,
			mon0, (mon0 >> 8) & 0x7, (mon0 >> 6) & 0x3, !!(mon0 & BIT(0)),
			mon1, (mon1 >> 3) & 0x3f,
			mon2, (mon2 >> 6) & 0x3f, mon2 & 0x3f);
	}

	aks_phy_dump_regs(phy);

	reg_0x14 = aks_icp_read(phy, ICP_REG_0x14 * 4);
	reg_0x20 = aks_icp_read(phy, ICP_REG_0x20 * 4);
	dev_info(phy->dev, "aks_phy_configure: icp reg 0x14=0x%08x, reg 0x20=0x%08x\n",
			 reg_0x14, reg_0x20);

	dev_info(phy->dev, "AKS PHY configure done (pixel_clk=%d ratio=%d)\n",
		 pixel_clk_khz, tmds_ratio);

unlock:
	mutex_unlock(&phy->lock);
	return ret;
}

void aks_phy_power_off(struct aks_phy *phy)
{
	dev_info(phy->dev, "aks_phy_power_off: enter\n");
	mutex_lock(&phy->lock);

	aks_top_write_mask(phy, TOP_PHY_RESET, BIT(0), BIT(0));
	phy->initialized = false;
	dev_info(phy->dev, "aks_phy_power_off: PHY powered off, initialized cleared\n");
	mutex_unlock(&phy->lock);
}

MODULE_AUTHOR("Sophgo multimedia team");
MODULE_DESCRIPTION("AkroStar x4NS HDMI 2.0 TX PHY driver for BM1684X2");
MODULE_LICENSE("GPL");
