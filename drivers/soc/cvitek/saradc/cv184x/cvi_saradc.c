/*
 * Cvitek SoCs saradc driver
 *
 * Copyright (c) 2023 Cvitek Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/uaccess.h>
#include <linux/dma-buf.h>
#include <linux/dma-direction.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include "plat_cv184x.h"

#define EFUSE_ADC_TRIM_REG 0x18
#define TOP_ADC_TRIM_MASK 0xf0000000
#define TOP_ADC_TRIM_OFFSET 28
#define RTC_ADC_TRIM_MASK 0x0f000000
#define RTC_ADC_TRIM_OFFSET 24


enum ADCChannel {
	/* Top domain ADC0~2, every ADC has 3 channels */
	ADC1 = 1, /* ADC1 <== ADC1 */
	ADC2, /* ADC2 <== ADC2 */
	ADC3, /* ADC3 <== ADC3 */
	ADC4, /* ADC4 <== PWM0_BUCK */
	ADC5, /* ADC5 <== USB_VBUS_EN */
	ADC6, /* ADC6 <== USB_ID */
	ADC7, /* ADC7 <== IIC3_SDA */
	ADC8, /* ADC8 <== IIC3_SCL */
	ADC9, /* ADC9 <== CAM_MCLK1 */
	/* RTC domain RTC_ADC0~1, every ADC has 3 channels */
	PWR_ADC1,/* PWR_ADC1 <== PWR_SEQ3 */
	PWR_ADC2,/* PWR_ADC2 <== PWR_SEQ1 */
	PWR_ADC3,/* PWR_ADC3 <== PWR_VBAT_DET */
	PWR_ADC4,/* PWR_ADC4 <== PWR_GPIO0 */
	PWR_ADC5,/* PWR_ADC5 <== PWR_GPIO1 */
	PWR_ADC6,/* PWR_ADC6 <== PWR_GPIO2 */
};

#define IOBLK_G1_REG_ADC1		0x03001810
#define IOBLK_G1_REG_ADC2		0x0300180C
#define IOBLK_G1_REG_ADC3		0x03001808

#define IOBLK_G1_REG_PWM0_BUCK		0x03001804
#define IOBLK_G1_REG_USB_VBUS_EN		0x03001818
#define IOBLK_G1_REG_USB_ID		0x03001814

#define IOBLK_G11_REG_IIC3_SDA		0x03001B18
#define IOBLK_G11_REG_IIC3_SCL		0x03001B14
#define IOBLK_G11_REG_CAM_MCLK1		0x03001B0c

#define IOBLK_GRTC_REG_PWR_SEQ3		0x05027010
#define IOBLK_GRTC_REG_PWR_SEQ1		0x05027008
#define IOBLK_GRTC_REG_PWR_VBAT_DET		0x05027000

#define IOBLK_GRTC_REG_PWR_GPIO0		0x0502702c
#define IOBLK_GRTC_REG_PWR_GPIO1		0x05027030
#define IOBLK_GRTC_REG_PWR_GPIO2		0x05027034

#define CVI_SARADC_FILTER_NSAMP 15
#define CVI_SARADC_FILTER_K 3

