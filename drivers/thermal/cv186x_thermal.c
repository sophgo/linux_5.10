// SPDX-License-Identifier: GPL-2.0
/*
 * CVITEK cv186x thermal driver
 *
 * Copyright 2023 CVITEK Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/thermal.h>
#include <linux/types.h>

#define tempsen_top_tempsen_version                       0x0
#define tempsen_top_tempsen_ctrl                          0x4
#define tempsen_top_tempsen_status                        0x8
#define tempsen_top_tempsen_set                           0xc
#define tempsen_top_tempsen_intr_en                       0x10
#define tempsen_top_tempsen_intr_clr                      0x14
#define tempsen_top_tempsen_intr_sta                      0x18
#define tempsen_top_tempsen_intr_raw                      0x1c
#define tempsen_top_tempsen_ch0_result                    0x20
#define tempsen_top_tempsen_ch1_result                    0x24
#define tempsen_top_tempsen_ch2_result                    0x28
#define tempsen_top_tempsen_ch3_result                    0x2c
#define tempsen_top_tempsen_ddr_temp_ctrl                 0x30
#define tempsen_top_tempsen_ddr_temp_th                   0x34
#define tempsen_top_tempsen_ch0_temp_th                   0x40
#define tempsen_top_tempsen_ch1_temp_th                   0x44
#define tempsen_top_tempsen_ch2_temp_th                   0x48
#define tempsen_top_tempsen_ch3_temp_th                   0x4c
#define tempsen_top_Overheat_th                           0x60
#define tempsen_top_tempsen_auto_cycle                    0x64
#define tempsen_top_tempsen_auto_prediv                   0x64
#define tempsen_top_tempsen_overheat_ctrl                 0x68
#define tempsen_top_tempsen_overheat_countdown            0x6c
#define tempsen_top_tempsen_ch0_temp_th_cnt               0x70
#define tempsen_top_tempsen_ch1_temp_th_cnt               0x74
#define tempsen_top_tempsen_ch2_temp_th_cnt               0x78
#define tempsen_top_tempsen_ch3_temp_th_cnt               0x7c
#define tempsen_top_tempsen_test_force                    0x80
#define tempsen_top_reg_ip_version                        0x0
#define tempsen_top_reg_ip_version_OFFSET                 0
#define tempsen_top_reg_ip_version_MASK                   0xffffffff
#define tempsen_top_reg_tempsen_en                        0x4
#define tempsen_top_reg_tempsen_en_OFFSET                 0
#define tempsen_top_reg_tempsen_en_MASK                   0x1
#define tempsen_top_reg_tempsen_sel                       0x4
#define tempsen_top_reg_tempsen_sel_OFFSET                4
#define tempsen_top_reg_tempsen_sel_MASK                  0xf0
#define tempsen_top_reg_tempsen_ovhl_cnt_to_irq           0x4
#define tempsen_top_reg_tempsen_ovhl_cnt_to_irq_OFFSET    16
#define tempsen_top_reg_tempsen_ovhl_cnt_to_irq_MASK      0xff0000
#define tempsen_top_reg_tempsen_udll_cnt_to_irq           0x4
#define tempsen_top_reg_tempsen_udll_cnt_to_irq_OFFSET    24
#define tempsen_top_reg_tempsen_udll_cnt_to_irq_MASK      0xff000000
#define tempsen_top_sta_tempsen_busy                      0x8
#define tempsen_top_sta_tempsen_busy_OFFSET               0
#define tempsen_top_sta_tempsen_busy_MASK                 0x1
#define tempsen_top_reg_tempsen_bgen                      0xc
#define tempsen_top_reg_tempsen_bgen_OFFSET               0
#define tempsen_top_reg_tempsen_bgen_MASK                 0x1
#define tempsen_top_reg_tempsen_chopen                    0xc
#define tempsen_top_reg_tempsen_chopen_OFFSET             1
#define tempsen_top_reg_tempsen_chopen_MASK               0x2
#define tempsen_top_reg_tempsen_choppol                   0xc
#define tempsen_top_reg_tempsen_choppol_OFFSET            2
#define tempsen_top_reg_tempsen_choppol_MASK              0x4
#define tempsen_top_reg_tempsen_clkpol                    0xc
#define tempsen_top_reg_tempsen_clkpol_OFFSET             3
#define tempsen_top_reg_tempsen_clkpol_MASK               0x8
#define tempsen_top_reg_tempsen_chopsel                   0xc
#define tempsen_top_reg_tempsen_chopsel_OFFSET            4
#define tempsen_top_reg_tempsen_chopsel_MASK              0x30
#define tempsen_top_reg_tempsen_accsel                    0xc
#define tempsen_top_reg_tempsen_accsel_OFFSET             6
#define tempsen_top_reg_tempsen_accsel_MASK               0xc0
#define tempsen_top_reg_tempsen_cyc_clkdiv                0xc
#define tempsen_top_reg_tempsen_cyc_clkdiv_OFFSET         8
#define tempsen_top_reg_tempsen_cyc_clkdiv_MASK           0xff00
#define tempsen_top_reg_tempsen_tsel                      0xc
#define tempsen_top_reg_tempsen_tsel_OFFSET               16
#define tempsen_top_reg_tempsen_tsel_MASK                 0x30000
#define tempsen_top_sta_tempsen_intr_en                   0x10
#define tempsen_top_sta_tempsen_intr_en_OFFSET            0
#define tempsen_top_sta_tempsen_intr_en_MASK              0xffffffff
#define tempsen_top_sta_tempsen_intr_clr                  0x14
#define tempsen_top_sta_tempsen_intr_clr_OFFSET           0
#define tempsen_top_sta_tempsen_intr_clr_MASK             0xffffffff
#define tempsen_top_sta_tempsen_intr_sta                  0x18
#define tempsen_top_sta_tempsen_intr_sta_OFFSET           0
#define tempsen_top_sta_tempsen_intr_sta_MASK             0xffffffff
#define tempsen_top_sta_tempsen_intr_raw                  0x1c
#define tempsen_top_sta_tempsen_intr_raw_OFFSET           0
#define tempsen_top_sta_tempsen_intr_raw_MASK             0xffffffff
#define tempsen_top_sta_tempsen_ch0_result                0x20
#define tempsen_top_sta_tempsen_ch0_result_OFFSET         0
#define tempsen_top_sta_tempsen_ch0_result_MASK           0x1fff
#define tempsen_top_sta_tempsen_ch0_max_result            0x20
#define tempsen_top_sta_tempsen_ch0_max_result_OFFSET     16
#define tempsen_top_sta_tempsen_ch0_max_result_MASK       0x1fff0000
#define tempsen_top_clr_tempsen_ch0_max_result            0x20
#define tempsen_top_clr_tempsen_ch0_max_result_OFFSET     31
#define tempsen_top_clr_tempsen_ch0_max_result_MASK       0x80000000
#define tempsen_top_sta_tempsen_ch1_result                0x24
#define tempsen_top_sta_tempsen_ch1_result_OFFSET         0
#define tempsen_top_sta_tempsen_ch1_result_MASK           0x1fff
#define tempsen_top_sta_tempsen_ch1_max_result            0x24
#define tempsen_top_sta_tempsen_ch1_max_result_OFFSET     16
#define tempsen_top_sta_tempsen_ch1_max_result_MASK       0x1fff0000
#define tempsen_top_clr_tempsen_ch1_max_result            0x24
#define tempsen_top_clr_tempsen_ch1_max_result_OFFSET     31
#define tempsen_top_clr_tempsen_ch1_max_result_MASK       0x80000000
#define tempsen_top_sta_tempsen_ch2_result                0x28
#define tempsen_top_sta_tempsen_ch2_result_OFFSET         0
#define tempsen_top_sta_tempsen_ch2_result_MASK           0x1fff
#define tempsen_top_sta_tempsen_ch2_max_result            0x28
#define tempsen_top_sta_tempsen_ch2_max_result_OFFSET     16
#define tempsen_top_sta_tempsen_ch2_max_result_MASK       0x1fff0000
#define tempsen_top_clr_tempsen_ch2_max_result            0x28
#define tempsen_top_clr_tempsen_ch2_max_result_OFFSET     31
#define tempsen_top_clr_tempsen_ch2_max_result_MASK       0x80000000
#define tempsen_top_sta_tempsen_ch3_result                0x2c
#define tempsen_top_sta_tempsen_ch3_result_OFFSET         0
#define tempsen_top_sta_tempsen_ch3_result_MASK           0x1fff
#define tempsen_top_sta_tempsen_ch3_max_result            0x2c
#define tempsen_top_sta_tempsen_ch3_max_result_OFFSET     16
#define tempsen_top_sta_tempsen_ch3_max_result_MASK       0x1fff0000
#define tempsen_top_clr_tempsen_ch3_max_result            0x2c
#define tempsen_top_clr_tempsen_ch3_max_result_OFFSET     31
#define tempsen_top_clr_tempsen_ch3_max_result_MASK       0x80000000
#define tempsen_top_reg_tempsen_ddr_out_en                0x30
#define tempsen_top_reg_tempsen_ddr_out_en_OFFSET         0
#define tempsen_top_reg_tempsen_ddr_out_en_MASK           0x1
#define tempsen_top_reg_tempsen_ddr_ow_en                 0x30
#define tempsen_top_reg_tempsen_ddr_ow_en_OFFSET          1
#define tempsen_top_reg_tempsen_ddr_ow_en_MASK            0x2
#define tempsen_top_reg_tempsen_ddr_ow_val                0x30
#define tempsen_top_reg_tempsen_ddr_ow_val_OFFSET         2
#define tempsen_top_reg_tempsen_ddr_ow_val_MASK           0x4
#define tempsen_top_sta_tempsen_ddr_high_temp_raw         0x30
#define tempsen_top_sta_tempsen_ddr_high_temp_raw_OFFSET  8
#define tempsen_top_sta_tempsen_ddr_high_temp_raw_MASK    0xf00
#define tempsen_top_sta_tempsen_ddr_high_temp_o           0x30
#define tempsen_top_sta_tempsen_ddr_high_temp_o_OFFSET    12
#define tempsen_top_sta_tempsen_ddr_high_temp_o_MASK      0xf000
#define tempsen_top_reg_tempsen_ddr_hi_th                 0x34
#define tempsen_top_reg_tempsen_ddr_hi_th_OFFSET          0
#define tempsen_top_reg_tempsen_ddr_hi_th_MASK            0x1fff
#define tempsen_top_reg_tempsen_ddr_lo_th                 0x34
#define tempsen_top_reg_tempsen_ddr_lo_th_OFFSET          16
#define tempsen_top_reg_tempsen_ddr_lo_th_MASK            0x1fff0000
#define tempsen_top_reg_tempsen_ch0_hi_th                 0x40
#define tempsen_top_reg_tempsen_ch0_hi_th_OFFSET          0
#define tempsen_top_reg_tempsen_ch0_hi_th_MASK            0x1fff
#define tempsen_top_reg_tempsen_ch0_lo_th                 0x40
#define tempsen_top_reg_tempsen_ch0_lo_th_OFFSET          16
#define tempsen_top_reg_tempsen_ch0_lo_th_MASK            0x1fff0000
#define tempsen_top_reg_tempsen_ch1_hi_th                 0x44
#define tempsen_top_reg_tempsen_ch1_hi_th_OFFSET          0
#define tempsen_top_reg_tempsen_ch1_hi_th_MASK            0x1fff
#define tempsen_top_reg_tempsen_ch1_lo_th                 0x44
#define tempsen_top_reg_tempsen_ch1_lo_th_OFFSET          16
#define tempsen_top_reg_tempsen_ch1_lo_th_MASK            0x1fff0000
#define tempsen_top_reg_tempsen_ch2_hi_th                 0x48
#define tempsen_top_reg_tempsen_ch2_hi_th_OFFSET          0
#define tempsen_top_reg_tempsen_ch2_hi_th_MASK            0x1fff
#define tempsen_top_reg_tempsen_ch2_lo_th                 0x48
#define tempsen_top_reg_tempsen_ch2_lo_th_OFFSET          16
#define tempsen_top_reg_tempsen_ch2_lo_th_MASK            0x1fff0000
#define tempsen_top_reg_tempsen_ch3_hi_th                 0x4c
#define tempsen_top_reg_tempsen_ch3_hi_th_OFFSET          0
#define tempsen_top_reg_tempsen_ch3_hi_th_MASK            0x1fff
#define tempsen_top_reg_tempsen_ch3_lo_th                 0x4c
#define tempsen_top_reg_tempsen_ch3_lo_th_OFFSET          16
#define tempsen_top_reg_tempsen_ch3_lo_th_MASK            0x1fff0000
#define tempsen_top_reg_tempsen_overheat_th               0x60
#define tempsen_top_reg_tempsen_overheat_th_OFFSET        0
#define tempsen_top_reg_tempsen_overheat_th_MASK          0x1fff
#define tempsen_top_reg_tempsen_auto_cycle                0x64
#define tempsen_top_reg_tempsen_auto_cycle_OFFSET         0
#define tempsen_top_reg_tempsen_auto_cycle_MASK           0xffffff
#define tempsen_top_reg_tempsen_auto_prediv               0x64
#define tempsen_top_reg_tempsen_auto_prediv_OFFSET        24
#define tempsen_top_reg_tempsen_auto_prediv_MASK          0xff000000
#define tempsen_top_reg_tempsen_overheat_cycle            0x68
#define tempsen_top_reg_tempsen_overheat_cycle_OFFSET     0
#define tempsen_top_reg_tempsen_overheat_cycle_MASK       0x3fffffff
#define tempsen_top_reg_overheat_reset_clr                0x68
#define tempsen_top_reg_overheat_reset_clr_OFFSET         30
#define tempsen_top_reg_overheat_reset_clr_MASK           0x40000000
#define tempsen_top_reg_overheat_reset_en                 0x68
#define tempsen_top_reg_overheat_reset_en_OFFSET          31
#define tempsen_top_reg_overheat_reset_en_MASK            0x80000000
#define tempsen_top_sta_tempsen_overheat_countdown        0x6c
#define tempsen_top_sta_tempsen_overheat_countdown_OFFSET 0
#define tempsen_top_sta_tempsen_overheat_countdown_MASK   0x3fffffff
#define tempsen_top_sta_overheat_reset                    0x6c
#define tempsen_top_sta_overheat_reset_OFFSET             31
#define tempsen_top_sta_overheat_reset_MASK               0x80000000
#define tempsen_top_sta_ch0_over_hi_temp_th_cnt           0x70
#define tempsen_top_sta_ch0_over_hi_temp_th_cnt_OFFSET    0
#define tempsen_top_sta_ch0_over_hi_temp_th_cnt_MASK      0xff
#define tempsen_top_sta_ch0_under_lo_temp_th_cnt          0x70
#define tempsen_top_sta_ch0_under_lo_temp_th_cnt_OFFSET   8
#define tempsen_top_sta_ch0_under_lo_temp_th_cnt_MASK     0xff00
#define tempsen_top_reg_ch0_temp_th_cnt_clr               0x70
#define tempsen_top_reg_ch0_temp_th_cnt_clr_OFFSET        16
#define tempsen_top_reg_ch0_temp_th_cnt_clr_MASK          0x10000
#define tempsen_top_sta_ch1_over_hi_temp_th_cnt           0x74
#define tempsen_top_sta_ch1_over_hi_temp_th_cnt_OFFSET    0
#define tempsen_top_sta_ch1_over_hi_temp_th_cnt_MASK      0xff
#define tempsen_top_sta_ch1_under_lo_temp_th_cnt          0x74
#define tempsen_top_sta_ch1_under_lo_temp_th_cnt_OFFSET   8
#define tempsen_top_sta_ch1_under_lo_temp_th_cnt_MASK     0xff00
#define tempsen_top_reg_ch1_temp_th_cnt_clr               0x74
#define tempsen_top_reg_ch1_temp_th_cnt_clr_OFFSET        16
#define tempsen_top_reg_ch1_temp_th_cnt_clr_MASK          0x10000
#define tempsen_top_sta_ch2_over_hi_temp_th_cnt           0x78
#define tempsen_top_sta_ch2_over_hi_temp_th_cnt_OFFSET    0
#define tempsen_top_sta_ch2_over_hi_temp_th_cnt_MASK      0xff
#define tempsen_top_sta_ch2_under_lo_temp_th_cnt          0x78
#define tempsen_top_sta_ch2_under_lo_temp_th_cnt_OFFSET   8
#define tempsen_top_sta_ch2_under_lo_temp_th_cnt_MASK     0xff00
#define tempsen_top_reg_ch2_temp_th_cnt_clr               0x78
#define tempsen_top_reg_ch2_temp_th_cnt_clr_OFFSET        16
#define tempsen_top_reg_ch2_temp_th_cnt_clr_MASK          0x10000
#define tempsen_top_sta_ch3_over_hi_temp_th_cnt           0x7c
#define tempsen_top_sta_ch3_over_hi_temp_th_cnt_OFFSET    0
#define tempsen_top_sta_ch3_over_hi_temp_th_cnt_MASK      0xff
#define tempsen_top_sta_ch3_under_lo_temp_th_cnt          0x7c
#define tempsen_top_sta_ch3_under_lo_temp_th_cnt_OFFSET   8
#define tempsen_top_sta_ch3_under_lo_temp_th_cnt_MASK     0xff00
#define tempsen_top_reg_ch3_temp_th_cnt_clr               0x7c
#define tempsen_top_reg_ch3_temp_th_cnt_clr_OFFSET        16
#define tempsen_top_reg_ch3_temp_th_cnt_clr_MASK          0x10000
#define tempsen_top_reg_tempsen_force_result              0x80
#define tempsen_top_reg_tempsen_force_result_OFFSET       0
#define tempsen_top_reg_tempsen_force_result_MASK         0x1fff
#define tempsen_top_reg_tempsen_force_valid               0x80
#define tempsen_top_reg_tempsen_force_valid_OFFSET        13
#define tempsen_top_reg_tempsen_force_valid_MASK          0x2000
#define tempsen_top_reg_tempsen_force_busy                0x80
#define tempsen_top_reg_tempsen_force_busy_OFFSET         14
#define tempsen_top_reg_tempsen_force_busy_MASK           0x4000
#define tempsen_top_reg_tempsen_force_en                  0x80
#define tempsen_top_reg_tempsen_force_en_OFFSET           15
#define tempsen_top_reg_tempsen_force_en_MASK             0x8000

#define RTC_EN_THM_SHDN 0x050260c4
#define HW_THM_SHDN_EN 0x05025008

#define TEMPSENSER_NUM 1
#define TEMPSEN_MASK(REG_NAME)   tempsen_top_##REG_NAME##_MASK
#define TEMPSEN_OFFSET(REG_NAME) tempsen_top_##REG_NAME##_OFFSET
#define TEMPSEN_SET(BASE_ADDR, REG_NAME, VAL)                             \
    clrsetbits(BASE_ADDR + tempsen_top_##REG_NAME,                        \
               TEMPSEN_MASK(REG_NAME), (VAL) << TEMPSEN_OFFSET(REG_NAME))
#define TEMPSEN_GET(BASE_ADDR, REG_NAME)                                  \
    ((readl(BASE_ADDR + tempsen_top_##REG_NAME) &                         \
      TEMPSEN_MASK(REG_NAME)) >>                                          \
     TEMPSEN_OFFSET(REG_NAME))

#define MAX(a, b, c) (a > b ? (a > c ? a : c) : (b > c ? b : c))

static void __maybe_unused clrsetbits(void __iomem *reg, u32 clrval,
                                      u32 setval)
{
    u32 regval;

    regval = readl(reg);
    regval &= ~(clrval);
    regval |= setval;
    writel(regval, reg);
}

struct cv186x_thermal_zone {
    unsigned int ch;
    void __iomem *base;
    struct cv186x_thermal *ct;
};

struct cv186x_thermal {
    struct device *dev;
    void __iomem *base;
    struct clk *clk_tempsen;
};

static void __iomem *rtc_en_thm; 
static void __iomem *hw_thm;

static void cv186x_thermal_init(struct cv186x_thermal *ct)
{
    void __iomem *base = ct->base;
    u32 regval;
    /* u32 force_val = 0x30; */

    /* clear all interrupt status */
    regval = TEMPSEN_GET(base, sta_tempsen_intr_raw);
    TEMPSEN_SET(base, sta_tempsen_intr_clr, regval);

    /* clear max result */
    TEMPSEN_SET(base, clr_tempsen_ch0_max_result, 1);
    TEMPSEN_SET(base, clr_tempsen_ch1_max_result, 1);

    /* set chop period to 3:1024T */
    TEMPSEN_SET(base, reg_tempsen_chopsel, 0x3);

    /* set acc period to 2:2048T*/
    TEMPSEN_SET(base, reg_tempsen_accsel, 0x2);

    /* set tempsen clock divider to 25M/(0x31+1)= 0.5M ,T=2us */
    TEMPSEN_SET(base, reg_tempsen_cyc_clkdiv, 0x31);

    /* set reg_tempsen_auto_cycle */
    TEMPSEN_SET(base, reg_tempsen_auto_cycle, 500000); // 1s

    /* set ddr hi/lo threshold */
    // TEMPSEN_SET(base, reg_tempsen_ddr_hi_th, 0x400); // 85 C
    // TEMPSEN_SET(base, reg_tempsen_ddr_lo_th, 0x3E4); // 75 C

    /* enable ddr auto refresh rate ctrl signal output */
    // TEMPSEN_SET(base, reg_tempsen_ddr_out_en, 1);

    /* set/enable force value*/
    // TEMPSEN_SET(base, reg_tempsen_force_result, force_val);
    // TEMPSEN_SET(base, reg_tempsen_force_en, 1);

    /* set rtc hw poweroff, 127 C */
    TEMPSEN_SET(base, reg_tempsen_overheat_th, 0x423);
    TEMPSEN_SET(base, reg_tempsen_overheat_cycle, 3000000); // 6s
    TEMPSEN_SET(base, reg_overheat_reset_clr, 0x1);
    TEMPSEN_SET(base, reg_overheat_reset_en, 0x1);
    writel(0x1, rtc_en_thm);
    writel(readl(hw_thm) | 0xffff0004, hw_thm);

    /* enable tempsen channel */
    TEMPSEN_SET(base, reg_tempsen_sel, 0x7);
    TEMPSEN_SET(base, reg_tempsen_en, 1);
}

