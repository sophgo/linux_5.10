#ccflags-y += -I$(PWD)/../include/common/uapi/ -I$(PWD)/../include/common/kapi/
ccflags-y +=  \
             -I$(srctree)/$(src)/common/uapi \
             -I$(srctree)/$(src)/common/kapi \
             -I$(srctree)/$(src)/common \
			 -I$(srctree)/$(src)/../include/common/uapi \
			 -I$(srctree)/drivers/staging/android

obj-$(CONFIG_CV181X_SYS) += ${CHIP_SEGMENT}_sys.o
#obj-$(CONFIG_CV181X_SYS) += cv181x_sys.o
#cv181x_sys-y += common/sys.o \
                common/sys_context.o
${CHIP_SEGMENT}_sys-y += common/sys.o \
                common/sys_context.o
