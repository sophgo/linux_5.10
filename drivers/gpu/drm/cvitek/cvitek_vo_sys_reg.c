#include <drm/drm_print.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include "cvitek_vo_sys_reg.h"

#define CVITEK_VOSYS_DEBUG_LOG 0

#if CVITEK_VOSYS_DEBUG_LOG
#define vo_dbg(fmt, ...) printk(fmt, ##__VA_ARGS__)
#else
#define vo_dbg(fmt, ...) no_printk(fmt, ##__VA_ARGS__)
#endif

void __iomem  *vo_sys_f_base;
#ifdef CONFIG_ARCH_CV84X6
void __iomem  *vo_sys_div_base;
void __iomem  *vpss0_base;
void __iomem  *sys_ctrl_base;
#else
void __iomem  *vo_sys_b_base;
void __iomem  *top_pll_base;
#endif

static DEFINE_RAW_SPINLOCK(__io_lock);
u32 _reg_read(void __iomem *addr)
{
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&__io_lock, flags);
	value = readl_relaxed(addr);
	raw_spin_unlock_irqrestore(&__io_lock, flags);
	return value;
}

void _reg_write(void __iomem *addr, u32 value)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&__io_lock, flags);
	writel_relaxed(value, addr);
	raw_spin_unlock_irqrestore(&__io_lock, flags);
}

void _reg_write_mask(void __iomem *addr, u32 mask, u32 data)
{
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&__io_lock, flags);
	value = readl_relaxed(addr) & ~mask;
	value |= (data & mask);
	writel(value, addr);
	raw_spin_unlock_irqrestore(&__io_lock, flags);
}

#ifndef CONFIG_ARCH_CV84X6
void extend_axi_to_36bit(u32 high_bit, enum drm_intf intf)
{
	switch (intf) {
	case DRM_INTF_DISP0:
		_reg_write_mask(REG_VO_SYS_AXI_ADDR_EXT_OW, 0xff000, 0xff000);
		_reg_write(REG_VO_SYS_AXI_ADDR_EXT2, (high_bit) | (high_bit << 4) | (high_bit << 8) | (high_bit << 12) |
									     (high_bit << 16) | (high_bit << 20) | (high_bit << 24) | (high_bit << 28));
		break;
	case DRM_INTF_DISP1:
		_reg_write_mask(REG_VO_SYS_AXI_ADDR_EXT_OW, 0xff00000, 0xff00000);
		_reg_write(REG_VO_SYS_AXI_ADDR_EXT3, (high_bit) | (high_bit << 4) | (high_bit << 8) | (high_bit << 12) |
									     (high_bit << 16) | (high_bit << 20) | (high_bit << 24) | (high_bit << 28));
		break;
	default:
		DRM_ERROR("drm intf %d is not support\n", intf);
		break;
	}
}
#endif

void reset_disp(void)
{
	DRM_DEBUG_DRIVER("---- enter reset_disp. ----\n");
	/*ip and apb reset*/
	_reg_write_mask(REG_VO_SYS_IP_RESET, DISP_RESET_MASK, DISP_RESET_MASK);
	_reg_write_mask(REG_VO_SYS_APB_RESET, DISP_RESET_MASK, DISP_RESET_MASK);
	udelay(10);
	_reg_write_mask(REG_VO_SYS_IP_RESET, DISP_RESET_MASK, 0x00);
	_reg_write_mask(REG_VO_SYS_APB_RESET, DISP_RESET_MASK, 0x00);
}

#ifdef CONFIG_ARCH_CV84X6
void cvitek_hdmi_reset(void)
{
	_reg_write(REG_VO_SYS_IP_RESET, HDMI_RESET_MASK);
	_reg_write(REG_VO_SYS_APB_RESET, HDMI_RESET_MASK);
	udelay(100);
	_reg_write(REG_VO_SYS_IP_RESET, 0x0);
	_reg_write(REG_VO_SYS_APB_RESET, 0x0);
}

/* disp0 pixel clk mux select (REG_VO_SYS_DISP0_CLK_SEL bit0, rst=1):
 *   bit0 = 1 : chip_top pll  (default / safe fallback)
 *   bit0 = 0 : hdmi phy pll  (only when PHY PLL is locked)
 */
void disp0_clk_sel_use_hdmi_pll(void)
{
	u32 sel;

	if (!vo_sys_f_base)
		return;

	sel = _reg_read(REG_VO_SYS_DISP0_CLK_SEL);
	vo_dbg("disp0_clk_sel_use_hdmi_pll: current CLK_SEL=0x%x\n", sel);
	/*
	 * Match the bare-metal reference which writes the whole register to 0
	 * ( _reg_write(VO_DISP0_CLK_SEL, 0x0) ) to select the HDMI PHY PLL,
	 * rather than only clearing bit0, so no stale bits in this register can
	 * change the mux behaviour.
	 */
	_reg_write(REG_VO_SYS_DISP0_CLK_SEL, DISP0_CLK_SEL_HDMI_PLL_VAL);

	sel = _reg_read(REG_VO_SYS_DISP0_CLK_SEL);
	vo_dbg("disp0_clk_sel_use_hdmi_pll: after switch CLK_SEL=0x%x\n", sel);

	_reg_write_mask(REG_VO_SYS_IP_RESET, DISP_RESET_MASK, DISP_RESET_MASK);
	udelay(50);
	_reg_write_mask(REG_VO_SYS_IP_RESET, DISP_RESET_MASK, 0x00);
}

void disp0_clk_sel_use_top_pll(void)
{
	u32 sel;

	if (!vo_sys_f_base)
		return;

	sel = _reg_read(REG_VO_SYS_DISP0_CLK_SEL);
	sel |= DISP0_CLK_SEL_TOP_PLL_BIT;
	_reg_write(REG_VO_SYS_DISP0_CLK_SEL, sel);
}
#endif /* CONFIG_ARCH_CV84X6 */

