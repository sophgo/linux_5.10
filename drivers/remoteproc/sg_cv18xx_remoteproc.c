// SPDX-License-Identifier: GPL-2.0
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/arm-smccc.h>
#include "remoteproc_internal.h"
#include <asm/sbi.h>

#define AP_SYSTEM_REG_BASE 0x1901000
#define RTOS_BOOT_STATUS_OFFSET 0xFF0
#define RTOS_BOOT_STATUS (AP_SYSTEM_REG_BASE + RTOS_BOOT_STATUS_OFFSET)
#define RTOS_BOOT_RSC_ADDR 0x1900400
#define RTOS_BOOT_RSC_SIZE 0x1900404
#define RTOS_BOOT_STATUS_RUNNING 0x1

struct cvitek_rproc_mem {
    void __iomem *cpu_addr;
    phys_addr_t bus_addr;
    u32 dev_addr;
    size_t size;
}
;
struct cvitek_rproc {
	struct rproc *rproc;
	struct cvitek_rproc_mem mem[4];
	struct reset_control *reset;
	struct clk *clk;
	struct regmap *boot_base;
	u32 boot_offset;
	int num_mems;
	struct platform_device *pdev;
	struct workqueue_struct *workqueue;
	struct work_struct vq_work;
	int vq_id;
};

static inline void setbits_32(void __iomem *reg, u32 set)                                                                                                                                                         
{
	iowrite32(ioread32(reg) | set, reg); 
}

#define OPTEE_SMC_CALL_CV_C906 0x03000014
static int cvitek_rproc_start(struct rproc *rproc)
{
#ifdef __riscv
    struct cvitek_rproc *cproc = rproc->priv;
	void __iomem *rvba_reg = ioremap(0x020B0000, 0x30);

	clk_prepare_enable(cproc->clk);
	sbi_reset_c906l(rproc->bootaddr);
	/*
    reset_control_assert(cproc->reset);

	setbits_32(rvba_reg + 0x04, 0x1 << 13);	
	iowrite32(rproc->bootaddr, rvba_reg + 0x20);
	iowrite32(rproc->bootaddr >> 32, rvba_reg + 0x24);

    reset_control_deassert(cproc->reset);
	*/
	clk_disable_unprepare(cproc->clk);
	iounmap(rvba_reg);
#else
    struct arm_smccc_res res = { 0 };
    struct cvitek_rproc *cproc = rproc->priv;

	//read sec reg to check if efuse is ready
	clk_prepare_enable(cproc->clk);
	arm_smccc_smc(OPTEE_SMC_CALL_CV_C906, (unsigned long)rproc->bootaddr,
		      0, 0, 0, 0, 0, 0, &res);
	clk_disable_unprepare(cproc->clk);
#endif
	dev_info(rproc->dev.parent, "Started from 0x%llx\n", rproc->bootaddr);
	return 0;
}
static int cvitek_rproc_stop(struct rproc *rproc)
{
    struct cvitek_rproc *cproc = rproc->priv;

    reset_control_assert(cproc->reset);
    dev_info(&rproc->dev, "remote processor stopped\n");
	return 0;
}

static int cvitek_rproc_mem_alloc(struct rproc *rproc,
				  struct rproc_mem_entry *mem)
{
	struct device *dev = rproc->dev.parent;
	void *va;
	va = ioremap_wc(mem->dma, mem->len);
	if (!va) {
		dev_err(dev, "Unable to map memory region: %pa%zx\n", &mem->dma,
			mem->len);
		return -ENOMEM;
	}
	/* Update memory entry va */
	mem->va = va;
	return 0;
}

static int cvitek_rproc_mem_release(struct rproc *rproc,
				    struct rproc_mem_entry *mem)
{
	iounmap(mem->va);
	return 0;
}

