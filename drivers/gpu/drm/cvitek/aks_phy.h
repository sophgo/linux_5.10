#ifndef __AKS_PHY_H__
#define __AKS_PHY_H__

#include <linux/types.h>

#define AKS_PHY_NUM_LANES    4

#define HDMI_TOP_BASE        0x0067220000UL
#define HDMI_ICP_BASE        0x0067280000UL
#define HDMI_ICP_SRAM_OFF    0x20000
#define HDMI_ICP_PCS_OFF     0x50000

#define HDMI_TOP_REGS       (HDMI_TOP_BASE)
#define HDMI_ICP_PHY_REG    (HDMI_ICP_BASE)
#define HDMI_ICP_SRAM       (HDMI_ICP_BASE + HDMI_ICP_SRAM_OFF)
#define HDMI_ICP_PCS        (HDMI_ICP_BASE + HDMI_ICP_PCS_OFF)

#define TOP_PHY_RESET           0x00
#define TOP_PIPE_TX0_CTRL       0x10
#define TOP_PIPE_TX1_CTRL       0x14
#define TOP_PIPE_TX2_CTRL       0x18
#define TOP_PIPE_TX3_CTRL       0x1c
#define TOP_PHY_MODE            0x28
#define TOP_PHY_SRAM_CTRL       0x2c
#define TOP_REF_CLK_CTRL        0x30
#define TOP_PHY_CMP_CTRL        0x34
#define TOP_PHY_RX_VREF_CTRL    0x38
#define TOP_PHY_ICP_ACS         0x44
#define TOP_PHY_ICP_CLK_EN      0x48
#define TOP_PHY_ICP_SEL         0x4c
#define TOP_ICP_BRG_MODE        0x50

#define SRAM_INIT_BYP       BIT(1)
#define SRAM_STARTUP_BYP    BIT(2)
#define SRAM_LOAD_DONE      BIT(3)
#define SRAM_ECC_EN         BIT(4)
#define SRAM_ACCESS_RDY     BIT(6)
#define SRAM_INIT_DONE      BIT(8)

#define SRAM_SIZE_WORDS    4096
#define SRAM_DATA_WIDTH    16

#define ICP_TIMEOUT_MS          400
#define SRAM_INIT_TIMEOUT_MS    50

/* icp registers (word offsets, code does *4 for byte address) */
#define ICP_REG_0x14    0x14
#define ICP_REG_0x20    0x20

/* PPUAOFG registers (word offsets, code does *4 for byte address) */
#define ICP_PPUAOFG_SRAM_CTRL    0x01B5

#define ICP_PPUAOFG_ATE_CONFIG0      0x01A9
#define ICP_PPUAOFG_CMPLLC           0x01B0
#define ICP_PPUAOFG_REF_CLK_INFO0    0x01B2

/* PPUAOFG_MUX registers (word offsets, code does *4 for byte address) */
#define ICP_PPUAOFG_MUX_ACC_CFG            0x1014
#define ICP_PPUAOFG_MUX_INIT_PWRUP_DONE    0x1028
#define ICP_PPUAOFG_MUX_REV_TRANS_CTRL     0x1065

/* SUP_DIG_IN registers (word offsets, code does *4 for byte address) */
#define ICP_SUP_DIG_IN_REF_CLK_CTL                   0x33
#define ICP_SUP_DIG_IN_BG_CTL                        0x34

/*
 * SUP_RES_CFG (word offset 0x22): reference resistor & cal enable.
 *   [6:4] SUP_TERM_CTRL  RW  reset 0x2  reference resistor impedance select
 *         0=55.0 1=52.4 2=50.0 3=47.8 4=45.8 5=44.0 6=42.3 7=40.7 (Ohm)
 *   [1]   TX_CAL_EN      RW  reset 0x1  enable TX resistor calibration
 *   [0]   RX_CAL_EN      RW  reset 0x1  enable RX resistor calibration
 */
