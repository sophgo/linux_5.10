// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024, Cvitek Co. Ltd. All rights reserved.
 *
 * Cvitek remoteproc driver for dual-core rpmsg communication.
 *
 * Linux (A53) is the HOST/DRIVER end of the virtio rpmsg link; the AliOS
 * small core (C906L) is the REMOTE/DEVICE end and is already running when this
 * driver probes.  Therefore this driver does NOT load any firmware — it uses
 * the remoteproc "attach" path (rproc->state = RPROC_DETACHED) and reads the
 * resource table that AliOS has placed at the start of the shared memory.
 *
 * Notification (kick) between the cores goes through the cvitek mailbox
 * controller (cvitek-mailbox.c): a kick writes mailbox slot <notifyid> and
 * triggers an interrupt on the target core.  All rpmsg payload travels through
 * the vrings in shared memory.
 *
 * The shared-memory layout (offsets from CVIMMAP_SHARE_MEM_ADDR) and the
 * notify ids MUST match components/cvi_rpmsg/include/cvi_rpmsg_rsc_table.h on
 * the AliOS side.
 */

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/slab.h>
#include "remoteproc_internal.h"

/* Shared-memory layout — must match the AliOS cvi_rpmsg_rsc_table.h. */
#define CVI_RPROC_SHM_RSC_OFFSET	0x0000
#define CVI_RPROC_SHM_RSC_SIZE		0x1000
#define CVI_RPROC_SHM_VRING0_OFFSET	0x1000
#define CVI_RPROC_SHM_VRING0_SIZE	0x3000
#define CVI_RPROC_SHM_VRING1_OFFSET	0x4000
#define CVI_RPROC_SHM_VRING1_SIZE	0x4000
#define CVI_RPROC_SHM_BUFFER_OFFSET	0x8000
#define CVI_RPROC_SHM_BUFFER_SIZE	0x10000
#define CVI_RPROC_SHM_TOTAL_SIZE	0x18000

/* Index of the vdev0buffer memory-region in the DTS "memory-region" list
 * (0 = vdev0vring0, 1 = vdev0vring1, 2 = vdev0buffer). */
#define CVI_RPROC_BUFFER_MEM_IDX	2

/* Mailbox CPU index: Linux A53 = 0, AliOS C906L = 2 (matches cvitek-mailbox.c
 * and the AliOS IPCM CPU_C906B=0 / CPU_C960L=2 definitions). */

struct cvi_rproc {
	struct device *dev;
	struct rproc *rproc;

	struct mbox_client cl;
	struct mbox_chan *tx_chan;
	struct mbox_chan *rx_chan;

	void __iomem *shm_va;
	phys_addr_t shm_phys;
};

/*
 * Carveout alloc/release.  The shared memory is ioremapped once in probe
 * (uncached, so the A53 always observes what the C906L has flushed), so the
 * per-carveout ops are no-ops — they only need to keep mem->va valid.
 */
static int cvi_rproc_mem_alloc(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	(void)rproc;
	return mem->va ? 0 : -ENOMEM;
}

static int cvi_rproc_mem_release(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	(void)rproc;
	(void)mem;
	return 0;
}

/*
 * Register the two vring carveouts.  rp_find_vq() in remoteproc_virtio.c looks
 * up vring memory by name "vdev0vring0" / "vdev0vring1", so the names must
 * match exactly.  The va is the pre-ioremapped shared-memory offset; da is the
 * matching device (== physical) address that AliOS also uses.
 */
static int cvi_rproc_add_carveouts(struct cvi_rproc *cproc)
{
	struct rproc *rproc = cproc->rproc;
	struct device *dev = cproc->dev;
	struct rproc_mem_entry *mem;

	mem = rproc_mem_entry_init(dev, cproc->shm_va + CVI_RPROC_SHM_VRING0_OFFSET,
				   (dma_addr_t)(cproc->shm_phys + CVI_RPROC_SHM_VRING0_OFFSET),
				   CVI_RPROC_SHM_VRING0_SIZE,
				   cproc->shm_phys + CVI_RPROC_SHM_VRING0_OFFSET,
				   cvi_rproc_mem_alloc, cvi_rproc_mem_release,
				   "vdev0vring0");
	if (!mem)
		return -ENOMEM;
	rproc_add_carveout(rproc, mem);

	mem = rproc_mem_entry_init(dev, cproc->shm_va + CVI_RPROC_SHM_VRING1_OFFSET,
				   (dma_addr_t)(cproc->shm_phys + CVI_RPROC_SHM_VRING1_OFFSET),
				   CVI_RPROC_SHM_VRING1_SIZE,
				   cproc->shm_phys + CVI_RPROC_SHM_VRING1_OFFSET,
				   cvi_rproc_mem_alloc, cvi_rproc_mem_release,
				   "vdev0vring1");
	if (!mem)
		return -ENOMEM;
	rproc_add_carveout(rproc, mem);

	/* vdev0buffer: registered with of_resm_idx so that rproc_add_virtio_dev
	 * attaches the matching DTS memory-region (index 2 = vdev0buffer_reserved
	 * @ SHM+0x8000, which is exactly the AliOS shared buffer pool) as the
	 * virtio device's DMA coherent pool.  Without this, the core falls back
	 * to memory-region index 0 (vdev0vring0) and dma_alloc_coherent() would
	 * hand out buffers that overwrite the resource table / vring0. */
	mem = rproc_of_resm_mem_entry_init(dev, CVI_RPROC_BUFFER_MEM_IDX,
					   CVI_RPROC_SHM_BUFFER_SIZE,
					   cproc->shm_phys + CVI_RPROC_SHM_BUFFER_OFFSET,
					   "vdev0buffer");
	if (!mem)
		return -ENOMEM;
	rproc_add_carveout(rproc, mem);

	return 0;
}

