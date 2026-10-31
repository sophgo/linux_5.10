// SPDX-License-Identifier: GPL-2.0+
//
// Secure OTP implementation
//

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <linux/arm-smccc.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-direction.h>
#include <linux/sophon-otp.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define SYSTEM_OTP2_BS          (0x0)

#define OPTEE_SMC_CALL_CV_OTP3_READ		0x03000006
#define OPTEE_SMC_CALL_CV_OTP3_WRITE	0x03000007

#define OTP_RETRY_TIMES 3

#define DEVICE_NAME  "otp"

struct sophon_otp {
	void __iomem            *regs;
	struct clk_bulk_data	*clks;
	int num_clks;
};

/* list of required clocks */
static const char * const sophon_otp_clocks[] = {
	"otp_c", "otp", "apb_otp",
};

static void __iomem *otp_base;
static int otp_major;
static struct class *otp_class;

static inline uint32_t otp2_segment_read(uint32_t segment, uint32_t line)
{
	pr_debug("otp2 read 0x%x\n", (SYSTEM_OTP2_BS + (((segment << 5) + line) << 2)));
	return ioread32(otp_base + SYSTEM_OTP2_BS + (((segment << 5) + line) << 2));
}

static inline void otp2_segment_addr_program(uint32_t segment, uint32_t line, uint32_t value)
{
	pr_debug("otp2 program 0x%x:0x%x\n", (SYSTEM_OTP2_BS + (((segment << 5) + line) << 2))
			, value);
	iowrite32(value, otp_base + SYSTEM_OTP2_BS + (((segment << 5) + line) << 2));
}

#if defined(CONFIG_ARM) || defined(__arm__) || defined(__aarch64__)
static int otp3_read(uint32_t segment, uint32_t line, uint32_t size, uint32_t *buffer)
{
	void *value = NULL;
	struct arm_smccc_res res = {0};
	phys_addr_t value_phys;
	uint32_t addr = ((segment << 5) + line) << 2;
	int retry;

	pr_debug("segment[%d] line[%d] size[%d]\n", segment, line, size);

	value = kzalloc((size << 2), GFP_KERNEL);
	if (!value) {
		return -ENOMEM;
	}

	value_phys = virt_to_phys(value);

	/*
	 * The secure payload may be preempted by a non-secure interrupt in
	 * the middle of the call.  In that case the dispatcher returns
	 * SMC_PREEMPTED (-2) after saving the interrupted secure context;
	 * re-issuing the same call resumes it.  Retry a bounded number of
	 * times until the call completes normally.
	 */
	for (retry = 0; retry < OTP_RETRY_TIMES; retry++) {
		arm_smccc_smc(OPTEE_SMC_CALL_CV_OTP3_READ, 0, addr, size,
			      (unsigned long)value_phys, 0, 0, 0, &res);
		if ((long)res.a0 != -2)
			break;
	}
	if (res.a0 != size) {
		pr_err("smc OTP3_READ failed: a0=0x%lx a1=0x%lx a2=0x%lx a3=0x%lx (expected size=%u)\n",
			res.a0, res.a1, res.a2, res.a3, size);
		kfree(value);
		return -EIO;
	}

	arch_sync_dma_for_cpu(value_phys, (size << 2), DMA_FROM_DEVICE);
	memcpy(buffer, value, (size << 2));
	kfree(value);

	return 0;
}

static int otp3_write(uint32_t segment, uint32_t line, uint32_t size, uint32_t *buffer)
{
	struct arm_smccc_res res = {0};
	phys_addr_t value_phys;
	void *value = NULL;
	uint32_t addr = ((segment << 5) + line) << 2;
	int retry;

	value = kzalloc((size << 2), GFP_KERNEL);
	if (!value) {
		return -ENOMEM;
	}

	memcpy(value, (void *)buffer, (size << 2));
	__flush_dcache_area(value, (size << 2));
	value_phys = virt_to_phys(value);

	/*
	 * The secure payload may be preempted by a non-secure interrupt in
	 * the middle of the call.  In that case the dispatcher returns
	 * SMC_PREEMPTED (-2) after saving the interrupted secure context;
	 * re-issuing the same call resumes it.  Retry a bounded number of
	 * times until the call completes normally.
	 */
	for (retry = 0; retry < OTP_RETRY_TIMES; retry++) {
		arm_smccc_smc(OPTEE_SMC_CALL_CV_OTP3_WRITE, 0, addr, size,
			      (unsigned long)value_phys, 0, 0, 0, &res);
		if ((long)res.a0 != -2)
			break;
	}
	if (res.a0 != size) {
		pr_err("smc OTP3_WRITE failed: a0=0x%lx a1=0x%lx a2=0x%lx a3=0x%lx (expected size=%u)\n",
			res.a0, res.a1, res.a2, res.a3, size);
		kfree(value);
		return -EIO;
	}

	kfree(value);

	return 0;
}
#else
static int otp3_read(uint32_t segment, uint32_t addr, uint32_t size, uint32_t *buffer)
{
    return 0;
}

