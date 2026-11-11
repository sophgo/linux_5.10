/*
 * Copyright (c) 2024, Cvitek Co. Ltd. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 * Cvitek mailbox controller driver for Linux.
 * Provides standard mailbox controller API so that remoteproc
 * can use mbox_client to send/receive notifications to/from
 * the AliOS (small core).
 *
 * Hardware: Mailbox at 0x01900000, 8 slots, each 8 bytes.
 * Data transfer is NOT through mailbox - only notification (kick).
 * rpmsg data goes through vrings in shared memory.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mailbox_controller.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/cvi_mailbox.h>

/* Mailbox register structures (same as AliOS cvi_mailbox.h) */
union cpu_mailbox_info_offset { char mbox_info; int reserved; };
union cpu_mailbox_int_clr_offset { char mbox_int_clr; int reserved; };
union cpu_mailbox_int_mask_offset { char mbox_int_mask; int reserved; };
union cpu_mailbox_int_offset { char mbox_int; int reserved; };
union cpu_mailbox_int_raw_offset { char mbox_int_raw; int reserved; };
union mailbox_set { char mbox_set; int reserved; };
union mailbox_status { char mbox_status; int reserved; };
union cpu_mailbox_status { char mbox_status; int reserved; };

struct cpu_mbox_int {
    union cpu_mailbox_int_clr_offset  cpu_mbox_int_clr;
    union cpu_mailbox_int_mask_offset cpu_mbox_int_mask;
    union cpu_mailbox_int_offset      cpu_mbox_int_int;
    union cpu_mailbox_int_raw_offset  cpu_mbox_int_raw;
};

struct mailbox_set_register {
    union cpu_mailbox_info_offset cpu_mbox_en[4];
    struct cpu_mbox_int cpu_mbox_set[4];
    int reserved[4];
    union mailbox_set mbox_set;
    union mailbox_status mbox_status;
    int reserved2[2];
    union cpu_mailbox_status cpu_mbox_status[4];
};

#define MAILBOX_MAX_NUM         8
#define MAILBOX_CONTEXT_OFFSET  0x0400

#define LOCAL_CPU_ID 0
#define REMOTE_CPU_ID 2

struct cvi_mbox_chan {
    int idx;           /* Channel index (0..7), maps to mailbox slot */
    struct cvi_mbox *mbox;
};

struct cvi_mbox {
    struct device *dev;
    void __iomem *regs;
    unsigned long context_base;  /* offset to mailbox_context */
    int irq;
    struct mbox_controller controller;
    struct cvi_mbox_chan chans[MAILBOX_MAX_NUM];
    spinlock_t lock;
    /* bitmask of slots that fired and are awaiting threaded delivery.
     * Set in the hard IRQ, consumed in the IRQ thread; protected by lock. */
    unsigned char pending_slots;
    struct miscdevice misc;   /* /dev/mailbox for hw spinlock ioctls */
};

/* ===========================================================================
 * Hardware spinlock (sys_spinlock module)
 *
 * The mailbox block contains a sys_spinlock module at offset 0xc0 (per the
 * Mars3 mailbox spec / IPCM driver: spinlock_base(reg_base + 0xc0)).  It
 * provides 16 independent hardware spinlocks (LOCK_NUM = 16), each a 32-bit
 * register holding a 16-bit "lock value" (0 == unlocked).
 *
 * Lock/unlock protocol (spec "spinlock program guide"):
 *   - Lock  : write a non-zero owner value A to lock #n, then read back.  If
 *             the read value == A, the lock was free and is now held by this
 *             core.  If another core holds it (value B != A), the write is
 *             ignored and the read returns B.
 *   - Unlock: write the same value A again.  Writing the currently-held value
 *             releases the lock (value -> 0).  Only do this when the current
 *             value == A (i.e. we still own it); writing A to a free lock
 *             would instead take it.
 *
 * Each CPU MUST use a distinct non-zero value so the hardware can tell the
 * owners apart.  Linux is CPU0 (low byte); AliOS/C906L is CPU2 and uses the
 * high byte (per IPCM MB_LOCKCNT_SHIRT=8 convention), so they never collide.
 * =========================================================================== */

#define CVI_MBOX_SPINLOCK_OFFSET   0xc0
#define CVI_MBOX_SPINLOCK_NUM      16

/* Well-known lock field indices (shared convention with the AliOS side). */
#define CVI_MBOX_SPIN_MBOX         1   /* protect mailbox register access */
#define CVI_MBOX_SPIN_SHM          2   /* protect shared memory access  */

/* Owner values: per-CPU, non-zero, distinct.  Linux CPU0 = 0x0001. */
#define CVI_MBOX_SPINLOCK_CPU0_VAL 0x0001u


