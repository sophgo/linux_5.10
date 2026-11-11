#ccflags-y += -I$(PWD)/../include/common/uapi/ -I$(PWD)/../include/common/kapi/
ccflags-y += -DCONFIG_LOG=1
ccflags-y += -I$(srctree)/$(src)/../osal/include/ \
             -I$(srctree)/$(src)/../osal/include/module/ \
             -I$(srctree)/$(src)/../../../../../osdrv/interdrv/include/common/kapi/ \
             -I$(srctree)/$(src)/../../../../../osdrv/interdrv/include/chip/$(CHIP_ARCH_L)/uapi/ \
             -I$(srctree)/$(src)/../../../../../osdrv/interdrv/include/common/uapi/ \
             -I$(srctree)/$(src)/../../../../../osdrv/interdrv/include/chip/$(CHIP_ARCH_L)/kapi/ \
             -I$(srctree)/$(src)/../../../../drivers/staging/android/ \
             -I$(srctree)/$(src)/../../../../../build/media/include/internal/osdrv_uapi/ \
             -I$(srctree)/$(src)/../../../../../build/media/include/release/ \
             -I$(srctree)/$(src)/../../../../../build/media/include/internal/comm/

$(CHIP_SEGMENT)_base-objs += driver/base.o \
                driver/base_common.o \
                driver/bind.o \
                driver/callback.o \
                driver/vb.o \
                driver/vbq.o \
                driver/sys_proc.o \
                driver/vb_proc.o \
                driver/base_ctrl.o

ccflags-y += -I$(srctree)/$(src)/driver/

obj-$(CONFIG_CVI_BASE) += ${CHIP_SEGMENT}_base.o