static int otp3_write(uint32_t segment, uint32_t addr, uint32_t size, uint32_t *buffer)
{
    return 0;
}
#endif

static inline int otp3_segment_read(uint32_t segment, uint32_t *buffer)
{
	return otp3_read(segment, 0, 32, buffer);
}

static ssize_t otp3_reserve_segment2_show(struct class *class,
					  struct class_attribute *attr, char *buf)
{
	int ret;

	ret = otp3_segment_read(2, (uint32_t *)buf);
	if (ret)
		return ret;

	return 128;
}

static ssize_t otp3_reserve_segment2_store(struct class *class,
					   struct class_attribute *attr,
					   const char *buf, size_t count)
{
	uint32_t addr = 0, value = 0;
	int ret;

	if (sscanf(buf, "0x%x=0x%x", &addr, &value) != 2)
		return -ENOMEM;

	pr_debug("addr=%x value=%x\n", addr, value);
	if (addr > 31)
		return -1;

	ret = otp3_write(2, addr, 1, &value);
	if (ret)
		return ret;

	return count;
}

static ssize_t otp3_reserve_segment3_show(struct class *class,
					  struct class_attribute *attr, char *buf)
{
	int ret;

	ret = otp3_segment_read(3, (uint32_t *)buf);
	if (ret)
		return ret;

	return 128;
}

static ssize_t otp3_reserve_segment3_store(struct class *class,
					   struct class_attribute *attr,
					   const char *buf, size_t count)
{
	uint32_t addr = 0, value = 0;
	int ret;

	if (sscanf(buf, "0x%x=0x%x", &addr, &value) != 2)
		return -ENOMEM;

	pr_debug("addr=%x value=%x\n", addr, value);
	if (addr > 31)
		return -1;

	ret = otp3_write(3, addr, 1, &value);
	if (ret)
		return ret;

	return count;
}

static ssize_t otp2_reserve_segment0_show(struct class *class,
					  struct class_attribute *attr, char *buf)
{
	uint32_t value[32] = {0};
	int i;

	for (i = 0; i < 32; i++) {
		value[i] = otp2_segment_read(0, i);
	}

	memcpy(buf, (void *)value, sizeof(value));

	return sizeof(value);
}

static ssize_t otp2_reserve_segment0_store(struct class *class,
					   struct class_attribute *attr,
					   const char *buf, size_t count)
{
	uint32_t addr = 0, value = 0;

	if (sscanf(buf, "0x%x=0x%x", &addr, &value) != 2)
		return -ENOMEM;

	pr_debug("addr=%x value=%x\n", addr, value);
	if (addr > 31)
		return -1;

	otp2_segment_addr_program(0, addr, value);

	return count;
}

CLASS_ATTR_RW(otp2_reserve_segment0);
CLASS_ATTR_RW(otp3_reserve_segment2);
CLASS_ATTR_RW(otp3_reserve_segment3);

static int otp_open(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t otp_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	return 0;
}

static ssize_t otp_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	return 0;
}

