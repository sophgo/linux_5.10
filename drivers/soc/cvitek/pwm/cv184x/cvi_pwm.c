#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/version.h>

#define REG_GROUP			0x8 //discard
#define PWM_REG_NUM				0x80 //discard
/*--------------------------------------------------------------*/
#define REG_PWM0_BASE		0x03060000
#define REG_PWM1_BASE		0x03061000
#define REG_PWM2_BASE		0x03062000
#define REG_PWM3_BASE		0x03063000
#define REG_PWM_BASE		0x03060000
/*---------------------------------------------------------------*/
#define REG_HLPERIOD0			0x0
#define REG_PERIOD0				0x4
#define REG_HLPERIOD1			0x8
#define REG_PERIOD1				0xc
#define REG_HLPERIOD2			0x10
#define REG_PERIOD2				0x14
#define REG_HLPERIOD3			0x18
#define REG_PERIOD3				0x1c
#define REG_HLPERIOD4			0x20
#define REG_PERIOD4				0x24
#define REG_HLPERIOD5			0x28
#define REG_PERIOD5				0x2c


#define REG_FREQ0NUM			0x30
#define REG_FREQ0DATA			0x34
#define REG_FREQ1NUM			0x38
#define REG_FREQ1DATA			0x3c
#define REG_FREQ2NUM			0x40
#define REG_FREQ2DATA			0x44
#define REG_FREQ3NUM			0x48
#define REG_FREQ3DATA			0x4c
#define REG_FREQ4NUM			0x50
#define REG_FREQ4DATA			0x54
#define REG_FREQ5NUM			0x58
#define REG_FREQ5DATA			0x5c

#define REG_POLARITY			0x60
#define REG_PWMSTART			0x64
#define REG_PWMDONE             0x68
#define REG_PWMUPDATE            0x6c

#define REG_PCOUNT0				0x70
#define REG_PCOUNT1				0x74
#define REG_PCOUNT2				0x78
#define REG_PCOUNT3				0x7C
#define REG_PCOUNT4				0x80
#define REG_PCOUNT5				0x84

#define REG_PCOUNT0_STATUS		0x88
#define REG_PCOUNT1_STATUS		0x8c
#define REG_PCOUNT2_STATUS		0x90
#define REG_PCOUNT3_STATUS		0x94
#define REG_PCOUNT4_STATUS		0x98
#define REG_PCOUNT5_STATUS		0x9c

#define REG_PWMCNT0             0xa0
#define REG_PWMCNT1             0xa4
#define REG_PWMCNT2             0xa8
#define REG_PWMCNT3             0xac
#define REG_PWMCNT4             0xb0
#define REG_PWMCNT5             0xb4

#define REG_SHIFTCOUNT0			0xb8
#define REG_SHIFTCOUNT1			0xbc
#define REG_SHIFTCOUNT2			0xc0
#define REG_SHIFTCOUNT3			0xc4
#define REG_SHIFTCOUNT4			0xc8
#define REG_SHIFTCOUNT5			0xcc

#define REG_SHIFTSTART			0xd0

#define REG_FREQEN				0xd4

#define REG_FREQ0_HCOUNT        0xd8
#define REG_FREQ0_LCOUNT        0xdc
#define REG_FREQ1_HCOUNT        0xe0
#define REG_FREQ1_LCOUNT        0xe4
#define REG_FREQ2_HCOUNT        0xe8
#define REG_FREQ2_LCOUNT        0xec
#define REG_FREQ3_HCOUNT        0xf0
#define REG_FREQ3_LCOUNT        0xf4
#define REG_FREQ4_HCOUNT        0xf8
#define REG_FREQ4_LCOUNT        0xfc
#define REG_FREQ5_HCOUNT        0x100
#define REG_FREQ5_LCOUNT        0x104


#define REG_FREQ0_DONE_NUM		0x108 //0
#define REG_FREQ1_DONE_NUM		0x10c
#define REG_FREQ2_DONE_NUM		0x110
#define REG_FREQ3_DONE_NUM		0x114
#define REG_FREQ4_DONE_NUM		0x118
#define REG_FREQ5_DONE_NUM		0x11c

