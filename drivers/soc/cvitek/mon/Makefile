
ccflags-y += -I$(srctree)/$(src)/common -I$(srctree)/$(src)/hal/$(CHIP_SEGMENT)

obj-$(CONFIG_CVI_MON) += ${CHIP_SEGMENT}_mon.o

${CHIP_SEGMENT}_mon-y += common/cvi_mon_interface.o \
			hal/$(CHIP_SEGMENT)/mon_platform.o 