static void cv186x_thermal_uninit(struct cv186x_thermal *ct)
{
    void __iomem *base = ct->base;
    u32 regval;

    /* disable all tempsen channel */
    TEMPSEN_SET(base, reg_tempsen_sel, 0);
    TEMPSEN_SET(base, reg_tempsen_en, 0);

    /* clear all interrupt status */
    regval = TEMPSEN_GET(base, sta_tempsen_intr_raw);
    TEMPSEN_SET(base, sta_tempsen_intr_clr, regval);
}

int find_mid(int a, int b, int c)
{

	if ((a >= b && a <= c) || (a >= c && a <= b))
		return a;
	else if ((b >= a && b <= c) || (b >= c && b <= a))
		return b;
	else
		return c;

}

static int calc_temp(int ts1, int ts2, int ts3)
{
	int temp1, temp2, temp3;

	// TS1: y=0.3757x-278.79
	temp1 = (ts1 * 3757 - 2787900) / 10;

	// TS2: y=0.3563x-259.37
	temp2 = (ts2 *3563 - 2593700) / 10;

	// TS3: y=0.3729x-278.11
	temp3 = (ts3 * 3729 - 2781100) / 10;

	pr_debug("avg temp: (ts1, ts2, ts3) = (%d, %d, %d)\n", temp1, temp2, temp3);
	return find_mid(temp1, temp2, temp3);
}

