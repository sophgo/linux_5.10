// SPDX-License-Identifier: GPL-2.0-only
/*
 * cv84x6 SDHCI Platform Driver for Linux
 *
 * Based on sdhci-cv186x.c, adapted for cv84x6 standard DWC_mshc IP:
 *	- EMMC_CTRL_R at P_VENDOR_SPECIFIC_AREA + 0x2c (not 0x200)
 *	- RST_N is BIT(2) in EMMC_CTRL_R (DWC standard); BIT(8) is HS400 ES
 *	- PHY pad config at offset 0x300
 *	- Software Tuning per DWC User Guide Section 8.6.4
 *	- 8-bit eMMC + HS400 support
 *	- PHY + eMMC RST_N must be programmed before sdhci_add_host()
 *
 * Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 * Copyright (C) Sophgo 2025-2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/slab.h>
#include <linux/reset.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/of_gpio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/ktime.h>
#include <linux/clk.h>
#include <linux/sizes.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>

#include "../../core/card.h"
#include "../sdhci-pltfm.h"

/* ===== Standard DWC_mshc Vendor Register Offsets ===== */
#define DWC_EMMC_CTRL_R (0x2c) /* eMMC Control register (16-bit) */
#define DWC_BOOT_CTRL_R (0x2e) /* eMMC Boot Control register */

/* DWC_EMMC_CTRL_R bit fields (match U-Boot sdhci-cv84x6.c) */
#define EMMC_CARD_IS_EMMC BIT(0)
#define EMMC_DISABLE_CRC_CHK BIT(1)
/* DWC standard: bit2 = RST_N output value (controls the eMMC RST_n pin) */
#define EMMC_RST_N BIT(2)
/* DWC standard: bit8 = HS400 enhanced strobe enable */
#define ENH_STROBE_ENABLE BIT(8)

/* DWC_mshc MSHC_CTRL_R (vendor_base + 0x8) */
#define DWC_MSHC_CTRL_R (0x8)
#define DWC_MSHC_NEGEDGE_DATAOUT_EN BIT(2) /* HS400 negedge data out */

/* DWC_mshc PHY registers (offset 0x300 from SDHCI base) */
#define SDHCI_PHY_R_OFFSET 0x300
#define SDHCI_P_PHY_CNFG (SDHCI_PHY_R_OFFSET + 0x00)
#define SDHCI_P_CMDPAD_CNFG (SDHCI_PHY_R_OFFSET + 0x04)
#define SDHCI_P_DATPAD_CNFG (SDHCI_PHY_R_OFFSET + 0x06)
#define SDHCI_P_CLKPAD_CNFG (SDHCI_PHY_R_OFFSET + 0x08)
#define SDHCI_P_STBPAD_CNFG (SDHCI_PHY_R_OFFSET + 0x0A)
#define SDHCI_P_RSTNPAD_CNFG (SDHCI_PHY_R_OFFSET + 0x0C)
#define SDHCI_P_COMMDL_CNFG (SDHCI_PHY_R_OFFSET + 0x1C)
#define SDHCI_P_SDCLKDL_CNFG (SDHCI_PHY_R_OFFSET + 0x1D)
#define SDHCI_P_SDCLKDL_DC (SDHCI_PHY_R_OFFSET + 0x1E)
#define SDHCI_P_SMPLDL_CNFG (SDHCI_PHY_R_OFFSET + 0x20)
#define SDHCI_P_ATDL_CNFG (SDHCI_PHY_R_OFFSET + 0x21)

/* DWC_mshc PHY DLL registers at PHY+0x300 (HS400), match U-Boot sdhci-cv84x6.c */
#define SDHCI_PHY_DLL_CTRL (SDHCI_PHY_R_OFFSET + 0x24)
#define SDHCI_PHY_DLL_CNFG1 (SDHCI_PHY_R_OFFSET + 0x25)
#define SDHCI_PHY_DLL_CNFG2 (SDHCI_PHY_R_OFFSET + 0x26)
#define SDHCI_PHY_DLLDL_CNFG (SDHCI_PHY_R_OFFSET + 0x28)
#define SDHCI_PHY_DLL_STATUS (SDHCI_PHY_R_OFFSET + 0x2E)
#define DLL_CTRL_DLL_EN BIT(0)
#define DLL_STATUS_LOCK_STS BIT(0)
#define DLL_STATUS_ERR_STS BIT(1)

/* PHY_CNFG bit fields */
#define PHY_CNFG_PHY_RSTN 0
#define PHY_CNFG_PHY_PWRGOOD 1
#define PHY_CNFG_PAD_SN 20
#define PHY_CNFG_PAD_SP 16

/*
 * DWC_mshc HOST_CONTROL2 bit4 = SAMPLE_CLK_SEL (select the tuned sampling
 * clock). This IP uses bit4, NOT the standard SDHCI_CTRL_TUNED_CLK (bit7).
 * Match U-Boot sdhci-cv84x6.c.
 */
#define SDHCI_CTRL_SAMPLE_CLK_SEL BIT(4)

/*
 * DWC_mshc selects HS400 with UHS_MODE_SEL=7 in HOST_CONTROL2[2:0], not the
 * generic SDHCI value (0x5). The kernel-wide SDHCI_CTRL_HS400 in sdhci.h is
 * 0x5, so define the correct DWC value locally (match U-Boot include/sdhci.h).
 */
#define DWC_MSHC_CTRL_HS400 0x7

/* PAD_CNFG bit fields */
#define PAD_CNFG_RXSEL 0
#define PAD_CNFG_WEAKPULL_EN 3
#define PAD_CNFG_TXSLEW_CTRL_P 5
#define PAD_CNFG_TXSLEW_CTRL_N 9

/* DWC_mshc Auto-Tuning registers */
#define DWC_AT_CTRL_R (0x40)
#define DWC_AT_STAT_R (0x44)
#define AT_SW_TUNE_EN BIT(4)
#define AT_CENTER_PH_CODE_MASK 0xFF

#define MAX_TUNING_CMD_RETRY_COUNT 50
#define TUNE_MAX_PHCODE 128
#define TAP_WINDOW_THLD 20

#define SDHCI_GPIO_CD_DEBOUNCE_TIME 10
#define SDHCI_GPIO_CD_DEBOUNCE_DELAY_TIME 200

/* cv84x6: SD0/SD1 CD/PWR on G3 pinmux (NOT cv186x G9/G11) */
#define G3_PINMUX_REG_BASE 0x28104300
#define SD0_PWR_EN_PIN (G3_PINMUX_REG_BASE + 0x5c) /* SD0_PWR_EN */
#define SD0_CD_PIN (G3_PINMUX_REG_BASE + 0x60) /* SD0_CD_X */
#define SD1_CD_PIN (G3_PINMUX_REG_BASE + 0x58) /* SD1_CD_X */
#define PIN_MUX_MASK (0xf << 4)
#define PIN_PU_EN BIT(2)
#define DEFAULT_DIV_SD_INIT_CLOCK 0x2
#define SDCLKDL_CNFG_EXTDLY_EN 0
#define SMPLDL_CNFG_BYPASS_EN 1
#define ATDL_CNFG_INPSEL_CNFG 2

#define G8_IO_BASE 0x28104800
#define G11_IO_BASE 0x28104b00
#define PULL_MASK (0x3 << 2)
#define DRIVE_MASK (0xf << 8)
#define PAD_MASK (0x1 << 13)
#define PULL_DOWN (0x1 << 3)
#define PULL_UP (0x1 << 2)
#define DRIVE_VALUE (0x4 << 8)
#define IO_MASK (DRIVE_MASK | PULL_MASK | PAD_MASK)

/* Backward compatibility defines for cv186x vendor register names */
#define SDHCI_VENDOR_OFFSET 0x200
#define SDHCI_VENDOR_MSHC_CTRL_R (SDHCI_VENDOR_OFFSET + 0x0)
#define SDHCI_PHY_TX_RX_DLY (SDHCI_VENDOR_OFFSET + 0x40)
#define SDHCI_PHY_DS_DLY (SDHCI_VENDOR_OFFSET + 0x44)
#define SDHCI_PHY_DLY_STS (SDHCI_VENDOR_OFFSET + 0x48)
#define SDHCI_PHY_CONFIG (SDHCI_VENDOR_OFFSET + 0x4C)
#define SDHCI_CVI_QUIRK_FORCE_CDTEST BIT(0)

struct sdhci_cvi_host {
	struct sdhci_host *host;
	struct platform_device *pdev;
	void __iomem *core_mem;
	struct clk *clk;
	struct clk *clk100k;
	struct clk *clkaxi;
	struct mmc_host *mmc;
	struct reset_control *reset;

	struct reset_control *clk_rst_axi_emmc_ctrl;
	struct reset_control *clk_rst_emmc_ctrl;
	struct reset_control *clk_rst_100k_emmc_ctrl;

	void __iomem *pinmuxbase;
	void __iomem *clkgenbase;

	u32 reg_ctrl2;
	u32 reg_clk_ctrl;
	u32 reg_host_ctrl;
	u8 final_tap;
	u8 sd_voltage_1_8_v;
	int sd_save_count;
	struct mmc_gpio *cvi_gpio;
	int pwr_gpio;
	struct delayed_work cd_debounce_work;
	spinlock_t cd_debounce_lock;
	int pre_gpio_cd;
	bool is_debounce_work_running;
	unsigned int quirks;
};

