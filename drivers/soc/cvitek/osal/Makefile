ccflags-y += -I$(srctree)/$(src)/include/ \
             -I$(srctree)/$(src)/include/module/


osal-obj := src/linux/kernel/osal_addr.o \
            src/linux/kernel/osal_atomic.o \
            src/linux/kernel/osal_barrier.o \
            src/linux/kernel/osal_bitmap.o \
            src/linux/kernel/osal_cache.o \
            src/linux/kernel/osal_completion.o \
            src/linux/kernel/osal_debug.o \
            src/linux/kernel/osal_delayedwork.o \
            src/linux/kernel/osal_fileops.o \
            src/linux/kernel/osal_hash.o \
            src/linux/kernel/osal_init.o \
            src/linux/kernel/osal_interrupt.o \
            src/linux/kernel/osal_math.o \
            src/linux/kernel/osal_mutex.o \
            src/linux/kernel/osal_platform.o \
            src/linux/kernel/osal_proc.o \
            src/linux/kernel/osal_rtc.o \
            src/linux/kernel/osal_rwlock.o \
            src/linux/kernel/osal_semaphore.o \
            src/linux/kernel/osal_spinlock.o \
            src/linux/kernel/osal_string.o \
            src/linux/kernel/osal_task.o \
            src/linux/kernel/osal_timer.o \
            src/linux/kernel/osal_wait.o \
            src/linux/kernel/osal_workqueue.o


${CHIP_SEGMENT}_osal-objs := $(osal-obj)

obj-$(CONFIG_CVI_OSAL) += ${CHIP_SEGMENT}_osal.o