static void array_sort(int *arr, size_t num)
{
	size_t i, j;
	int temp;

	if (!arr || num <= 1)
		return;

	for (i = 0; i < num - 1; i++) {
		for (j = 0; j < num - i - 1; j++) {
			if (arr[j] > arr[j + 1]) {
				temp = arr[j];
				arr[j] = arr[j + 1];
				arr[j + 1] = temp;
			}
		}
	}
}

static int calc_average(int array[])
{
	int sum, buffer_array[20];

	memcpy(buffer_array, array, 20 * sizeof(int));

	array_sort(buffer_array, 20);
	sum = buffer_array[8] + buffer_array[9] + buffer_array[10] + buffer_array[11];
	// pr_debug("%d %d %d %d", buffer_array[8], buffer_array[9], buffer_array[10], buffer_array[11]);
	return sum / 4;
}

static int cv186x_read_temp(void *data, int *temperature)
{
	struct cv186x_thermal_zone *ctz = data;
	void __iomem *base = ctz->base;
	unsigned int ch = ctz->ch;
	static int index, read_cnt, r1[20], r2[20], r3[20];
	int average_r1, average_r2, average_r3;

	/* read temperature */
	switch (ch) {
	case 0:
		r1[index] = TEMPSEN_GET(base, sta_tempsen_ch0_result);
		r2[index] = TEMPSEN_GET(base, sta_tempsen_ch1_result);
		r3[index] = TEMPSEN_GET(base, sta_tempsen_ch2_result);
		if (likely(read_cnt == 19)) {
			average_r1 = calc_average(r1);
			average_r2 = calc_average(r2);
			average_r3 = calc_average(r3);
		} else {
			average_r1 = r1[index];
			average_r2 = r2[index];
			average_r3 = r3[index];
			read_cnt++;
		}
		index = (index + 1) % 20;
		break;
	case 1:
		r2[index] = TEMPSEN_GET(base, sta_tempsen_ch1_result);
		average_r2 = r2[index];
		index = (index + 1) % 20;
		break;
	case 2:
		r3[index] = TEMPSEN_GET(base, sta_tempsen_ch2_result);
		average_r3 = r3[index];
		index = (index + 1) % 20;
		break;
	default:
		average_r1 = average_r2 = average_r3 = 0;
	}

	// pr_debug("ts1: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
	//	r1[0], r1[1], r1[2], r1[3], r1[4], r1[5], r1[6], r1[7], r1[8], r1[9],
	//	r1[10], r1[11], r1[12], r1[13], r1[14], r1[15], r1[16], r1[17], r1[18], r1[19]);
	// pr_debug("ts2: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
	//	r2[0], r2[1], r2[2], r2[3], r2[4], r2[5], r2[6], r2[7], r2[8], r2[9],
	//	r2[10], r2[11], r2[12], r2[13], r2[14], r2[15], r2[16], r2[17], r2[18], r2[19]);
	// pr_debug("ts3: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
	//	r3[0], r3[1], r3[2], r3[3], r3[4], r3[5], r3[6], r3[7], r3[8], r3[9],
	//	r3[10], r3[11], r3[12], r3[13], r3[14], r3[15], r3[16], r3[17], r3[18], r3[19]);
	// pr_debug("avg: (ts1, ts2, ts3) = (%d, %d, %d)\n", average_r1, average_r2, average_r3);
	*temperature = calc_temp(average_r1, average_r2, average_r3);
	pr_debug("ch%d temp = %d mC\n", ch, *temperature);

	return 0;
}