#define ICP_SUP_RES_CFG                              0x22
#define SUP_RES_TERM_CTRL_SHIFT                      4
#define SUP_RES_TERM_CTRL_MASK                       GENMASK(6, 4)
#define SUP_RES_TX_CAL_EN                            BIT(1)
#define SUP_RES_RX_CAL_EN                            BIT(0)
#define SUP_RES_TERM_40_7OHM                         0x7
#define AKS_PHY_TERM_CTRL_OVRD                       0x7
#define ICP_SUP_DIG_IN_TX_VBOOST_LVL                 0x58
#define ICP_SUP_DIG_IN_CMPLL1_CTL                    0x49
#define ICP_SUP_DIG_IN_CMPLL1_DIV_FREQ_CTL           0x4B
#define ICP_SUP_DIG_IN_CMPLL1_BWC                    0x4C
#define ICP_SUP_DIG_IN_CMPLL1_BWC_VAL                0x4D
#define ICP_SUP_DIG_IN_CMPLL1_WORD_CLK_EN            0x46
#define ICP_SUP_DIG_IN_RESREF_CASCADE                0x57
#define ICP_SUP_DIG_IN_CMPLL1_DESKEW_EN              0xA1
#define ICP_SUP_DIG_IN_CMPLL1_SSC                    0x4E
#define ICP_SUP_DIG_IN_CMPLL1_SSC_STEP_SIZE_20_16    0x4F
#define ICP_SUP_DIG_IN_CMPLL1_SSC_STEP_SIZE_15_0     0x50
#define ICP_SUP_DIG_IN_CMPLL1_SSC_PEAK_19_16         0x51
#define ICP_SUP_DIG_IN_CMPLL1_SSC_PEAK_15_0          0x52

/* SUP_DIG_IN registers for CMPLL0 (word offsets, code does *4 for byte address) */
#define ICP_SUP_DIG_IN_CMPLL0_WORD_CLK_EN     0x36
#define ICP_SUP_DIG_IN_CMPLL0_DIV_CLK_EN      0x37
#define ICP_SUP_DIG_IN_CMPLL1_DIV_CLK_EN      0x47
#define ICP_SUP_DIG_IN_CMPLL0_CTL             0x39
#define ICP_SUP_DIG_IN_CMPLL0_DIV_FREQ_CTL    0x3B
#define ICP_SUP_DIG_IN_CMPLL0_BWC             0x3C
#define ICP_SUP_DIG_IN_CMPLL0_BWC_VAL         0x3D
#define ICP_SUP_DIG_IN_CMPLL0_SSC             0x3E

/* PPUPSFG registers (word offsets, code does *4 for byte address) */
#define ICP_PPUPSFG_CMPLLC_3_SR_0               0x11E
#define ICP_PPUPSFG_CMPLLC_1_SR_0_SPLIT0        0x119
#define ICP_PPUPSFG_CMPLLC_1_SR_0_SPLIT1        0x11A
#define ICP_PPUPSFG_CMPLLC_1_SR_1_SPLIT0        0x11B
#define ICP_PPUPSFG_CMPLLC_1_SR_1_SPLIT1        0x11C
#define ICP_PPUPSFG_CMPLLC_3_SR_1               0x11F
#define ICP_PPUPSFG_CMPLLC_2_SR                 0x11D
#define ICP_PPUPSFG_RES_CAL_REQ                 0x0056
#define ICP_PPUPSFG_CMPLL_FRAC_2_SR_1_SPLIT0    0x123
#define ICP_PPUPSFG_CMPLL_FRAC_2_SR_1_SPLIT1    0x124
#define ICP_PPUPSFG_CMPLL_FRAC_3_SR_1_SPLIT0    0x127
#define ICP_PPUPSFG_CMPLL_FRAC_3_SR_1_SPLIT1    0x128

/* PPUPSFG CMPLL0 FRAC registers (word offsets, code does *4 for byte address) */
#define ICP_PPUPSFG_CMPLL_FRAC_2_SR_0_SPLIT0    0x121
#define ICP_PPUPSFG_CMPLL_FRAC_2_SR_0_SPLIT1    0x122
#define ICP_PPUPSFG_CMPLL_FRAC_3_SR_0_SPLIT0    0x125
#define ICP_PPUPSFG_CMPLL_FRAC_3_SR_0_SPLIT1    0x126

/* PPUPSFG CMPLL SSC register (word offset, code does *4 for byte address) */
#define ICP_PPUPSFG_CMPLL_SSC_2_SR    0x131  /* SSC_UP_SPREAD + REF_DIV override */