static void io_config(u32 channel)
{
	void *vaddr = NULL;
	switch (channel) {
	case ADC1:
		PINMUX_CONFIG(ADC1, XGPIOB_3);
		vaddr = ioremap(IOBLK_G1_REG_ADC1, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case ADC2:
		PINMUX_CONFIG(ADC2, XGPIOB_2);
		vaddr = ioremap(IOBLK_G1_REG_ADC2, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case ADC3:
		PINMUX_CONFIG(ADC3, XGPIOB_1);
		vaddr = ioremap(IOBLK_G1_REG_ADC3, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case ADC4:
		PINMUX_CONFIG(PWM0_BUCK, XGPIOB_0);
		vaddr = ioremap(IOBLK_G1_REG_PWM0_BUCK, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case ADC5:
		PINMUX_CONFIG(USB_VBUS_EN, XGPIOB_5);
		vaddr = ioremap(IOBLK_G1_REG_USB_VBUS_EN, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case ADC6:
		PINMUX_CONFIG(USB_ID, XGPIOB_4);
		vaddr = ioremap(IOBLK_G1_REG_USB_ID, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case ADC7:
		PINMUX_CONFIG(IIC3_SDA, XGPIOA_6);
		vaddr = ioremap(IOBLK_G11_REG_IIC3_SDA, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case ADC8:
		PINMUX_CONFIG(IIC3_SCL, XGPIOA_5);
		vaddr = ioremap(IOBLK_G11_REG_IIC3_SCL, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case ADC9:
		PINMUX_CONFIG(CAM_MCLK1, XGPIOA_3);
		vaddr = ioremap(IOBLK_G11_REG_CAM_MCLK1, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case PWR_ADC1:
		PINMUX_CONFIG(PWR_SEQ3, PWR_GPIO_5);
		vaddr = ioremap(IOBLK_GRTC_REG_PWR_SEQ3, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case PWR_ADC2:
		PINMUX_CONFIG(PWR_SEQ1, PWR_GPIO_3);
		vaddr = ioremap(IOBLK_GRTC_REG_PWR_SEQ1, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case PWR_ADC3:
		PINMUX_CONFIG(PWR_VBAT_DET, PWR_VBAT_DET);
		vaddr = ioremap(IOBLK_GRTC_REG_PWR_VBAT_DET, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case PWR_ADC4:
		PINMUX_CONFIG(PWR_GPIO0, PWR_GPIO_0);
		vaddr = ioremap(IOBLK_GRTC_REG_PWR_GPIO0, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case PWR_ADC5:
		PINMUX_CONFIG(PWR_GPIO1, PWR_GPIO_1);
		vaddr = ioremap(IOBLK_GRTC_REG_PWR_GPIO1, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	case PWR_ADC6:
		PINMUX_CONFIG(PWR_GPIO2, PWR_GPIO_2);
		vaddr = ioremap(IOBLK_GRTC_REG_PWR_GPIO2, 0x4);
		iowrite32(0, vaddr);
		iounmap(vaddr);
		break;
	default:
		pr_err("%s: invalid channel index\n", __func__);
		break;
	}
}

#define	SARADC_CHAN_VOLTAGE(lval, idx, addr)			  \
	{													  \
		lval.type =	IIO_VOLTAGE;						  \
		lval.channel = idx;								  \
		lval.indexed = 1;								  \
		lval.address = addr;							  \
		lval.info_mask_separate	= BIT(IIO_CHAN_INFO_RAW); \
		lval.scan_index	= idx;							  \
		lval.scan_type.sign	= 'u';						  \
		lval.scan_type.realbits	= 12;					  \
		lval.scan_type.storagebits = 16;				  \
		lval.scan_type.endianness =	IIO_LE;				  \
	}

static ssize_t filter_enable_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t filter_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len);

static IIO_DEVICE_ATTR(filter_enable, 0644, filter_enable_show, filter_enable_store, 0);

static struct attribute *cvi_saradc_attrs[] = {
	&iio_dev_attr_filter_enable.dev_attr.attr,
	NULL,
};

static const struct attribute_group cvi_saradc_attr_group = {
	.attrs = cvi_saradc_attrs,
};

static ssize_t filter_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct cvi_saradc_device *ndev = iio_priv(indio_dev);

	return sysfs_emit(buf, "%u\n", ndev->filter_enable ? 1 : 0);
}

static ssize_t filter_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct cvi_saradc_device *ndev = iio_priv(indio_dev);
	bool en;
	int ret;

	ret = kstrtobool(buf, &en);
	if (ret)
		return ret;
	spin_lock(&ndev->close_lock);
	ndev->filter_enable = en;
	spin_unlock(&ndev->close_lock);

	return len;
}
static int platform_saradc_clk_init(struct cvi_saradc_device *ndev)
{
	// enable clock
	if (ndev->clk_saradc) {
		pr_debug("cvi_saradc enable	clock\n");
		clk_prepare_enable(ndev->clk_saradc);
	}
	if (ndev->clk_rtc_sys_saradc) {
		pr_debug("clk_rtc_sys_saradc enable	clock\n");
		clk_prepare_enable(ndev->clk_rtc_sys_saradc);
	}
	if (ndev->clk_rtc_sys_saradc1) {
		pr_debug("clk_rtc_sys_saradc1 enable	clock\n");
		clk_prepare_enable(ndev->clk_rtc_sys_saradc1);
	}

	return 0;
}

static void	platform_saradc_clk_deinit(struct cvi_saradc_device	*ndev)
{
	// disable clock
	if (ndev->clk_saradc) {
		pr_debug("cvi_saradc disable clock\n");
		clk_disable_unprepare(ndev->clk_saradc);
	}
	if (ndev->clk_rtc_sys_saradc) {
		pr_debug("clk_rtc_sys_saradc disable clock\n");
		clk_disable_unprepare(ndev->clk_rtc_sys_saradc);
	}
	if (ndev->clk_rtc_sys_saradc1) {
		pr_debug("clk_rtc_sys_saradc1 disable clock\n");
		clk_disable_unprepare(ndev->clk_rtc_sys_saradc1);
	}
}

static irqreturn_t cvi_saradc_irq(int irq, void	*data)
{
	struct cvi_saradc_device *ndev = data;
	unsigned long flags	= 0;

	spin_lock_irqsave(&ndev->close_lock, flags);

	// clear irq
	writel(0x1,	ndev->saradc_vaddr + SARADC_INTR_CLR);

	spin_unlock_irqrestore(&ndev->close_lock, flags);

	return IRQ_HANDLED;
}

static void	set_saradc_addr(struct cvi_saradc_device *ndev,	int	index)
{
	if (index <= ADC3)
		ndev->saradc_vaddr = ndev->top_saradc0_base_addr;
	else if (index <= ADC6)
		ndev->saradc_vaddr = ndev->top_saradc1_base_addr;
	else if (index <= ADC9)
		ndev->saradc_vaddr = ndev->top_saradc2_base_addr;
	else if (index <= PWR_ADC3)
		ndev->saradc_vaddr = ndev->rtcsys_saradc0_base_addr;
	else
		ndev->saradc_vaddr = ndev->rtcsys_saradc1_base_addr;
	return;
}

static int get_saradc_idx(int chan)
{
	return (chan - 1) % 3 + 1;
}

static void	cvi_saradc_cyc_setting(struct cvi_saradc_device	*ndev)
{
	u32 value;

	value =	readl(ndev->saradc_vaddr + SARADC_CYC_SET);
	value |= (0xf << 12); // set saradc	clock cycle=840ns
	writel(value, ndev->saradc_vaddr + SARADC_CYC_SET);
}

static u32 saradc_get_val(struct cvi_saradc_device *ndev, struct iio_chan_spec const *chan)
{
	u32 adc_value;
	// Trigger measurement
	writel((readl(ndev->saradc_vaddr + SARADC_CTRL) | 0x3), ndev->saradc_vaddr + SARADC_CTRL);
	pr_debug("cv_saradc_show: SARADC_CTRL =	%#X\n", readl(ndev->saradc_vaddr + SARADC_CTRL));
	// Check busy status
	// while (readl(ndev->saradc_vaddr + SARADC_STATUS) & (0x10 << ((chan->channel - 1) % 3 + 1)));
	udelay(10);
	adc_value = readl(ndev->saradc_vaddr + chan->address) & 0xFFF;

	pr_debug("cvi_saradc channel%d value = %#X\n", chan->channel,
		 adc_value);
	return adc_value;
}

static u32 saradc_get_val_with_filter(struct cvi_saradc_device *ndev, struct iio_chan_spec const *chan)
{
	int vals[CVI_SARADC_FILTER_NSAMP];
	int tmp[CVI_SARADC_FILTER_NSAMP];
	int median, mad, th;
	long sum = 0;
	int cnt = 0;
	int i;

	for (i = 0; i < CVI_SARADC_FILTER_NSAMP; i++)
		vals[i] = (int)saradc_get_val(ndev, chan);

	for (i = 0; i < CVI_SARADC_FILTER_NSAMP; i++)
		tmp[i] = vals[i];

	for (i = 1; i < CVI_SARADC_FILTER_NSAMP; i++) {
		int key = tmp[i];
		int j = i - 1;

		while (j >= 0 && tmp[j] > key) {
			tmp[j + 1] = tmp[j];
			j--;
		}
		tmp[j + 1] = key;
	}

	median = tmp[CVI_SARADC_FILTER_NSAMP / 2];

	for (i = 0; i < CVI_SARADC_FILTER_NSAMP; i++) {
		int d = vals[i] - median;

		if (d < 0)
			d = -d;
		tmp[i] = d;
	}

	for (i = 1; i < CVI_SARADC_FILTER_NSAMP; i++) {
		int key = tmp[i];
		int j = i - 1;

		while (j >= 0 && tmp[j] > key) {
			tmp[j + 1] = tmp[j];
			j--;
		}
		tmp[j + 1] = key;
	}

	mad = tmp[CVI_SARADC_FILTER_NSAMP / 2];

	if (mad == 0)
		mad = 1;

	th = CVI_SARADC_FILTER_K * mad;

	for (i = 0; i < CVI_SARADC_FILTER_NSAMP; i++) {
		int d = vals[i] - median;

		if (d < 0)
			d = -d;
		if (d <= th) {
			sum += vals[i];
			cnt++;
		}
	}

	if (cnt == 0)
		return (u32)median;

	return (u32)(sum / cnt);
}
static int saradc_read_raw(struct iio_dev	      *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long info)
{
	struct cvi_saradc_device *ndev = iio_priv(indio_dev);
	u32 adc_value;
	unsigned long flags = 0;
	u32 sel = 0;
	int index;

	platform_saradc_clk_init(ndev);
	io_config(chan->channel);

	spin_lock_irqsave(&ndev->close_lock, flags);

	set_saradc_addr(ndev, chan->channel);

	index =	get_saradc_idx(chan->channel);

	sel	= readl(ndev->saradc_vaddr + SARADC_CTRL);

	// Set saradc cycle
	cvi_saradc_cyc_setting(ndev);
	// enable channel
	writel((sel & ~(0xf << SARADC_SEL_SHIFT)) |
	 (1	<< (SARADC_SEL_SHIFT + index)),	ndev->saradc_vaddr + SARADC_CTRL);

	// Disable saradc interrupt
	writel(0x0, ndev->saradc_vaddr + SARADC_INTR_EN);

	pr_debug("adc_channel_index: %d\n", chan->channel);
	if (ndev->filter_enable)
		adc_value = saradc_get_val_with_filter(ndev, chan);
	else
		adc_value = saradc_get_val(ndev, chan);

	spin_unlock_irqrestore(&ndev->close_lock, flags);

	platform_saradc_clk_deinit(ndev);

	*val = adc_value;
	return IIO_VAL_INT;
}

static const struct	iio_info saradc_info = {
	.read_raw =	saradc_read_raw,
	.attrs = &cvi_saradc_attr_group,
};

static void cvi_saradc_trim(struct cvi_saradc_device *ndev)
{
	u32 top_trim, rtc_trim;
	u64 efuse_value;

	efuse_value = cvi_efuse_read_from_shadow(EFUSE_ADC_TRIM_REG);

	top_trim = (efuse_value & TOP_ADC_TRIM_MASK) >> TOP_ADC_TRIM_OFFSET;
	rtc_trim = (efuse_value & RTC_ADC_TRIM_MASK) >> RTC_ADC_TRIM_OFFSET;

	platform_saradc_clk_init(ndev);
	pr_debug("Setting top_trim: 0x%x, rtc_trim: 0x%x\n", top_trim,
		 rtc_trim);
	writel(top_trim, ndev->top_saradc0_base_addr + SARADC_TRIM);
	writel(top_trim, ndev->top_saradc1_base_addr + SARADC_TRIM);
	writel(top_trim, ndev->top_saradc2_base_addr + SARADC_TRIM);
	writel(rtc_trim, ndev->rtcsys_saradc0_base_addr + SARADC_TRIM);
	writel(rtc_trim, ndev->rtcsys_saradc1_base_addr + SARADC_TRIM);
	pr_debug("Getting top_trim: 0x%x, rtc_trim: 0x%x\n",
		 readl(ndev->top_saradc0_base_addr + SARADC_TRIM) & 0xf,
		 readl(ndev->rtcsys_saradc0_base_addr + SARADC_TRIM) & 0xf);
	platform_saradc_clk_deinit(ndev);
}

static int cvi_saradc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cvi_saradc_device *ndev;
	struct resource	*res;
	struct iio_dev *indio_dev;
	int	ret, i;

	pr_debug("cvi_saradc_probe start\n");

	indio_dev =	devm_iio_device_alloc(dev, sizeof(*ndev));
	if (!indio_dev)
		return -ENOMEM;

	ndev = iio_priv(indio_dev);
	ndev->dev =	dev;
	ndev->private_data = pdev;

	ndev->saradc_vaddr = NULL;
	ndev->channel_index = 0;
	ndev->filter_enable = 1;
	memset(ndev->enable, 0,	SARADC_CHAN_NUM);

	platform_set_drvdata(pdev, indio_dev);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "saradc0");
	if (!res) {
		dev_err(dev, "failed to retrieve saradc0 io\n");
		return -ENXIO;
	}

	ndev->top_saradc0_base_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ndev->top_saradc0_base_addr))
		return PTR_ERR(ndev->top_saradc0_base_addr);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "saradc1");
	if (!res) {
		dev_err(dev, "failed to retrieve saradc1 io\n");
		return -ENXIO;
	}

	ndev->top_saradc1_base_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ndev->top_saradc1_base_addr))
		return PTR_ERR(ndev->top_saradc1_base_addr);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "saradc2");
	if (!res) {
		dev_err(dev, "failed to retrieve saradc2 io\n");
		return -ENXIO;
	}

	ndev->top_saradc2_base_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ndev->top_saradc2_base_addr))
		return PTR_ERR(ndev->top_saradc2_base_addr);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rtc_saradc0");
	if (!res) {
		dev_err(dev, "failed to retrieve rtc_saradc0 io\n");
		return -ENXIO;
	}

	ndev->rtcsys_saradc0_base_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ndev->rtcsys_saradc0_base_addr))
		return PTR_ERR(ndev->rtcsys_saradc0_base_addr);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rtc_saradc1");
	if (!res) {
		dev_err(dev, "failed to retrieve rtc_saradc1 io\n");
		return -ENXIO;
	}

	ndev->rtcsys_saradc1_base_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ndev->rtcsys_saradc1_base_addr))
		return PTR_ERR(ndev->rtcsys_saradc1_base_addr);

	indio_dev->name	= "cvi_saradc";
	indio_dev->info	= &saradc_info;
	indio_dev->dev.parent =	dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	for	(i = 1;	i <= SARADC_CHAN_NUM; ++i) {
		SARADC_CHAN_VOLTAGE(ndev->iio_channels[i - 1], i, SARADC_CH1_RESULT	+ ((i - 1) % 3) *	4);
	}

	indio_dev->channels	= ndev->iio_channels;
	indio_dev->num_channels	= SARADC_CHAN_NUM;

	ndev->saradc_irq = platform_get_irq(pdev, 0);
	if (ndev->saradc_irq < 0) {
		dev_err(dev, "failed to	retrieve saradc	irq");
		return -ENXIO;
	}

	ret = devm_request_irq(&pdev->dev, ndev->saradc_irq, cvi_saradc_irq, 0,
			       "cvi-saradc", ndev);
	if (ret)
		return -ENXIO;

	ndev->clk_saradc = devm_clk_get(&pdev->dev,	"clk_saradc");
	if (IS_ERR(ndev->clk_saradc)) {
		dev_err(dev, "failed to	retrieve clk_saradc\n");
		ndev->clk_saradc = NULL;
	}
	ndev->clk_rtc_sys_saradc = devm_clk_get(&pdev->dev,	"clk_rtc_sys_saradc");
	if (IS_ERR(ndev->clk_rtc_sys_saradc)) {
		dev_err(dev, "failed to	retrieve clk_rtc_sys_saradc\n");
		ndev->clk_rtc_sys_saradc = NULL;
	}
	ndev->clk_rtc_sys_saradc1 = devm_clk_get(&pdev->dev,	"clk_rtc_sys_saradc1");
	if (IS_ERR(ndev->clk_rtc_sys_saradc1)) {
		dev_err(dev, "failed to	retrieve clk_rtc_sys_saradc1\n");
		ndev->clk_rtc_sys_saradc1 = NULL;
	}

	cvi_saradc_trim(ndev);

	ndev->rst_saradc = devm_reset_control_get(&pdev->dev, "res_saradc");
	if (IS_ERR(ndev->rst_saradc)) {
		dev_err(dev, "failed to	retrieve res_saradc\n");
		ndev->rst_saradc = NULL;
	}

	spin_lock_init(&ndev->close_lock);

	ret	= iio_device_register(indio_dev);
	if (ret) {
		dev_err(dev, "failed to	register iio device: %d\n",	ret);
		return ret;
	}

	// platform_set_drvdata(pdev, ndev);
	pr_debug("cvi_saradc_probe end\n");
	return 0;
}

