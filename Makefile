# SPDX-License-Identifier: GPL-2.0-only
#
# OpenBRCM — build glue.
#   out-of-tree:  make
#   DKMS:         see dkms.conf
#   in-tree:      use Kconfig + the objs below
#
ifneq ($(KERNELRELEASE),)

obj-m		:= openbrcm.o
openbrcm-y	:= src/ob_main.o src/ob_core.o src/ob_si.o \
		   src/ob_dma.o src/ob_irq.o src/ob_rx.o src/ob_d3a0.o \
		   src/ob_fw.o src/ob_ucode.o src/ob_initvals.o \
		   src/ob_channel.o src/ob_rate.o src/ob_mac80211.o

else

KDIR	?= /lib/modules/$(shell uname -r)/build
PWD	:= $(shell pwd)

.PHONY: all modules clean install kunit hosttest signed sign

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(MAKE) -C tests/host clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

# Build, then sign the final openbrcm.ko as the last artifact-producing step.
# Do not rebuild afterwards unless you sign again.
signed: modules
	./scripts/sign.sh

sign: signed

# Host-side unit tests for the pure math (no kernel needed).
hosttest:
	$(MAKE) -C tests/host run

endif