static int cvitek_rproc_get_loaded_rsc_table(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct cvitek_rproc *cproc = rproc->priv;
	void __iomem *rsc_reg_addr, *rsc_reg_size;
	phys_addr_t rsc_pa;
	size_t rsc_size;

	rsc_reg_addr = ioremap(RTOS_BOOT_RSC_ADDR, sizeof(u32));
	if (!rsc_reg_addr) {
		dev_err(dev, "failed to map rsc addr reg\n");
		return -ENOMEM;
	}

	rsc_reg_size = ioremap(RTOS_BOOT_RSC_SIZE, sizeof(u32));
	if (!rsc_reg_size) {
		dev_err(dev, "failed to map rsc size reg\n");
		iounmap(rsc_reg_addr);
		return -ENOMEM;
	}

	rsc_pa = ioread32(rsc_reg_addr);
	rsc_size = ioread32(rsc_reg_size);
	dev_err(dev, "rsc_pa: 0x%lx, rsc_size: 0x%zx\n", rsc_pa, rsc_size);

	iounmap(rsc_reg_addr);
	iounmap(rsc_reg_size);

	if (!rsc_pa || !rsc_size) {
		dev_warn(dev, "no rsc table found\n");
		return -EINVAL;
	}

	rproc->table_ptr = (struct resource_table *)devm_ioremap(dev, rsc_pa, rsc_size);
	if (IS_ERR_OR_NULL(rproc->table_ptr)) {
		dev_err(dev, "Unable to map memory region: %pa+%zx\n",
			&rsc_pa, rsc_size);
		rproc->table_ptr = NULL;
		return -ENOMEM;
	}

	rproc->cached_table = NULL;
	rproc->table_sz = rsc_size;
	return 0;
}

int cvitek_rproc_parse_memory_regions(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct device_node *np = dev->of_node;
	struct rproc_mem_entry *mem;
	struct reserved_mem *rmem;
	struct of_phandle_iterator it;
	int index = 0;

	of_phandle_iterator_init(&it, np, "memory-region", NULL, 0);
	while (of_phandle_iterator_next(&it) == 0) {
		rmem = of_reserved_mem_lookup(it.node);
		if (!rmem) {
			dev_err(dev, "unable to acquire memory-region\n");
			return -EINVAL;
		}
		/*  No need to map vdev buffer */
		dev_dbg(dev, "Memory region: %s\n", it.node->name);
		dev_dbg(dev, "Memory region base: %pa, size: %llx\n", &rmem->base, rmem->size);
		if (strcmp(it.node->name, "vdev0buffer")) {
			
			/* Register memory region */
			mem = rproc_mem_entry_init(
				dev, NULL, (dma_addr_t)rmem->base, rmem->size,
				rmem->base, cvitek_rproc_mem_alloc,
				cvitek_rproc_mem_release, it.node->name);

		} else {
			/* Register reserved memory for vdev buffer allocation */
			mem = rproc_of_resm_mem_entry_init(dev, index,
							   rmem->size,
							   rmem->base,
							   it.node->name);
		}
		if (!mem) {
			dev_err(dev, "Failed to register memory region: %s\n", it.node->name);
			return -ENOMEM;
		}
		rproc_add_carveout(rproc, mem);
		index++;
	}
	return 0;
}

static int cvitek_rproc_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	int ret;
	struct device *dev = rproc->dev.parent;

	ret = cvitek_rproc_parse_memory_regions(rproc);
	if (ret)
		return ret;

	if (rproc_elf_load_rsc_table(rproc, fw))
		dev_warn(dev, "no resource table found for this firmware\n");

	return 0;
}

static void mailbox_notify(int vringid);
void cvitek_rproc_kick(struct rproc *rproc, int id)
{
	dev_warn(rproc->dev.parent, "kick %d\n", id);
	mailbox_notify(id);
}

static int cvitek_rproc_attach(struct rproc *rproc)
{
	return 0;
}

static const struct rproc_ops cvitek_rproc_ops = {
	.start = cvitek_rproc_start,
	.stop = cvitek_rproc_stop,
	.parse_fw = cvitek_rproc_parse_fw,
	.load = rproc_elf_load_segments,
	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
	.sanity_check = rproc_elf_sanity_check,
	.get_boot_addr = rproc_elf_get_boot_addr,
	.kick = cvitek_rproc_kick,
	.attach = cvitek_rproc_attach,
};

static const struct of_device_id cvitek_rproc_match[] = {
	{
		.compatible = "cvitek,cv18xx-c906l-rproc",
	},
	{},
};
MODULE_DEVICE_TABLE(of, cvitek_rproc_match);
static const char *cvitek_rproc_get_firmware(struct platform_device *pdev)
{
	const char *fw_name;
	int ret;
	ret = of_property_read_string(pdev->dev.of_node, "firmware-name",
				      &fw_name);
	if (ret)
		return ERR_PTR(ret);
	return fw_name;
}
static int cvitek_rproc_parse_dt(struct platform_device *pdev)
{
	//TODO
	return 0;
}

#if 1
#define PHYS_ADDR   0x1900000
#define MAP_SIZE    0x400
static void __iomem *reg_base;