/* PMA LANE override registers (word offsets for lane0, code uses icp_write_lane which adds lane*0x200*4) */
#define ICP_LANE_OVRD_TX_PPU_GP_OVRD0      0x3111
#define ICP_LANE_OVRD_TX_PPU_GP_OVRD1      0x311E
#define ICP_LANE_OVRD_TX_PPU_GP_OVRD2      0x311F
#define ICP_LANE_OVRD_TX_PPU_EQ_OVRD0      0x3120
#define ICP_LANE_OVRD_TX_PPU_EQ_OVRD1      0x3121
#define ICP_LANE_TX_PPU_GP_OUT_MON0        0x312E
/* TX per-lane monitor (RO, value from ASIC) for verifying tx params took effect */
#define ICP_LANE_TX_PPU_GP_MON0            0x3127  /* [12:11]WIDTH [10:8]RATE [7:6]PSTATE [0]CLK_RDY */
#define ICP_LANE_TX_PPU_GP_MON1            0x3128  /* [8:3]MAIN_CURSOR [1]TX_DISABLE */
#define ICP_LANE_TX_PPU_GP_MON2            0x3129  /* [11:6]POST_CURSOR [5:0]PRE_CURSOR */
/* RESCAL common calibration status (global, no lane stride) */
#define ICP_PPUAOFG_RESCAL_INFO0_RO        0x18B   /* [2]RESCAL_STATUS(doing) [1]RESCAL_DONE [0]RESCAL_INIT */
/* per-lane analog TX vboost (RW override); no dedicated RO monitor exists */
#define ICP_LANE_ANA_TX_VBOOST             0x31C5  /* [7]OVRD_VBOOST_EN [6]VBOOST_EN_REG */
#define ICP_LANE_OVRD_RX_PPU_GP_OVRD0      0x312F
#define ICP_LANE_OVRD_RX_PPU_GP_OVRD1      0x3130
#define ICP_LANE_OVRD_RX_PPU_CDR_OVRD1     0x3135
#define ICP_LANE_OVRD_RX_PPU_GP_OUT_MON    0x3147
#define ICP_LANE_OVRD_RX_ANA_VCO_OVRD0     0x317B

/* RX_ANA_VCO_OVRD0 fields (from phy_regs.v) */
#define RX_CDR_VCO_BAND_ATOP            BIT(6)
#define RX_CDR_VCO_BAND_ATOP_OVRD_EN    BIT(7)

/* PPU_LCFG registers (word offsets for lane0, code uses lcfg_write_lane/icp_lane_offset which adds lane stride) */
#define PPU_LCFG_MCU_OVRD_CTL             0x209C
#define PPU_LCFG_MCU_JMP_BANK             0x209D
#define PPU_LCFG_RXCLK_CONFIG             0x212C
#define PPU_LCFG_TX_CLK_ENABLE            0x212E
#define PPU_LCFG_RX_MAP_IQ_CODE_RO        0x200E
#define PPU_LCFG_RX_RATE_INT_ACK_SET      0x2012
#define PPU_LCFG_DFE_DAC_OFST_CONFIG3     0x2016
#define PPU_LCFG_AFE_DAC_OFST_CONFIG1     0x2018
#define PPU_LCFG_AFE_DAC_OFST_CONFIG3     0x201A
#define PPU_LCFG_ETH_RX_CLK_WAIT_TIME     0x2022
#define PPU_LCFG_RX_ADPT_IQ               0x2000
#define PPU_LCFG_TX_INFO_FW               0x2166
#define PPU_LCFG_RX_INFO_FW               0x214E
#define PPU_LCFG_RX_EQ_INFO_4_SR          0x215B
#define PPU_LCFG_RX_EQ_INFO_1_SR          0x2358
#define PPU_LCFG_RX_EQ_INFO_5_SR          0x235C
#define PPU_LCFG_TRANS_REQ_0              0x2164
#define PPU_LCFG_TX_MISC_0_SR             0x216A
#define PPU_LCFG_TX_MISC_1_SR             0x216C
#define PPU_LCFG_TXRX_TERM_SEL_OVRD_RO    0x213E
#define PPU_LCFG_TX_CLK_CTRL              0x2047
#define PPU_LCFG_RX_ACK_OVRD              0x2132
#define PPU_LCFG_TX_PCU_TX_ACK_OVRD       0x2133
#define PPU_LCFG_TX_ACK_FW_RO             0x2168
#define PPU_LCFG_RX_ACK_FW_RO             0x2150
#define PPU_LCFG_RX_MAP_IQ_CODE_ADD       0x2010
#define PPU_LCFG_REV_REQ_5                0x214C
#define PPU_LCFG_RX_EQ_INFO_5_SR_PROC0    0x215C
#define PPU_LCFG_RX_MISC_INFO_0_SR        0x215D

