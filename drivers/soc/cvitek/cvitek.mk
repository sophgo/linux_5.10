#ccflags-y += -save-temps=obj

ifeq ($(CONFIG_BUILD_FOR_DEBUG), y)
ccflags-y += -DDRV_DEBUG -DDRV_TEST
endif

ifeq ("$(CHIP_SEGMENT)", "cv181x")
ccflags-y += -D__CV181X__
endif
ifeq ("$(CHIP_SEGMENT)", "cv180x")
ccflags-y += -D__CV180X__
endif
ifeq ("$(CHIP_SEGMENT)", "cv184x")
ccflags-y += -D__CV184X__
endif
