// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022-2023 CVITEK
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/version.h>
#include <linux/jiffies.h>
#include <linux/init.h>
#include <asm/cacheflush.h>
#include <linux/dma-buf.h>
#include <linux/dma-map-ops.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/cvitek_spacc.h>
#include <cvitek-spacc-regs.h>
#include <linux/arm-smccc.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#define DEVICE_NAME "spacc"
#define OPTEE_SMC_CALL_CV_BASE64 0x0300000D
#define OPTEE_SMC_CALL_CV_SHA256 0x0300000E
#define OPTEE_SMC_CALL_CV_AES 0x0300000F
#define OPTEE_SMC_CALL_CV_SM4 0x03000010
#define OPTEE_SMC_CALL_CV_DES 0x03000011
#define OPTEE_SMC_CALL_CV_TDES 0x03000012
#define OPTEE_SMC_CALL_CV_SM3 0x03000013

static DECLARE_WAIT_QUEUE_HEAD(wq);
struct cvi_spacc {
	struct device *dev;
	struct cdev cdev;
	dev_t tdev;
	void __iomem *spacc_base;
	struct class *spacc_class;
#ifdef CONFIG_PM_SLEEP
	struct clk *efuse_clk;
#endif
};
struct cvi_spacc_private {
	struct mutex lock;        // 添加互斥锁
	void *buffer;
	u32 buffer_size;
	u32 used_size;
	u32 read_size;
	u32 state[8];
	u32 dma_handle;
	u32 data_size;
};
static void cvi_spacc_free_pool(struct cvi_spacc_private *pool)
{
    if (pool->buffer && pool->buffer_size) {
        pr_err("free pool: %p, size: %u\n", pool->buffer, pool->buffer_size);
        free_pages((unsigned long)pool->buffer, get_order(pool->buffer_size));
    }
    pool->buffer = NULL;
    pool->buffer_size = 0;
    pool->dma_handle = 0;
    pool->data_size = 0;
    pool->read_size = 0;
}

static int cvi_spacc_create_pool(struct cvi_spacc_private *pool, unsigned int size)
{
	// Free existing memory pool if any
	 cvi_spacc_free_pool(pool);

	size = PAGE_ALIGN(size);
	unsigned int order = get_order(size);

	pr_info("Attempting to allocate %u bytes (order %u)\n", size, order);

	// First try DMA zone allocation
	pool->buffer =
		(void *)__get_free_pages(GFP_DMA | __GFP_ZERO, order);
	if (pool->buffer) {
		pool->buffer_size = size;
		pool->dma_handle = virt_to_phys(pool->buffer);
		pr_info("DMA pages allocation succeeded at phys 0x%llx\n",
			 (unsigned long long)pool->dma_handle);
		return 0;
	}

	// If failed, wait and try DMA32
	// mdelay(100);
	pr_err("DMA allocation failed, trying DMA32\n");

	pool->buffer =
		(void *)__get_free_pages(GFP_DMA32 | __GFP_ZERO, order);
	if (pool->buffer) {
		pool->buffer_size = size;
		pool->dma_handle = virt_to_phys(pool->buffer);
		pr_err("DMA32 pages allocation succeeded at phys 0x%llx\n",
			 (unsigned long long)pool->dma_handle);
		return 0;
	}

	// Finally try with retry mechanism
	int retry_count = 3;
	while (retry_count--) {
		pool->buffer =
			(void *)__get_free_pages(GFP_DMA | __GFP_ZERO |
							 __GFP_DIRECT_RECLAIM |
							 __GFP_RETRY_MAYFAIL,
						 order);

		if (pool->buffer) {
			pool->buffer_size = size;
			pool->dma_handle = virt_to_phys(pool->buffer);
			pr_err("Retry allocation succeeded at phys 0x%llx\n",
				 (unsigned long long)pool->dma_handle);
			return 0;
		}

		if (retry_count > 0) {
			pr_err("Allocation failed, retrying after delay\n");
			// mdelay(200);
		}
	}

	pr_err("All allocation attempts failed for %u bytes\n"
		"Process: %s (PID: %d)\n"
		"Order required: %u\n",
		size, current->comm, current->pid, order);

	return -ENOMEM;
}
#ifdef riscv
#ifdef CONFIG_PM_SLEEP
static int cvitek_spacc_suspend(struct device *dev)
{
	struct cvi_spacc *spacc = dev_get_drvdata(dev);
	void __iomem *sec_top;

	clk_prepare_enable(spacc->efuse_clk);

	sec_top = ioremap(0x020b0000, 4);
	iowrite32(0x3, sec_top);
	iounmap(sec_top);

	clk_disable_unprepare(spacc->efuse_clk);
	return 0;
}

