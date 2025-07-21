#ccflags-y += -I$(PWD)/../include/common/uapi/ -I$(PWD)/../include/common/kapi/
ccflags-y += -I$(PWD)/../osal/include/ \
             -I$(PWD)/../osal/include/module/ \
             -I$(PWD)/../osdrv/interdrv/include/common/kapi/ \
             -I$(PWD)/../osdrv/interdrv/include/chip/cv184x/uapi/ \
             -I$(PWD)/../osdrv/interdrv/include/common/uapi/ \
             -I$(PWD)/../osdrv/interdrv/tde/platform/linux/ \
             -I$(PWD)/../osdrv/interdrv/tde/chip/cv184x/ \
             -I$(PWD)/../osdrv/interdrv/tde/driver/ \
             -I$(PWD)/../osdrv/interdrv/include/chip/cv184x/kapi/

$(CHIP_ARCH_L)_tde-objs += driver/tde_core.o
$(CHIP_ARCH_L)_tde-objs += driver/tde_sdk_layer.o
$(CHIP_ARCH_L)_tde-objs += platform/linux/tde_dev.o
$(CHIP_ARCH_L)_tde-objs += platform/linux/tde_ioctl.o
$(CHIP_ARCH_L)_tde-objs += platform/linux/tde_proc.o
$(CHIP_ARCH_L)_tde-objs += chip/$(CHIP_ARCH_L)/tde_reg.o
$(CHIP_ARCH_L)_tde-objs += chip/$(CHIP_ARCH_L)/tde_ip_ctrl.o


obj-$(CONFIG_CVI_TDE) += ${CHIP_SEGMENT}_tde.o
#obj-$(CONFIG_CV181X_SYS) += cv181x_sys.o
#cv181x_sys-y += common/sys.o \
                common/sys_context.o