static inline void __iomem *
cvi_mbox_spinlock_reg(struct cvi_mbox *mbox, unsigned char lock_id)
{
    return mbox->regs + CVI_MBOX_SPINLOCK_OFFSET + lock_id * sizeof(u32);
}

/*
 * Acquire hardware spinlock @lock.  Writes the owner value and reads it back;
 * on match the lock is ours.  Retries with a 1us backoff until acquired or the
 * budget is exhausted.  Returns 0 on success, -ETIMEDOUT on timeout,
 * -EINVAL on a bad field/value.
 *
 * NOTE: this may be called from contexts that can spin briefly but not block
 * indefinitely; the retry budget bounds the worst case.  Pair every successful
 * lock with hw_spin_unlock().
 */
static int hw_spin_lock(struct cvi_mbox *mbox, unsigned char lock_id)
{
    void __iomem *reg;
    unsigned int tries;
    u16 v = CVI_MBOX_SPINLOCK_CPU0_VAL;

    if (!mbox || lock_id >= CVI_MBOX_SPINLOCK_NUM)
        return -EINVAL;

    reg = cvi_mbox_spinlock_reg(mbox, lock_id);

    for (tries = 0; tries < 1000000; tries++) {
        writew(v, reg);
        if (readw(reg) == v)
            return 0;
        udelay(1);
    }

    dev_err(mbox->dev, "hw spinlock %u acquire timeout (val=0x%x)\n", lock_id, v);
    return -ETIMEDOUT;
}

/*
 * Release hardware spinlock @lock.  Only writes (which unlocks) when the
 * current lock value matches our owner value, i.e. we still own it.  Writing
 * the held value clears it to 0 (unlocked).  No-op if we don't hold it.
 */
static void hw_spin_unlock(struct cvi_mbox *mbox, unsigned char lock_id)
{
    void __iomem *reg;
    u16 v = CVI_MBOX_SPINLOCK_CPU0_VAL;

    if (!mbox || lock_id >= CVI_MBOX_SPINLOCK_NUM)
        return;

    reg = cvi_mbox_spinlock_reg(mbox, lock_id);

    if (readw(reg) == v)
        writew(v, reg);
}

/* ===========================================================================
 * /dev/mailbox character device — exposes hw_spin_lock/unlock to userspace.
 *
 * The misc framework's misc_open() stores the struct miscdevice pointer in
 * file->private_data, so we recover the parent cvi_mbox via container_of.
 * =========================================================================== */

static long cvi_mailbox_ioctl(struct file *filp, unsigned int cmd,
                              unsigned long arg)
{
    struct cvi_mbox *mbox = container_of(filp->private_data,
                                         struct cvi_mbox, misc);
    unsigned char lock_id;

    if (!mbox)
        return -ENODEV;

    switch (cmd) {
    case CVI_MAILBOX_IOC_HW_SPIN_LOCK:
        lock_id = (unsigned char)arg;
        return hw_spin_lock(mbox, lock_id);

    case CVI_MAILBOX_IOC_HW_SPIN_UNLOCK:
        lock_id = (unsigned char)arg;
        hw_spin_unlock(mbox, lock_id);
        return 0;

    default:
        return -ENOTTY;
    }
}

static const struct file_operations cvi_mailbox_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = cvi_mailbox_ioctl,
    .compat_ioctl   = cvi_mailbox_ioctl,   /* 32-bit userspace compat */
};

/*
 * Hard IRQ half: acknowledge the mailbox hardware (clear the interrupt,
 * disable the slot's enable bit, clear the mailbox context buffer) and
 * record which slots fired.  No client delivery here — that must run in a
 * sleepable context because virtio rpmsg NS handling (device_add /
 * devtmpfs) sleeps, which is illegal in hard IRQ.
 */
