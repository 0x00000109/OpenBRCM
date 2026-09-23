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
		   src/ob_channel.o src/ob_rate.o

else

KDIR	?= /lib/modules/$(shell uname -r)/build
PWD	:= $(shell pwd)

.PHONY: all modules clean install kunit hosttest

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(MAKE) -C tests/host clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

# Host-side unit tests for the pure math (no kernel needed).
hosttest:
	$(MAKE) -C tests/host run

endif
