/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2024, Cvitek Co. Ltd. All rights reserved.
 *
 * Userspace ioctl interface for /dev/mailbox (cvitek mailbox driver).
 *
 * Exposes the sys_spinlock hardware locks (16 locks at mailbox + 0xc0) so
 * userspace can acquire/release a cross-core hw spinlock by id.
 *
 * Lock owner value is fixed per CPU inside the driver (Linux CPU0); userspace
 * only selects the lock id (0..15).  The AliOS peer must use the matching
 * hw spinlock with its own distinct owner value.
 */
#ifndef _UAPI_LINUX_CVI_MAILBOX_H
#define _UAPI_LINUX_CVI_MAILBOX_H

#include <asm/ioctl.h>


#define HW_SPIN_LOCK_NUM 16

#define CVI_MAILBOX_IOCTL_MAGIC		'm'

/*
 * Acquire hardware spinlock @arg (lock id, 0..15).  Blocks (busy-spinning in
 * the kernel, bounded ~1s) until acquired or timeout.
 * Returns 0 on success, -ETIMEDOUT on timeout, -EINVAL on bad id.
 */
#define CVI_MAILBOX_IOC_HW_SPIN_LOCK	_IOW(CVI_MAILBOX_IOCTL_MAGIC, 1, unsigned char)

/*
 * Release hardware spinlock @arg (lock id).  No-op if not held by us.
 * Returns 0.
 */
#define CVI_MAILBOX_IOC_HW_SPIN_UNLOCK	_IOW(CVI_MAILBOX_IOCTL_MAGIC, 2, unsigned char)

#endif /* _UAPI_LINUX_CVI_MAILBOX_H */