static int cvitek_spacc_resume(struct device *dev)
{
	struct cvi_spacc *spacc = dev_get_drvdata(dev);
	void __iomem *sec_top;

	clk_prepare_enable(spacc->efuse_clk);

	sec_top = ioremap(0x020b0000, 4);
	iowrite32(0x0, sec_top);
	iounmap(sec_top);

	clk_disable_unprepare(spacc->efuse_clk);
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(cvitek_spacc_pm_ops, cvitek_spacc_suspend,
			 cvitek_spacc_resume);

static inline void cvi_sha256_init(struct cvi_spacc_private *spacc_private)
{
	spacc_private->state[0] = cpu_to_be32(0x6A09E667);
	spacc_private->state[1] = cpu_to_be32(0xBB67AE85);
	spacc_private->state[2] = cpu_to_be32(0x3C6EF372);
	spacc_private->state[3] = cpu_to_be32(0xA54FF53A);
	spacc_private->state[4] = cpu_to_be32(0x510E527F);
	spacc_private->state[5] = cpu_to_be32(0x9B05688C);
	spacc_private->state[6] = cpu_to_be32(0x1F83D9AB);
	spacc_private->state[7] = cpu_to_be32(0x5BE0CD19);
}

static inline void cvi_sha1_init(struct cvi_spacc_private *spacc_private)
{
	spacc_private->state[0] = cpu_to_be32(0x67452301);
	spacc_private->state[1] = cpu_to_be32(0xEFCDAB89);
	spacc_private->state[2] = cpu_to_be32(0x98BADCFE);
	spacc_private->state[3] = cpu_to_be32(0x10325476);
	spacc_private->state[4] = cpu_to_be32(0xC3D2E1F0);
}

static inline void
trigger_cryptodma_engine_and_wait_finish(struct cvi_spacc *spac)
{
	// Set cryptodma control
	iowrite32(0x3, spacc->spacc_base + CRYPTODMA_INT_MASK);

	// Clear interrupt
	// Important!!! must do this
	iowrite32(0x3, spacc->spacc_base + CRYPTODMA_WR_INT);

	// Trigger cryptodma engine
	iowrite32(DMA_WRITE_MAX_BURST << 24 | DMA_READ_MAX_BURST << 16 |
			  DMA_DESCRIPTOR_MODE << 1 | DMA_ENABLE,
		  spacc->spacc_base + CRYPTODMA_DMA_CTRL);

	wait_event_interruptible(wq, flag == 'y');
	flag = 'n';
}

static inline void get_hash_result(struct cvi_spacc_private *spacc_private,
				   int count)
{
	u32 i;
	u32 *result = (u32 *)spacc_private->buffer;

	for (i = 0; i < count; i++)
		result[i] = ioread32(spacc->spacc_base + CRYPTODMA_SHA_PARA +
				     i * 4);
}

static inline void setup_dma_descriptor(struct cvi_spacc_private *spacc_private,
					uint32_t *dma_descriptor)
{
	phys_addr_t descriptor_phys;

	descriptor_phys = virt_to_phys(dma_descriptor);

	arch_sync_dma_for_device(descriptor_phys,
				 /*sizeof(dma_descriptor)*/ 4 * 22,
				 DMA_TO_DEVICE);

	// set dma descriptor addr
	iowrite32((uint32_t)((uint64_t)descriptor_phys & 0xFFFFFFFF),
		  spacc->spacc_base + CRYPTODMA_DES_BASE_L);
	iowrite32((uint32_t)((uint64_t)descriptor_phys >> 32),
		  spacc->spacc_base + CRYPTODMA_DES_BASE_H);
}

static inline void setup_src(u32 *dma_descriptor, uintptr_t src, u32 len)
{
	phys_addr_t src_phys;

	src_phys = virt_to_phys((void *)src);
	arch_sync_dma_for_device(src_phys, len, DMA_TO_DEVICE);

	dma_descriptor[CRYPTODMA_SRC_LEN] = len;
	dma_descriptor[CRYPTODMA_SRC_ADDR_L] =
		(uint32_t)((uint64_t)src_phys & 0xFFFFFFFF);
	dma_descriptor[CRYPTODMA_SRC_ADDR_H] =
		(uint32_t)((uint64_t)src_phys >> 32);
}

static void setup_src_dst(u32 *dma_descriptor, phys_addr_t buffer, u32 len)
{
	dma_descriptor[CRYPTODMA_SRC_LEN] = len;
	dma_descriptor[CRYPTODMA_SRC_ADDR_L] =
		(uint32_t)((uint64_t)buffer & 0xFFFFFFFF);
	dma_descriptor[CRYPTODMA_SRC_ADDR_H] =
		(uint32_t)((uint64_t)buffer >> 32);

	dma_descriptor[CRYPTODMA_DST_ADDR_L] =
		(uint32_t)((uint64_t)buffer & 0xFFFFFFFF);
	dma_descriptor[CRYPTODMA_DST_ADDR_H] =
		(uint32_t)((uint64_t)buffer >> 32);
}

#define setup_dst(dst)                                                         \
	do {                                                                   \
		phys_addr_t dst_phys = virt_to_phys((void *)dst);              \
		dma_descriptor[CRYPTODMA_DST_ADDR_L] =                         \
			(uint32_t)((uint64_t)dst_phys & 0xFFFFFFFF);           \
		dma_descriptor[CRYPTODMA_DST_ADDR_H] =                         \
			(uint32_t)((uint64_t)dst_phys >> 32);                  \
	} while (0)

static inline void setup_mode(u32 *dma_descriptor, SPACC_ALGO_MODE_E mode,
			      unsigned char *iv)
{
	switch (mode) {
	case SPACC_ALGO_MODE_CBC:
		dma_descriptor[CRYPTODMA_CTRL] |= DES_USE_DESCRIPTOR_IV;
		dma_descriptor[CRYPTODMA_CIPHER] = CBC_ENABLE << 1;
		memcpy(&dma_descriptor[CRYPTODMA_IV], iv, 16);
		break;
	case SPACC_ALGO_MODE_CTR:
		dma_descriptor[CRYPTODMA_CTRL] |= DES_USE_DESCRIPTOR_IV;
		dma_descriptor[CRYPTODMA_CIPHER] = 0x1 << 2;
		memcpy(&dma_descriptor[CRYPTODMA_IV], iv, 16);
		break;
	case SPACC_ALGO_MODE_ECB:
	default:
		break;
	}
}

static inline void setup_key_size(u32 *dma_descriptor, SPACC_KEY_SIZE_E size,
				  unsigned char *key)
{
	switch (size) {
	case SPACC_KEY_SIZE_64BITS:
		memcpy(&dma_descriptor[CRYPTODMA_KEY], key, 8);
		break;
	case SPACC_KEY_SIZE_128BITS:
		dma_descriptor[CRYPTODMA_CIPHER] |= (0x1 << 5);
		memcpy(&dma_descriptor[CRYPTODMA_KEY], key, 16);
		break;
	case SPACC_KEY_SIZE_192BITS:
		dma_descriptor[CRYPTODMA_CIPHER] |= (0x1 << 4);
		memcpy(&dma_descriptor[CRYPTODMA_KEY], key, 24);
		break;
	case SPACC_KEY_SIZE_256BITS:
		dma_descriptor[CRYPTODMA_CIPHER] |= (0x1 << 3);
		memcpy(&dma_descriptor[CRYPTODMA_KEY], key, 32);
		break;
	default:
		break;
	}
}

static inline void setup_action(u32 *dma_descriptor, SPACC_ACTION_E action)
{
	if (action == SPACC_ACTION_ENCRYPTION)
		dma_descriptor[CRYPTODMA_CIPHER] |= 0x1;
}

int spacc_sha256(struct cvi_spacc_private *spacc_private, uintptr_t src,
		 uint32_t len)
{
	__aligned(32) u32 dma_descriptor[22] = { 0 };
	u32 i;

	// must mark DES_USE_DESCRIPTOR_KEY flag
	dma_descriptor[CRYPTODMA_CTRL] =
		DES_USE_DESCRIPTOR_KEY | DES_USE_SHA | 0xF;
	dma_descriptor[CRYPTODMA_CIPHER] = (0x1 << 1) | 0x1;

	for (i = 0; i < 8; i++)
		dma_descriptor[CRYPTODMA_KEY + i] = spacc_private->state[i];

	setup_src(dma_descriptor, src, len);
	setup_dma_descriptor(spacc, dma_descriptor);

	trigger_cryptodma_engine_and_wait_finish(spacc);
	return 0;
}

int spacc_sha1(struct cvi_spacc_private *spacc_private, uintptr_t src,
	       uint32_t len)
{
	__aligned(32) u32 dma_descriptor[22] = { 0 };
	u32 i;

	// must mark DES_USE_DESCRIPTOR_KEY flag
	dma_descriptor[CRYPTODMA_CTRL] =
		DES_USE_DESCRIPTOR_KEY | DES_USE_SHA | 0xF;
	dma_descriptor[CRYPTODMA_CIPHER] = 0x1;

	for (i = 0; i < 5; i++)
		dma_descriptor[CRYPTODMA_KEY + i] = spacc_private->state[i];

	setup_src(dma_descriptor, src, len);
	setup_dma_descriptor(spacc, dma_descriptor);

	trigger_cryptodma_engine_and_wait_finish(spacc);
	return 0;
}

int spacc_base64(struct cvi_spacc_private *spacc_private, phys_addr_t src,
		 uint32_t len, SPACC_ACTION_E ation)
{
	__aligned(32) u32 dma_descriptor[22] = { 0 };

	dma_descriptor[CRYPTODMA_CTRL] = DES_USE_BASE64 | 0xF;

	if (ation == SPACC_ACTION_ENCRYPTION) {
		dma_descriptor[CRYPTODMA_CIPHER] = 0x1;
		spacc_private->used_size = (len + (3 - 1)) / 3 * 4;
		dma_descriptor[CRYPTODMA_DST_LEN] = spacc_private->used_size;
	} else {
		spacc_private->used_size = (len / 4) * 3;
		dma_descriptor[CRYPTODMA_DST_LEN] = spacc_private->used_size;
	}

	setup_src_dst(dma_descriptor, src, len);
	setup_dma_descriptor(spacc, dma_descriptor);

	trigger_cryptodma_engine_and_wait_finish(spacc);
	return 0;
}

int spacc_aes(struct cvi_spacc_private *spacc_private, phys_addr_t src,
	      uint32_t len, spacc_aes_config_s config)
{
	__aligned(32) u32 dma_descriptor[22] = { 0 };

	spacc_private->used_size = len;
	dma_descriptor[CRYPTODMA_CTRL] =
		DES_USE_DESCRIPTOR_KEY | DES_USE_AES | 0xF;

	setup_mode(dma_descriptor, config.mode, config.iv);
	setup_key_size(dma_descriptor, config.size, config.key);
	setup_action(dma_descriptor, config.action);

	setup_src_dst(dma_descriptor, src, len);
	setup_dma_descriptor(spacc, dma_descriptor);

	trigger_cryptodma_engine_and_wait_finish(spacc);
	return 0;
}

int spacc_sm4(struct cvi_spacc_private *spacc_private, phys_addr_t src,
	      uint32_t len, spacc_sm4_config_s config)
{
	__aligned(32) u32 dma_descriptor[22] = { 0 };

	spacc_private->used_size = len;
	dma_descriptor[CRYPTODMA_CTRL] =
		DES_USE_DESCRIPTOR_KEY | DES_USE_SM4 | 0xF;

	setup_mode(dma_descriptor, config.mode, config.iv);
	setup_key_size(dma_descriptor, config.size, config.key);
	setup_action(dma_descriptor, config.action);

	setup_src_dst(dma_descriptor, src, len);
	setup_dma_descriptor(spacc, dma_descriptor);

	trigger_cryptodma_engine_and_wait_finish(spacc);
	return 0;
}

int spacc_des(struct cvi_spacc_private *spacc_private, phys_addr_t src,
	      uint32_t len, spacc_des_config_s config, int is_tdes)
{
	__aligned(32) u32 dma_descriptor[22] = { 0 };

	spacc_private->used_size = len;
	dma_descriptor[CRYPTODMA_CTRL] =
		DES_USE_DESCRIPTOR_KEY | DES_USE_DES | 0xF;

	setup_mode(dma_descriptor, config.mode, config.iv);
	if (is_tdes) {
		dma_descriptor[CRYPTODMA_CIPHER] |= (0x1 << 3);
		memcpy(&dma_descriptor[CRYPTODMA_KEY], config.key, 24);
	} else {
		memcpy(&dma_descriptor[CRYPTODMA_KEY], config.key, 8);
	}
	setup_action(dma_descriptor, config.action);

	setup_src_dst(dma_descriptor, src, len);
	setup_dma_descriptor(spacc, dma_descriptor);

	trigger_cryptodma_engine_and_wait_finish(spacc);
	return 0;
}
#endif
static int spacc_base64(struct cvi_spacc_private *spacc_private,
			u32 customer_code, u32 action)
{
	struct arm_smccc_res res = { 0 };
	phys_addr_t value_phys;
	value_phys = virt_to_phys(spacc_private->buffer);
	//for gcc 9.3.0

	arch_sync_dma_for_device(value_phys, spacc_private->used_size,
				 DMA_TO_DEVICE);
	arm_smccc_smc(OPTEE_SMC_CALL_CV_BASE64, (unsigned long)value_phys,
		      spacc_private->used_size, (unsigned long)value_phys,
		      customer_code, action, 0, 0, &res);
	//a0 is result_size
	printk("res a0 : %lu\n", res.a0);

	return res.a0;
}

static int spacc_base64_inner(struct spacc_base64_inner *b64)
{
	struct arm_smccc_res res = { 0 };
	arch_sync_dma_for_device(b64->src, b64->len, DMA_TO_DEVICE);
	arm_smccc_smc(OPTEE_SMC_CALL_CV_BASE64, b64->src, b64->len, b64->dst,
		      b64->customer_code, b64->action, 0, 0, &res);
	arch_sync_dma_for_device(b64->dst, res.a0, DMA_FROM_DEVICE);
	pr_debug("res a0 : %lu\n", res.a0);

	return res.a0;
}
static int spacc_sha256(phys_addr_t buffer, unsigned int size)
{
	struct arm_smccc_res res = { 0 };

	arch_sync_dma_for_device(buffer, size, DMA_TO_DEVICE);
	printk("sizeof(phys_addr_t)=%zu\n", sizeof(phys_addr_t));
	// 或者如果是32位
	printk("spacc_sha256 buffer:%x,size:%u\n", (unsigned int)buffer, size);
	arm_smccc_smc(OPTEE_SMC_CALL_CV_SHA256, (unsigned long)buffer, size,
		      (unsigned long)buffer, 0, 0, 0, 0, &res);
	printk("res a0 : %lu\n", res.a0);

	return res.a0;
}
static int spacc_sm3(phys_addr_t buffer, unsigned int size)
{
	struct arm_smccc_res res = { 0 };

	arch_sync_dma_for_device(buffer, size, DMA_TO_DEVICE);
	printk("sizeof(phys_addr_t)=%zu\n", sizeof(phys_addr_t));
	printk("spacc_sm3 buffer:%llx,size:%u\n", (unsigned long long)buffer, size);
	// 或者如果是32位
	printk("spacc_sm3 buffer:%x,size:%u\n", (unsigned int)buffer, size);
	arm_smccc_smc(OPTEE_SMC_CALL_CV_SM3, (unsigned long)buffer, size,
		      (unsigned long)buffer, 0, 0, 0, 0, &res);
	printk("res a0 : %lu\n", res.a0);

	return res.a0;
}

static int spacc_aes(phys_addr_t src_phys, uint32_t len, phys_addr_t key_phys,
		     uint32_t key_len, phys_addr_t iv_phys,
		     spacc_aes_config_s *config)
{
	struct arm_smccc_res res = { 0 };
	uint64_t arg = 0;
	arch_sync_dma_for_device(src_phys, len, DMA_TO_DEVICE);
	arg = (u8)config->mode | ((u8)config->key_mode << 2) |
	      ((u8)config->action << 4) | ((u8)config->otp << 5);
	arm_smccc_smc(OPTEE_SMC_CALL_CV_AES, (unsigned long)src_phys,
		      (unsigned long)src_phys, len, key_phys, iv_phys, key_len,
		      (unsigned long)arg, &res);

	printk("res a0 : %lu\n", res.a0);

	return res.a0;
}
static int spacc_sm4(phys_addr_t src_phys, uint32_t len, phys_addr_t key_phys,
		     uint32_t key_len, phys_addr_t iv_phys,
		     spacc_aes_config_s *config)
{
	struct arm_smccc_res res = { 0 };
	uint64_t arg = 0;
	arch_sync_dma_for_device(src_phys, len, DMA_TO_DEVICE);
	arg = (u8)config->mode | ((u8)config->key_mode << 2) |
	      ((u8)config->action << 4) | ((u8)config->otp << 5);
	arm_smccc_smc(OPTEE_SMC_CALL_CV_SM4, (unsigned long)src_phys,
		      (unsigned long)src_phys, len, key_phys, iv_phys, key_len,
		      (unsigned long)arg, &res);

	printk("res a0 : %lu\n", res.a0);

	return res.a0;
}

static int spacc_des(phys_addr_t src_phys, uint32_t len, phys_addr_t key_phys,
		     uint32_t key_len, phys_addr_t iv_phys,
		     spacc_des_config_s *config, int tdes)
{
	struct arm_smccc_res res = { 0 };
	arch_sync_dma_for_device(src_phys, len, DMA_TO_DEVICE);
	arm_smccc_smc(tdes ? OPTEE_SMC_CALL_CV_TDES : OPTEE_SMC_CALL_CV_DES,
		      (unsigned long)src_phys, len, (unsigned long)src_phys,
		      key_phys, iv_phys, config->mode, config->action, &res);
	printk("res a0 : %lu\n", res.a0);
	return res.a0;
}
// #endif
static int cvi_spacc_init_buffer(struct cvi_spacc_private *spacc_private,
				 size_t size)
{
	unsigned int order = get_order(size);
	struct page *page;

	if (size == spacc_private->buffer_size) {
		return 0;
	} else if (spacc_private->buffer_size) {
		free_pages((unsigned long)spacc_private->buffer,
			   get_order(spacc_private->buffer_size));
		spacc_private->buffer_size = 0;
	}

	page = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!page)
		return -ENOMEM;

	spacc_private->buffer = page_address(page);
	spacc_private->buffer_size = size;
	return 0;
}

