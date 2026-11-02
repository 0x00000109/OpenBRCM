# SPDX-License-Identifier: GPL-2.0-only
#
# OpenBRCM — build glue.
#   out-of-tree:  make
#   DKMS:         see dkms.conf
#   in-tree:      use Kconfig + the objs below
#
ifneq ($(KERNELRELEASE),)

obj-m		:= openbrcm.o
openbrcm-y	:= src/ob_main.o src/chip/ob_profile.o \
		   src/chip/ob_profile_bcm4352.o \
		   src/chip/ob_profile_bcm4360.o src/ob_core.o \
		   src/ob_guard.o src/ob_si.o \
		   src/ob_dma.o src/ob_dma_owner.o src/ob_dma_fatal.o \
		   src/ob_irq.o src/ob_rx.o \
		   src/ob_rx_d11_rev42.o \
		   src/ob_tx_sw.o src/ob_tx_path.o src/ob_rx_sw.o src/ob_dpc_sw.o src/ob_d3a0.o \
		   src/ob_d3a1.o src/ob_d3b.o src/ob_radio.o src/ob_diag.o \
		   src/ob_device_loss_trace.o \
		   src/ob_fw.o src/ob_ucode.o src/ob_initvals.o \
		   src/ob_fw_chain.o \
		   src/ob_lifecycle.o src/ob_phy.o src/ob_phy_lpf.o \
		   src/ob_phy_tripoint.o \
		   src/ob_acphy_cal_contract.o src/ob_acphy_cal_run.o \
		   src/ob_acphy_cal_stage.o src/ob_acphy_cal_write_stage.o \
		   src/ob_acphy_cal_w2_replay.o \
		   src/ob_phy_post_lpf.o src/ob_phy_96203.o \
		   src/ob_phy_a0be4.o \
		   src/ob_phy_a7089_cal.o \
		   src/ob_phy_sub92500.o \
		   src/ob_channel.o src/ob_rate.o src/ob_rx_mac80211.o \
		   src/bcm43xx_chan_tuning.o src/bcm43xx_chan_tuning_data.o \
		   src/ob_rx_status.o src/ob_tx_status.o \
		   src/ob_tx_status_decode.o src/ob_mac80211.o

else

KDIR	?= /lib/modules/$(shell uname -r)/build
PWD	:= $(shell pwd)

.PHONY: all modules clean install kunit hosttest signed sign step-ledger-test kb kb-validate

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

step-ledger-test:
	cargo test --manifest-path tools/step-ledger/Cargo.toml

# Rebuild and validate the consolidated knowledge base under docs/data/.
# Derived, provenance-linked view over docs/artifact-ledger.json and the rest
# of docs/; never a second registry. See docs/data/README.md.
kb:
	sh scripts/kb/build_all.sh

kb-validate:
	python3 scripts/kb/validate.py

# Refresh the read-only stand snapshot (network; read-only controller only).
kb-refresh-stand:
	python3 scripts/kb/stand_check.py --refresh

endif