static int cvi_saradc_remove(struct	platform_device	*pdev)
{
	struct iio_dev *indio_dev =	platform_get_drvdata(pdev);
	struct cvi_saradc_device *ndev = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	platform_saradc_clk_deinit(ndev);

	pr_debug("cvi_saradc remove\n");
	return 0;
}

static const struct	of_device_id cvi_saradc_match[]	= {
	{.compatible = "cvitek,saradc"},
	{},
};
MODULE_DEVICE_TABLE(of,	cvi_saradc_match);

#ifdef CONFIG_PM_SLEEP
static int saradc_cv_suspend(struct	device *dev)
{
	struct cvi_saradc_device *ndev = iio_priv(dev_get_drvdata(dev));
	platform_saradc_clk_init(ndev);
	/*Save all registers*/
	memcpy_fromio(ndev->top_saradc0_saved_regs, ndev->top_saradc0_base_addr, SARADC_REGS_SIZE);
	memcpy_fromio(ndev->top_saradc1_saved_regs, ndev->top_saradc1_base_addr, SARADC_REGS_SIZE);
	memcpy_fromio(ndev->top_saradc2_saved_regs, ndev->top_saradc2_base_addr, SARADC_REGS_SIZE);
	memcpy_fromio(ndev->rtcsys_saradc0_saved_regs, ndev->rtcsys_saradc0_base_addr, SARADC_REGS_SIZE);
	memcpy_fromio(ndev->rtcsys_saradc1_saved_regs, ndev->rtcsys_saradc1_base_addr, SARADC_REGS_SIZE);
	platform_saradc_clk_deinit(ndev);
	return 0;
}

