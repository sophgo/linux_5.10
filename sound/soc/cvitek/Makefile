# Cvitek Platform Support
obj-$(CONFIG_ARCH_CV84X6) += cv84x6_i2s.o
obj-$(CONFIG_ARCH_CV186X) += cv1835_i2s.o

# Cvitek Machine Support
snd-soc-cv1835-adau1372-objs := cv1835_adau1372.o

obj-$(CONFIG_SND_SOC_CV1835_ADAU1372) += snd-soc-cv1835-adau1372.o

obj-$(CONFIG_SND_SOC_CV1835_CV1835PDM) += cv1835_cv1835pdm.o

obj-$(CONFIG_SND_SOC_CV182X_CV182XPDM) += cv182x_cv182xpdm.o

obj-$(CONFIG_SND_SOC_CV1835PDM) += cv1835pdm.o

#cv1835
obj-$(CONFIG_SND_SOC_CV1835_CV1835ADC) += cv1835_cv1835adc.o
obj-$(CONFIG_SND_SOC_CV1835ADC) += cv1835adc.o
obj-$(CONFIG_SND_SOC_CV1835_CV1835DAC) += cv1835_cv1835dac.o
obj-$(CONFIG_SND_SOC_CV1835DAC) += cv1835dac.o


#cv182x
obj-$(CONFIG_SND_SOC_CV182XADC) += cv182xadc.o
obj-$(CONFIG_SND_SOC_CV182X_CV182XADC) += cv182x_cv182xadc.o
obj-$(CONFIG_SND_SOC_CV182XDAC) += cv182xdac.o
obj-$(CONFIG_SND_SOC_CV182X_CV182XDAC) += cv182x_cv182xdac.o


#cv181x
obj-$(CONFIG_SND_SOC_CV182XAADC) += cv181xadc.o
obj-$(CONFIG_SND_SOC_CV182XA_CV182XAADC) += cv181x_cv181xadc.o
obj-$(CONFIG_SND_SOC_CV182XADAC) += cv181xdac.o
obj-$(CONFIG_SND_SOC_CV182XA_CV182XADAC) += cv181x_cv181xdac.o

#cv186x
obj-$(CONFIG_SND_SOC_CV186XADC) += cvitek_adc.o
obj-$(CONFIG_SND_SOC_CV186X_CV186XADC) += cv186x_cv186xadc.o
obj-$(CONFIG_SND_SOC_CV186XDAC) += cvitek_dac.o
obj-$(CONFIG_SND_SOC_CV186X_CV186XDAC) += cv186x_cv186xdac.o

#cv186x -dwi2s
obj-$(CONFIG_SND_SOC_CV186X_DWI2S) += cvitek_dw_i2s.o
obj-$(CONFIG_SND_SOC_CV186X_DWU2S_ADAU1372) += cv186x_adau1372.o

#other
obj-$(CONFIG_SND_SOC_CV183x_DUMMY_CARD) += cv1835_dummy_codec.o

obj-$(CONFIG_ARCH_CV84X6) += cv84x6_dummy_codec.o
obj-$(CONFIG_ARCH_CV186X) += dummy_codec.o

# Cvitek audio subsys support
obj-$(CONFIG_ARCH_CV84X6) += cv84x6_i2s_subsys.o
obj-$(CONFIG_ARCH_CV186X) += cv1835_i2s_subsys.o

obj-$(CONFIG_SND_SOC_CV1835_LT9611) += cv1835_lt9611.o

#cvitek hdmi i2s
obj-$(CONFIG_SND_SOC_CVITEK_HDMI_I2S) += soph_dw_hdmi_i2s.o
soph_dw_hdmi_i2s-y := cvitek_dw_hdmi_i2s.o
obj-$(CONFIG_SND_SOC_CVITEK_HDMI_CARD) += soph_hdmi_card.o
soph_hdmi_card-y := cv84x6_hdmi_card.o
