/* SPDX-License-Identifier: GPL-2.0 */
/*
 * VPSS bare-metal test code -> Linux kernel compatibility shim.
 *
 * Provides the register access primitives (_reg_read/_reg_write/
 * _reg_write_mask) that translate the bare-metal *physical* VPSS0
 * addresses (0x67110000 ~ 0x67114000) into the ioremap'd virtual base
 * (vpss0_base) mapped by the DRM driver from device-tree.
 *
 * It also redirects bare-metal logging (uartlog/printf) to pr_debug.
 */
#ifndef _VPSS_COMPAT_H_
#define _VPSS_COMPAT_H_

#include <linux/types.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/printk.h>

/* ioremap'd base of VOSYS_VPSS0_CFG (phys 0x67110000, 16KB), defined in
 * cvitek_vo_sys_reg.c and mapped in cvitek_drm.c from device-tree.
 */
extern void __iomem *vpss0_base;

#define VPSS0_PHYS 0x67110000UL

/*
 * The scaler_reg.h macros expand to full *physical* addresses in the
 * 0x67110000 block (VPSS_VO, IP_IDX==0). Translate to the ioremap'd
 * virtual address before every access.
 */
#define _reg_read(a)        readl(vpss0_base + ((uintptr_t)(a) - VPSS0_PHYS))
#define _reg_write(a, d)    writel((u32)(d), vpss0_base + ((uintptr_t)(a) - VPSS0_PHYS))
#define _reg_write_mask(a, m, d)                                              \
	do {                                                                  \
		u32 __v = _reg_read(a) & ~(u32)(m);                           \
		__v |= ((u32)(d) & (u32)(m));                                 \
		_reg_write((a), __v);                                         \
	} while (0)

/*
 * Redirect bare-metal logging to kernel logging.
 *
 * The ported bare-metal code uses %ld/%lx to print u64 values in many places,
 * which would trigger -Wformat warnings (and -Werror build failures) under the
 * kernel build. Route logging through a small wrapper (vpss_uartlog) that is
 * deliberately NOT annotated with __printf(), so the compiler does not perform
 * format-string checking on these call sites, while the messages remain
 * available at pr_debug() level (i.e. only emitted with dynamic-debug enabled).
 */
void vpss_uartlog(const char *fmt, ...);

#undef uartlog
#define uartlog(fmt, ...)   vpss_uartlog(fmt, ##__VA_ARGS__)
#undef printf
#define printf(fmt, ...)    vpss_uartlog(fmt, ##__VA_ARGS__)

#endif /* _VPSS_COMPAT_H_ */