static irqreturn_t cvi_mbox_isr_hard(int irq, void *dev_id)
{
    struct cvi_mbox *mbox = dev_id;
    unsigned char set_val;
    unsigned char pending = 0;
    int i;
    unsigned long flags;

    spin_lock_irqsave(&mbox->lock, flags);

    set_val = readb((void __iomem *)
        ((unsigned long)mbox->regs +
         offsetof(struct mailbox_set_register, cpu_mbox_set[LOCAL_CPU_ID]) +
         offsetof(struct cpu_mbox_int, cpu_mbox_int_int)));

    for (i = 0; i < MAILBOX_MAX_NUM && set_val > 0; i++) {
        unsigned char valid_val = set_val & (1 << i);

        if (valid_val) {
            /* Clear interrupt */
            writeb(valid_val, (void __iomem *)
                ((unsigned long)mbox->regs +
                 offsetof(struct mailbox_set_register, cpu_mbox_set[LOCAL_CPU_ID]) +
                 offsetof(struct cpu_mbox_int, cpu_mbox_int_clr)));
            /* Disable enable bit */
            writeb(readb((void __iomem *)
                ((unsigned long)mbox->regs +
                 offsetof(struct mailbox_set_register, cpu_mbox_en[LOCAL_CPU_ID]))) &
                   ~(1 << i),
                (void __iomem *)
                ((unsigned long)mbox->regs +
                 offsetof(struct mailbox_set_register, cpu_mbox_en[LOCAL_CPU_ID]) +
                 offsetof(union cpu_mailbox_info_offset, mbox_info)));

            /* Clear mailbox buffer */
            writel(0, (void __iomem *)(mbox->context_base + i * sizeof(u32)));

            pending |= (1 << i);
        }
    }

    mbox->pending_slots |= pending;
    spin_unlock_irqrestore(&mbox->lock, flags);

    return pending ? IRQ_WAKE_THREAD : IRQ_NONE;
}

/*
 * Threaded IRQ half: deliver each pending slot to the mailbox client (the
 * remoteproc driver).  Runs in a sleepable context, so the virtio vring
 * processing and rpmsg NS device registration it triggers may sleep safely.
 */
static irqreturn_t cvi_mbox_isr_thread(int irq, void *dev_id)
{
    struct cvi_mbox *mbox = dev_id;
    struct mbox_chan *chan = &mbox->controller.chans[1];
    unsigned char pending;
    int i;
    unsigned long flags;

    spin_lock_irqsave(&mbox->lock, flags);
    pending = mbox->pending_slots;
    mbox->pending_slots = 0;
    spin_unlock_irqrestore(&mbox->lock, flags);

    /*
     * Always deliver to channel 1 (the "rx" channel) and pass the slot
     * index (= vring notify id) as the message so the remoteproc
     * rx_callback can forward it to rproc_vq_interrupt().
     */
    for (i = 0; i < MAILBOX_MAX_NUM; i++) {
        if (pending & (1 << i)) {
            int slot = i;
            mbox_chan_received_data(chan, &slot);
        }
    }

    return IRQ_HANDLED;
}

static int cvi_mbox_send_data(struct mbox_chan *chan, void *data)
{
    struct cvi_mbox_chan *cvi_chan = chan->con_priv;
    struct cvi_mbox *mbox = cvi_chan->mbox;
    unsigned long flags;
    int idx;
    u32 val = 0;

    /*
     * Use *data as the slot index when the caller passes a non-NULL
     * pointer.  This lets the remoteproc driver map vqid (0 or 1)
     * directly to the matching mailbox slot, so that the AliOS ISR
     * can use the slot index as the notifyid.
     *
     * When data is NULL the channel's own index is used (backward
     * compatible).
     */
    idx = data ? min(*(int *)data, MAILBOX_MAX_NUM - 1) : cvi_chan->idx;

    spin_lock_irqsave(&mbox->lock, flags);

    /* Write data to mailbox context (slot idx) */
    writel(val | (idx & 0xFF), (void __iomem *)
        (mbox->context_base + idx * sizeof(u32)));

    /* Clear any pending interrupt for this slot on target CPU */
    writeb(1 << idx, (void __iomem *)
        ((unsigned long)mbox->regs +
         offsetof(struct mailbox_set_register, cpu_mbox_set[REMOTE_CPU_ID]) +  /* SEND_TO_CPU = 2 (C906L) */
         offsetof(struct cpu_mbox_int, cpu_mbox_int_clr)));

    /* Enable the slot for target CPU */
    readb((void __iomem *)
        ((unsigned long)mbox->regs +
         offsetof(struct mailbox_set_register, cpu_mbox_en[REMOTE_CPU_ID])));
    writeb(readb((void __iomem *)
        ((unsigned long)mbox->regs +
         offsetof(struct mailbox_set_register, cpu_mbox_en[REMOTE_CPU_ID]))) | (1 << idx),
        (void __iomem *)
        ((unsigned long)mbox->regs +
         offsetof(struct mailbox_set_register, cpu_mbox_en[REMOTE_CPU_ID]) +
         offsetof(union cpu_mailbox_info_offset, mbox_info)));

    /* Trigger mailbox interrupt on target CPU */
    writeb(1 << idx, (void __iomem *)
        ((unsigned long)mbox->regs +
         offsetof(struct mailbox_set_register, mbox_set) +
         offsetof(union mailbox_set, mbox_set)));

    spin_unlock_irqrestore(&mbox->lock, flags);
    return 0;
}

static int cvi_mbox_startup(struct mbox_chan *chan)
{
    return 0;
}