static int spacc_open(struct inode *inode, struct file *file)
{
	struct cvi_spacc_private *spacc_private;

	spacc_private = kzalloc(sizeof(*spacc_private), GFP_KERNEL);
	if (!spacc_private) {
		pr_err("kzalloc for spacc_private failed\n");
		return -ENOMEM;
	}
	mutex_init(&spacc_private->lock);  // 初始化互斥锁
    spacc_private->used_size = 0;
    spacc_private->read_size = 0;
    spacc_private->buffer = NULL;
    spacc_private->buffer_size = 0;
    spacc_private->dma_handle = 0;
    spacc_private->data_size = 0;
    
    file->private_data = spacc_private;
	return 0;
}

static ssize_t spacc_read(struct file *filp, char *buf, size_t count,
			  loff_t *f_pos)
{
	struct cvi_spacc_private *spacc_private = filp->private_data;
	int ret;

	if (!spacc_private->used_size)
		return 0;

	ret = spacc_private->used_size - spacc_private->read_size;
	count = (ret >= count) ? count : ret;
	ret = copy_to_user(
		buf, (char *)spacc_private->buffer + spacc_private->read_size,
		count);
	if (ret != 0)
		return -1;

	spacc_private->read_size += count;
	if (spacc_private->used_size == spacc_private->read_size) {
		spacc_private->used_size = 0;
		spacc_private->read_size = 0;
	}
	
	return count;
}