#define DRIVER_NAME "cvi"
#define SDHCI_DUMP(f, x...) \
	pr_err("%s: " DRIVER_NAME ": " f, mmc_hostname(host->mmc), ##x)

#define MAX_CARD_TYPE 4
#define MAX_SPEED_MODE 5

#define CVI_PARENT "cvi"
#define CVI_STATS_PROC "cvi_info"
#define MAX_CLOCK_SCALE (4)

#define UNSTUFF_BITS(resp, start, size)                                   \
	({                                                                \
		const int __size = size;                                  \
		const u32 __mask = (__size < 32 ? 1 << __size : 0) - 1;   \
		const int __off = 3 - ((start) / 32);                     \
		const int __shft = (start)&31;                            \
		u32 __res;                                                \
		__res = resp[__off] >> __shft;                            \
		if (__size + __shft > 32)                                 \
			__res |= resp[__off - 1] << ((32 - __shft) % 32); \
		__res &__mask;                                            \
	})

#define BOUNDARY_OK(addr, len) \
	((addr | (SZ_128M - 1)) == ((addr + len - 1) | (SZ_128M - 1)))

static struct proc_dir_entry *proc_cvi_dir;

static char *card_type[MAX_CARD_TYPE + 1] = {"MMC card", "SD card", "SD1 card",
					     "SDIO card",
					     "SD combo (IO+mem) card"};

static char *cvi_get_card_type(unsigned int sd_type)
{
	if (sd_type >= MAX_CARD_TYPE)
		return card_type[MAX_CARD_TYPE];
	else
		return card_type[sd_type];
}

static inline int is_card_uhs(unsigned char timing)
{
	return timing >= MMC_TIMING_UHS_SDR12 && timing <= MMC_TIMING_UHS_DDR50;
};

static inline int is_card_hs(unsigned char timing)
{
	return timing == MMC_TIMING_SD_HS || timing == MMC_TIMING_MMC_HS;
};

static void cvi_stats_seq_printout(struct seq_file *s)
{
	const char *type = NULL;
	unsigned int present;
	unsigned char timing;
	struct sdhci_cvi_host *cvi_host = NULL;
	struct mmc_host *mmc = NULL;
	struct mmc_card *card = NULL;
	const char *uhs_bus_speed_mode = "";
	u32 speed_class, grade_speed_uhs;
	static const char *const uhs_speeds[] = {
		[UHS_SDR12_BUS_SPEED] = "SDR12 ",
		[UHS_SDR25_BUS_SPEED] = "SDR25 ",
		[UHS_SDR50_BUS_SPEED] = "SDR50 ",
		[UHS_SDR104_BUS_SPEED] = "SDR104 ",
		[UHS_DDR50_BUS_SPEED] = "DDR50 ",
	};
	cvi_host = (struct sdhci_cvi_host *)s->private;

	if (!cvi_host || !cvi_host->mmc) {
		seq_printf(s, "cvi s : %p: s->private %p\n", s, s->private);
		return;
	}

	seq_printf(s, "cvi.%d", cvi_host->mmc->index);

	mmc = cvi_host->mmc;
	present = mmc->ops->get_cd(mmc);

	if (present)
		seq_puts(s, ": plugged");
	else
		seq_puts(s, ": unplugged");

	card = mmc->card;

	if (!card) {
		seq_puts(s, "_disconnected\n");
	} else {
		seq_puts(s, "_connected\n");
		seq_printf(s, "\tType: %s", cvi_get_card_type(card->type));

		if (mmc_card_blockaddr(card)) {
			if (card->state & MMC_CARD_SDXC)
				type = "SDXC";
			else
				type = "SDHC";
			seq_printf(s, "(%s)\n", type);
		}

		timing = mmc->ios.timing;

		if (is_card_uhs(mmc->ios.timing) &&
		    card->sd_bus_speed < ARRAY_SIZE(uhs_speeds))
			uhs_bus_speed_mode = uhs_speeds[card->sd_bus_speed];

		seq_printf(s, "\tMode: %s%s%s%s\n",
			   is_card_uhs(timing) ?
				   "UHS " :
				   (is_card_hs(timing) ? "HS " : ""),
			   timing == MMC_TIMING_MMC_HS400 ?
				   "HS400 " :
				   (timing == MMC_TIMING_MMC_HS200 ? "HS200 " :
								     ""),
			   timing == MMC_TIMING_MMC_DDR52 ? "DDR " : "",
			   uhs_bus_speed_mode);

		speed_class = UNSTUFF_BITS(card->raw_ssr, 440 - 384, 8);
		grade_speed_uhs = UNSTUFF_BITS(card->raw_ssr, 396 - 384, 4);
		seq_printf(s, "\tSpeed Class: Class %s\n",
			   (speed_class == 0x00) ?
				   "0" :
				   (speed_class == 0x01) ?
				   "2" :
				   (speed_class == 0x02) ?
				   "4" :
				   (speed_class == 0x03) ?
				   "6" :
				   (speed_class == 0x04) ? "10" : "Reserved");
		seq_printf(s, "\tUhs Speed Grade: %s\n",
			   (grade_speed_uhs == 0x00) ?
				   "Less than 10MB/sec(0h)" :
				   (grade_speed_uhs == 0x01) ?
				   "10MB/sec and above(1h)" :
				   "Reserved");
	}
}

/* proc interface setup */
static void *cvi_seq_start(struct seq_file *s, loff_t *pos)
{
	/*   counter is used to tracking multi proc interfaces
	 *  We have only one interface so return zero
	 *  pointer to start the sequence.
	 */
	static unsigned long counter;

	if (*pos == 0)
		return &counter;

	*pos = 0;
	return NULL;
}

/* define parameters where showed in proc file */
static int cvi_stats_seq_show(struct seq_file *s, void *v)
{
	cvi_stats_seq_printout(s);
	return 0;
}

/* proc interface stop */
static void cvi_seq_stop(struct seq_file *s, void *v)
{
}

/* proc interface operation */
static const struct seq_operations cvi_stats_seq_ops = {
	.start = cvi_seq_start,
	.stop = cvi_seq_stop,
	.show = cvi_stats_seq_show};

/* proc file open*/
static int cvi_stats_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cvi_stats_seq_show, PDE_DATA(inode));
};

/* proc file operation */
static const struct proc_ops cvi_stats_proc_ops = {
	.proc_open = cvi_stats_proc_open,
	.proc_read = seq_read,
	.proc_release = single_release,
};

int cvi_proc_init(struct sdhci_cvi_host *cvi_host)
{
	struct proc_dir_entry *proc_stats_entry;

	pr_info("%s cvi_host 0x%p\n", __func__, cvi_host);

	proc_cvi_dir = proc_mkdir(CVI_PARENT, NULL);

	if (!proc_cvi_dir) {
		pr_err("%s: failed to create proc file %s\n", __func__,
		       CVI_PARENT);
		return 1;
	}

	proc_stats_entry =
		proc_create_data(CVI_STATS_PROC, 0400, proc_cvi_dir,
				 &cvi_stats_proc_ops, (void *)cvi_host);

	if (!proc_stats_entry) {
		pr_err("%s: failed to create proc file %s\n", __func__,
		       CVI_STATS_PROC);
		return 1;
	}

	return 0;
}

int cvi_proc_shutdown(struct sdhci_cvi_host *cvi_host)
{
	pr_info("%s\n", __func__);

	if (proc_cvi_dir) {
		remove_proc_entry(CVI_STATS_PROC, proc_cvi_dir);
		remove_proc_entry(CVI_PARENT, NULL);
		proc_cvi_dir = NULL;
	}

	return 0;
}

static void sdhci_cv84x6_emmc_phy_config(struct sdhci_host *host)
{
	u32 val;
	int loop = 100;

	/* Wait for PHY power on ready (no SDHCI soft-reset here) */
	while (!(sdhci_readl(host, SDHCI_P_PHY_CNFG) &
		 BIT(PHY_CNFG_PHY_PWRGOOD))) {
		if (loop-- <= 0) {
			pr_err("eMMC PHY power good timeout phy=0x%x\n",
			       sdhci_readl(host, SDHCI_P_PHY_CNFG));
			return;
		}
		mdelay(1);
	}

	/* Assert PHY reset */
	val = sdhci_readl(host, SDHCI_P_PHY_CNFG);
	val &= ~BIT(PHY_CNFG_PHY_RSTN);
	sdhci_writel(host, val, SDHCI_P_PHY_CNFG);

	/* Set PAD_SN=0xA, PAD_SP=0xA (match U-Boot emmc_phy_pad_setting) */
	sdhci_writel(host,
		     BIT(PHY_CNFG_PHY_PWRGOOD) | (0xA << PHY_CNFG_PAD_SP) |
			     (0xA << PHY_CNFG_PAD_SN),
		     SDHCI_P_PHY_CNFG);

	/* CMDPAD: RXSEL=6, WEAKPULL=1, TXSLEW=2/2 */
	sdhci_writew(host,
		     (6 << PAD_CNFG_RXSEL) | (1 << PAD_CNFG_WEAKPULL_EN) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_P) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_N),
		     SDHCI_P_CMDPAD_CNFG);

	/* DATPAD: RXSEL=6, WEAKPULL=1, TXSLEW=2/2 */
	sdhci_writew(host,
		     (6 << PAD_CNFG_RXSEL) | (1 << PAD_CNFG_WEAKPULL_EN) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_P) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_N),
		     SDHCI_P_DATPAD_CNFG);

	/* CLKPAD: RXSEL=6, WEAKPULL=0, TXSLEW=2/2 */
	sdhci_writew(host,
		     (6 << PAD_CNFG_RXSEL) | (0 << PAD_CNFG_WEAKPULL_EN) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_P) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_N),
		     SDHCI_P_CLKPAD_CNFG);

	/* STBPAD: RXSEL=6, WEAKPULL=2, TXSLEW=2/2 */
	sdhci_writew(host,
		     (6 << PAD_CNFG_RXSEL) | (2 << PAD_CNFG_WEAKPULL_EN) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_P) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_N),
		     SDHCI_P_STBPAD_CNFG);

	/* RSTPAD: RXSEL=6, WEAKPULL=1, TXSLEW=2/2 */
	sdhci_writew(host,
		     (6 << PAD_CNFG_RXSEL) | (1 << PAD_CNFG_WEAKPULL_EN) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_P) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_N),
		     SDHCI_P_RSTNPAD_CNFG);

	/* COMMDL_CNFG = 0 */
	sdhci_writeb(host, 0x0, SDHCI_P_COMMDL_CNFG);

	/* Set SDCLKDL_CNFG, enable update DC (match U-Boot emmc_phy_pad_setting) */
	sdhci_writeb(host, 0x10, SDHCI_P_SDCLKDL_CNFG);
	/* ADD 70ps step */
	sdhci_writeb(host, 0x3f, SDHCI_P_SDCLKDL_DC);
	/* Set SDCLKDL_CNFG, EXTDLY_EN = 1, fix delay */
	sdhci_writeb(host, 0x11, SDHCI_P_SDCLKDL_CNFG);
	/* Set SDCLKDL_CNFG, disable update DC */
	sdhci_writeb(host, 0x1, SDHCI_P_SDCLKDL_CNFG);

	/* SMPLDL_CNFG: bypass */
	sdhci_writeb(host, 0xC, SDHCI_P_SMPLDL_CNFG);

	/* ATDL_CNFG: bypass */
	sdhci_writeb(host, 0xC, SDHCI_P_ATDL_CNFG);

	/* De-assert PHY reset */
	val = sdhci_readl(host, SDHCI_P_PHY_CNFG);
	val |= BIT(PHY_CNFG_PHY_RSTN);
	sdhci_writel(host, val, SDHCI_P_PHY_CNFG);
}