union cpu_mailbox_info_offset {
    char mbox_info;
    int  reserved;
};

union cpu_mailbox_int_clr_offset {
    char mbox_int_clr;
    int  reserved;
};
union cpu_mailbox_int_mask_offset {
    char mbox_int_mask;
    int  reserved;
};
union cpu_mailbox_int_offset {
    char mbox_int;
    int  reserved;
};
union cpu_mailbox_int_raw_offset {
    char mbox_int_raw;
    int  reserved;
};

union mailbox_set {
    char mbox_set;
    int  reserved;
};
union mailbox_status {
    char mbox_status;
    int  reserved;
};

union cpu_mailbox_status {
    char mbox_status;
    int  reserved;
};

/* register mapping refers to mailbox user guide*/
struct cpu_mbox_int {
    union cpu_mailbox_int_clr_offset  cpu_mbox_int_clr;
    union cpu_mailbox_int_mask_offset cpu_mbox_int_mask;
    union cpu_mailbox_int_offset      cpu_mbox_int_int;
    union cpu_mailbox_int_raw_offset  cpu_mbox_int_raw;
};

struct mailbox_set_register {
    union cpu_mailbox_info_offset cpu_mbox_en[4]; //0x00, 0x04, 0x08, 0x0c
    struct cpu_mbox_int
            cpu_mbox_set[4]; //0x10~0x1C, 0x20~0x2C, 0x30~0x3C, 0x40~0x4C
    int     reserved[4]; //0x50~0x5C
    union mailbox_set        mbox_set; //0x60
    union mailbox_status     mbox_status; //0x64
    int                      reserved2[2]; //0x68~0x6C
    union cpu_mailbox_status cpu_mbox_status[4]; //0x70
};

struct mailbox_done_register {
    union cpu_mailbox_info_offset cpu_mbox_done_en[4];
    struct cpu_mbox_int           cpu_mbox_done[4];
};

volatile struct mailbox_set_register  *mbox_reg;
volatile struct mailbox_done_register *mbox_done_reg;
volatile unsigned long *mailbox_context; // mailbox buffer context is 64 Bytess

#define MAILBOX_MAX_NUM        0x0008
#define MAILBOX_DONE_OFFSET    0x0002
#define MAILBOX_CONTEXT_OFFSET 0x0400
#define RECV_FROM_CPU     1
#define SEND_TO_CPU  	   2

static void cvitek_rproc_vq_worker(struct work_struct *work)
{
	int i;
    struct cvitek_rproc *cv_rproc =
            container_of(work, struct cvitek_rproc, vq_work);
    struct rproc *rproc = cv_rproc->rproc;

    /* if rproc is going down, vdev may be gone already */
    // if (rproc_is_offlining(rproc))
    //     return;

    /* check both vrings */
    for (i = 0; i < MAILBOX_MAX_NUM && cv_rproc->vq_id != 0; i++) {
		if (cv_rproc->vq_id & (1 << i))
        	rproc_vq_interrupt(rproc, i);
	}
	cv_rproc->vq_id = 0;
}

static irqreturn_t mailbox_interrupt(int irq, void *data)
{
	struct rproc *rproc = data;
	struct cvitek_rproc *cproc = rproc->priv;
	char set_val, done_val;
	int i = 0;
    mbox_reg      = (struct mailbox_set_register *)reg_base;
    mbox_done_reg = (struct mailbox_done_register *)(reg_base +
                                                         MAILBOX_DONE_OFFSET);
    printk(KERN_INFO "mailbox interrupt in\n");
        /* clear the interrupt */
	set_val = mbox_reg->cpu_mbox_set[RECV_FROM_CPU].cpu_mbox_int_int.mbox_int;
	done_val = mbox_done_reg->cpu_mbox_done[RECV_FROM_CPU].cpu_mbox_int_int.mbox_int;
	cproc->vq_id = set_val;
	for (i = 0; i < MAILBOX_MAX_NUM && set_val > 0; i++) {
		/* valid_val uses unsigned char because of mailbox register table
		 * ~valid_val will be 0xFF
		 */
		unsigned char valid_val = set_val & (1 << i);
		if (valid_val) {
			printk(KERN_INFO "mailbox interrupt valid val: 0x%x\n", valid_val);
			/* mailbox buffer context is send from rtos, clear mailbox interrupt */
			mbox_reg->cpu_mbox_set[RECV_FROM_CPU].cpu_mbox_int_clr.mbox_int_clr = valid_val;
			// need to disable enable bit
			mbox_reg->cpu_mbox_en[RECV_FROM_CPU].mbox_info &= ~valid_val;
			
		}
	}
	// schedule_work(&cproc->vq_work);
	queue_work(cproc->workqueue, &cproc->vq_work);
	return IRQ_HANDLED;
}