static ssize_t spacc_write(struct file *filp, const char *buf, size_t count,
			   loff_t *f_pos)
{
	struct cvi_spacc_private *spacc_private = filp->private_data;
	int ret = spacc_private->buffer_size - spacc_private->used_size;

	if (ret <= 0)
		return spacc_private->used_size;

	if (count > ret)
		count = ret;

	ret = copy_from_user(((unsigned char *)spacc_private->buffer +
			      spacc_private->used_size),
			     buf, count);
	if (ret != 0)
		return -1;

	spacc_private->used_size += count;
	return spacc_private->used_size;
}

static int spacc_release(struct inode *inode, struct file *file)
{
	struct cvi_spacc_private *spacc_private;
    
    if (!file || !file->private_data)
        return -EINVAL;
        
    spacc_private = file->private_data;
    mutex_destroy(&spacc_private->lock);  // 销毁互斥锁
    cvi_spacc_free_pool(spacc_private);
    kfree(spacc_private);
    file->private_data = NULL;

	return 0;
}
static int handle_key_iv(spacc_aes_config_s *config, void **key_kernel_addr,
			 void **iv_kernel_addr, uint64_t *key_len)
{
	int ret = 0;
	
	// 初始化指针为NULL
	*key_kernel_addr = NULL;
	*iv_kernel_addr = NULL;

	switch (config->key_mode) {
	case SPACC_KEY_SIZE_64BITS:
		*key_len = 8;
		break;
	case SPACC_KEY_SIZE_128BITS:
		*key_len = 16;
		break;
	case SPACC_KEY_SIZE_192BITS:
		*key_len = 24;
		break;
	case SPACC_KEY_SIZE_256BITS:
		*key_len = 32;
		break;
	default:
		return -EINVAL;
	}

	if (config->otp != SPACC_KEY_SOURCE_OTP) {
		*key_kernel_addr = kmalloc(*key_len, GFP_KERNEL);
		if (!*key_kernel_addr) {
			pr_err("kmalloc for key failed\n");
			ret = -ENOMEM;
			goto cleanup;
		}
		
		if (copy_from_user(*key_kernel_addr, config->key, *key_len) != 0) {
			pr_err("copy_from_user key failed\n");
			ret = -EFAULT;
			goto cleanup;
		}
	}

	if (config->mode != SPACC_ALGO_MODE_ECB) {
		*iv_kernel_addr = kmalloc(16, GFP_KERNEL);
		if (!*iv_kernel_addr) {
			pr_err("kmalloc for IV failed\n");
			ret = -ENOMEM;
			goto cleanup;
		}
		
		if (copy_from_user(*iv_kernel_addr, config->iv, 16) != 0) {
			pr_err("copy_from_user iv failed\n");
			ret = -EFAULT;
			goto cleanup;
		}
	}

	return 0;

cleanup:
	if (*key_kernel_addr) {
		kfree(*key_kernel_addr);
		*key_kernel_addr = NULL;
	}
	if (*iv_kernel_addr) {
		kfree(*iv_kernel_addr);
		*iv_kernel_addr = NULL;
	}
	return ret;
}
static int handle_src_phys(struct cvi_spacc_private *spacc_private,
			   spacc_aes_config_s *config, phys_addr_t *src_phys,
			   uint32_t *len)
{
	
	if (!spacc_private->buffer || spacc_private->used_size == 0) {
        printk(KERN_ERR "Memory pool is empty or uninitialized\n");
        return -EINVAL;
    }
	*src_phys = virt_to_phys(spacc_private->buffer);
	*len = spacc_private->used_size;
	return 0;
}
static long spacc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct cvi_spacc_private *spacc_private = filp->private_data;
	struct cvi_spacc *spacc = container_of(filp->f_inode->i_cdev, struct cvi_spacc, cdev);
    struct device *dev = spacc->dev;
    int ret = 0;
    
    if (!spacc_private)
        return -EINVAL;

	mutex_lock(&spacc_private->lock);

	switch (cmd) {
	case IOCTL_SPACC_CREATE_MEMPOOL: {
		unsigned int size = 0;
		
		ret = copy_from_user((unsigned char *)&size,
				     (unsigned char *)arg, sizeof(size));
		if (ret != 0){
			printk("copy_from_user size failed, ret: %d\n", ret);
			goto out_unlock;
		}
		ret = cvi_spacc_create_pool(spacc_private, size);
		// ret = cvi_spacc_init_buffer(spacc_private, size);
		if (ret != 0){
			printk("cvi_spacc_create_pool failed, ret: %d\n", ret);
			goto out_unlock;
		}
		break;
	}
	case IOCTL_SPACC_GET_MEMPOOL_SIZE: {
		ret = copy_to_user((unsigned char *)arg,
				   (unsigned char *)&spacc_private->buffer_size,
				   sizeof(spacc_private->buffer_size));
		if (ret != 0){
			printk("copy_to_user buffer_size failed, ret: %d\n", ret);
			goto out_unlock;
		}

		break;
	}
	case IOCTL_SPACC_SHA256_ACTION: {
		if (spacc_private->used_size == 0) {
			printk("used_size : %d\n", spacc_private->used_size);
			ret = -EINVAL;
			goto out_unlock;
		}
		phys_addr_t src_phys = virt_to_phys(spacc_private->buffer);
		ret = spacc_sha256(src_phys, spacc_private->used_size);
		if (ret < 0) {
			dev_err(dev, "plat_cryptodma_do failed\n");
			goto out_unlock;
		}
		spacc_private->used_size = ret;
		spacc_private->read_size = 0;
		break;
	}
	case IOCTL_SPACC_SM3_ACTION: {
		if (spacc_private->used_size < 0) {
			dev_err(dev, "used_size : %d\n", spacc_private->used_size);
			ret = -EINVAL;
			goto out_unlock;
		}
		phys_addr_t src_phys = virt_to_phys(spacc_private->buffer);
		ret = spacc_sm3(src_phys, spacc_private->used_size);
		if (ret < 0) {
			dev_err(dev, "plat_cryptodma_do failed\n");
			goto out_unlock;
		}
		spacc_private->used_size = ret;
		spacc_private->read_size = 0;
		break;
	}
	case IOCTL_SPACC_BASE64_ACTION: {
		struct spacc_base64 b64 = { 0 };
		u32 padding_size = 0;
		ret = copy_from_user((unsigned char *)&b64,
				     (unsigned char *)arg, sizeof(b64));
		if (ret != 0){
			dev_err(dev, "copy_from_user b64 failed, ret: %d\n", ret);
			goto out_unlock;
		}

		if (!b64.action) {
			char *buf = spacc_private->buffer;

			if (buf[spacc_private->used_size - 1] == '=')
				padding_size++;
			if (buf[spacc_private->used_size - 2] == '=')
				padding_size++;
		}

		ret = spacc_base64(spacc_private, b64.customer_code,
				   b64.action);
		if (ret < 0) {
			dev_err(dev, "plat_cryptodma_do failed\n");
			goto out_unlock;
		}

		if (!b64.action)
			ret -= padding_size;
		spacc_private->used_size = ret;
		spacc_private->read_size = 0;
		break;
	}
	case IOCTL_SPACC_BASE64_INNER: {
		spacc_base64_inner_config_s b64;

		ret = copy_from_user((unsigned char *)&b64,
				     (unsigned char *)arg, sizeof(b64));
		if (ret != 0){
			dev_err(dev, "copy_from_user b64 failed, ret: %d\n", ret);
			goto out_unlock;
		}

		ret = spacc_base64_inner(&b64);
		break;
	}
	case IOCTL_SPACC_AES_ACTION: {
		spacc_aes_config_s config = { 0 };
		void *key_kernel_addr = NULL;
		void *iv_kernel_addr = NULL;
		phys_addr_t src_phys;
		uint32_t len;
		uint64_t key_len = 0;

		ret = copy_from_user((unsigned char *)&config,
				     (unsigned char *)arg, sizeof(config));
		if (ret != 0) {
            dev_err(dev, "copy_from_user config failed, ret: %d\n", ret);
            goto out_unlock;
        }

        ret = handle_src_phys(spacc_private, &config, &src_phys, &len);
        if (ret != 0) {
            dev_err(dev, "handle_src_phys failed, ret: %d\n", ret);
            goto out_unlock;
        }

        ret = handle_key_iv(&config, &key_kernel_addr, &iv_kernel_addr,
                    &key_len);
        if (ret != 0) {
            dev_err(dev, "handle_key_iv failed, ret: %d\n", ret);
            goto out_unlock;
        }
		if(key_kernel_addr)
			arch_sync_dma_for_device(virt_to_phys(key_kernel_addr), key_len, DMA_TO_DEVICE);
		if(iv_kernel_addr)
			arch_sync_dma_for_device(virt_to_phys(iv_kernel_addr), 16, DMA_TO_DEVICE);
		ret = spacc_aes(src_phys, len, virt_to_phys(key_kernel_addr),
				key_len, virt_to_phys(iv_kernel_addr), &config);
		if (ret < 0) {
			dev_err(dev, "spacc_aes failed, ret: %d\n", ret);
			if (key_kernel_addr)
				kfree(key_kernel_addr);
			if (iv_kernel_addr)
				kfree(iv_kernel_addr);
			goto out_unlock;
		}
		if (ret > 0) {
            arch_sync_dma_for_device(src_phys, ret, DMA_FROM_DEVICE);
            spacc_private->used_size = ret;
            spacc_private->read_size = 0;
        }
		if (key_kernel_addr)
			kfree(key_kernel_addr);
		if (iv_kernel_addr)
			kfree(iv_kernel_addr);
        break;
	}
	case IOCTL_SPACC_SM4_ACTION: {
		spacc_sm4_config_s config = { 0 };
		void *key_kernel_addr = NULL;
		void *iv_kernel_addr = NULL;
		phys_addr_t src_phys;
		uint32_t len;
		uint64_t key_len = 0;
		ret = copy_from_user((unsigned char *)&config,
				     (unsigned char *)arg, sizeof(config));
		if (ret != 0) {
			dev_err(dev, "copy_from_user config failed, ret: %d\n", ret);
			goto out_unlock;
		}

		ret = handle_src_phys(spacc_private, &config, &src_phys, &len);
		if (ret != 0) {
			dev_err(dev, "copy_from_user config failed, ret: %d\n", ret);
			goto out_unlock;
		}

		ret = handle_key_iv((spacc_aes_config_s *)&config,
				    &key_kernel_addr, &iv_kernel_addr,
				    &key_len);
		if (ret != 0) {
			dev_err(dev, "handle_key_iv failed, ret: %d\n", ret);
			goto out_unlock;
		}
		if(key_kernel_addr)
			arch_sync_dma_for_device(virt_to_phys(key_kernel_addr), key_len, DMA_TO_DEVICE);
		if(iv_kernel_addr)
			arch_sync_dma_for_device(virt_to_phys(iv_kernel_addr), 16, DMA_TO_DEVICE);
		ret = spacc_sm4(src_phys, len, virt_to_phys(key_kernel_addr),
				key_len, virt_to_phys(iv_kernel_addr), &config);
		if (ret < 0) {
			dev_err(dev, "spacc_sm4 failed\n");
			if (key_kernel_addr)
				kfree(key_kernel_addr);
			if (iv_kernel_addr)
				kfree(iv_kernel_addr);
			goto out_unlock;
		}

		arch_sync_dma_for_device(src_phys, spacc_private->used_size,
					 DMA_FROM_DEVICE);
		spacc_private->used_size = ret;
		spacc_private->read_size = 0;
		if (key_kernel_addr)
			kfree(key_kernel_addr);
		if (iv_kernel_addr)
			kfree(iv_kernel_addr);
		break;
	}
	case IOCTL_SPACC_DES_ACTION: {
		spacc_des_config_s config = { 0 };
		phys_addr_t src_phys, key_phys, iv_phys;
		void *key_kernel_addr = NULL;
		void *iv_kernel_addr = NULL;
		int key_len;
		if (spacc_private->used_size == 0) {
			printk("spacc_dev->used_size : %d\n",
			       spacc_private->used_size);
			ret = -EINVAL;
			goto out_unlock;
		}

		ret = copy_from_user((unsigned char *)&config,
				     (unsigned char *)arg, sizeof(config));
		if (ret != 0){
			dev_err(dev, "copy_from_user config failed, ret: %d\n", ret);
			goto out_unlock;
		}
		src_phys = virt_to_phys(spacc_private->buffer);
		key_len = 16;
		key_kernel_addr = kmalloc(key_len, GFP_KERNEL);
		if (!key_kernel_addr) {
			dev_err(dev, "kmalloc for key failed\n");
			goto out_unlock;
		}
		if (copy_from_user(key_kernel_addr, config.key, key_len) != 0) {
			dev_err(dev, "copy_from_user key failed\n");
			if (key_kernel_addr)
				kfree(key_kernel_addr);
			goto out_unlock;
		}
		if (config.mode != SPACC_ALGO_MODE_ECB) {
			iv_kernel_addr = kmalloc(16, GFP_KERNEL);
			if (!iv_kernel_addr) {
				dev_err(dev, "kmalloc for IV failed\n");
				if (key_kernel_addr)
					kfree(key_kernel_addr);
				goto out_unlock;
			}
			if (copy_from_user(iv_kernel_addr, config.iv, 16) != 0) {
				dev_err(dev, "copy_from_user iv failed\n");
				if (key_kernel_addr)
					kfree(key_kernel_addr);
				if (iv_kernel_addr)
					kfree(iv_kernel_addr);
				goto out_unlock;
			}
		}
		key_phys = virt_to_phys(key_kernel_addr);
		iv_phys = virt_to_phys(iv_kernel_addr);
		if(key_phys)
			arch_sync_dma_for_device(key_phys, key_len, DMA_TO_DEVICE);
		if(iv_phys)
			arch_sync_dma_for_device(iv_phys, 16, DMA_TO_DEVICE);
		ret = spacc_des(src_phys, spacc_private->used_size, key_phys,
				key_len, iv_phys, &config, 0);
		if (ret < 0) {
			dev_err(dev, "plat_cryptodma_do failed\n");
			if (key_kernel_addr)
				kfree(key_kernel_addr);
			if (iv_kernel_addr)
				kfree(iv_kernel_addr);
			goto out_unlock;
		}
		arch_sync_dma_for_device(src_phys, spacc_private->used_size,
					 DMA_FROM_DEVICE);
		spacc_private->used_size = ret;
		spacc_private->read_size = 0;
		if (key_kernel_addr)
			kfree(key_kernel_addr);
		if (iv_kernel_addr)
			kfree(iv_kernel_addr);
		break;
	}
	case IOCTL_SPACC_TDES_ACTION: {
		spacc_tdes_config_s config = { 0 };
		phys_addr_t src_phys, key_phys, iv_phys;
		void *key_kernel_addr = NULL;
		void *iv_kernel_addr = NULL;
		int key_len;
		if (spacc_private->used_size == 0) {
			printk("spacc_dev->used_size : %d\n",
			       spacc_private->used_size);
			ret = -EINVAL;
			goto out_unlock;
		}

		ret = copy_from_user((unsigned char *)&config,
				     (unsigned char *)arg, sizeof(config));
		if (ret != 0){
			dev_err(dev, "copy_from_user config failed, ret: %d\n", ret);
			goto out_unlock;
		}
		src_phys = virt_to_phys(spacc_private->buffer);
		key_len = 24;
		key_kernel_addr = kmalloc(key_len, GFP_KERNEL);
		if (!key_kernel_addr) {
			dev_err(dev, "kmalloc for key failed\n");
			goto out_unlock;
		}
		if (copy_from_user(key_kernel_addr, config.key, key_len) != 0) {
			dev_err(dev, "copy_from_user key failed\n");
			if (key_kernel_addr)
				kfree(key_kernel_addr);
			goto out_unlock;
		}
		if (config.mode != SPACC_ALGO_MODE_ECB) {
			iv_kernel_addr = kmalloc(16, GFP_KERNEL);
			if (!iv_kernel_addr) {
				dev_err(dev, "kmalloc for IV failed\n");
				if (key_kernel_addr)
					kfree(key_kernel_addr);
				ret = -ENOMEM;
				goto out_unlock;
			}
			if (copy_from_user(iv_kernel_addr, config.iv, 16) != 0) {
				dev_err(dev, "copy_from_user iv failed\n");
				if (key_kernel_addr)
					kfree(key_kernel_addr);
				if (iv_kernel_addr)
					kfree(iv_kernel_addr);
				ret = -EFAULT;
				goto out_unlock;
			}
		}
		key_phys = virt_to_phys(key_kernel_addr);
		iv_phys = virt_to_phys(iv_kernel_addr);
		ret = spacc_des(src_phys, spacc_private->used_size, key_phys,
				key_len, iv_phys, &config, 1);
		if (ret < 0) {
			dev_err(dev, "plat_cryptodma_do failed\n");
			if (key_kernel_addr)
				kfree(key_kernel_addr);
			if (iv_kernel_addr)
				kfree(iv_kernel_addr);
			goto out_unlock;
		}
		arch_sync_dma_for_device(src_phys, spacc_private->used_size,
					 DMA_FROM_DEVICE);
		spacc_private->used_size = ret;
		spacc_private->read_size = 0;
		if (key_kernel_addr)
			kfree(key_kernel_addr);
		if (iv_kernel_addr)
			kfree(iv_kernel_addr);
		break;
	}
	default:
		ret = -EINVAL;
		goto out_unlock;
	}
