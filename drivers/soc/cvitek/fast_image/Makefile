
ccflags-y += -I$(srctree)/$(src)/../rtos_cmdqu/
ccflags-y += -I$(srctree)/drivers/staging/android/

ccflags-y += -I$(srctree)/$(src)/../include/common/uapi/
ccflags-y += -I$(srctree)/$(src)/../include/
ccflags-y += -I$(srctree)/$(src)/../base/
ccflags-y += -I$(srctree)/$(src)/../base/chip/$(CHIP_SEGMENT)

ccflags-y += -I$(srctree)/$(src)/../sys/common/ -I$(srctree)/$(src)/../sys/common/uapi

obj-$(CONFIG_CVI_FAST_IMAGE) += cvi_fast_image.o

cvi_fast_image-y += fast_image.o 