static void mailbox_notify(int vringid)
{
	int valid = vringid;
	mbox_reg = (struct mailbox_set_register *)reg_base;

    mbox_reg->cpu_mbox_set[SEND_TO_CPU].cpu_mbox_int_clr.mbox_int_clr =
                (1 << valid);
    // trigger mailbox valid to rtos
    mbox_reg->cpu_mbox_en[SEND_TO_CPU].mbox_info |= (1 << valid);
    mbox_reg->mbox_set.mbox_set = (1 << valid);
}
#endif

static int cvitek_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct cvitek_rproc *cproc;
	struct device_node *np = dev->of_node;
	struct rproc *rproc;
	const char *firmware;
	static void __iomem *rtos_status_base;
	int rtos_status;
	int ret;
	int irq;
	int i;

	firmware = cvitek_rproc_get_firmware(pdev);
	if (IS_ERR(firmware))
		return PTR_ERR(firmware);

	rproc = rproc_alloc(dev, dev_name(dev), &cvitek_rproc_ops, firmware,
			    sizeof(*cproc));
	if (!rproc)
		return -ENOMEM;

	cproc = rproc->priv;
	cproc->rproc = rproc;
	rproc->has_iommu = false;
	cproc->pdev = pdev;
    
	cproc->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(cproc->reset)) {
		ret = PTR_ERR(cproc->reset);
		goto free_rproc;
	}

	cproc->clk = devm_clk_get(dev, "clk_efuse");
	if (IS_ERR(cproc->clk)) {
		dev_err(dev, "Failed to get clk_efuse: %ld\n", PTR_ERR(cproc->clk));
		ret = PTR_ERR(cproc->clk);
		goto free_rproc;
	}

	cproc->workqueue = alloc_workqueue("cvitek-rproc", WQ_UNBOUND, 0);
	if (!cproc->workqueue) {
		ret = -ENOMEM;
		goto free_rproc;
	}
	INIT_WORK(&cproc->vq_work, cvitek_rproc_vq_worker);

	reg_base = devm_ioremap(dev, PHYS_ADDR, MAP_SIZE);
	if (!reg_base) {
		dev_err(dev, "ioremap failed!\n");
		return -ENOMEM;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Failed to get IRQ: %d\n", irq);
		ret = irq;
		goto free_rproc;
	}

	ret = devm_request_irq(dev, irq, mailbox_interrupt, 0, "MAILBOX", rproc);
	if (ret < 0) {
		dev_err(dev, "Failed to request IRQ\n");
		return ret;
	}	

	rtos_status_base = devm_ioremap(dev, RTOS_BOOT_STATUS, sizeof(u32));
	if (!rtos_status_base) {
		dev_err(dev, "ioremap failed!\n");
		return -ENOMEM;
	}
	rtos_status = ioread32(rtos_status_base); // read once to make sure rtos is ready
	if (rtos_status == RTOS_BOOT_STATUS_RUNNING) {
		dev_dbg(dev, "rtos running, status: 0x%x\n", rtos_status);
		rproc->state = RPROC_DETACHED;
		cvitek_rproc_parse_memory_regions(rproc);
		cvitek_rproc_get_loaded_rsc_table(rproc);
	}

	ret = rproc_add(rproc);
	if (ret)
		goto free_rproc;

	platform_set_drvdata(pdev, rproc);

	return 0;
free_rproc:
	rproc_free(rproc);
	return ret;
}

static int cvitek_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct cvitek_rproc *cproc = rproc->priv;
	int i;
	rproc_del(rproc);
	destroy_workqueue(cproc->workqueue);
	rproc_put(rproc);
	reset_control_assert(cproc->reset);
	rproc_free(rproc);
	return 0;
}
static struct platform_driver cvitek_rproc_driver = {
    .probe = cvitek_rproc_probe,
    .remove = cvitek_rproc_remove,
    .driver = { .name = "cvitek-rproc",
                .of_match_table = of_match_ptr(cvitek_rproc_match),
    },
};
module_platform_driver(cvitek_rproc_driver);
MODULE_DESCRIPTION("Cvitek Remote Processor Control Driver");
MODULE_LICENSE("GPL v2");