static void cvi_mbox_shutdown(struct mbox_chan *chan)
{
    /* Nothing to do */
}

static bool cvi_mbox_last_tx_done(struct mbox_chan *chan)
{
    /* Mailbox is always "done" immediately (no TX queue) */
    return true;
}

static const struct mbox_chan_ops cvi_mbox_ops = {
    .send_data    = cvi_mbox_send_data,
    .startup      = cvi_mbox_startup,
    .shutdown     = cvi_mbox_shutdown,
    .last_tx_done = cvi_mbox_last_tx_done,
};

static int cvi_mbox_probe(struct platform_device *pdev)
{
    struct cvi_mbox *mbox;
    struct resource *res;
    int i, ret;

    mbox = devm_kzalloc(&pdev->dev, sizeof(*mbox), GFP_KERNEL);
    if (!mbox)
        return -ENOMEM;

    mbox->dev = &pdev->dev;
    spin_lock_init(&mbox->lock);

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    mbox->regs = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(mbox->regs))
        return PTR_ERR(mbox->regs);

    mbox->context_base = (unsigned long)mbox->regs + MAILBOX_CONTEXT_OFFSET;

    /* Initialize channels */
    for (i = 0; i < MAILBOX_MAX_NUM; i++) {
        mbox->chans[i].idx = i;
        mbox->chans[i].mbox = mbox;
    }

    /* Register mailbox controller */
    mbox->controller.dev = &pdev->dev;
    mbox->controller.ops = &cvi_mbox_ops;
    mbox->controller.chans = devm_kzalloc(&pdev->dev,
        sizeof(*mbox->controller.chans) * MAILBOX_MAX_NUM, GFP_KERNEL);
    mbox->controller.num_chans = MAILBOX_MAX_NUM;
    mbox->controller.txdone_irq = false;
    mbox->controller.txdone_poll = true;
    mbox->controller.txpoll_period = 1;

    for (i = 0; i < MAILBOX_MAX_NUM; i++)
        mbox->controller.chans[i].con_priv = &mbox->chans[i];

    ret = devm_mbox_controller_register(&pdev->dev, &mbox->controller);
    if (ret) {
        dev_err(&pdev->dev, "Failed to register mailbox controller\n");
        return ret;
    }

    /* Register interrupt handler */
    mbox->irq = platform_get_irq(pdev, 0);
    if (mbox->irq < 0) {
        dev_err(&pdev->dev, "Failed to get IRQ\n");
        return mbox->irq;
    }

    ret = devm_request_threaded_irq(&pdev->dev, mbox->irq,
                                    cvi_mbox_isr_hard, cvi_mbox_isr_thread,
                                    0, dev_name(&pdev->dev), mbox);
    if (ret) {
        dev_err(&pdev->dev, "Failed to register IRQ handler\n");
        return ret;
    }

    /* Enable mailbox interrupts for our CPU (CPU0) */
    /*writeb(0x00, (void __iomem *)
        ((unsigned long)mbox->regs +
         offsetof(struct mailbox_set_register, cpu_mbox_set[0]) +
         offsetof(struct cpu_mbox_int, cpu_mbox_int_mask)));
    */

    platform_set_drvdata(pdev, mbox);

    /* /dev/mailbox: userspace hw spinlock ioctl interface. */
    mbox->misc.minor = MISC_DYNAMIC_MINOR;
    mbox->misc.name = "mailbox";
    mbox->misc.fops = &cvi_mailbox_fops;
    mbox->misc.parent = &pdev->dev;
    ret = misc_register(&mbox->misc);
    if (ret) {
        dev_err(&pdev->dev, "Failed to register /dev/mailbox misc device\n");
        return ret;
    }

    dev_info(&pdev->dev, "Cvitek mailbox controller registered\n");

    return 0;
}

static int cvi_mbox_remove(struct platform_device *pdev)
{
    struct cvi_mbox *mbox = platform_get_drvdata(pdev);

    if (mbox)
        misc_deregister(&mbox->misc);
    /* devm resources are auto-released */
    return 0;
}

static const struct of_device_id cvi_mbox_match[] = {
    { .compatible = "cvitek,cv184x-mailbox" },
    { .compatible = "cvitek,cv190x-mailbox" },
    {},
};
MODULE_DEVICE_TABLE(of, cvi_mbox_match);

static struct platform_driver cvi_mbox_driver = {
    .probe  = cvi_mbox_probe,
    .remove = cvi_mbox_remove,
    .driver = {
        .name = "cvitek-mailbox",
        .of_match_table = cvi_mbox_match,
    },
};
module_platform_driver(cvi_mbox_driver);

MODULE_DESCRIPTION("Cvitek Mailbox Controller Driver");
MODULE_LICENSE("GPL");