/* ---- rproc_ops ---- */

static int cvi_rproc_prepare(struct rproc *rproc)
{
	/* In attach mode rproc_actuate() does not call ops->prepare (only the
	 * firmware-boot path does), so carveouts are registered from probe()
	 * instead.  Keep this as a no-op for safety. */
	(void)rproc;
	return 0;
}

static int cvi_rproc_start(struct rproc *rproc)
{
	/* AliOS is already running; nothing to boot. */
	(void)rproc;
	return 0;
}

static int cvi_rproc_stop(struct rproc *rproc)
{
	(void)rproc;
	return 0;
}

static int cvi_rproc_attach(struct rproc *rproc)
{
	/* Attaching to the already-running AliOS core; nothing to do. */
	(void)rproc;
	return 0;
}

static void cvi_rproc_kick(struct rproc *rproc, int vqid)
{
	struct cvi_rproc *cproc = rproc->priv;
	int slot = vqid;
	int ret;

	if (!cproc->tx_chan)
		return;

	/* The mailbox controller uses *data as the slot index (= notify id),
	 * which the AliOS ISR decodes back into a vring notify id. */
	ret = mbox_send_message(cproc->tx_chan, &slot);
	if (ret < 0)
		dev_dbg(cproc->dev, "mbox_send_message failed: %d\n", ret);
}

static void *cvi_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len)
{
	struct cvi_rproc *cproc = rproc->priv;
	u64 offset;

	if (da < cproc->shm_phys ||
	    da + len > cproc->shm_phys + CVI_RPROC_SHM_TOTAL_SIZE)
		return NULL;

	offset = da - cproc->shm_phys;
	return cproc->shm_va + offset;
}

/*
 * Return the resource table AliOS has prepared in shared memory.  This is the
 * key op for the attach path: rproc_set_rsc_table() stores the returned
 * pointer and rproc_handle_resources() parses it to create the virtio vdev.
 */
static struct resource_table *
cvi_rproc_find_loaded_rsc_table(struct rproc *rproc, const struct firmware *fw)
{
	struct cvi_rproc *cproc = rproc->priv;

	rproc->table_sz = CVI_RPROC_SHM_RSC_SIZE;
	return (struct resource_table *)(cproc->shm_va + CVI_RPROC_SHM_RSC_OFFSET);
}

static const struct rproc_ops cvi_rproc_ops = {
	.prepare              = cvi_rproc_prepare,
	.start                = cvi_rproc_start,
	.stop                 = cvi_rproc_stop,
	.attach               = cvi_rproc_attach,
	.kick                 = cvi_rproc_kick,
	.da_to_va             = cvi_rproc_da_to_va,
	.find_loaded_rsc_table = cvi_rproc_find_loaded_rsc_table,
};

/* ---- mailbox rx callback ---- */

/*
 * Called by the mailbox controller when AliOS kicks us.  The mailbox driver
 * passes the slot index (= vring notify id) as the message, so we forward it
 * to the remoteproc virtio layer.
 */
static void cvi_rproc_rx_callback(struct mbox_client *client, void *msg)
{
	struct cvi_rproc *cproc = container_of(client, struct cvi_rproc, cl);
	int notifyid;

	if (!msg || !cproc->rproc)
		return;

	notifyid = *(int *)msg;
	rproc_vq_interrupt(cproc->rproc, notifyid);
}

/* ---- probe / remove ---- */