out_unlock:
    mutex_unlock(&spacc_private->lock);
    return ret;
}
const struct file_operations spacc_fops = {
	.owner = THIS_MODULE,
	.open = spacc_open,
	.read = spacc_read,
	.write = spacc_write,
	.release = spacc_release,
	.unlocked_ioctl = spacc_ioctl,
};

static int cvitek_spacc_drv_probe(struct platform_device *pdev)
{
	struct cvi_spacc *spacc;
	struct device *dev = &pdev->dev;
	int ret = 0;

	spacc = devm_kzalloc(dev, sizeof(*spacc), GFP_KERNEL);
	if (!spacc)
		return -ENOMEM;

	spacc->dev = dev;
	spacc->spacc_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spacc->spacc_base)) {
		dev_err(dev, "Failed to ioremap resource\n");
		return PTR_ERR(spacc->spacc_base);
	}

	ret = alloc_chrdev_region(&spacc->tdev, 0, 1, DEVICE_NAME);
	if (ret) {
		dev_err(dev, "Failed to allocate chrdev region\n");
		return ret;
	}

	cdev_init(&spacc->cdev, &spacc_fops);
	spacc->cdev.owner = THIS_MODULE;

	ret = cdev_add(&spacc->cdev, spacc->tdev, 1);
	if (ret) {
		dev_err(dev, "Failed to add cdev\n");
		goto failed_cdev;
	}

	spacc->spacc_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(spacc->spacc_class)) {
		dev_err(dev, "Failed to create class\n");
		ret = PTR_ERR(spacc->spacc_class);
		goto failed_class;
	}

	if (IS_ERR(device_create(spacc->spacc_class, NULL, spacc->tdev, spacc,
				 DEVICE_NAME))) {
		dev_err(dev, "Failed to create device\n");
		ret = PTR_ERR(device_create(spacc->spacc_class, NULL,
					    spacc->tdev, spacc, DEVICE_NAME));
		goto failed_device;
	}

	platform_set_drvdata(pdev, spacc);
	dev_info(dev, "cvitek_spacc_drv_probe success\n");
	return 0;

failed_device:
	class_destroy(spacc->spacc_class);
failed_class:
	cdev_del(&spacc->cdev);
failed_cdev:
	unregister_chrdev_region(spacc->tdev, 1);
	return ret;
}

static int cvitek_spacc_drv_remove(struct platform_device *pdev)
{
	struct cvi_spacc *spacc = platform_get_drvdata(pdev);
	
	device_destroy(spacc->spacc_class, spacc->tdev);
	class_destroy(spacc->spacc_class);
    cdev_del(&spacc->cdev);
    unregister_chrdev_region(spacc->tdev, 1);
    platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id cvitek_spacc_of_match[] = {
	{
		.compatible = "cvitek,spacc",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cvitek_spacc_of_match);
#endif

static struct platform_driver cvitek_spacc_driver = {
	.probe		= cvitek_spacc_drv_probe,
	.remove		= cvitek_spacc_drv_remove,
	.driver		= {
		.name	= "cvitek_spacc",
		.of_match_table = of_match_ptr(cvitek_spacc_of_match),
		// .pm     = &cvitek_spacc_pm_ops,
	},
};

module_platform_driver(cvitek_spacc_driver);

MODULE_DESCRIPTION("Cvitek Spacc Driver");
MODULE_LICENSE("GPL");