static void sdhci_cv84x6_emmc_phy_pad_setting(struct sdhci_host *host)
{
	int loop = 100;

	/* Reset hardware once, then configure PHY pads */
	sdhci_writeb(host, SDHCI_RESET_ALL | SDHCI_RESET_CMD | SDHCI_RESET_DATA,
		     SDHCI_SOFTWARE_RESET);
	while (sdhci_readb(host, SDHCI_SOFTWARE_RESET)) {
		if (loop-- <= 0)
			break;
		mdelay(10);
	}

	sdhci_cv84x6_emmc_phy_config(host);
}

static u16 sdhci_cv84x6_vendor_base(struct sdhci_host *host)
{
	return sdhci_readw(host, 0xE8) & 0xFFF;
}

/* Match U-Boot: mark eMMC and assert RST_N after soft reset */
static void sdhci_cv84x6_emmc_ctrl_enable(struct sdhci_host *host)
{
	u16 vendor_base = sdhci_cv84x6_vendor_base(host);
	u16 ctrl;
	u16 hc2;

	ctrl = sdhci_readw(host, vendor_base + DWC_EMMC_CTRL_R);
	ctrl |= EMMC_CARD_IS_EMMC | EMMC_RST_N;
	sdhci_writew(host, ctrl, vendor_base + DWC_EMMC_CTRL_R);

	/* eMMC I/O is 1.8V on this SoC (U-Boot sets HOST_CONTROL2.VDD_180) */
	hc2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	hc2 |= SDHCI_CTRL_VDD_180;
	sdhci_writew(host, hc2, SDHCI_HOST_CONTROL2);

	sdhci_writeb(host, 0xe, SDHCI_TIMEOUT_CONTROL);
}

static void sdhci_cv84x6_dump_emmc_state(struct sdhci_host *host,
					 const char *tag)
{
	u16 vendor_base = sdhci_cv84x6_vendor_base(host);

	pr_info("mmc eMMC %s: pstate=0x%x clk=0x%x pwr=0x%x hc2=0x%x emmc_ctrl=0x%x phy=0x%x f_src=%u ver=%u v4=%d\n",
		tag, sdhci_readl(host, SDHCI_PRESENT_STATE),
		sdhci_readw(host, SDHCI_CLOCK_CONTROL),
		sdhci_readb(host, SDHCI_POWER_CONTROL),
		sdhci_readw(host, SDHCI_HOST_CONTROL2),
		sdhci_readw(host, vendor_base + DWC_EMMC_CTRL_R),
		sdhci_readl(host, SDHCI_P_PHY_CNFG), host->mmc->f_src,
		host->version, host->v4_mode);
}

/* Match U-Boot cvi_emmc_dev_reset: pulse RST_N so the device exits reset */
static void sdhci_cv84x6_emmc_dev_reset(struct sdhci_host *host)
{
	u16 vendor_base = sdhci_cv84x6_vendor_base(host);
	u16 ctrl;

	ctrl = sdhci_readw(host, vendor_base + DWC_EMMC_CTRL_R);
	ctrl |= EMMC_CARD_IS_EMMC;
	ctrl &= ~EMMC_RST_N;
	sdhci_writew(host, ctrl, vendor_base + DWC_EMMC_CTRL_R);
	mdelay(1);
	ctrl |= EMMC_RST_N;
	sdhci_writew(host, ctrl, vendor_base + DWC_EMMC_CTRL_R);
	mdelay(1);
}

/* Match U-Boot sdio_phy_pad_setting (RXSEL=6, PAD_SP/SN=0xB, delay bypass). */
static void sdhci_cv84x6_sdio_phy_config(struct sdhci_host *host)
{
	u32 val;
	int loop = 100;

	while (!(sdhci_readl(host, SDHCI_P_PHY_CNFG) &
		 BIT(PHY_CNFG_PHY_PWRGOOD))) {
		if (loop-- <= 0) {
			pr_err("SDIO PHY power good timeout phy=0x%x\n",
			       sdhci_readl(host, SDHCI_P_PHY_CNFG));
			return;
		}
		mdelay(10);
	}

	val = sdhci_readl(host, SDHCI_P_PHY_CNFG);
	val &= ~BIT(PHY_CNFG_PHY_RSTN);
	sdhci_writel(host, val, SDHCI_P_PHY_CNFG);

	sdhci_writel(host,
		     BIT(PHY_CNFG_PHY_PWRGOOD) | (0xC << PHY_CNFG_PAD_SP) |
			     (0xC << PHY_CNFG_PAD_SN),
		     SDHCI_P_PHY_CNFG);

	/* CMDPAD: RXSEL=6, WEAKPULL=1, TXSLEW=2/2 (match U-Boot) */
	sdhci_writew(host,
		     (6 << PAD_CNFG_RXSEL) | (1 << PAD_CNFG_WEAKPULL_EN) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_P) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_N),
		     SDHCI_P_CMDPAD_CNFG);
	/* DATPAD: RXSEL=6, WEAKPULL=1, TXSLEW=2/2 */
	sdhci_writew(host,
		     (6 << PAD_CNFG_RXSEL) | (1 << PAD_CNFG_WEAKPULL_EN) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_P) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_N),
		     SDHCI_P_DATPAD_CNFG);
	/* CLKPAD: RXSEL=6, WEAKPULL=0, TXSLEW=2/2 */
	sdhci_writew(host,
		     (6 << PAD_CNFG_RXSEL) | (0 << PAD_CNFG_WEAKPULL_EN) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_P) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_N),
		     SDHCI_P_CLKPAD_CNFG);
	/* STBPAD: RXSEL=6, WEAKPULL=2, TXSLEW=2/2 */
	sdhci_writew(host,
		     (6 << PAD_CNFG_RXSEL) | (2 << PAD_CNFG_WEAKPULL_EN) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_P) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_N),
		     SDHCI_P_STBPAD_CNFG);
	/* RSTPAD: RXSEL=6, WEAKPULL=1, TXSLEW=2/2 */
	sdhci_writew(host,
		     (6 << PAD_CNFG_RXSEL) | (1 << PAD_CNFG_WEAKPULL_EN) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_P) |
			     (2 << PAD_CNFG_TXSLEW_CTRL_N),
		     SDHCI_P_RSTNPAD_CNFG);

	sdhci_writeb(host, 0x0, SDHCI_P_COMMDL_CNFG);
	sdhci_writeb(host, 0x10, SDHCI_P_SDCLKDL_CNFG);
	sdhci_writeb(host, 0x0, SDHCI_P_SDCLKDL_DC);
	sdhci_writeb(host, 0x11, SDHCI_P_SDCLKDL_CNFG);
	sdhci_writeb(host, 0x1, SDHCI_P_SDCLKDL_CNFG);
	sdhci_writeb(host, 0xC, SDHCI_P_SMPLDL_CNFG);
	sdhci_writeb(host, 0xC, SDHCI_P_ATDL_CNFG);

	val = sdhci_readl(host, SDHCI_P_PHY_CNFG);
	val |= BIT(PHY_CNFG_PHY_RSTN);
	sdhci_writel(host, val, SDHCI_P_PHY_CNFG);
}

/* Match U-Boot cvi_phy_hw_reset: enable divider clock before RESET_ALL */
static void sdhci_cv84x6_sdio_phy_pad_setting(struct sdhci_host *host)
{
	u16 clk;

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	sdhci_writew(host,
		     (clk & 0x3f) |
			     (DEFAULT_DIV_SD_INIT_CLOCK << SDHCI_DIVIDER_SHIFT),
		     SDHCI_CLOCK_CONTROL);
	mdelay(1);
	sdhci_writeb(host, SDHCI_RESET_ALL, SDHCI_SOFTWARE_RESET);
	while (sdhci_readb(host, SDHCI_SOFTWARE_RESET))
		;

	sdhci_cv84x6_sdio_phy_config(host);
}

static void sdhci_cv84x6_sd_setup_pad(struct sdhci_host *host)
{
	void __iomem *base;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);
	u32 val = 0;
	u32 reg;

	/* SD0_CD_X: mux0 = hardware CD, mux3 = GPIO CD */
	base = ioremap(SD0_CD_PIN, 4);
	if (!base) {
		pr_err("%s: CD pinmux ioremap failed\n", __func__);
		return;
	}
	reg = readl(base);
	reg &= ~PIN_MUX_MASK;
	if (cvi_host->cvi_gpio && cvi_host->cvi_gpio->cd_gpio)
		val = 3;
	reg |= (val << 4) | PIN_PU_EN;
	writel(reg, base);
	iounmap(base);

	/* SD0_PWR_EN: mux0 so controller can power the slot (U-Boot board.c) */
	base = ioremap(SD0_PWR_EN_PIN, 4);
	if (!base) {
		pr_err("%s: PWR_EN pinmux ioremap failed\n", __func__);
		return;
	}
	reg = readl(base);
	reg &= ~PIN_MUX_MASK;
	writel(reg, base);
	iounmap(base);

	pr_debug("%s: SD0 CD/PWR pinmux ready\n", __func__);
}