static int cvi_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *shm_node;
	struct resource res;
	struct cvi_rproc *cproc;
	struct rproc *rproc;
	int ret;

	cproc = devm_kzalloc(dev, sizeof(*cproc), GFP_KERNEL);
	if (!cproc)
		return -ENOMEM;
	cproc->dev = dev;

	/* Shared memory base from the first memory-region (vdev0vring0). */
	shm_node = of_parse_phandle(np, "memory-region", 0);
	if (!shm_node) {
		dev_err(dev, "missing memory-region\n");
		return -ENODEV;
	}
	ret = of_address_to_resource(shm_node, 0, &res);
	of_node_put(shm_node);
	if (ret) {
		dev_err(dev, "can't get shared memory address\n");
		return ret;
	}
	cproc->shm_phys = res.start;
	cproc->shm_va = devm_ioremap(dev, res.start, CVI_RPROC_SHM_TOTAL_SIZE);
	if (!cproc->shm_va) {
		dev_err(dev, "failed to ioremap shared memory\n");
		return -ENOMEM;
	}

	/* The vdev0buffer carveout (registered in cvi_rproc_add_carveouts with
	 * of_resm_idx=2) makes rproc_add_virtio_dev attach the matching DTS
	 * memory-region as the virtio device's DMA coherent pool, so that
	 * virtio_rpmsg_bus's dma_alloc_coherent() hands out buffers from the
	 * same physical memory the AliOS side uses as its shared buffer pool. */
	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));

	/* Mailbox client: one tx channel (kick AliOS), one rx channel (AliOS
	 * kicks us). */
	cproc->cl.dev = dev;
	cproc->cl.tx_block = false;
	cproc->cl.knows_txdone = false;
	cproc->cl.rx_callback = cvi_rproc_rx_callback;

	cproc->tx_chan = mbox_request_channel_byname(&cproc->cl, "tx");
	if (IS_ERR(cproc->tx_chan)) {
		ret = PTR_ERR(cproc->tx_chan);
		dev_err(dev, "failed to request tx mailbox channel: %d\n", ret);
		return ret;
	}
	cproc->rx_chan = mbox_request_channel_byname(&cproc->cl, "rx");
	if (IS_ERR(cproc->rx_chan)) {
		ret = PTR_ERR(cproc->rx_chan);
		dev_err(dev, "failed to request rx mailbox channel: %d\n", ret);
		goto err_tx_chan;
	}

	rproc = rproc_alloc(dev, dev_name(dev), &cvi_rproc_ops,
			    "c906l-rpmsg.elf", sizeof(*cproc));
	if (!rproc) {
		ret = -ENOMEM;
		goto err_rx_chan;
	}

	rproc->has_iommu = false;
	/* AliOS is already running: attach, do not load firmware. */
	rproc->auto_boot = true;
	rproc->state = RPROC_DETACHED;

	cproc->rproc = rproc;
	rproc->priv = cproc;

	/* Register vring/buffer carveouts here (not in ops->prepare, which is
	 * not called in attach mode).  rproc_alloc_vring() looks up vring
	 * memory by name "vdev0vring0/1" and rproc_add_virtio_dev() looks up
	 * "vdev0buffer"; both run during rproc_actuate() after rproc_add(). */
	ret = cvi_rproc_add_carveouts(cproc);
	if (ret) {
		dev_err(dev, "failed to register carveouts: %d\n", ret);
		goto err_rproc_free;
	}

	platform_set_drvdata(pdev, rproc);

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(dev, "rproc_add failed: %d\n", ret);
		goto err_rproc_free;
	}

	dev_info(dev, "Cvitek remoteproc (attach) registered, shm @ %pa\n",
		 &cproc->shm_phys);
	return 0;

err_rproc_free:
	rproc_free(rproc);
err_rx_chan:
	mbox_free_channel(cproc->rx_chan);
err_tx_chan:
	mbox_free_channel(cproc->tx_chan);
	return ret;
}

static int cvi_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct cvi_rproc *cproc = rproc->priv;

	rproc_del(rproc);
	mbox_free_channel(cproc->rx_chan);
	mbox_free_channel(cproc->tx_chan);
	rproc_free(rproc);
	return 0;
}

static const struct of_device_id cvi_rproc_match[] = {
	{ .compatible = "cvitek,cv18xx-rproc", },
	{ .compatible = "cvitek,cv19xx-rproc", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, cvi_rproc_match);

static struct platform_driver cvi_rproc_driver = {
	.probe  = cvi_rproc_probe,
	.remove = cvi_rproc_remove,
	.driver = {
		.name = "cvitek-remoteproc",
		.of_match_table = cvi_rproc_match,
	},
};
module_platform_driver(cvi_rproc_driver);

MODULE_DESCRIPTION("Cvitek remoteproc driver for dual-core rpmsg (attach mode)");
MODULE_AUTHOR("Cvitek Co. Ltd.");
MODULE_LICENSE("GPL v2");