static const struct thermal_zone_of_device_ops cv186x_thermal_ops = {
    .get_temp = cv186x_read_temp,
};

static const struct of_device_id cv186x_thermal_of_match[] = {
    {
        .compatible = "cvitek,cv186x-thermal",
    },
    {},
};
MODULE_DEVICE_TABLE(of, cv186x_thermal_of_match);

static int cv186x_thermal_probe(struct platform_device *pdev)
{
    struct cv186x_thermal *ct;
    struct cv186x_thermal_zone *ctz;
    struct resource *res;
    struct thermal_zone_device *tz;
    int i;

    rtc_en_thm = devm_ioremap(&pdev->dev, RTC_EN_THM_SHDN, 4);
    if (!rtc_en_thm) {
        dev_err(&pdev->dev, "failed to remap rtc_en_thm addr\n");
        return -EACCES;
    }

    hw_thm = devm_ioremap(&pdev->dev, HW_THM_SHDN_EN, 4);
    if (!hw_thm) {
        dev_err(&pdev->dev, "failed to remap hw_thm addr\n");
        return -EACCES;
    }

    ct = devm_kzalloc(&pdev->dev, sizeof(*ct), GFP_KERNEL);
    if (!ct)
        return -ENOMEM;

    ct->clk_tempsen = devm_clk_get(&pdev->dev, "clk_tempsen");
    if (IS_ERR(ct->clk_tempsen)) {
        dev_err(&pdev->dev, "failed to get clk_tempsen\n");
        return PTR_ERR(ct->clk_tempsen);
    }

    /* enable clk_tempsen */
    clk_prepare_enable(ct->clk_tempsen);

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    ct->base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(ct->base)) {
        dev_err(&pdev->dev, "failed to map tempsen registers\n");
        return PTR_ERR(ct->base);
    }

    ct->dev = &pdev->dev;

    cv186x_thermal_init(ct);

    platform_set_drvdata(pdev, ct);

    for (i = 0; i < TEMPSENSER_NUM; i++) {
        ctz = devm_kzalloc(&pdev->dev, sizeof(*ctz), GFP_KERNEL);
        if (!ctz)
            return -ENOMEM;

        ctz->base = ct->base;
        ctz->ct = ct;
        ctz->ch = i;
        tz = devm_thermal_zone_of_sensor_register(&pdev->dev, i, ctz,
                                                  &cv186x_thermal_ops);
        if (IS_ERR(tz)) {
            dev_err(&pdev->dev, "failed to register thermal zone %d\n", i);
            return PTR_ERR(tz);
        }
    }

    return 0;
}

