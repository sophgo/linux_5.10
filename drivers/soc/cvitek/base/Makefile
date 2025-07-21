#ccflags-y += -I$(PWD)/../include/common/uapi/ -I$(PWD)/../include/common/kapi/
ccflags-y += -I$(PWD)/../osal/include/ \
             -I$(PWD)/../osal/include/module/ \
             -I$(PWD)/../osdrv/interdrv/include/common/kapi/ \
             -I$(PWD)/../osdrv/interdrv/include/chip/cv184x/uapi/ \
             -I$(PWD)/../osdrv/interdrv/include/common/uapi/ \
             -I$(PWD)/../osdrv/interdrv/base/platform/linux/ \
             -I$(PWD)/../osdrv/interdrv/base/platform/linux/proc/ \
             -I$(PWD)/../osdrv/interdrv/include/chip/cv184x/kapi/ \
             -I$(PWD)/../linux_5.10/drivers/staging/android/ \
             -I$(PWD)/../build/media/include/internal/osdrv_uapi/ \
             -I$(PWD)/../build/media/include/release/ \
             -I$(PWD)/../build/media/include/internal/comm/ \

$(CHIP_SEGMENT)_base-objs += driver/base_common.o \
                driver/bind.o \
                driver/callback.o \
                driver/vb.o \
                driver/vbq.o

$(CHIP_SEGMENT)_base-objs += platform/linux/base.o \
                platform/linux/ion.o \
                platform/linux/reg.o \
                platform/linux/proc/log_proc.o \
                platform/linux/proc/sys_proc.o \
                platform/linux/proc/vb_proc.o

ccflags-y += -I$(PWD)/platform/linux
ccflags-y += -I$(PWD)/platform/linux/proc

ccflags-y += -I$(PWD)/driver/

obj-$(CONFIG_CVI_SYS) += ${CHIP_SEGMENT}_base.o
#obj-$(CONFIG_CV181X_SYS) += cv181x_sys.o
#cv181x_sys-y += common/sys.o \
                common/sys_context.o