#define REG_PWM_OE				0x120
#define REG_PWM_REV0            0x124 //dummy registers
#define REG_PWM_VER             0x12c //version

#define REG_MASK_PEROID0        0x12c
#define REG_MASK_PEROID1        0x130
#define REG_MASK_PEROID2        0x134
#define REG_MASK_PEROID3        0x138
#define REG_MASK_PEROID4        0x13c
#define REG_MASK_PEROID5        0x140
#define REG_MASK_CNT0			0x144
#define REG_MASK_CNT1			0x148
#define REG_MASK_CNT2			0x14c
#define REG_MASK_CNT3			0x150
#define REG_MASK_CNT4			0x154
#define REG_MASK_CNT5			0x158

#define REG_PWM0_START_POINT	0x174
#define REG_PWM0_END_POINT		0x178
#define REG_PWM1_START_POINT	0x17c
#define REG_PWM1_END_POINT		0x180
#define REG_PWM2_START_POINT	0x184
#define REG_PWM2_END_POINT		0x188
#define REG_PWM3_START_POINT	0x18c
#define REG_PWM3_END_POINT		0x190
#define REG_PWM4_START_POINT	0x194
#define REG_PWM4_END_POINT		0x198
#define REG_PWM5_START_POINT	0x19c
#define REG_PWM5_END_POINT		0x1a0

#define REG_PWM_START_TOGGLE	0x1d4
#define REG_PWM_END_TOGGLE		0x1d8
#define REG_PWM2ADC_CNT_H0		0x1a4
#define REG_PWM2ADC_CNT_L0		0x1a8

enum _pwm_polarity {
	POLARITY_RESTORE,
	POLARITY_MODIFY,
};
/**
 * struct cv_pwm_channel - private data of PWM channel
 * @period_ns:	current period in nanoseconds programmed to the hardware
 * @duty_ns:	current duty time in nanoseconds programmed to the hardware
 * @tin_ns:	time of one timer tick in nanoseconds with current timer rate
 */
struct cv_pwm_channel {
	u32 period;
	u32 hlperiod;
	u32 start_point;
	u32 end_point;
};

/**
 * struct cv_pwm_chip - private data of PWM chip
 * @chip:		generic PWM chip
 * @variant:		local copy of hardware variant data
 * @inverter_mask:	inverter status for all channels - one bit per channel
 * @base:		base address of mapped PWM registers
 * @base_clk:		base clock used to drive the timers
 * @tclk0:		external clock 0 (can be ERR_PTR if not present)
 * @tclk1:		external clock 1 (can be ERR_PTR if not present)
 */
struct cv_pwm_chip {
	struct pwm_chip chip;
	void __iomem *base;
	struct clk *base_clk;
	u8 polarity_mask;
	u8 special_polarity_flag;
	bool no_polarity;
	uint32_t pwm_saved_regs[PWM_REG_NUM];
};


static inline
struct cv_pwm_chip *to_cv_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct cv_pwm_chip, chip);
}

static int pwm_cv_request(struct pwm_chip *chip, struct pwm_device *pwm_dev)
{
	struct cv_pwm_channel *channel;

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return -ENOMEM;

	return pwm_set_chip_data(pwm_dev, channel);
}

static void pwm_cv_free(struct pwm_chip *chip, struct pwm_device *pwm_dev)
{
	struct cv_pwm_channel *channel = pwm_get_chip_data(pwm_dev);

	pwm_set_chip_data(pwm_dev, NULL);
	kfree(channel);
}

static int _pwm_cv_reset_polarity(struct pwm_chip *chip, struct pwm_device *pwm_dev,
									enum _pwm_polarity polarity)
{
	struct cv_pwm_chip *our_chip = to_cv_pwm_chip(chip);
	unsigned long polarity_mask = (unsigned long)our_chip->polarity_mask;

	if (polarity) {
		set_bit(pwm_dev->hwpwm, (unsigned long *)&our_chip->special_polarity_flag);
		change_bit(pwm_dev->hwpwm, &polarity_mask);
		pr_debug("%s: special polarity!\n", __func__);
	} else {
		clear_bit(pwm_dev->hwpwm, (unsigned long *)&our_chip->special_polarity_flag);
		pr_debug("%s: original polarity!\n", __func__);
	}
	writel((u8)polarity_mask, our_chip->base + REG_POLARITY);