static void sdhci_cv84x6_sd1_setup_pad(struct sdhci_host *host)
{
	void __iomem *base;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);
	u32 val = 0;
	u32 reg;

	/* SD1_CD_X: mux0 = hardware CD, mux3 = GPIO CD (mirror SD0) */
	base = ioremap(SD1_CD_PIN, 4);
	if (!base) {
		pr_err("%s: CD pinmux ioremap failed\n", __func__);
		return;
	}
	reg = readl(base);
	reg &= ~PIN_MUX_MASK;
	if (cvi_host->cvi_gpio && cvi_host->cvi_gpio->cd_gpio)
		val = 3;
	reg |= (val << 4) | PIN_PU_EN;
	writel(reg, base);
	iounmap(base);

	/*
	 * Do NOT force SD1_PWR_EN to mux0 here. Unlike SD0, driving that pad
	 * from the controller interferes with the GPIO-based 1.8V voltage
	 * switch (pwr-gpios) and makes SDR50/SDR104 fall back to high speed.
	 * The sd1_default pinctrl already sets SD1_PWR_EN mux0 at probe.
	 */
	pr_debug("%s: SD1 CD pinmux ready\n", __func__);
}

static void sdhci_cvi_reset_helper(struct sdhci_host *host, u8 mask)
{
	/* disable Intr before reset */
	sdhci_writel(host, 0, SDHCI_INT_ENABLE);
	sdhci_writel(host, 0, SDHCI_SIGNAL_ENABLE);

	/*
	 * U-Boot enables internal clock before soft reset; without it DWC
	 * RESET_ALL can leave the controller / PHY in a bad state.
	 */
	if (mask & SDHCI_RESET_ALL) {
		u16 clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);

		if (!(clk & SDHCI_CLOCK_INT_EN)) {
			clk |= SDHCI_CLOCK_INT_EN;
			sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
			udelay(150);
		}
	}

	sdhci_reset(host, mask);

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
}

/* Always force CLOCK_PLL_EN; stock sdhci only does this for v4 + SPEC_410+ */
static void sdhci_cv84x6_set_clock(struct sdhci_host *host, unsigned int clock)
{
	u16 clk;
	ktime_t timeout;
	static int emmc_clk_logs;
	u16 clk_before, clk_after;
	bool added_pll = false;

	clk_before = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	sdhci_set_clock(host, clock);
	if (!clock)
		return;

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	if (!(clk & SDHCI_CLOCK_INT_EN)) {
		if (strstr(dev_name(mmc_dev(host->mmc)), "cv-emmc") &&
		    emmc_clk_logs < 8) {
			pr_info("%s: set_clock %u but INT_EN=0 clk=0x%x->0x%x\n",
				mmc_hostname(host->mmc), clock, clk_before,
				clk);
			emmc_clk_logs++;
		}
		return;
	}
	if (!(clk & SDHCI_CLOCK_PLL_EN)) {
		clk |= SDHCI_CLOCK_PLL_EN;
		clk &= ~SDHCI_CLOCK_INT_STABLE;
		sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
		added_pll = true;

		timeout = ktime_add_ms(ktime_get(), 150);
		while (1) {
			clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
			if (clk & SDHCI_CLOCK_INT_STABLE)
				break;
			if (ktime_after(ktime_get(), timeout)) {
				pr_err("%s: PLL never stabilised clk=0x%x\n",
				       mmc_hostname(host->mmc), clk);
				return;
			}
			udelay(10);
		}
	}

	clk_after = sdhci_readw(host, SDHCI_CLOCK_CONTROL);

	/*
	 * Hypothesis check: during detect, clock/power/PHY must be alive.
	 * Cap logs so bring-up is readable.
	 */
	if (strstr(dev_name(mmc_dev(host->mmc)), "cv-emmc") &&
	    emmc_clk_logs < 8) {
		pr_info("%s: set_clock %u before=0x%x after=0x%x added_pll=%d pwr=0x%x phy=0x%x hc2=0x%x\n",
			mmc_hostname(host->mmc), clock, clk_before, clk_after,
			added_pll, sdhci_readb(host, SDHCI_POWER_CONTROL),
			sdhci_readl(host, SDHCI_P_PHY_CNFG),
			sdhci_readw(host, SDHCI_HOST_CONTROL2));
		emmc_clk_logs++;
	}
}

static int sdhci_cv84x6_general_select_drive_strength(
	struct sdhci_host *host, struct mmc_card *card, unsigned int max_dtr,
	int host_drv, int card_drv, int *drv_type)
{
	return MMC_SET_DRIVER_TYPE_A;
}

/*
 * HS400 DLL configuration. Called when the host switches to HS400 (including
 * enhanced strobe) at full frequency. Mirrors U-Boot sdhci_cvi_set_ios_post()
 * HS400 branch.
 */
static void sdhci_cv84x6_hs400_dll_enable(struct sdhci_host *host)
{
	u16 vendor_base = sdhci_cv84x6_vendor_base(host);
	u8 dll_status;
	int dll_retry;
	u16 clk;

	/* Enable negedge data out for HS400 */
	sdhci_writew(host,
		     sdhci_readw(host, vendor_base + DWC_MSHC_CTRL_R) |
			     DWC_MSHC_NEGEDGE_DATAOUT_EN,
		     vendor_base + DWC_MSHC_CTRL_R);

	/*
	 * DLL config (ref: U-Boot / kernel sdhci-of-dwcmshc):
	 *   1. Stop card clock
	 *   2. Program DLL registers
	 *   3. Restart card clock
	 *   4. Enable DLL, wait for lock
	 */
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	sdhci_writeb(host, 0x20, SDHCI_PHY_DLL_CNFG1);
	sdhci_writeb(host, 0x00, SDHCI_PHY_DLL_CNFG2);
	sdhci_writeb(host, 0x60, SDHCI_PHY_DLLDL_CNFG);

	clk |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	/* Enable DLL, wait for lock */
	sdhci_writeb(host, DLL_CTRL_DLL_EN, SDHCI_PHY_DLL_CTRL);

	dll_retry = 100;
	do {
		dll_status = sdhci_readb(host, SDHCI_PHY_DLL_STATUS);
		if (dll_status & DLL_STATUS_LOCK_STS)
			break;
		udelay(100);
	} while (--dll_retry > 0);

	pr_info("%s: HS400 DLL status=0x%x %s\n", mmc_hostname(host->mmc),
		dll_status,
		(dll_status & DLL_STATUS_LOCK_STS) ? "locked" : "timeout");
}

static void sdhci_cv84x6_hs400_dll_disable(struct sdhci_host *host)
{
	u16 vendor_base = sdhci_cv84x6_vendor_base(host);

	/* Disable HS400 features when downgrading */
	sdhci_writew(host,
		     sdhci_readw(host, vendor_base + DWC_MSHC_CTRL_R) &
			     ~DWC_MSHC_NEGEDGE_DATAOUT_EN,
		     vendor_base + DWC_MSHC_CTRL_R);
	sdhci_writeb(host, 0, SDHCI_PHY_DLL_CTRL);
}

static void sdhci_cvi_general_set_uhs_signaling(struct sdhci_host *host,
						unsigned int uhs)
{
	struct mmc_host *mmc = host->mmc;
	u16 ctrl_2;

	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	switch (uhs) {
	case MMC_TIMING_UHS_SDR12:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12;
		break;
	case MMC_TIMING_UHS_SDR25:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
		break;
	case MMC_TIMING_UHS_SDR50:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50;
		break;
	case MMC_TIMING_MMC_HS200:
	case MMC_TIMING_UHS_SDR104:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104;
		break;
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50;
		break;
	case MMC_TIMING_MMC_HS400:
		ctrl_2 |= DWC_MSHC_CTRL_HS400; /* DWC_mshc UHS_MODE_SEL=7 */
		break;
	}

	/*
	 * When clock frequency is less than 100MHz, the feedback clock must be
	 * provided and DLL must not be used so that tuning can be skipped. To
	 * provide feedback clock, the mode selection can be any value less
	 * than 3'b011 in bits [2:0] of HOST CONTROL2 register.
	 */
	if (host->clock <= 100000000 &&
	    (uhs == MMC_TIMING_MMC_HS400 || uhs == MMC_TIMING_MMC_HS200 ||
	     uhs == MMC_TIMING_UHS_SDR104))
		ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;

	/*
	 * bit4 is SAMPLE_CLK_SEL on this IP, but it overlaps the standard
	 * SDHCI driver-type field (DRV_TYPE_A == BIT(4)) that the core rewrote
	 * just before calling us. For the tuned sampling modes we must restore
	 * SAMPLE_CLK_SEL here (the last HOST_CONTROL2 write in set_ios) so the
	 * phase picked during tuning is actually used. execute_tuning() still
	 * owns bit4 while it scans (cleared at start, set at end); it does not
	 * go through set_ios, so scanning is unaffected.
	 */
	if (uhs == MMC_TIMING_MMC_HS400 || uhs == MMC_TIMING_MMC_HS200 ||
	    uhs == MMC_TIMING_UHS_SDR104)
		ctrl_2 |= SDHCI_CTRL_SAMPLE_CLK_SEL;

	dev_dbg(mmc_dev(mmc), "%s: clock=%u uhs=%u ctrl_2=0x%x\n",
		mmc_hostname(host->mmc), host->clock, uhs, ctrl_2);
	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);

	/*
	 * HS400 (including enhanced strobe) needs the PHY DLL. The mmc core only
	 * calls hs400_complete() for non-ES HS400, so HS400ES never enabled the
	 * DLL. Enable/disable it here, keyed off the final UHS mode, so both
	 * HS400 and HS400ES get it once the clock is above 100MHz (below that the
	 * mode is cleared to legacy and the DLL must stay off).
	 */
	if ((ctrl_2 & SDHCI_CTRL_UHS_MASK) == DWC_MSHC_CTRL_HS400)
		sdhci_cv84x6_hs400_dll_enable(host);
	else
		sdhci_cv84x6_hs400_dll_disable(host);
}

static unsigned int sdhci_cvi_general_get_max_clock(struct sdhci_host *host)
{
	pr_debug(DRIVER_NAME ":%s : %d\n", __func__, host->mmc->f_src);
	return host->mmc->f_src;
}

static void sdhci_cv84x6_hs400_complete(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);

	sdhci_cv84x6_hs400_dll_enable(host);
}

