ccflags-y += -I$(srctree)/$(src)/driver/include/ \
             -I$(srctree)/$(src)/driver/include/linux/ \
             -I$(srctree)/$(src)/driver/src/ \
             -I$(srctree)/$(src)/platform/linux/include/ \
             -I$(PWD)/../osdrv/interdrv/include/common/uapi/ \
             -I$(PWD)/../osdrv/interdrv/ipcm/driver/include/ \
             -I$(srctree)/$(src)/../../../../scripts/dtc/include-prefixes


cvi_ipcm-y := driver/src/ipcm_common.o \
	driver/src/ipcm_pool.o \
	driver/src/ipcm.o \
	driver/src/msg_queue.o \
	driver/src/mailbox.o \
	driver/src/cvi_spinlock.o \
	driver/src/ipcm_dbg_rec_info.o \
	driver/src/ipcm_port.o \
	driver/src/ipcm_message.o \
	driver/src/ipcm_custom.o

cvi_ipcm-y += platform/linux/src/ipcm_dev.o \
	platform/linux/src/ipcm_dev_port.o \
	platform/linux/src/ipcm_plat_adapter.o \
	platform/linux/src/ipcm_custom_linux.o \
	platform/linux/src/ipcm_system.o

cvi_ipcm-y += platform/linux/src/ipcmsg.o \
	platform/linux/src/ipcmsg_dev.o


${CHIP_SEGMENT}_ipcm-objs := $(cvi_ipcm-y)

obj-$(CONFIG_CVI_IPCM) += ${CHIP_SEGMENT}_ipcm.o