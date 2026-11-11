dtb-$(CONFIG_ARCH_CVITEK) += $(foreach dts,$(patsubst %.dts,%.dtb,$(wildcard ${srctree}/${src}/*.dts)),$(notdir ${dts}))

always          := $(dtb-y)
subdir-y        := $(dts-dirs)
clean-files     := *.dtb

DTC_FLAGS += -p 0x1000
