#ccflags-y += -I$(PWD)/../include/common/uapi/ -I$(PWD)/../include/common/kapi/
ccflags-y += -I$(PWD)/../osal/include/ \
             -I$(PWD)/../osal/include/module/ \
             -I$(PWD)/../osdrv/interdrv/include/common/kapi/ \
             -I$(PWD)/../osdrv/interdrv/include/chip/cv184x/uapi/ \
             -I$(PWD)/../osdrv/interdrv/include/common/uapi/ \
             -I$(PWD)/../osdrv/interdrv/sys/platform/linux/ \
             -I$(PWD)/../osdrv/interdrv/sys/chip/cv184x/ \
             -I$(PWD)/../osdrv/interdrv/include/chip/cv184x/kapi/ \
             -I$(PWD)/../build/media/include/internal/mpi_uapi/ \
             -I$(PWD)/../build/media/include/internal/comm/

$(CHIP_ARCH_L)_sys-objs += chip/$(CHIP_ARCH_L)/sys_common.o
$(CHIP_ARCH_L)_sys-objs += chip/$(CHIP_ARCH_L)/vi_sys.o
$(CHIP_ARCH_L)_sys-objs += chip/$(CHIP_ARCH_L)/cmdq.o
$(CHIP_ARCH_L)_sys-objs += platform/linux/sys.o

obj-$(CONFIG_CVI_SYS) += ${CHIP_SEGMENT}_sys.o
#obj-$(CONFIG_CV181X_SYS) += cv181x_sys.o
#cv181x_sys-y += common/sys.o \
                common/sys_context.o