	return 0;
}

static int pwm_cv_config(struct pwm_chip *chip, struct pwm_device *pwm_dev,
			     int duty_ns, int period_ns)
{
	struct cv_pwm_chip *our_chip = to_cv_pwm_chip(chip);
	struct cv_pwm_channel *channel = pwm_get_chip_data(pwm_dev);
	u64 cycles;
	unsigned long value;
	u32 toggle_mask = 0x3 << pwm_dev->hwpwm * 0x2;
	/*make sure polarity is original*/
	writel(our_chip->polarity_mask, our_chip->base + REG_POLARITY);

	cycles = clk_get_rate(our_chip->base_clk);
	pr_debug("clk_get_rate=%llu\n", cycles);

	cycles *= period_ns;
	do_div(cycles, NSEC_PER_SEC);

	channel->period = cycles;
	cycles = cycles * duty_ns;
	do_div(cycles, period_ns);

	channel->hlperiod = channel->period - cycles;
	if (cycles == 0) {
		/* end point force to 0*/
		writel(readl(our_chip->base + REG_PWM_END_TOGGLE) | toggle_mask, our_chip->base + REG_PWM_END_TOGGLE);
	}
	if (cycles == channel->period) {
		/* if want duty_cycle = 100% ,set duty_cycle = 0% and polarity inversed*/
		writel(readl(our_chip->base + REG_PWM_END_TOGGLE) | toggle_mask, our_chip->base + REG_PWM_END_TOGGLE);
		/*set polarity to inversed*/
		writel(readl(our_chip->base + REG_POLARITY) ^ (1 << pwm_dev->hwpwm), our_chip->base + REG_POLARITY);

	}
	pr_debug("%s: period_ns=%d, duty_ns=%d\n", __func__, period_ns, duty_ns);

	writel(channel->period, our_chip->base + 0x8 * pwm_dev->hwpwm + REG_PERIOD0);
	writel(readl(our_chip->base + REG_PWM_START_TOGGLE) | toggle_mask, our_chip->base + REG_PWM_START_TOGGLE);
	if (channel->hlperiod != 0 && channel->hlperiod != channel->period) {
		/*start point force to 0*/
		writel(readl(our_chip->base + REG_PWM_END_TOGGLE) & ~toggle_mask, our_chip->base + REG_PWM_END_TOGGLE);
		writel(channel->hlperiod, our_chip->base + 0x8 * pwm_dev->hwpwm + REG_PWM0_END_POINT);
	}	
	pr_debug("%s: REG_PERIOD = 0x%x, REG_HLPERIOD = 0x%x\n", __func__,
			 readl(our_chip->base + REG_GROUP * pwm_dev->hwpwm + REG_PERIOD0),
			 readl(our_chip->base + REG_GROUP * pwm_dev->hwpwm + REG_HLPERIOD0));//discard

	value = readl(our_chip->base + REG_PWMSTART);
	set_bit(pwm_dev->hwpwm, &value);
	writel(value, our_chip->base + REG_PWMUPDATE);
	pr_debug("%s: REG_PWMUPDATE = 0x%lx\n", __func__, value);

	clear_bit(pwm_dev->hwpwm, &value);
	writel(value, our_chip->base + REG_PWMUPDATE);
	pr_debug("%s: REG_PWMUPDATE = 0x%lx\n", __func__, value);

	return 0;
}

static int pwm_cv_enable(struct pwm_chip *chip, struct pwm_device *pwm_dev)
{
	struct cv_pwm_chip *our_chip = to_cv_pwm_chip(chip);
	uint32_t pwm_start_value;
	uint32_t value;

	pwm_start_value = readl(our_chip->base + REG_PWMSTART);

	writel(pwm_start_value & (~(1 << (pwm_dev->hwpwm))), our_chip->base + REG_PWMSTART);

	value = pwm_start_value | (1 << pwm_dev->hwpwm);
	pr_debug("pwm_cv_enable: value = %x\n", value);

	writel(value, our_chip->base + REG_PWM_OE);
	writel(value, our_chip->base + REG_PWMSTART);

	return 0;
}

