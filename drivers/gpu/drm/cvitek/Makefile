# SPDX-License-Identifier: GPL-2.0-only
soph_drm-y := cvitek_drm.o cvitek_vo_sys_reg.o cvitek_disp.o cvitek_mipipll_cfg.o

ifdef CONFIG_ARCH_CV84X6
# VPSS disp-online (ported from bare-metal test, reuses scaler.c)
# cv84x6 only; A2 has no VPSS online path.
soph_drm-y += vpss/scaler.o vpss/cmdq.o vpss/vpss_disp_online.o
endif

soph_drm-$(CONFIG_CVITEK_MIPI_DSI) += cvitek_dsi.o
soph_drm-$(CONFIG_CVITEK_HDMI) += cvitek_dw_hdmi.o aks_phy.o
soph_drm-$(CONFIG_CVITEK_LVDS) += cvitek_lvds.o
soph_drm-$(CONFIG_CVITEK_DRM_DEBUG) += cvitek_drm_debugfs.o

ifdef CONFIG_ARCH_CV84X6
ccflags-y += -I$(src)/vpss

# The VPSS files are ported from bare-metal test code that uses C99-style
# declarations (e.g. "for (int i = ...)"). The kernel defaults to gnu89 for
# these; build just these ported units with gnu99 to avoid touching the
# upstream-style bare-metal source extensively.
CFLAGS_vpss/scaler.o          += -std=gnu99
CFLAGS_vpss/cmdq.o            += -std=gnu99
CFLAGS_vpss/vpss_disp_online.o += -std=gnu99
endif

# ccflags-y += -DDEBUG
obj-$(CONFIG_DRM_CVITEK) += soph_drm.o