static int saradc_cv_resume(struct device *dev)
{
	struct cvi_saradc_device *ndev = iio_priv(dev_get_drvdata(dev));
	platform_saradc_clk_init(ndev);
	/*Restore register settings*/
	memcpy_toio(ndev->top_saradc0_base_addr, ndev->top_saradc0_saved_regs, SARADC_REGS_SIZE);
	memcpy_toio(ndev->top_saradc1_base_addr, ndev->top_saradc1_saved_regs, SARADC_REGS_SIZE);
	memcpy_toio(ndev->top_saradc2_base_addr, ndev->top_saradc2_saved_regs, SARADC_REGS_SIZE);
	memcpy_toio(ndev->rtcsys_saradc0_base_addr, ndev->rtcsys_saradc0_saved_regs, SARADC_REGS_SIZE);
	memcpy_toio(ndev->rtcsys_saradc1_base_addr, ndev->rtcsys_saradc1_saved_regs, SARADC_REGS_SIZE);
	platform_saradc_clk_deinit(ndev);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(saradc_cv_pm_ops, saradc_cv_suspend,
						 saradc_cv_resume);

static struct platform_driver cvi_saradc_driver	= {
	.probe = cvi_saradc_probe,
	.remove	= cvi_saradc_remove,
	.driver	= {
		.owner = THIS_MODULE,
		.name =	"cvi-saradc",
		.pm	= &saradc_cv_pm_ops,
		.of_match_table	= cvi_saradc_match,
	},
};
static int __init saradc_init(void)
{
	return platform_driver_register(&cvi_saradc_driver);
}
late_initcall(saradc_init);

static void __exit saradc_exit(void)
{
	platform_driver_unregister(&cvi_saradc_driver);
}
module_exit(saradc_exit);
MODULE_AUTHOR("zixun.li <zixun.li@sophgo.com>");
MODULE_DESCRIPTION("Cvitek SoC saradc driver");
MODULE_LICENSE("GPL");
