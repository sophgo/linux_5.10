
obj-$(CONFIG_CV181X_IPCM) := cvi_ipcm.o
cvi_ipcm-y := core/ipcm_common.o \
	core/ipcm_pool.o \
	core/ipcm.o \
	core/msg_queue.o \
    core/mailbox/mailbox.o \
    core/mailbox/cvi_spinlock.o \
    sysdeps/linux/cvi_sys.o \
    plat/plat_common/ipcm_port_common.o \
    plat/linux/driver/ipcm_dev.o \
    plat/linux/linux_common/ipcm_drv_port_common.o \
    plat/linux/message/ipcmsg_dev.o \
    plat/linux/message/ipcmsg.o \
    plat/linux/linux_common/ipcm_drv_port_common.o \
    plat/linux/message/ipcm_message_ker.o \
    plat/linux/anonymous/ipcm_port_anon.o
    # plat/linux/system/ipcm_port_sys.o

ccflags-y += -D__LINUX_DRV__ -DIPCM_SHIRNK
ccflags-y += -I$(srctree)/$(src)/core \
			-I$(srctree)/$(src)/core/mailbox \
			-I$(srctree)/$(src)/plat/include \
			-I$(srctree)/$(src)/plat/linux/include \
			-I$(srctree)/$(src)/plat/linux/message \
			-I$(srctree)/$(src)/plat/common \
			-I$(srctree)/$(src)/sysdeps/linux \
			-I$(srctree)/$(src)/../include/common/uapi
