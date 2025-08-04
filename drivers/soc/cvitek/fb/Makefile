ccflags-y +=  \
             -I$(PWD)/common \
             -I$(PWD)/../include/common/uapi \
             -I$(srctree)/drivers/staging/android

obj-$(CONFIG_CV181X_FB) += cvi_fb.o
cvi_fb-y += common/cvifb.o \
            common/scaler.o \
            common/reg.o