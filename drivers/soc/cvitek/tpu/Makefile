ccflags-y +=	-I$(srctree)/$(src)/../include/uapi \
				-I$(srctree)/$(src)/common \
				-I$(srctree)/$(src)/hal/$(CHIP_SEGMENT) \
				-I$(srctree)/drivers/tee \
				-I$(srctree)/arch/arm64/include \
				-I$(srctree)/drivers/staging/android/ion \
				-I$(srctree)/$(src)/../include/common/uapi/linux

obj-$(CONFIG_CV181X_TPU) += $(CHIP_SEGMENT)_tpu.o
$(CHIP_SEGMENT)_tpu-y += common/cvi_tpu_interface.o
$(CHIP_SEGMENT)_tpu-y += hal/$(CHIP_SEGMENT)/tpu_platform.o
$(CHIP_SEGMENT)_tpu-y += hal/$(CHIP_SEGMENT)/tpu_pmu.o