#define PPU_LCFG_LANE_STRIDE    0x200

#define PCS_PSFG_PHY_PARAM_CFG             0x14000
#define PCS_PSFG_LANE_CLK_CTRL_SETTING     0x14001
#define PCS_PSFG_PCS_CFG_OVRD              0x14002
#define PCS_PSFG_TX_RDY_OVRD               0x14003
#define PCS_PSFG_STS_REG                   0x14004
#define PCS_PSFG_CRG_CMN_REF_CLK_EN_STS    0x14080
#define PCS_PSFG_CRG_REF_CLK_EN_OVRD       0x14081
#define PCS_PSFG_CRG_MISC_OVRD_OUT         0x14082
#define PCS_PSFG_CRG_CLK_DIV_CFG           0x14083

#define PCS_LANE_HDMI_RATIO_OVRD     0x15000
#define PCS_LANE_CMN_OVRD_VAL        0x15001
#define PCS_LANE_CTRL_IN_OUT         0x15010
#define PCS_LANE_CTRL_OVRD_IN        0x15011
#define PCS_LANE_CTRL_OVRD_OUT       0x15012
#define PCS_LANE_TX_EQ_OVRD_VAL_1    0x15013
#define PCS_LANE_TX_EQ_OVRD_VAL_2    0x15014
#define PCS_LANE_CORE_RO0            0x15040
#define PCS_LANE_CORE_RO1            0x15041
#define PCS_LANE_CORE_RO2            0x15042
#define PCS_LANE_CORE_OVRD0          0x15043
#define PCS_LANE_CORE_OVRD1          0x15044
#define PCS_LANE_CORE_OVRD2          0x15045
#define PCS_LANE_CORE_PHY_CFG        0x15046
#define PCS_LANE_CORE_TX_DEF_CFG     0x15047
#define PCS_LANE_CORE_TX_PS_CFG      0x15048
#define PCS_LANE_CORE_TX_P4_CFG      0x15049
#define PCS_LANE_CORE_TX_P3_CFG      0x1504A
#define PCS_LANE_CORE_TX_P2_CFG      0x1504B
#define PCS_LANE_CORE_TX_P1_CFG      0x1504C
#define PCS_LANE_CORE_TX_P0_CFG      0x1504D

#define PCS_LANE_WORD_STRIDE    0x100

static inline u32 ppu_lcfg_lane_offset(u16 word_addr, int lane)
{
	return ((word_addr + lane * PPU_LCFG_LANE_STRIDE) * 4);
}

static inline u32 pcs_psfg_byte_off(u32 word_off)
{
	return word_off * 4;
}

static inline u32 pcs_lane_byte_off(u32 lane_word_off, int lane)
{
	return (lane_word_off + lane * PCS_LANE_WORD_STRIDE) * 4;
}

static inline u32 icp_lane_offset(u16 block_base_word_off, int lane)
{
	return ((block_base_word_off + lane * 0x200) * 4);
}