/* Clear enhanced strobe before the HS -> DDR switch (User Guide Fig 8-42) */
static int sdhci_cv84x6_hs400_prepare_ddr(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u16 vendor_base = sdhci_cv84x6_vendor_base(host);

	sdhci_writew(host,
		     sdhci_readw(host, vendor_base + DWC_EMMC_CTRL_R) &
			     ~ENH_STROBE_ENABLE,
		     vendor_base + DWC_EMMC_CTRL_R);
	return 0;
}

/*
 * Downgrade out of HS400: disable DLL + negedge dataout so the lower speed
 * modes work after a HS400 -> HS200/HS transition.
 */
static void sdhci_cv84x6_hs400_downgrade(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);

	sdhci_cv84x6_hs400_dll_disable(host);
}

static void sdhci_cv84x6_hs400_enhanced_strobe(struct mmc_host *mmc,
					       struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u16 vendor_base = sdhci_cv84x6_vendor_base(host);
	u16 ctrl;

	ctrl = sdhci_readw(host, vendor_base + DWC_EMMC_CTRL_R);
	if (ios->enhanced_strobe)
		ctrl |= ENH_STROBE_ENABLE;
	else
		ctrl &= ~ENH_STROBE_ENABLE;
	sdhci_writew(host, ctrl, vendor_base + DWC_EMMC_CTRL_R);
}

/* Used for wifi driver due if no SD card detect pin implemented */
static struct mmc_host *wifi_mmc;

int cvi_sdio_rescan(void)
{
	if (!wifi_mmc) {
		pr_err("invalid wifi mmc, please check the argument\n");
		return -EINVAL;
	}

	mmc_detect_change(wifi_mmc, 0);

	wifi_mmc->rescan_entered = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(cvi_sdio_rescan);

// register sysfs
static ssize_t rescan_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	// mmc rescan
	cvi_sdio_rescan();

	return count;
}

static DEVICE_ATTR_WO(rescan);

void sdhci_cvi_emmc_voltage_switch(struct sdhci_host *host)
{
}

static void sdhci_cv84x6_tuning_setup_transfer(struct sdhci_host *host)
{
	/* Step 2: Configure BLOCK_SIZE_R and BLOCKCOUNT_R for tuning data */
	sdhci_writew(host, SDHCI_MAKE_BLKSZ(7, 64), SDHCI_BLOCK_SIZE);
	sdhci_writew(host, 1, SDHCI_BLOCK_COUNT);
}

static void sdhci_cv84x6_tuning_phase_recovery(struct sdhci_host *host)
{
	int timeout = 1000;

	/* Clear error interrupts */
	sdhci_writew(host, 0xFFFF, SDHCI_INT_STATUS);

	/* Step 7: SW_RST_DAT + SW_RST_CMD to clear buffered tuning block */
	sdhci_writeb(host,
		     sdhci_readb(host, SDHCI_SOFTWARE_RESET) | BIT(2) | BIT(1),
		     SDHCI_SOFTWARE_RESET);

	/* Wait for reset to complete */
	while (sdhci_readb(host, SDHCI_SOFTWARE_RESET) & 0x6) {
		if (--timeout <= 0)
			break;
		udelay(1);
	}
	/* Clear reset bits */
	sdhci_writeb(host,
		     sdhci_readb(host, SDHCI_SOFTWARE_RESET) &
			     ~(BIT(2) | BIT(1)),
		     SDHCI_SOFTWARE_RESET);
}

static int sdhci_cv84x6_tuning_try_phase(struct sdhci_host *host, u32 opcode)
{
	int ret;

	/*
	 * mmc_send_tuning() issues the tuning command, reads the tuning block
	 * back and compares it against the expected pattern; its return value
	 * alone decides whether this phase passes. Do NOT additionally poll
	 * BUF_RD_READY afterwards -- the core has already consumed the buffer,
	 * so a second wait would always time out and fail every phase.
	 * (U-Boot polls BUF_RD_READY only because it issues CMD21 by hand
	 * instead of going through the MMC core.)
	 */
	ret = mmc_send_tuning(host->mmc, opcode, NULL);

	/* Clear latched status and reset CMD/DAT before the next phase */
	sdhci_cv84x6_tuning_phase_recovery(host);

	return ret;
}

static void sdhci_cv84x6_set_tap(struct sdhci_host *host, u32 vendor_base,
				 unsigned int phase)
{
	u16 clk;
	u32 at_stat;

	/* Step 9: Turn off clock, set phase, turn on clock */
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk &= ~BIT(2); /* SD_CLK_EN = 0 */
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	/* Set CENTER_PH_CODE */
	at_stat = sdhci_readl(host, vendor_base + DWC_AT_STAT_R);
	at_stat &= ~AT_CENTER_PH_CODE_MASK;
	at_stat |= (phase & AT_CENTER_PH_CODE_MASK);
	sdhci_writel(host, at_stat, vendor_base + DWC_AT_STAT_R);

	udelay(1);

	clk |= BIT(2); /* SD_CLK_EN = 1 */
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
}

static int sdhci_cv84x6_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);
	/* Offset only: sdhci_readl() already adds host->ioaddr */
	u32 vendor_base = sdhci_cv84x6_vendor_base(host);
	u16 ctrl2;
	u32 at_ctrl;
	u8 phase;
	int ret;
	u8 pass_map[TUNE_MAX_PHCODE / 8];
	u8 best_start = 0, best_len = 0;
	u8 cur_start = 0, cur_len = 0;
	bool in_window = false;

	memset(pass_map, 0, sizeof(pass_map));

	/* Step 1: Reset tuning engine (clear SAMPLE_CLK_SEL on this IP) */
	ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl2 &= ~SDHCI_CTRL_EXEC_TUNING;
	ctrl2 &= ~SDHCI_CTRL_SAMPLE_CLK_SEL;
	sdhci_writew(host, ctrl2, SDHCI_HOST_CONTROL2);

	/* Step 2: Setup registers for tuning transfer */
	sdhci_cv84x6_tuning_setup_transfer(host);

	/* Step 3: Enable software tuning */
	at_ctrl = sdhci_readl(host, vendor_base + DWC_AT_CTRL_R);
	at_ctrl |= AT_SW_TUNE_EN;
	sdhci_writel(host, at_ctrl, vendor_base + DWC_AT_CTRL_R);

	/* Step 4: Set CENTER_PH_CODE=0 */
	sdhci_writel(host, 0, vendor_base + DWC_AT_STAT_R);

	/* Step 5-10: Scan all phase codes */
	for (phase = 0; phase < TUNE_MAX_PHCODE; phase++) {
		sdhci_cv84x6_set_tap(host, vendor_base, phase);

		ret = sdhci_cv84x6_tuning_try_phase(host, opcode);
		if (!ret)
			pass_map[phase / 8] |= (1 << (phase % 8));
	}

	/* Step 11: Find center of widest passing window */
	for (phase = 0; phase < TUNE_MAX_PHCODE; phase++) {
		bool pass = pass_map[phase / 8] & (1 << (phase % 8));

		if (pass && !in_window) {
			in_window = true;
			cur_start = phase;
			cur_len = 1;
		} else if (pass && in_window) {
			cur_len++;
		} else if (!pass && in_window) {
			in_window = false;
			if (cur_len > best_len) {
				best_len = cur_len;
				best_start = cur_start;
			}
		}
	}
	if (in_window && cur_len > best_len) {
		best_len = cur_len;
		best_start = cur_start;
	}

	if (best_len == 0)
		return -EIO;

	phase = best_start + best_len / 2;

	/* Step 11 (note): Check DL2_BUSY before final CENTER_PH_CODE write */
	{
		int dl2_retry = 0;

		while (sdhci_readl(host, vendor_base + DWC_AT_STAT_R) &
		       BIT(31)) {
			if (++dl2_retry > 1000)
				break;
			udelay(1);
		}
	}

	sdhci_cv84x6_set_tap(host, vendor_base, phase);

	/* Step 12: Disable software tuning, set SAMPLE_CLK_SEL=1 */
	at_ctrl &= ~AT_SW_TUNE_EN;
	sdhci_writel(host, at_ctrl, vendor_base + DWC_AT_CTRL_R);

	ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl2 |= SDHCI_CTRL_SAMPLE_CLK_SEL;
	sdhci_writew(host, ctrl2, SDHCI_HOST_CONTROL2);

	cvi_host->final_tap = phase;
	pr_debug("%s finished tuning, code:%d\n", __func__, phase);

	return 0;
}

static void sdhci_cv84x6_emmc_reset(struct sdhci_host *host, u8 mask)
{
	u8 ctrl;
	u32 phy_before = 0, phy_after_rst = 0, phy_after_cfg = 0;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);

	if (mask & SDHCI_RESET_ALL)
		phy_before = sdhci_readl(host, SDHCI_P_PHY_CNFG);

	sdhci_cvi_reset_helper(host, mask);

	/*
	 * Hypothesis: RESET_ALL drops PHY pad config on this IP.
	 * Dump before / right after reset / after re-config to prove it.
	 */
	if (mask & SDHCI_RESET_ALL) {
		phy_after_rst = sdhci_readl(host, SDHCI_P_PHY_CNFG);
		sdhci_cv84x6_emmc_phy_config(host);
		sdhci_cv84x6_emmc_ctrl_enable(host);
		phy_after_cfg = sdhci_readl(host, SDHCI_P_PHY_CNFG);
		pr_info("%s: RESET_ALL phy 0x%x -> 0x%x -> 0x%x (reconfig) clk=0x%x pwr=0x%x\n",
			mmc_hostname(host->mmc), phy_before, phy_after_rst,
			phy_after_cfg, sdhci_readw(host, SDHCI_CLOCK_CONTROL),
			sdhci_readb(host, SDHCI_POWER_CONTROL));
	}

	if (cvi_host->quirks & SDHCI_CVI_QUIRK_FORCE_CDTEST) {
		ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
		ctrl |= SDHCI_CTRL_CDTEST_INS | SDHCI_CTRL_CDTEST_EN;
		sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
	}
}