static int otp_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long otp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct otp_config config;
	int ret;

	switch (cmd) {
	case IOCTL_OTP2_READ_CONFIG: {
		int i;

		ret = copy_from_user((unsigned char *)&config, (unsigned char *)arg, sizeof(config));
		if (ret != 0)
			return -1;

		if (config.segment > 27)
			return -1;

		if (config.addr > 31)
			return -1;

		if ((config.addr + config.size) > 32) {
			config.size = 32 - config.addr;
		}

		for (i = 0; i < config.size; i++) {
			config.value[i] = otp2_segment_read(config.segment, (config.addr + i));
		}

		ret = copy_to_user((unsigned char *)arg, (unsigned char *)&config, sizeof(config));
		if (ret != 0)
			return -1;

		break;
	}
	case IOCTL_OTP2_WRITE_CONFIG: {
		int i;

		ret = copy_from_user((unsigned char *)&config, (unsigned char *)arg, sizeof(config));
		if (ret != 0)
			return -1;

		if (config.segment > 27)
			return -1;

		if (config.addr > 31)
			return -1;

		if ((config.addr + config.size) > 32) {
			config.size = 32 - config.addr;
		}

		for (i = 0; i < config.size; i++) {
			otp2_segment_addr_program(config.segment, (config.addr + i), config.value[i]);
		}

		break;
	}
	case IOCTL_OTP3_READ_CONFIG: {
		ret = copy_from_user((unsigned char *)&config, (unsigned char *)arg, sizeof(config));
		if (ret != 0)
			return -1;

		if (config.segment > 27)
			return -1;

		if (config.addr > 31)
			return -1;

		if ((config.addr + config.size) > 32) {
			config.size = 32 - config.addr;
		}

		otp3_read(config.segment, config.addr, config.size, config.value);

		ret = copy_to_user((unsigned char *)arg, (unsigned char *)&config, sizeof(config));
		if (ret != 0)
			return -1;

		break;
	}
	case IOCTL_OTP3_WRITE_CONFIG: {
		ret = copy_from_user((unsigned char *)&config, (unsigned char *)arg, sizeof(config));
		if (ret != 0)
			return -1;

		if (config.segment > 27)
			return -1;

		if (config.addr > 31)
			return -1;

		if ((config.addr + config.size) > 32) {
			config.size = 32 - config.addr;
		}

		otp3_write(config.segment, config.addr, config.size, config.value);

		break;
	}
	}

	return 0;
}

const struct file_operations otp_fops = {
	.owner  =   THIS_MODULE,
	.open   =   otp_open,
	.read   =   otp_read,
	.write  =   otp_write,
	.release =  otp_release,
	.unlocked_ioctl = otp_ioctl,
};

static int sophon_otp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sophon_otp *otp;
	int ret;

	otp = devm_kzalloc(dev, sizeof(*otp), GFP_KERNEL);
	if (!otp)
		return -ENOMEM;

	otp_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(otp_base))
		return PTR_ERR(otp_base);

	otp->regs = otp_base;
	/*
	 * Keep OTP driver usable even if DTS does not provide
	 * the expected clocks ("otp_c", "otp", "apb_otp").
	 */
	dev_warn(dev, "skip clock request/enable, run OTP without managed clocks\n");

	otp_major = register_chrdev(0, DEVICE_NAME, &otp_fops);
	if (otp_major < 0) {
		pr_err("can't register major number\n");
		goto failed;
	}

	otp_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(otp_class)) {
		pr_err("Err: failed in My Driver class.\n");
		goto failed;
	}

	ret = class_create_file(otp_class, &class_attr_otp3_reserve_segment2);
	if (ret) {
		pr_err("can't create sysfs otp3_reserve_segment0 file\n");
		goto failed;
	}

	ret = class_create_file(otp_class, &class_attr_otp3_reserve_segment3);
	if (ret) {
		pr_err("can't create sysfs otp3_reserve_segment0 file\n");
		goto failed;
	}

	ret = class_create_file(otp_class, &class_attr_otp2_reserve_segment0);
	if (ret) {
		pr_err("can't create sysfs otp3_reserve_segment0 file\n");
		goto failed;
	}

	device_create(otp_class, NULL, MKDEV(otp_major, 0), NULL, DEVICE_NAME);
	platform_set_drvdata(pdev, otp);
	return 0;
failed:
	if (otp_major) {
		unregister_chrdev(otp_major, DEVICE_NAME);
	}

	class_remove_file(otp_class, &class_attr_otp2_reserve_segment0);
	class_remove_file(otp_class, &class_attr_otp3_reserve_segment2);
	class_remove_file(otp_class, &class_attr_otp3_reserve_segment3);
	class_destroy(otp_class);

	return -1;
}

static int sophon_otp_remove(struct platform_device *pdev)
{
	class_remove_file(otp_class, &class_attr_otp2_reserve_segment0);
	class_remove_file(otp_class, &class_attr_otp3_reserve_segment2);
	class_remove_file(otp_class, &class_attr_otp3_reserve_segment3);

	device_destroy(otp_class, MKDEV(otp_major, 0));
	class_destroy(otp_class);
	unregister_chrdev(otp_major, DEVICE_NAME);

	return 0;
}

static const struct of_device_id sophon_otp_match[] = {
	{ .compatible = "sophon,secure-otp",},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sophon_otp_match);

static struct platform_driver sophon_otp_driver = {
	.probe = sophon_otp_probe,
	.remove = sophon_otp_remove,
	.driver = {
		.name = "sophon-otp",
		.of_match_table = sophon_otp_match,
	},
};

module_platform_driver(sophon_otp_driver);
MODULE_DESCRIPTION("Sophon OTP driver");
MODULE_LICENSE("GPL v2");