struct hdmi_phy_mode {
	u32  pixel_clk_khz;   /* pixel clock in kHz */
	u8   tmds_ratio;       /* TMDS clock ratio: 0=HDMI1.4, 1=HDMI2.0 */
	u8   cmpll_sel;        /* PLL select: 1=CMPLL1, 0=CMPLL0 */
	u16  freq_ctrl;        /* VCO freq_ctrl[9:0] for the active CMPLL */
	u8   frac_en;          /* fractional divider enable for the active CMPLL */
	u16  frac_quot;        /* fractional divider quotient[15:0] */
	u8   frac_rem;         /* fractional divider remainder[15:0] */
	u16  bwc;              /* loop bandwidth control[15:0] for the active CMPLL */
	u8   tx_clk_div;       /* tx_clk_div[2:0] for the active CMPLL */
	u8   tx_rate;          /* txX_rate[2:0]: 0=full, 1=1/2, 2=1/4, 3=1/8 */
	u8   rx_rate;          /* rxX_rate[2:0]: 1=full, 2=1/2, 3=1/4, 4=1/8, 5=1/16 */
	u16  rx_vco_cal_cfg_t; /* RX per-mode from phy_dut_cfg.sv */
	u8   rx_cdr_detect_th;  /* RX CDR check，per-mode from config_reference_manual xlsx */
	u8   rx_cdr_vco_band;   /* RX CDR VCO band force value，per-mode from phy_dut_cfg.sv (only proto170=1) */
};

extern const struct hdmi_phy_mode phy_modes[];

/* CMPLL1 ref clock parameters (shared, from config_reference_manual xlsx) */
#define CMPLL1_REF_DIV                2     /* cmpll1_ref_div[1:0]=2, all modes same */
#define CMPLL1_W_CLK_SEL              1     /* W_CLK_SEL[6:5]: vendor sim writes cmpll1_w_clk_div (=1) into this field, see hdmi_tx_dut_base_seq.svh:978 */
#define CMPLL1_W_CLK_DIV              1     /* cmpll1_w_clk_div[1:0]=1, xlsx all modes=1 */

/* CMPLL0 ref clock parameters (from phy_dut_cfg.sv proto52/53 CMPLL0 modes) */
#define CMPLL0_REF_DIV      2
#define CMPLL0_W_CLK_SEL    1
#define CMPLL0_W_CLK_DIV    1     /* cmpll0_w_clk_div[1:0]=1, phy_dut_cfg.sv cmpll0 modes */

#define TX_WIDTH            3
#define RX_WIDTH            3
#define TX_CTRL_MAIN        23
#define TX_CTRL_PRE         0
#define TX_CTRL_POST        4
#define TX_TERM_SEL         7
#define RX_TERM_SEL         2
#define RX_TERM_MODE        1
#define RX_DFE_BYPASS       1
#define RX_ADAPT_AFE_EN     0
#define RX_ADAPT_DFE_EN     0
#define TX_VBOOST_LVL       7
#define RX_VREF_CTRL        17
#define RX_VCO_CAL_CFG_R    25  /* static, all modes, from phy_dut_cfg.sv */
#define TX_IBOOST_EN        1
#define TX_IBOOST_CODE      15

struct aks_phy {
	/*
	 * Keep "struct device *dev" as the FIRST member. cvitek_dw_hdmi.c's
	 * data_to_cvitek_hdmi() relies on both struct aks_phy and struct
	 * cvitek_hdmi starting with this field to recover the glue context
	 * from dw-hdmi's opaque phy_data pointer.
	 */
	struct device *dev;
	void __iomem *top_regs;
	void __iomem *icp_base;
	void __iomem *pcs_base;
	struct mutex lock;
	bool initialized;
	/*
	 * Back-pointer to the glue-layer context (struct cvitek_hdmi).
	 *
	 * dw-hdmi passes plat_data->phy_data as the "data" argument to BOTH
	 * the PHY ops (which need this struct aks_phy) and the plat_data
	 * property/format callbacks (which need struct cvitek_hdmi). When a
	 * vendor PHY is used, phy_data points at this struct aks_phy, so the
	 * property/format callbacks must recover cvitek_hdmi via this field
	 * instead of blindly casting "data".
	 */
	void *ctx;
};

extern const struct hdmi_phy_mode *aks_phy_find_mode(u32 pixel_clk_khz, u8 tmds_ratio);
extern int aks_phy_probe_init(struct aks_phy *phy);
extern int aks_phy_configure(struct aks_phy *phy, u32 pixel_clk_khz, u8 tmds_ratio);
extern void aks_phy_power_off(struct aks_phy *phy);

extern struct aks_phy *aks_phy_get_instance(void);
extern struct aks_phy *aks_phy_instance;

#endif /* __AKS_PHY_H__ */