static void sdhci_cv84x6_sd_reset(struct sdhci_host *host, u8 mask)
{
	u8 ctrl;
	int i;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);

	sdhci_cvi_reset_helper(host, mask);

	/*
	 * Linux mmc detect always RESET_ALL on power-up, which drops DWC PHY.
	 * Re-apply pad config WITHOUT another soft-reset (nested RESET_ALL
	 * previously left CMD_INHIBIT stuck). Match U-Boot timeout=0xe.
	 */
	if (mask & SDHCI_RESET_ALL) {
		sdhci_cv84x6_sdio_phy_config(host);
		if (strstr(dev_name(mmc_dev(host->mmc)), "cv-sd1"))
			sdhci_cv84x6_sd1_setup_pad(host);
		else if (strstr(dev_name(mmc_dev(host->mmc)), "cv-sd"))
			sdhci_cv84x6_sd_setup_pad(host);
		sdhci_writeb(host, 0xe, SDHCI_TIMEOUT_CONTROL);
	}

	/* Ensure CMD inhibit is cleared after cmd/data reset (timeout recovery) */
	if (mask & (SDHCI_RESET_CMD | SDHCI_RESET_ALL)) {
		for (i = 0; i < 100; i++) {
			if (!(sdhci_readl(host, SDHCI_PRESENT_STATE) &
			      SDHCI_CMD_INHIBIT))
				break;
			udelay(100);
		}
		if (i == 100)
			pr_err("%s: CMD_INHIBIT stuck after reset mask=0x%x pstate=0x%x\n",
			       mmc_hostname(host->mmc), mask,
			       sdhci_readl(host, SDHCI_PRESENT_STATE));
	}

	if (cvi_host->quirks & SDHCI_CVI_QUIRK_FORCE_CDTEST) {
		ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
		ctrl |= SDHCI_CTRL_CDTEST_INS | SDHCI_CTRL_CDTEST_EN;
		sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
	}
}

static void sdhci_cv84x6_sdio_reset(struct sdhci_host *host, u8 mask)
{
	sdhci_cvi_reset_helper(host, mask);
}

static void sdhci_cv84x6_sd_voltage_switch(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);

	pr_debug("%s\n", __func__);
	if (gpio_is_valid(cvi_host->pwr_gpio))
		gpio_set_value(cvi_host->pwr_gpio, 1);

	mdelay(2);
	/* 84x6: 1.8V switch is GPIO-only; no MS power-switch register */
	cvi_host->sd_voltage_1_8_v = 1;
}

void sdhci_cv84x6_sd1_voltage_switch(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);

	pr_debug("%s\n", __func__);
	if (gpio_is_valid(cvi_host->pwr_gpio))
		gpio_set_value(cvi_host->pwr_gpio, 1);

	mdelay(2);
	/* 84x6: 1.8V switch is GPIO-only; no MS power-switch register */
	cvi_host->sd_voltage_1_8_v = 1;
}

void sdhci_cv84x6_sd_voltage_restore(struct sdhci_host *host, bool bunplug)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);

	if (gpio_is_valid(cvi_host->pwr_gpio))
		gpio_set_value(cvi_host->pwr_gpio, 0);
	mdelay(1);

	/* 84x6: GPIO-only, no MS power-switch register */
	cvi_host->sd_voltage_1_8_v = 0;
}

void sdhci_cv84x6_sd1_voltage_restore(struct sdhci_host *host, bool bunplug)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);

	if (gpio_is_valid(cvi_host->pwr_gpio))
		gpio_set_value(cvi_host->pwr_gpio, 0);
	mdelay(1);

	/* 84x6: GPIO-only, no MS power-switch register */
	cvi_host->sd_voltage_1_8_v = 0;
}

static void sdhci_cv84x6_sd_set_power(struct sdhci_host *host,
				      unsigned char mode, unsigned short vdd)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);

	pr_debug("%s:mode %u, vdd %u\n", __func__, mode, vdd);

	/* CD/PWR mux set at probe + RESET_ALL only (avoid rescan spam) */

	if (mode == MMC_POWER_OFF) {
		pr_debug("[%s]===> power off\n", __func__);
		sdhci_cv84x6_sd_voltage_restore(host, true);
		sdhci_set_power_noreg(host, mode, vdd);
		/* Give slot power rail time to drop before next power-up */
		mdelay(50);
	} else {
		/* POWER_UP / POWER_ON */
		if (!cvi_host->sd_voltage_1_8_v)
			sdhci_cv84x6_sd_voltage_restore(host, false);
		sdhci_set_power_noreg(host, mode, vdd);
		/* SD card power-up delay before CMD0 */
		mdelay(10);
		sdhci_writeb(host, 0xe, SDHCI_TIMEOUT_CONTROL);
	}
}

static void sdhci_cv84x6_sd1_set_power(struct sdhci_host *host,
				       unsigned char mode, unsigned short vdd)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);

	pr_debug("%s:mode %u, vdd %u\n", __func__, mode, vdd);

	/* CD/PWR mux set at probe + RESET_ALL only (avoid rescan spam) */

	if (mode == MMC_POWER_OFF) {
		pr_debug("[%s]===> power off\n", __func__);
		sdhci_cv84x6_sd1_voltage_restore(host, true);
		sdhci_set_power_noreg(host, mode, vdd);
		/* Give slot power rail time to drop before next power-up */
		mdelay(50);
	} else {
		/* POWER_UP / POWER_ON */
		if (!cvi_host->sd_voltage_1_8_v)
			sdhci_cv84x6_sd1_voltage_restore(host, false);
		sdhci_set_power_noreg(host, mode, vdd);
		/* SD card power-up delay before CMD0 */
		mdelay(10);
		sdhci_writeb(host, 0xe, SDHCI_TIMEOUT_CONTROL);
	}
}

static void sdhci_cv84x6_emmc_dump_vendor_regs(struct sdhci_host *host)
{
}

static void sdhci_cv84x6_sd_dump_vendor_regs(struct sdhci_host *host)
{
	// struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	// struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);
}

static void cvi_adma_write_desc(struct sdhci_host *host, void **desc,
				dma_addr_t addr, int len, unsigned int cmd)
{
	int tmplen, offset;

	if (likely(!len || BOUNDARY_OK(addr, len))) {
		sdhci_adma_write_desc(host, desc, addr, len, cmd);
		return;
	}

	offset = addr & (SZ_128M - 1);
	tmplen = SZ_128M - offset;
	sdhci_adma_write_desc(host, desc, addr, tmplen, cmd);

	addr += tmplen;
	len -= tmplen;
	sdhci_adma_write_desc(host, desc, addr, len, cmd);
}

static const struct sdhci_ops sdhci_cv84x6_emmc_ops = {
	.reset = sdhci_cv84x6_emmc_reset,
	.set_clock = sdhci_cv84x6_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.get_max_clock = sdhci_cvi_general_get_max_clock,
	.voltage_switch = sdhci_cvi_emmc_voltage_switch,
	.set_uhs_signaling = sdhci_cvi_general_set_uhs_signaling,
	.platform_execute_tuning = sdhci_cv84x6_execute_tuning,
	.select_drive_strength = sdhci_cv84x6_general_select_drive_strength,
	.dump_vendor_regs = sdhci_cv84x6_emmc_dump_vendor_regs,
	.adma_write_desc = cvi_adma_write_desc,
};

static const struct sdhci_ops sdhci_cv84x6_sd_ops = {
	.reset = sdhci_cv84x6_sd_reset,
	.set_clock = sdhci_cv84x6_set_clock,
	.set_power = sdhci_cv84x6_sd_set_power,
	.set_bus_width = sdhci_set_bus_width,
	.get_max_clock = sdhci_cvi_general_get_max_clock,
	.voltage_switch = sdhci_cv84x6_sd_voltage_switch,
	.set_uhs_signaling = sdhci_cvi_general_set_uhs_signaling,
	/* SDR50/SDR104 require phase tuning (same engine as eMMC HS200) */
	.platform_execute_tuning = sdhci_cv84x6_execute_tuning,
	.select_drive_strength = sdhci_cv84x6_general_select_drive_strength,
	.dump_vendor_regs = NULL,
	.adma_write_desc = cvi_adma_write_desc,
};

static const struct sdhci_ops sdhci_cv84x6_sd1_ops = {
	.reset = sdhci_cv84x6_sd_reset,
	.set_clock = sdhci_cv84x6_set_clock,
	.set_power = sdhci_cv84x6_sd1_set_power,
	.set_bus_width = sdhci_set_bus_width,
	.get_max_clock = sdhci_cvi_general_get_max_clock,
	.voltage_switch = sdhci_cv84x6_sd1_voltage_switch,
	.set_uhs_signaling = sdhci_cvi_general_set_uhs_signaling,
	.platform_execute_tuning = sdhci_cv84x6_execute_tuning,
	.select_drive_strength = sdhci_cv84x6_general_select_drive_strength,
	.dump_vendor_regs = NULL,
	.adma_write_desc = cvi_adma_write_desc,
};

static const struct sdhci_ops sdhci_cv84x6_sdio_ops = {
	.reset = sdhci_cv84x6_sdio_reset,
	.set_clock = sdhci_cv84x6_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.get_max_clock = sdhci_cvi_general_get_max_clock,
	.voltage_switch = sdhci_cv84x6_sd_voltage_switch,
	.set_uhs_signaling = sdhci_cvi_general_set_uhs_signaling,
	.select_drive_strength = sdhci_cv84x6_general_select_drive_strength,
	.platform_execute_tuning = sdhci_cv84x6_execute_tuning,
	.adma_write_desc = cvi_adma_write_desc,
};

static const struct sdhci_ops sdhci_cv84x6_fpga_emmc_ops = {
	.reset = sdhci_cv84x6_sd_reset,
	.set_clock = sdhci_cv84x6_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.get_max_clock = sdhci_cvi_general_get_max_clock,
	.voltage_switch = sdhci_cvi_emmc_voltage_switch,
	.set_uhs_signaling = sdhci_cvi_general_set_uhs_signaling,
	.platform_execute_tuning = sdhci_cv84x6_execute_tuning,
	.select_drive_strength = sdhci_cv84x6_general_select_drive_strength,
	.dump_vendor_regs = sdhci_cv84x6_emmc_dump_vendor_regs,
	.adma_write_desc = cvi_adma_write_desc,
};