static int cv186x_thermal_remove(struct platform_device *pdev)
{
    struct cv186x_thermal *ct = platform_get_drvdata(pdev);

    cv186x_thermal_uninit(ct);

    clk_disable_unprepare(ct->clk_tempsen);

    return 0;
}

#ifdef CONFIG_PM_SLEEP
static int cv186x_thermal_suspend(struct device *dev)
{
    struct cv186x_thermal *ct = dev_get_drvdata(dev);

    cv186x_thermal_uninit(ct);

    clk_disable_unprepare(ct->clk_tempsen);

    return 0;
}

static int cv186x_thermal_resume(struct device *dev)
{
    struct cv186x_thermal *ct = dev_get_drvdata(dev);

    /* enable clk_tempsen */
    clk_prepare_enable(ct->clk_tempsen);

    cv186x_thermal_init(ct);

    return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(cv186x_thermal_pm_ops, cv186x_thermal_suspend,
                         cv186x_thermal_resume);

static struct platform_driver cv186x_thermal_driver = {
    .probe = cv186x_thermal_probe,
    .remove = cv186x_thermal_remove,
    .driver =
        {
            .name = "cv186x-thermal",
            .pm = &cv186x_thermal_pm_ops,
            .of_match_table = cv186x_thermal_of_match,
        },
};

module_platform_driver(cv186x_thermal_driver);
MODULE_DESCRIPTION("cv186x thermal driver");
MODULE_LICENSE("GPL");