static void pwm_cv_disable(struct pwm_chip *chip,
			       struct pwm_device *pwm_dev)
{
	struct cv_pwm_chip *our_chip = to_cv_pwm_chip(chip);
	uint32_t value;

	value = readl(our_chip->base + REG_PWMSTART) & (~(1 << (pwm_dev->hwpwm)));
	pr_debug("pwm_cv_disable: value = %x\n", value);
	writel(value, our_chip->base + REG_PWM_OE);
	writel(value, our_chip->base + REG_PWMSTART);

	writel(1, our_chip->base + REG_GROUP * pwm_dev->hwpwm + REG_PERIOD0);
	writel(2, our_chip->base + REG_GROUP * pwm_dev->hwpwm + REG_HLPERIOD0);
}

static int pwm_cv_set_polarity(struct pwm_chip *chip,
				    struct pwm_device *pwm_dev,
				    enum pwm_polarity polarity)
{
	struct cv_pwm_chip *our_chip = to_cv_pwm_chip(chip);
	u32 val = readl(our_chip->base + REG_POLARITY);
	if (our_chip->no_polarity) {
		dev_err(chip->dev, "no polarity\n");
		return -ENOTSUPP;
	}

	if (polarity == PWM_POLARITY_NORMAL) {
		if (our_chip->polarity_mask & (1 << pwm_dev->hwpwm))
			val ^= (1 << pwm_dev->hwpwm);
		our_chip->polarity_mask &= ~(1 << pwm_dev->hwpwm);
	} else {
		if (!(our_chip->polarity_mask & (1 << pwm_dev->hwpwm)))
			val ^= (1 << pwm_dev->hwpwm);
		our_chip->polarity_mask |= 1 << pwm_dev->hwpwm;
	}
	writel(val, our_chip->base + REG_POLARITY);
	return 0;
}

// #if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
// static int pwm_cv_apply(struct pwm_chip *chip, struct pwm_device *pwm,
// 			      const struct pwm_state *state)
// #else
// static int pwm_cv_apply(struct pwm_chip *chip, struct pwm_device *pwm,
// 			      struct pwm_state *state)
// #endif
// {
// 	int ret;

// 	ret = pwm_cv_config(chip, pwm, state->duty_cycle, state->period);
// 	if (ret) {
// 		dev_err(chip->dev, "pwm apply err\n");
// 		return ret;
// 	}
// 	dev_dbg(chip->dev, "pwm_cv_apply state->enabled = %d\n", state->enabled);
// 	if (state->enabled)
// 		ret = pwm_cv_enable(chip, pwm);
// 	else
// 		pwm_cv_disable(chip, pwm);

// 	if (ret) {
// 		dev_err(chip->dev, "pwm apply failed\n");
// 		return ret;
// 	}
// 	return ret;
// }

static int pwm_cv_capture(struct pwm_chip *chip, struct pwm_device *pwm_dev,
			   struct pwm_capture *result, unsigned long timeout)
{
	struct cv_pwm_chip *our_chip = to_cv_pwm_chip(chip);
	uint32_t value;
	u64 cycles;
	u64 cycle_cnt;

	// Set corresponding bit in PWM_OE to 0
	value = readl(our_chip->base + REG_PWM_OE) & (~(1 << (pwm_dev->hwpwm)));
	writel(value, our_chip->base + REG_PWM_OE);
	pr_debug("pwm_cv_capture: REG_PWM_OE = %x\n", value);

	// Enable capture
	writel(1, our_chip->base + REG_GROUP * pwm_dev->hwpwm + REG_FREQ0NUM);
	writel(1 << pwm_dev->hwpwm, our_chip->base + REG_FREQEN);
	pr_debug("pwm_cv_capture: REG_FREQEN = %x\n", readl(our_chip->base + REG_FREQEN));

	// Wait for done status
	while (timeout--) {
		mdelay(1);
		pr_debug("delay 1ms\n");
		value = readl(our_chip->base + REG_FREQ0_DONE_NUM + pwm_dev->hwpwm * 4);
		if (value != 0)
			break;
	}