static const struct sdhci_ops sdhci_cv84x6_fpga_sd_ops = {
	.reset = sdhci_cv84x6_sd_reset,
	.set_clock = sdhci_cv84x6_set_clock,
#ifndef CONFIG_ARCH_CV186X_FPGA
	.set_power = sdhci_cv84x6_sd_set_power,
#endif
	.set_bus_width = sdhci_set_bus_width,
	.get_max_clock = sdhci_cvi_general_get_max_clock,
	.voltage_switch = sdhci_cv84x6_sd_voltage_switch,
	.set_uhs_signaling = sdhci_cvi_general_set_uhs_signaling,
	.platform_execute_tuning = sdhci_cv84x6_execute_tuning,
	.select_drive_strength = sdhci_cv84x6_general_select_drive_strength,
	.dump_vendor_regs = sdhci_cv84x6_sd_dump_vendor_regs,
	.adma_write_desc = cvi_adma_write_desc,
};

static const struct sdhci_pltfm_data sdhci_cv84x6_emmc_pdata = {
	.ops = &sdhci_cv84x6_emmc_ops,
	.quirks = SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static const struct sdhci_pltfm_data sdhci_cv84x6_sd_pdata = {
	.ops = &sdhci_cv84x6_sd_ops,
	/* ADMA64 like eMMC (same cvi_adma_write_desc); DT needs 64_addressing */
	.quirks = SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	/* NO_1_8_V dropped: SDR50/SDR104 need 1.8V signalling + tuning */
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static const struct sdhci_pltfm_data sdhci_cv84x6_sd1_pdata = {
	.ops = &sdhci_cv84x6_sd1_ops,
	.quirks = SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static const struct sdhci_pltfm_data sdhci_cv84x6_sdio_pdata = {
	.ops = &sdhci_cv84x6_sdio_ops,
	.quirks = SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN | SDHCI_QUIRK2_NO_1_8_V,
};

static const struct sdhci_pltfm_data sdhci_cv84x6_fpga_emmc_pdata = {
	.ops = &sdhci_cv84x6_fpga_emmc_ops,
	.quirks = SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN | SDHCI_QUIRK2_BROKEN_HS200,
};

static const struct sdhci_pltfm_data sdhci_cv84x6_fpga_sd_pdata = {
	.ops = &sdhci_cv84x6_fpga_sd_ops,
	.quirks = SDHCI_QUIRK_INVERTED_WRITE_PROTECT | SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN | SDHCI_QUIRK2_NO_1_8_V,
};

static const struct of_device_id sdhci_cvi_dt_match[] = {
	{.compatible = "cvitek,cv186x-fpga-emmc",
	 .data = &sdhci_cv84x6_fpga_emmc_pdata},
	{.compatible = "cvitek,cv186x-fpga-sd",
	 .data = &sdhci_cv84x6_fpga_sd_pdata},
	{.compatible = "cvitek,cv186x-emmc", .data = &sdhci_cv84x6_emmc_pdata},
	{.compatible = "cvitek,cv186x-sd", .data = &sdhci_cv84x6_sd_pdata},
	{.compatible = "cvitek,cv186x-sd1", .data = &sdhci_cv84x6_sd1_pdata},
	{.compatible = "cvitek,cv186x-sdio", .data = &sdhci_cv84x6_sdio_pdata},

	/* cv84x6 entries */
	{.compatible = "cvitek,cv84x6-emmc", .data = &sdhci_cv84x6_emmc_pdata},
	{.compatible = "cvitek,cv84x6-sd", .data = &sdhci_cv84x6_sd_pdata},
	{.compatible = "cvitek,cv84x6-sd1", .data = &sdhci_cv84x6_sd1_pdata},

	{/* sentinel */}};

MODULE_DEVICE_TABLE(of, sdhci_cvi_dt_match);

static unsigned long sdhci_get_time_ms(void)
{
	ktime_t cur;

	cur = ktime_get();
	// Get milliseconds
	return ktime_to_ms(cur);
}

static void sdhci_cvi_cd_debounce_work(struct work_struct *work)
{
	struct sdhci_cvi_host *cvi_host = container_of(
		work, struct sdhci_cvi_host, cd_debounce_work.work);
	struct mmc_host *host = cvi_host->mmc;
	unsigned long start_time = sdhci_get_time_ms();
	int pre_gpio_cd;
	unsigned long flag;

	spin_lock_irqsave(&cvi_host->cd_debounce_lock, flag);
	pre_gpio_cd = cvi_host->pre_gpio_cd;
	cvi_host->is_debounce_work_running = true;
	spin_unlock_irqrestore(&cvi_host->cd_debounce_lock, flag);

	while (1) {
		if ((sdhci_get_time_ms() - start_time) >=
		    SDHCI_GPIO_CD_DEBOUNCE_TIME) {
			if (pre_gpio_cd == mmc_gpio_get_cd(host)) {
				host->ops->card_event(host);
				mmc_detect_change(
					host,
					msecs_to_jiffies(
						SDHCI_GPIO_CD_DEBOUNCE_DELAY_TIME));
				break;
			}
			pre_gpio_cd = mmc_gpio_get_cd(host);
			start_time = sdhci_get_time_ms();
		}
	}

	spin_lock_irqsave(&cvi_host->cd_debounce_lock, flag);
	cvi_host->is_debounce_work_running = false;
	spin_unlock_irqrestore(&cvi_host->cd_debounce_lock, flag);
}

static irqreturn_t sdhci_cvi_cd_handler(int irq, void *dev_id)
{
	/* Schedule a card detection after a debounce timeout */
	struct mmc_host *host = dev_id;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(mmc_priv(host));
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);
	unsigned long flag;

	spin_lock_irqsave(&cvi_host->cd_debounce_lock, flag);
	cvi_host->pre_gpio_cd = mmc_gpio_get_cd(host);
	if (!cvi_host->pre_gpio_cd)
		host->ever_unplugged = true;
	if (!cvi_host->is_debounce_work_running) {
		cancel_delayed_work(&cvi_host->cd_debounce_work);
		schedule_delayed_work(&cvi_host->cd_debounce_work, 0);
	}
	spin_unlock_irqrestore(&cvi_host->cd_debounce_lock, flag);

	return IRQ_HANDLED;
}

static int sdhci_cvi_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_cvi_host *cvi_host;
	const struct of_device_id *match;
	const struct sdhci_pltfm_data *pdata;
	int ret;
	int gpio_num = -EINVAL;
	u32 extra;

	pr_info(DRIVER_NAME ":%s\n", __func__);
	match = of_match_device(sdhci_cvi_dt_match, &pdev->dev);
	if (!match)
		return -EINVAL;

	pdata = match->data;

	host = sdhci_pltfm_init(pdev, pdata, sizeof(*cvi_host));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	cvi_host = sdhci_pltfm_priv(pltfm_host);
	cvi_host->host = host;
	cvi_host->mmc = host->mmc;
	cvi_host->pdev = pdev;
	cvi_host->core_mem = host->ioaddr;
	cvi_host->pwr_gpio = -EINVAL;

	/* Keep eMMC/SD clocks enabled; clk framework may gate unused clocks */
	cvi_host->clk = devm_clk_get_optional(&pdev->dev, "core");
	if (IS_ERR(cvi_host->clk)) {
		ret = PTR_ERR(cvi_host->clk);
		goto pltfm_free;
	}
	cvi_host->clk100k = devm_clk_get_optional(&pdev->dev, "100k");
	if (IS_ERR(cvi_host->clk100k)) {
		ret = PTR_ERR(cvi_host->clk100k);
		goto pltfm_free;
	}
	cvi_host->clkaxi = devm_clk_get_optional(&pdev->dev, "axi");
	if (IS_ERR(cvi_host->clkaxi)) {
		ret = PTR_ERR(cvi_host->clkaxi);
		goto pltfm_free;
	}
	ret = clk_prepare_enable(cvi_host->clk);
	if (ret)
		goto pltfm_free;
	ret = clk_prepare_enable(cvi_host->clk100k);
	if (ret)
		goto disable_clk;
	ret = clk_prepare_enable(cvi_host->clkaxi);
	if (ret)
		goto disable_clk100k;

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto disable_clkaxi;

	sdhci_get_of_property(pdev);

	if (pdata->ops->hw_reset) {
		cvi_host->reset = devm_reset_control_get(&pdev->dev, "sdio");
		if (IS_ERR(cvi_host->reset)) {
			ret = PTR_ERR(cvi_host->reset);
			goto io_free;
		}
	}

	if (pdev->dev.of_node)
		gpio_num =
			of_get_named_gpio(pdev->dev.of_node, "cvi-cd-gpios", 0);

	if (gpio_is_valid(gpio_num)) {
		cvi_host->cvi_gpio =
			devm_kzalloc(&cvi_host->pdev->dev,
				     sizeof(struct mmc_gpio), GFP_KERNEL);
		if (cvi_host->cvi_gpio) {
			cvi_host->cvi_gpio->cd_gpio_isr = sdhci_cvi_cd_handler;
			cvi_host->cvi_gpio->cd_debounce_delay_ms =
				SDHCI_GPIO_CD_DEBOUNCE_DELAY_TIME;
			cvi_host->cvi_gpio->cd_label = devm_kzalloc(
				&cvi_host->pdev->dev, strlen("cd-gpio-irq") + 1,
				GFP_KERNEL);
			strcpy(cvi_host->cvi_gpio->cd_label, "cd-gpio-irq");
			host->mmc->slot.handler_priv = cvi_host->cvi_gpio;
			ret = mmc_gpiod_request_cd(host->mmc, "cvi-cd", 0,
						   false,
						   SDHCI_GPIO_CD_DEBOUNCE_TIME);
			if (ret) {
				pr_err("card detect request cd failed: %d\n",
				       ret);
			} else {
				INIT_DELAYED_WORK(&cvi_host->cd_debounce_work,
						  sdhci_cvi_cd_debounce_work);
				spin_lock_init(&cvi_host->cd_debounce_lock);
				mmc_gpiod_request_cd_irq(host->mmc);
			}
		}
	}

	if (pdev->dev.of_node) {
		gpio_num = of_get_named_gpio(pdev->dev.of_node, "pwr-gpios", 0);
		if (gpio_is_valid(gpio_num)) {
			cvi_host->pwr_gpio = gpio_num;
			pr_info("get pwr gpio number:%d\n", gpio_num);
			ret = gpio_request_one(gpio_num, GPIOF_OUT_INIT_LOW,
					       "pwr_gpio");
			if (ret)
				goto io_free;
		}
	}

	if (device_property_read_bool(&pdev->dev, "force_cdtest"))
		cvi_host->quirks |= SDHCI_CVI_QUIRK_FORCE_CDTEST;

	// extra adma table cnt for cross 128M boundary handling.
	extra = DIV_ROUND_UP_ULL(dma_get_required_mask(&pdev->dev), SZ_128M);
	if (extra > SDHCI_MAX_SEGS)
		extra = SDHCI_MAX_SEGS;
	host->adma_table_cnt += extra;

	/*
	 * DWC_mshc needs SDHCI v4 mode so sdhci_enable_clk() sets CLOCK_PLL_EN
	 * (BIT3). Without it, SDCLK never reaches the card (U-Boot always
	 * enables VER4 + PLL). Must be before sdhci_add_host().
	 */
	sdhci_enable_v4_mode(host);

	/*
	 * PHY + eMMC RST_N must be ready before sdhci_add_host(): card detect
	 * / first CMD run inside add_host. Matching U-Boot probe order.
	 */
	if (pdata == &sdhci_cv84x6_emmc_pdata ||
	    pdata == &sdhci_cv84x6_fpga_emmc_pdata) {
		sdhci_cv84x6_emmc_phy_pad_setting(host);
		sdhci_cv84x6_emmc_ctrl_enable(host);
		sdhci_cv84x6_emmc_dev_reset(host);
		sdhci_cv84x6_dump_emmc_state(host, "pre-add_host");
	} else if (pdata == &sdhci_cv84x6_sd_pdata ||
		   pdata == &sdhci_cv84x6_sd1_pdata ||
		   pdata == &sdhci_cv84x6_sdio_pdata ||
		   pdata == &sdhci_cv84x6_fpga_sd_pdata) {
		sdhci_cv84x6_sdio_phy_pad_setting(host);
		if (pdata == &sdhci_cv84x6_sd_pdata ||
		    pdata == &sdhci_cv84x6_fpga_sd_pdata)
			sdhci_cv84x6_sd_setup_pad(host);
		else if (pdata == &sdhci_cv84x6_sd1_pdata)
			sdhci_cv84x6_sd1_setup_pad(host);
		sdhci_writeb(host, 0xe, SDHCI_TIMEOUT_CONTROL);
	}

	ret = sdhci_setup_host(host);
	if (ret)
		goto gpio_release;

	/*
	 * sdhci_setup_host() derives HS200/HS400 from HW CAPS. DT currently
	 * leaves those modes commented out for bring-up (max 50MHz legacy).
	 * Clear them here or mmc_init_card will enter execute_tuning.
	 */
	if (pdata == &sdhci_cv84x6_emmc_pdata ||
	    pdata == &sdhci_cv84x6_fpga_emmc_pdata) {
		if (!device_property_read_bool(&pdev->dev, "mmc-hs200-1_8v") &&
		    !device_property_read_bool(&pdev->dev, "mmc-hs200-1_2v"))
			host->mmc->caps2 &= ~(MMC_CAP2_HS200_1_8V_SDR |
					      MMC_CAP2_HS200_1_2V_SDR);
		if (!device_property_read_bool(&pdev->dev, "mmc-hs400-1_8v") &&
		    !device_property_read_bool(&pdev->dev, "mmc-hs400-1_2v"))
			host->mmc->caps2 &= ~(MMC_CAP2_HS400_1_8V |
					      MMC_CAP2_HS400_1_2V |
					      MMC_CAP2_HS400_ES);

		/*
		 * Wire the HS400 mmc_host_ops hooks (DLL lock, enhanced strobe,
		 * negedge dataout) when HS400 survives above. These live in
		 * host->mmc_host_ops, populated by sdhci_alloc_host().
		 */
		if (host->mmc->caps2 &
		    (MMC_CAP2_HS400_1_8V | MMC_CAP2_HS400_1_2V)) {
			host->mmc_host_ops.hs400_complete =
				sdhci_cv84x6_hs400_complete;
			host->mmc_host_ops.hs400_prepare_ddr =
				sdhci_cv84x6_hs400_prepare_ddr;
			host->mmc_host_ops.hs400_downgrade =
				sdhci_cv84x6_hs400_downgrade;
		}
		if (host->mmc->caps2 & MMC_CAP2_HS400_ES)
			host->mmc_host_ops.hs400_enhanced_strobe =
				sdhci_cv84x6_hs400_enhanced_strobe;
	}

	/*
	 * FPGA SD instance keeps PIO (no working DMA on FPGA). Real SD/SD1 use
	 * ADMA64 like eMMC (64_addressing in DT + cvi_adma_write_desc).
	 */
	if (pdata == &sdhci_cv84x6_fpga_sd_pdata) {
		host->flags &= ~(SDHCI_USE_ADMA | SDHCI_USE_SDMA |
				 SDHCI_USE_64_BIT_DMA | SDHCI_REQ_USE_DMA);
		sdhci_writeb(host, 0xe, SDHCI_TIMEOUT_CONTROL);
	}
	if (pdata == &sdhci_cv84x6_sd_pdata ||
	    pdata == &sdhci_cv84x6_sd1_pdata) {
		sdhci_writeb(host, 0xe, SDHCI_TIMEOUT_CONTROL);
		pr_info("%s: SD f_max=%u caps=0x%x caps2=0x%x flags=0x%x\n",
			mmc_hostname(host->mmc), host->mmc->f_max,
			host->mmc->caps, host->mmc->caps2, host->flags);
	}

	ret = __sdhci_add_host(host);
	if (ret)
		goto cleanup_host;

	/*
	 * mmc_add_host() schedules detect asynchronously. For eMMC rootfs boot
	 * without a working rootwait, flush so mmcblk0 exists before we return.
	 */
	if (pdata == &sdhci_cv84x6_emmc_pdata ||
	    pdata == &sdhci_cv84x6_fpga_emmc_pdata) {
		flush_delayed_work(&host->mmc->detect);
		sdhci_cv84x6_dump_emmc_state(host, "post-detect");
		if (!host->mmc->card)
			pr_err("mmc0: eMMC card not enumerated after detect\n");
	}

	platform_set_drvdata(pdev, cvi_host);

	if (strstr(dev_name(mmc_dev(host->mmc)), "wifi-sd")) {
		wifi_mmc = host->mmc;

		// add /sys/class/mmc_host/mmc*/rescan node
		if (device_create_file(&host->mmc->class_dev, &dev_attr_rescan))
			pr_err("Fail to create rescan sysfs file.\n");

	} else {
		wifi_mmc = NULL;
	}

	/* device proc entry */
	if ((!proc_cvi_dir) &&
	    (strstr(dev_name(mmc_dev(host->mmc)), "cv-sd"))) {
		ret = cvi_proc_init(cvi_host);
		if (ret)
			pr_err("device proc init is failed!");
	}

	return 0;

cleanup_host:
	sdhci_cleanup_host(host);
gpio_release:
	if (gpio_is_valid(cvi_host->pwr_gpio))
		gpio_free(cvi_host->pwr_gpio);
io_free:
disable_clkaxi:
	clk_disable_unprepare(cvi_host->clkaxi);
disable_clk100k:
	clk_disable_unprepare(cvi_host->clk100k);
disable_clk:
	clk_disable_unprepare(cvi_host->clk);
pltfm_free:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int sdhci_cvi_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cvi_host *cvi_host = sdhci_pltfm_priv(pltfm_host);
	int dead =
		(readl_relaxed(host->ioaddr + SDHCI_INT_STATUS) == 0xffffffff);

	if (gpio_is_valid(cvi_host->pwr_gpio))
		gpio_free(cvi_host->pwr_gpio);

	sdhci_remove_host(host, dead);
	sdhci_pltfm_free(pdev);

	cvi_proc_shutdown(cvi_host);

	return 0;
}

static void save_reg(struct sdhci_host *host, struct sdhci_cvi_host *cvi_host)
{
	cvi_host->reg_ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	cvi_host->reg_clk_ctrl = sdhci_readl(host, SDHCI_CLOCK_CONTROL);
	cvi_host->reg_host_ctrl = sdhci_readl(host, SDHCI_HOST_CONTROL);
}

static void restore_reg(struct sdhci_host *host,
			struct sdhci_cvi_host *cvi_host)
{
	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
	sdhci_writew(host, cvi_host->reg_ctrl2, SDHCI_HOST_CONTROL2);
	sdhci_writel(host, cvi_host->reg_clk_ctrl, SDHCI_CLOCK_CONTROL);
	sdhci_writel(host, cvi_host->reg_host_ctrl, SDHCI_HOST_CONTROL);
}

static int sdhci_cvi_suspend(struct device *dev)
{
	struct sdhci_cvi_host *cvi_host = dev_get_drvdata(dev);
	struct sdhci_host *host = cvi_host->host;

	if (!host)
		return 0;

	save_reg(host, cvi_host);

	return 0;
}

static int sdhci_cvi_resume(struct device *dev)
{
	struct sdhci_cvi_host *cvi_host = dev_get_drvdata(dev);
	struct sdhci_host *host = cvi_host->host;

	if (!host)
		return 0;

	restore_reg(host, cvi_host);

	return 0;
}

static const struct dev_pm_ops sdhci_cvi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdhci_cvi_suspend, sdhci_cvi_resume)};

static struct platform_driver sdhci_cvi_driver = {
	.probe = sdhci_cvi_probe,
	.remove = sdhci_cvi_remove,
	.driver = {
		.name = DRIVER_NAME,
		.pm = &sdhci_cvi_pm_ops,
		.of_match_table = sdhci_cvi_dt_match,
	},
};
module_platform_driver(sdhci_cvi_driver);

MODULE_DESCRIPTION("Cvitek Secure Digital Host Controller Interface driver");
MODULE_LICENSE("GPL v2");
