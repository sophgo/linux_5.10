#ccflags-y += -I$(PWD)/../include/common/uapi/ -I$(PWD)/../include/common/kapi/
ccflags-y += -DCONFIG_LOG=1
ccflags-y += -I$(srctree)/$(src)/../osal/include/ \
             -I$(srctree)/$(src)/../osal/include/module/ \
             -I$(srctree)/$(src)/../../../../../osdrv/interdrv/include/common/kapi/ \
             -I$(srctree)/$(src)/../../../../../osdrv/interdrv/include/chip/$(CHIP_ARCH_L)/uapi/ \
             -I$(srctree)/$(src)/../../../../../osdrv/interdrv/include/common/uapi/ \
             -I$(srctree)/$(src)/../../../../../osdrv/interdrv/sys/driver/ \
             -I$(srctree)/$(src)/../../../../../osdrv/interdrv/include/chip/$(CHIP_ARCH_L)/kapi/ \
             -I$(srctree)/$(src)/../../../../../build/media/include/internal/mpi_uapi/ \
             -I$(srctree)/$(src)/../../../../../build/media/include/internal/comm/

$(CHIP_ARCH_L)_sys-objs += driver/sys.o \
                driver/sys_ctrl.o
$(CHIP_ARCH_L)_sys-objs += chip/$(CHIP_ARCH_L)/sys_common.o \
                chip/$(CHIP_ARCH_L)/vi_sys.o \
                chip/$(CHIP_ARCH_L)/cmdq.o

ccflags-y += -I$(srctree)/$(src)/driver/
ccflags-y += -I$(srctree)/$(src)/chip/$(CHIP_ARCH_L)/

obj-$(CONFIG_CVI_SYS) += ${CHIP_SEGMENT}_sys.o