	// Read cycle count
	cycle_cnt = readl(our_chip->base + REG_GROUP * pwm_dev->hwpwm + REG_FREQ0DATA) + 1;
	pr_debug("pwm_cv_capture: cycle_cnt = %llu\n", cycle_cnt);

	// Convert from cycle count to period ns
	cycles = clk_get_rate(our_chip->base_clk);
	cycle_cnt *= NSEC_PER_SEC;
	do_div(cycle_cnt, cycles);

	result->period = cycle_cnt;
	result->duty_cycle = 0;

	// Disable capture
	writel(0x0, our_chip->base + REG_FREQEN);

	return 0;
}

static const struct pwm_ops pwm_cv_ops = {
	.request	= pwm_cv_request,
	.free		= pwm_cv_free,
	.enable		= pwm_cv_enable,
	.disable	= pwm_cv_disable,
	.config		= pwm_cv_config,
	.set_polarity	= pwm_cv_set_polarity,
	/*.apply		= pwm_cv_apply,*/
	.capture	= pwm_cv_capture,
	.owner		= THIS_MODULE,
};

static const struct of_device_id cv_pwm_match[] = {
	{ .compatible = "cvitek,cvi-pwm" },
	{ },
};
MODULE_DEVICE_TABLE(of, cv_pwm_match);

static int pwm_cv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cv_pwm_chip *chip;
	struct resource *res;
	int ret;

	// pr_debug("%s\n", __func__);

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->chip.dev = &pdev->dev;
	chip->chip.ops = &pwm_cv_ops;
	chip->chip.base = -1;
	chip->polarity_mask = 0;
	chip->special_polarity_flag = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chip->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(chip->base))
		return PTR_ERR(chip->base);

	chip->base_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(chip->base_clk)) {
		dev_err(dev, "failed to get pwm source clk\n");
		return PTR_ERR(chip->base_clk);
	}

	ret = clk_prepare_enable(chip->base_clk);
	if (ret < 0) {
		dev_err(dev, "failed to enable base clock\n");
		return ret;
	}

	//pwm-num default is 6, compatible with MARS3
	if (of_property_read_bool(pdev->dev.of_node, "pwm-num"))
		device_property_read_u32(&pdev->dev, "pwm-num", &chip->chip.npwm);
	else
		chip->chip.npwm = 6;

	//no_polarity default is false(have polarity) , compatible with bm1682
	if (of_property_read_bool(pdev->dev.of_node, "no-polarity"))
		chip->no_polarity = true;
	else
		chip->no_polarity = false;
	// pr_debug("chip->chip.npwm = %d  chip->no_polarity = %d\n", chip->chip.npwm, chip->no_polarity);

	platform_set_drvdata(pdev, chip);

	ret = pwmchip_add(&chip->chip);
	if (ret < 0) {
		dev_err(dev, "failed to register PWM chip\n");
		clk_disable_unprepare(chip->base_clk);
		return ret;
	}

	return 0;
}

static int pwm_cv_remove(struct platform_device *pdev)
{
	struct cv_pwm_chip *chip = platform_get_drvdata(pdev);
	int ret;

	ret = pwmchip_remove(&chip->chip);
	if (ret < 0)
		return ret;

	clk_disable_unprepare(chip->base_clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pwm_cv_suspend(struct device *dev)
{
	struct cv_pwm_chip *chip = dev_get_drvdata(dev);

	memcpy_fromio(chip->pwm_saved_regs, chip->base, PWM_REG_NUM * 4);

	return 0;
}

static int pwm_cv_resume(struct device *dev)
{
	struct cv_pwm_chip *chip = dev_get_drvdata(dev);

	memcpy_toio(chip->base, chip->pwm_saved_regs, PWM_REG_NUM * 4);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pwm_cv_pm_ops, pwm_cv_suspend,
			 pwm_cv_resume);

static struct platform_driver pwm_cv_driver = {
	.driver		= {
		.name	= "cvitek-pwm",
		.pm	= &pwm_cv_pm_ops,
		.of_match_table = of_match_ptr(cv_pwm_match),
	},
	.probe		= pwm_cv_probe,
	.remove		= pwm_cv_remove,
};
module_platform_driver(pwm_cv_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark.Hsieh");
MODULE_DESCRIPTION("Cvitek PWM driver");
