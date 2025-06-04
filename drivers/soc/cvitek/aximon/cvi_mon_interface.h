/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __CVI_MON_INTERFACE_H__
#define __CVI_MON_INTERFACE_H__

#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/dma-direction.h>

struct cvi_mon_work {
	struct task_struct *work_thread;
	wait_queue_head_t task_wait_queue;
	wait_queue_head_t done_wait_queue;
	struct list_head task_list;
	//Spinlock definition
	spinlock_t task_list_lock;
	struct list_head done_list;
	//Spinlock definition
	spinlock_t done_list_lock;
};

struct cvi_mon_device {
	struct device *dev;
	dev_t cdev_id;
	struct cdev cdev;
	struct completion aximon_completion;
	u8 __iomem *pcmon_vaddr;
	u8 __iomem *ddr_ctrl_vaddr;		//0x08004000
	u8 __iomem *ddr_phyd_vaddr;		//0x08006000
	u8 __iomem *ddr_aximon_vaddr;	//0x08008000
	u8 __iomem *ddr_top_vaddr;			//0x0800A000
	int aximon_irq;
	//Mutex definition
	struct mutex dev_lock;
	//Spinlock definition
	spinlock_t close_lock;
	int use_count;
	void *private_data;
	struct cvi_mon_work mon_work;
};

#endif

