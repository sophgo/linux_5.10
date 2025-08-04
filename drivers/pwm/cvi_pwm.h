#ifndef __CVI_PWM_H
#define __CVI_PWM_H

#define PWM_REG_NUM	0xD4
/**
 * struct cv_pwm_channel - private data of PWM channel
 * @period:	current period in nanoseconds programmed to the hardware
 * @hlperiod:	current duty time in nanoseconds programmed to the hardware
 * @out_count:	expected count of period of pwm output
 * @count_mode:	count mode enable
 */
struct cv_pwm_channel {
	u32 period;
	u32 hlperiod;
	u32 out_count;
	bool count_mode;
};

/**
 * struct cv_pwm_chip - private data of PWM chip
 * @chip:	generic PWM chip
 * @base:	base address of mapped PWM registers
 * @base_clk:	base clock used to drive the timers
 * @clk_rate:	base clock rate
 * @no_polarity:	support polarity or not
 * @pwm_saved_regs:	for suspend/resume
 * @shift_mode:	shift mode enable
 * @shift_enable:	enable shift mode
 * @shift[4]:	pwm0~pwm3 shift in nanoseconds
 */
struct cv_pwm_chip {
	struct pwm_chip chip;
	void __iomem *base;
	struct clk *base_clk;
	u64 clk_rate;
	bool no_polarity;
#ifdef CONFIG_PM_SLEEP
	uint32_t pwm_saved_regs[PWM_REG_NUM];
#endif
	bool shift_mode;
	bool shift_enable;
	u32 shift[4];
};

#endif /* __CVI_PWM_H */
