obj-$(CONFIG_CV181X_IVE) += $(CHIP_SEGMENT)_ive.o
$(CHIP_SEGMENT)_ive-y += common/cvi_ive_interface.o \
						hal/$(CHIP_SEGMENT)/cvi_reg.o \
						hal/$(CHIP_SEGMENT)/cvi_ive_platform.o

ccflags-y += -I$(srctree)/$(src)/../include/common/uapi \
			 -I$(srctree)/$(src)/../sys/common/uapi \
			 -I$(srctree)/$(src)/common \
			 -I$(srctree)/$(src)/hal/$(CHIP_SEGMENT) \
			 -I$(srctree)/drivers/staging/android \
			 -I$(srctree)/$(src)/../base/ \
			 -I$(srctree)/$(src)/../base/chip/$(CHIP_SEGMENT)
