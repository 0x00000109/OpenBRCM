# SPDX-License-Identifier: GPL-2.0-only
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -I. -I../../src
SRC     := ../../src
export PYTHONPATH := ../..:$(PYTHONPATH)

.PHONY: run clean
run: ob_math_test ob_channel_fixture_test ob_radio_2069_lookup_test ob_caps_test ob_dma_test ob_dma_owner_test ob_dma_fatal_test ob_irq_test ob_rx_test \
	     ob_rx_d11_rev42_test ob_rx_lifecycle_test ob_d11_irq_abi_txstatus_test \
	     ob_tx_sw_test ob_tx_path_test ob_d11_tx_frameid_test ob_rx_sw_test ob_dpc_sw_test ob_datapath_integration_test \
     ob_rx_mac80211_test ob_rx_integration_test ob_tx_status_test ob_rx_status_test ob_rx_fcs_contract_test \
     ob_rx_length_contract_test ob_tx_status_decode_test ob_mac80211_lifecycle_test \
	     ob_chip_profile_test ob_profile_evidence_test ob_admission_policy_test \
	     ob_acphy_start_test ob_acphy_preradio_mode_test ob_acphy_cal_contract_test \
     ob_acphy_cal_tempdelta_test ob_acphy_first_up_gate_test ob_acphy_core_mirror_test \
     ob_phy_tripoint_test \
     ob_acphy_cal_run_test ob_acphy_cal_stage_test ob_acphy_cal_write_stage_test \
     ob_acphy_cal_w2_replay_test \
	     ob_radio_local_0413_test ob_radio_post0413_test \
	     ob_radio_postphase1_test ob_radio_postphase2_test ob_si_powerup_test \
	     ob_phy_postbmac_test ob_otp8e2_test \
	     ob_fw_test ob_ucode_test ob_ucode_core_state_test ob_ucode_d2a_test ob_ucore_prep_test ob_initvals_test ob_fw_chain_test ob_fw_chain_failure_test ob_fw_chain_mode_test ob_d3a0_test ob_d3a1_test \
	     ob_d3a1_fifo530_test \
     ob_d3b_test ob_d11_tail_state_test ob_d11_prefix_admission_test ob_d11_lifecycle_states_test ob_guard_test ob_cflags_test ob_acphy_attach_arm_test ob_radio_test ob_phy_test ob_diag_test \
     ob_ai_wrapper_only_test ob_phase37a_admission_test \
	     ob_phase37b_enable_test \
     ob_device_loss_trace_test ob_lifecycle_test ob_runtime_suspend_test \
	     ob_fabric_baseline_test ob_sprom_access_test ob_phy_post_lpf_test \
	     ob_phy_a0be4_test ob_phy_a6b0f_test ob_phy_9e378_test ob_phy_9eaf9_test \
	     ob_phy_a7089_cal_test \
	     ob_phy_farrow_crs_test ob_phy_post_farrow_continuation_test \
	     ob_phy_99658_test ob_phy_9af05_loop_test ob_phy_9a121_test \
	     ob_phy_9a121_active_test ob_phy_9a539_test ob_phy_96203_test \
	     ob_phy_txpwr_index_test ob_phy_a7089_txpwr_test ob_phy_sub92500_test


	./ob_math_test
	./ob_chip_profile_test
	./ob_profile_evidence_test
	./ob_admission_policy_test
	./ob_channel_fixture_test
	./ob_radio_2069_lookup_test
	./ob_caps_test
	./ob_dma_test
	./ob_dma_owner_test
	./ob_dma_fatal_test
	./ob_irq_test
	./ob_rx_test
	./ob_rx_d11_rev42_test
	./ob_rx_lifecycle_test
	python3 test_rx_dma_quarantine.py
	./ob_d11_irq_abi_txstatus_test
	./ob_tx_sw_test
	./ob_tx_path_test
	./ob_d11_tx_frameid_test
	./ob_rx_sw_test
	./ob_dpc_sw_test
	./ob_datapath_integration_test
	./ob_rx_mac80211_test
	./ob_rx_integration_test
	./ob_tx_status_test
	./ob_rx_status_test
	./ob_rx_fcs_contract_test
	./ob_rx_length_contract_test
	./ob_tx_status_decode_test
	./ob_mac80211_lifecycle_test
	./ob_acphy_start_test
	./ob_acphy_preradio_mode_test
	./ob_acphy_cal_contract_test
	./ob_acphy_cal_tempdelta_test
	./ob_acphy_first_up_gate_test
	./ob_acphy_core_mirror_test
	./ob_phy_tripoint_test
	./ob_acphy_cal_run_test
	./ob_acphy_cal_stage_test
	./ob_acphy_cal_write_stage_test
	./ob_acphy_cal_w2_replay_test
	./ob_radio_local_0413_test
	./ob_radio_post0413_test
	./ob_radio_postphase1_test
	./ob_radio_postphase2_test
	./ob_phy_postbmac_test
	./ob_otp8e2_test
	./ob_si_powerup_test
	./ob_fw_test
	./ob_ucode_test
	./ob_ucode_core_state_test
	./ob_ucode_d2a_test
	./ob_ucore_prep_test
	python3 test_d2a_source_contract.py
	./ob_initvals_test
	./ob_fw_chain_test
	./ob_fw_chain_failure_test
	./ob_fw_chain_mode_test
	python3 test_fw_chain_source.py
	./ob_d3a0_test
	./ob_d3a1_test
	./ob_d3a1_fifo530_test
	python3 test_d3a1_fifo530_ctx.py
	./ob_d3b_test
	python3 test_d3b_premhf_safe_read.py
	python3 test_d3b_shm_override_width.py
	python3 test_d3b_boundary_witness.py
	./ob_d11_tail_state_test
	./ob_d11_prefix_admission_test
	./ob_d11_lifecycle_states_test
	./ob_guard_test
	./ob_cflags_test
	./ob_acphy_attach_arm_test
	./ob_radio_test
	./ob_phy_test
	./ob_phy_post_lpf_test
	./ob_phy_a0be4_test
	./ob_phy_a6b0f_test
	./ob_phy_9e378_test
	./ob_phy_9eaf9_test
	./ob_phy_a7089_cal_test
	./ob_phy_farrow_crs_test
	./ob_phy_post_farrow_continuation_test
	./ob_phy_99658_test
	./ob_phy_9af05_loop_test
	./ob_phy_9a121_test
	./ob_phy_9a121_active_test
	./ob_phy_9a539_test
	./ob_phy_96203_test
	./ob_phy_txpwr_index_test
	./ob_phy_a7089_txpwr_test
	./ob_phy_sub92500_test
	PYTHONPATH=../.. python3 test_bcm4352_txpwr_by_index.py
	PYTHONPATH=../.. python3 test_bcm4352_a7089_txpwr_join.py
	PYTHONPATH=../.. python3 test_bcm4352_a7089_sub995ab_tail.py
	PYTHONPATH=../.. python3 test_bcm4352_sub_96203_oracle.py
	PYTHONPATH=../.. python3 test_bcm4352_sub_9a539.py
	python3 test_bcm4352_sub_99658_payload.py
	python3 test_bcm4352_sub_9af05_loop.py
	python3 test_bcm4352_sub_9a121_runner.py
	python3 test_bcm4352_sub_9a121_active_core.py
	python3 test_bcm4352_sub_90493_continuation.py
	python3 test_bcm4352_post_farrow_downstream.py
	python3 test_bcm4352_post_farrow_composition.py
	python3 test_bcm4352_sub_9e378_variants.py
	PYTHONPATH=../.. python3 test_bcm4352_sub_9eaf9_cleanroom.py
	PYTHONPATH=../.. python3 test_bcm4352_sub_9eaf9_trace.py
	PYTHONPATH=../.. python3 test_bcm4352_post_9eaf9_caller_farrow_crs.py
	PYTHONPATH=../.. python3 test_sub_9eaf9_event_vector.py
	python3 test_bcm4352_lpf_descriptor_manifest.py
	python3 test_bcm4352_lpf_full_trace.py
	python3 test_bcm4352_post_lpf_selected_block.py
	PYTHONPATH=../.. python3 test_bcm4352_initial_a7089_a0be4_contract.py
	./ob_diag_test
	./ob_ai_wrapper_only_test
	python3 test_ai_wrapper_only_source.py
	./ob_phase37a_admission_test
	./ob_phase37b_enable_test
	python3 test_phase37a_source.py
	./ob_device_loss_trace_test
	./ob_lifecycle_test
	./ob_runtime_suspend_test
	./ob_fabric_baseline_test
	./ob_sprom_access_test
	python3 test_sprom11_decode.py
	python3 test_sprom_bcma_owned_state.py
	python3 test_srom_var_table.py
	python3 test_runtime_test_cli.py
	python3 test_stand_radio_checkpoint_run.py
	python3 test_stand_dev_sync.py
	python3 test_runtime_dependency_preflight.py
	python3 test_runtime_topology.py
	python3 test_d11_transaction_timing.py
	python3 test_d11_bcma_postcondition.py
	python3 test_d11_phyversion_boundary.py
	python3 test_acphy_identity_timing.py
	python3 test_acphy_identity_3e2_first.py
	python3 test_acphy_identity_span_probe.py
	python3 test_acphy_identity_control_reprobe.py
	python3 test_acphy_identity_control16_reprobe.py
	python3 test_acphy_identity_upper_probe.py
	python3 test_acphy_identity_edge_probe.py
	python3 test_acphy_identity_wide_probe.py
	python3 test_acphy_identity_edge2_probe.py
	python3 test_acphy_identity_lower_probe.py
	python3 test_acphy_identity_wrapper_witness.py
	python3 test_acphy_identity_pll_witness.py
	python3 test_acphy_identity_3e0_first.py
	python3 test_p1_candidates.py
	python3 test_phy_revision_provenance.py
	python3 test_bcm4352_pi164_provenance_contract.py
	python3 test_d11_timing_preflight.py
	python3 test_current_context.py
	python3 test_docs_check.py
	python3 test_re_packet.py
	python3 test_re_bootstrap.py
	python3 test_device_lost_guard.py
	python3 test_device_loss_trace.py
	python3 test_ob_diag_addressing.py
	python3 test_radio_probe_decoder.py
	python3 test_phy_radio_ops.py
	python3 test_precal_contract_generator.py
	python3 test_table_payload_relocations.py
	python3 test_post_crs_aci_runtime_channel_continuation.py
	python3 test_9eaf9_payload_contradiction.py
	cargo test --manifest-path ../../tools/step-ledger/Cargo.toml
	python3 test_step_ledger.py
	python3 test_ci_reproducibility.py
	python3 test_check_script.py
	python3 test_static_acceptance.py
	python3 test_pre_id_contract.py
	python3 test_acphy_reset_stage_order.py
	python3 test_core_mirror_stage_order.py
	python3 test_acphy_arm_mutation.py
	python3 test_chip_profile_admission.py
	python3 test_attach_identity_lifecycle.py
	python3 test_attach_identity_failure_injection.py
	python3 test_attach_runtime_markers.py
	python3 test_broadcom_source_index.py
	python3 test_broadcom_public_abi.py
	python3 test_acphy_start_contract.py
	python3 test_acphy_preradio_hardware_mode_contract.py
	python3 test_acphy_class0_radio_local_0413.py
	python3 test_acphy_post0413_phase1.py
	python3 test_acphy_postphase1_phase2.py
	python3 test_acphy_postphase2_return.py
	python3 test_acphy_preradio_firmware_preflight.py
	python3 test_otp8e2_mode_contract.py
	python3 test_ob_si_powerup_contract.py
	python3 test_ghidra_address.py

PROFILE_OBJS := $(SRC)/chip/ob_profile.c $(SRC)/chip/ob_profile_bcm4352.c \
		$(SRC)/chip/ob_profile_bcm4360.c

ob_chip_profile_test: ob_chip_profile_test.c $(PROFILE_OBJS) \
		$(SRC)/ob_profile.h $(SRC)/ob_chip_profile.h linux/types.h
	$(CC) $(CFLAGS) -o $@ ob_chip_profile_test.c $(PROFILE_OBJS)

ob_profile_evidence_test: ob_profile_evidence_test.c $(PROFILE_OBJS) \
		$(SRC)/ob_profile.h linux/types.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ \
		ob_profile_evidence_test.c $(PROFILE_OBJS)

ob_admission_policy_test: ob_admission_policy_test.c $(PROFILE_OBJS) \
		$(SRC)/ob_profile.h linux/types.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ \
		ob_admission_policy_test.c $(PROFILE_OBJS)

ob_phy_txpwr_index_test: ob_phy_txpwr_index_test.c $(SRC)/ob_phy_txpwr_index.c \
		$(SRC)/ob_phy_txpwr_index.h $(SRC)/ob_phy.h linux/types.h
	$(CC) $(CFLAGS) -o $@ ob_phy_txpwr_index_test.c $(SRC)/ob_phy_txpwr_index.c

ob_phy_a7089_txpwr_test: ob_phy_a7089_txpwr_test.c \
		$(SRC)/ob_phy_a7089_txpwr.c $(SRC)/ob_phy_96203.c \
		$(SRC)/ob_phy_txpwr_index.c $(SRC)/ob_phy_9a121.c \
		$(SRC)/ob_phy_a7089_txpwr.h \
		$(SRC)/ob_phy_96203.h $(SRC)/ob_phy_txpwr_index.h \
		$(SRC)/ob_phy_post_lpf.h $(SRC)/ob_phy.h linux/types.h
	$(CC) $(CFLAGS) -o $@ ob_phy_a7089_txpwr_test.c \
		$(SRC)/ob_phy_a7089_txpwr.c $(SRC)/ob_phy_96203.c \
		$(SRC)/ob_phy_txpwr_index.c $(SRC)/ob_phy_9a121.c

ob_phy_sub92500_test: ob_phy_sub92500_test.c $(SRC)/ob_phy_sub92500.c \
		$(SRC)/ob_phy_sub92500.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ \
		ob_phy_sub92500_test.c $(SRC)/ob_phy_sub92500.c

ob_math_test: ob_math_test.c $(SRC)/ob_channel.c $(SRC)/ob_rate.c
	$(CC) $(CFLAGS) -o $@ $^

ob_channel_fixture_test: ob_channel_fixture_test.c $(SRC)/ob_channel.c
	$(CC) $(CFLAGS) -o $@ $^

ob_radio_2069_lookup_test: ob_radio_2069_lookup_test.c $(SRC)/ob_channel.c
	$(CC) $(CFLAGS) -o $@ $^

ob_caps_test: ob_caps_test.c $(SRC)/ob_channel.c $(SRC)/ob_rate.c
	$(CC) $(CFLAGS) -o $@ $^

ob_dma_test: ob_dma_test.c $(SRC)/ob_dma.h
	$(CC) $(CFLAGS) -o $@ $<

ob_dma_owner_test: ob_dma_owner_test.c $(SRC)/ob_dma_owner.c \
		$(SRC)/ob_dma_owner.h
	$(CC) $(CFLAGS) -pthread -o $@ ob_dma_owner_test.c \
		$(SRC)/ob_dma_owner.c

ob_dma_fatal_test: ob_dma_fatal_test.c $(SRC)/ob_dma_fatal.c \
		$(SRC)/ob_dma_fatal.h $(SRC)/ob_dma_owner.h
	$(CC) $(CFLAGS) -pthread -o $@ ob_dma_fatal_test.c \
		$(SRC)/ob_dma_fatal.c

ob_irq_test: ob_irq_test.c $(SRC)/ob_irq.h
	$(CC) $(CFLAGS) -o $@ $<

ob_rx_test: ob_rx_test.c $(SRC)/ob_rx.h $(SRC)/ob_dma.h
	$(CC) $(CFLAGS) -o $@ $<

ob_rx_d11_rev42_test: ob_rx_d11_rev42_test.c $(SRC)/ob_rx_d11_rev42.c \
		$(SRC)/ob_rx_d11_rev42.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< \
		$(SRC)/ob_rx_d11_rev42.c

ob_rx_lifecycle_test: ob_rx_lifecycle_test.c $(SRC)/ob_rx_lifecycle.h
	$(CC) $(CFLAGS) -o $@ ob_rx_lifecycle_test.c

ob_d11_irq_abi_txstatus_test: ob_d11_irq_abi_txstatus_test.c \
		$(SRC)/ob_d11_irq_abi_txstatus.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $<

ob_tx_sw_test: ob_tx_sw_test.c $(SRC)/ob_tx_sw.c $(SRC)/ob_tx_sw.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< $(SRC)/ob_tx_sw.c

ob_tx_path_test: ob_tx_path_test.c $(SRC)/ob_tx_path.c $(SRC)/ob_tx_path.h \
		$(SRC)/ob_tx_sw.c $(SRC)/ob_tx_sw.h $(SRC)/ob_dma.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< \
		$(SRC)/ob_tx_path.c $(SRC)/ob_tx_sw.c

ob_d11_tx_frameid_test: ob_d11_tx_frameid_test.c $(SRC)/ob_d11_tx_frameid.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $<

ob_rx_sw_test: ob_rx_sw_test.c $(SRC)/ob_rx_sw.c $(SRC)/ob_rx_sw.h \
		$(SRC)/ob_rx_status.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< $(SRC)/ob_rx_sw.c

ob_dpc_sw_test: ob_dpc_sw_test.c $(SRC)/ob_dpc_sw.c $(SRC)/ob_dpc_sw.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< $(SRC)/ob_dpc_sw.c

ob_rx_mac80211_test: ob_rx_mac80211_test.c $(SRC)/ob_rx_mac80211.c \
		$(SRC)/ob_rx_mac80211.h $(SRC)/ob_rx_status.h \
		$(SRC)/ob_rx_sw.c $(SRC)/ob_rx_sw.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< \
		$(SRC)/ob_rx_mac80211.c $(SRC)/ob_rx_sw.c

ob_rx_integration_test: ob_rx_integration_test.c $(SRC)/ob_rx_d11_rev42.c \
		$(SRC)/ob_rx_status.c $(SRC)/ob_rx_sw.c $(SRC)/ob_rx_mac80211.c \
		$(SRC)/ob_rx_d11_rev42.h $(SRC)/ob_rx_status.h \
		$(SRC)/ob_rx_sw.h $(SRC)/ob_rx_mac80211.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< \
		$(SRC)/ob_rx_d11_rev42.c $(SRC)/ob_rx_status.c \
		$(SRC)/ob_rx_sw.c $(SRC)/ob_rx_mac80211.c

ob_tx_status_test: ob_tx_status_test.c $(SRC)/ob_tx_status.c \
		$(SRC)/ob_tx_status.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< \
		$(SRC)/ob_tx_status.c

ob_rx_status_test: ob_rx_status_test.c $(SRC)/ob_rx_status.c \
		$(SRC)/ob_rx_status.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< \
		$(SRC)/ob_rx_status.c

ob_rx_fcs_contract_test: ob_rx_fcs_contract_test.c $(SRC)/ob_rx_status.c \
		$(SRC)/ob_rx_status.h $(SRC)/ob_rx_sw.c $(SRC)/ob_rx_sw.h \
		$(SRC)/ob_rx_d11_rev42.c $(SRC)/ob_rx_d11_rev42.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< \
		$(SRC)/ob_rx_status.c $(SRC)/ob_rx_sw.c $(SRC)/ob_rx_d11_rev42.c

ob_rx_length_contract_test: ob_rx_length_contract_test.c \
		$(SRC)/ob_rx_status.c $(SRC)/ob_rx_status.h \
		$(SRC)/ob_rx_sw.c $(SRC)/ob_rx_sw.h \
		$(SRC)/ob_rx_d11_rev42.c $(SRC)/ob_rx_d11_rev42.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< \
		$(SRC)/ob_rx_status.c $(SRC)/ob_rx_sw.c $(SRC)/ob_rx_d11_rev42.c

ob_tx_status_decode_test: ob_tx_status_decode_test.c \
		$(SRC)/ob_tx_status_decode.c $(SRC)/ob_tx_status_decode.h \
		$(SRC)/ob_tx_status.c $(SRC)/ob_tx_status.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< \
		$(SRC)/ob_tx_status_decode.c $(SRC)/ob_tx_status.c

ob_datapath_integration_test: ob_datapath_integration_test.c \
		$(SRC)/ob_tx_sw.c $(SRC)/ob_rx_sw.c $(SRC)/ob_dpc_sw.c \
		$(SRC)/ob_rx_d11_rev42.c $(SRC)/ob_tx_sw.h $(SRC)/ob_rx_sw.h \
		$(SRC)/ob_rx_status.h $(SRC)/ob_dpc_sw.h \
		$(SRC)/ob_rx_d11_rev42.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $< \
		$(SRC)/ob_tx_sw.c $(SRC)/ob_rx_sw.c $(SRC)/ob_dpc_sw.c \
		$(SRC)/ob_rx_d11_rev42.c

ob_mac80211_lifecycle_test: ob_mac80211_lifecycle_test.c $(SRC)/ob_mac80211.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $<

ob_acphy_start_test: ob_acphy_start_test.c $(SRC)/ob_channel.c \
		$(SRC)/ob_channel.h $(SRC)/ob_phy.h $(SRC)/ob_irq.h
	$(CC) $(CFLAGS) -o $@ ob_acphy_start_test.c $(SRC)/ob_channel.c

ob_acphy_preradio_mode_test: ob_acphy_preradio_mode_test.c $(SRC)/ob_ucode.h \
		$(SRC)/ob_irq.h
	$(CC) $(CFLAGS) -o $@ $<

ob_acphy_cal_contract_test: ob_acphy_cal_contract_test.c \
		$(SRC)/ob_acphy_cal_contract.c $(SRC)/ob_acphy_cal_contract.h
	$(CC) $(CFLAGS) -o $@ ob_acphy_cal_contract_test.c \
		$(SRC)/ob_acphy_cal_contract.c

ob_acphy_cal_tempdelta_test: ob_acphy_cal_tempdelta_test.c \
		$(SRC)/ob_acphy_cal_contract.c $(SRC)/ob_acphy_cal_contract.h
	$(CC) $(CFLAGS) -o $@ ob_acphy_cal_tempdelta_test.c \
		$(SRC)/ob_acphy_cal_contract.c

ob_acphy_first_up_gate_test: ob_acphy_first_up_gate_test.c \
		$(SRC)/ob_phy.h $(SRC)/ob_phy_lpf.h $(SRC)/ob_phy_lpf.c \
		$(SRC)/ob_phy_lpf_payloads.h
	$(CC) $(CFLAGS) -o $@ ob_acphy_first_up_gate_test.c $(SRC)/ob_phy_lpf.c

ob_acphy_core_mirror_test: ob_acphy_core_mirror_test.c \
		$(SRC)/ob_phy.h $(SRC)/ob_phy_lpf.h $(SRC)/ob_phy_lpf.c \
		$(SRC)/ob_phy_lpf_payloads.h
	$(CC) $(CFLAGS) -o $@ ob_acphy_core_mirror_test.c $(SRC)/ob_phy_lpf.c

ob_phy_tripoint_test: ob_phy_tripoint_test.c \
		$(SRC)/ob_phy_tripoint.c $(SRC)/ob_phy_tripoint.h
	$(CC) $(CFLAGS) -o $@ ob_phy_tripoint_test.c \
		$(SRC)/ob_phy_tripoint.c

ob_acphy_cal_run_test: ob_acphy_cal_run_test.c \
		$(SRC)/ob_acphy_cal_run.c $(SRC)/ob_acphy_cal_run.h \
		$(SRC)/ob_phy_lpf.c $(SRC)/ob_phy.h $(SRC)/ob_phy_lpf.h
	$(CC) $(CFLAGS) -o $@ ob_acphy_cal_run_test.c \
		$(SRC)/ob_acphy_cal_run.c $(SRC)/ob_phy_lpf.c

ob_acphy_cal_stage_test: ob_acphy_cal_stage_test.c \
		$(SRC)/ob_acphy_cal_stage.c $(SRC)/ob_acphy_cal_stage.h \
		$(SRC)/ob_acphy_cal_run.c $(SRC)/ob_phy_lpf.c
	$(CC) $(CFLAGS) -o $@ ob_acphy_cal_stage_test.c \
		$(SRC)/ob_acphy_cal_stage.c $(SRC)/ob_acphy_cal_run.c \
		$(SRC)/ob_phy_lpf.c

ob_acphy_cal_write_stage_test: ob_acphy_cal_write_stage_test.c \
		$(SRC)/ob_acphy_cal_write_stage.c $(SRC)/ob_acphy_cal_write_stage.h \
		$(SRC)/ob_acphy_cal_stage.c $(SRC)/ob_acphy_cal_run.c $(SRC)/ob_phy_lpf.c
	$(CC) $(CFLAGS) -o $@ ob_acphy_cal_write_stage_test.c \
		$(SRC)/ob_acphy_cal_write_stage.c $(SRC)/ob_acphy_cal_stage.c \
		$(SRC)/ob_acphy_cal_run.c $(SRC)/ob_phy_lpf.c

ob_acphy_cal_w2_replay_test: ob_acphy_cal_w2_replay_test.c \
		$(SRC)/ob_acphy_cal_w2_replay.c $(SRC)/ob_acphy_cal_w2_replay.h \
		$(SRC)/ob_acphy_cal_stage.c $(SRC)/ob_acphy_cal_run.c $(SRC)/ob_phy_lpf.c
	$(CC) $(CFLAGS) -o $@ ob_acphy_cal_w2_replay_test.c \
		$(SRC)/ob_acphy_cal_w2_replay.c $(SRC)/ob_acphy_cal_stage.c \
		$(SRC)/ob_acphy_cal_run.c $(SRC)/ob_phy_lpf.c

ob_radio_local_0413_test: ob_radio_local_0413_test.c $(SRC)/ob_radio.h \
		$(SRC)/ob_ucode.h $(SRC)/ob_irq.h
	$(CC) $(CFLAGS) -o $@ $<

ob_radio_post0413_test: ob_radio_post0413_test.c $(SRC)/ob_radio_post0413.h
	$(CC) $(CFLAGS) -o $@ $<

ob_radio_postphase1_test: ob_radio_postphase1_test.c $(SRC)/ob_radio_postphase1.h
	$(CC) $(CFLAGS) -o $@ $<

ob_radio_postphase2_test: ob_radio_postphase2_test.c $(SRC)/ob_radio_postphase2.h
	$(CC) $(CFLAGS) -o $@ $<

ob_phy_postbmac_test: ob_phy_postbmac_test.c $(SRC)/ob_radio_postbmac.h \
		$(SRC)/ob_phy.h $(SRC)/ob_ucode.h
	$(CC) $(CFLAGS) -o $@ $<

ob_otp8e2_test: ob_otp8e2_test.c $(SRC)/ob_otp8e2.h
	$(CC) $(CFLAGS) -o $@ $<

ob_si_powerup_test: ob_si_powerup_test.c $(SRC)/ob_si_postcondition.h
	$(CC) $(CFLAGS) -o $@ $<

ob_fw_test: ob_fw_test.c $(SRC)/ob_fw.h
	$(CC) $(CFLAGS) -o $@ $<

ob_ucode_test: ob_ucode_test.c $(SRC)/ob_ucode.h $(SRC)/ob_irq.h
	$(CC) $(CFLAGS) -o $@ $<

ob_ucode_core_state_test: ob_ucode_core_state_test.c $(SRC)/ob_ucode.h $(SRC)/ob_irq.h
	$(CC) $(CFLAGS) -o $@ $<

ob_ucode_d2a_test: ob_ucode_d2a_test.c $(SRC)/ob_ucode_d2a_contract.h \
		$(SRC)/ob_ucode.h $(SRC)/ob_fw.h $(SRC)/ob_irq.h
	$(CC) $(CFLAGS) -std=c11 -pedantic -o $@ $<

ob_ucore_prep_test: ob_ucore_prep_test.c $(SRC)/ob_ucore_prep.h \
		$(SRC)/ob_ucode.h
	$(CC) $(CFLAGS) -std=c11 -pedantic -o $@ $<

ob_initvals_test: ob_initvals_test.c $(SRC)/ob_initvals.h $(SRC)/ob_fw.h \
		$(SRC)/ob_ucode.h $(SRC)/ob_irq.h
	$(CC) $(CFLAGS) -o $@ $<

ob_fw_chain_test: ob_fw_chain_test.c $(SRC)/ob_fw_chain_contract.h \
		$(SRC)/ob_ucode.h $(SRC)/ob_irq.h linux/types.h
	$(CC) $(CFLAGS) -std=c11 -pedantic -o $@ $<

ob_fw_chain_failure_test: ob_fw_chain_failure_test.c \
		$(SRC)/ob_fw_chain_contract.h linux/types.h
	$(CC) $(CFLAGS) -std=c11 -pedantic -o $@ $<

ob_fw_chain_mode_test: ob_fw_chain_mode_test.c $(SRC)/ob_ucode.h \
		$(SRC)/ob_irq.h linux/types.h
	$(CC) $(CFLAGS) -std=c11 -pedantic -o $@ $<

ob_d3a0_test: ob_d3a0_test.c $(SRC)/ob_d3a0.h $(SRC)/ob_rx.h \
		$(SRC)/ob_dma.h $(SRC)/ob_ucode.h
	$(CC) $(CFLAGS) -o $@ $<

ob_d3a1_test: ob_d3a1_test.c $(SRC)/ob_d3a1.h $(SRC)/ob_d3a0.h \
		$(SRC)/ob_rx.h $(SRC)/ob_dma.h $(SRC)/ob_ucode.h \
		$(SRC)/ob_initvals.h
	$(CC) $(CFLAGS) -o $@ $<

ob_d3a1_fifo530_test: ob_d3a1_fifo530_test.c $(SRC)/ob_d3a1.h
	$(CC) $(CFLAGS) -o $@ $<

ob_d3b_test: ob_d3b_test.c $(SRC)/ob_d3b.h $(SRC)/ob_d3a0.h \
		$(SRC)/ob_dma.h $(SRC)/ob_ucode.h $(SRC)/ob_irq.h \
		$(SRC)/ob_initvals.h $(SRC)/ob_fw.h
	$(CC) $(CFLAGS) -o $@ $<

ob_d11_tail_state_test: ob_d11_tail_state_test.c \
		$(SRC)/ob_d11_tail_plan.h linux/types.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $<

ob_d11_prefix_admission_test: ob_d11_prefix_admission_test.c \
		$(SRC)/ob_d11_tail_plan.h linux/types.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $<

ob_d11_lifecycle_states_test: ob_d11_lifecycle_states_test.c \
		$(SRC)/ob_lifecycle.h $(SRC)/ob_d11_tail_plan.h \
		$(SRC)/ob_irq.h linux/types.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ $<

ob_guard_test: ob_guard_test.c $(SRC)/ob_guard.h $(SRC)/ob_d3a0.h
	$(CC) $(CFLAGS) -o $@ $<

ob_cflags_test: ob_cflags_test.c $(SRC)/ob_guard.h
	$(CC) $(CFLAGS) -o $@ $<

ob_acphy_attach_arm_test: ob_acphy_attach_arm_test.c $(SRC)/ob_radio.h \
		$(SRC)/ob_guard.h linux/types.h
	$(CC) $(CFLAGS) -o $@ $<

ob_radio_test: ob_radio_test.c $(SRC)/ob_radio.h $(SRC)/ob_ucode.h \
		$(SRC)/ob_guard.h $(SRC)/ob_irq.h
	$(CC) $(CFLAGS) -o $@ $<

ob_phy_test: ob_phy_test.c $(SRC)/ob_phy.h $(SRC)/ob_phy_lpf.h \
		$(SRC)/ob_phy_lpf.c $(SRC)/ob_phy_lpf_payloads.h
	$(CC) $(CFLAGS) -o $@ ob_phy_test.c $(SRC)/ob_phy_lpf.c

ob_phy_post_lpf_test: ob_phy_post_lpf_test.c $(SRC)/ob_phy_post_lpf.h \
		$(SRC)/ob_phy_post_lpf.c $(SRC)/ob_phy_post_lpf_vector.h \
		$(SRC)/ob_phy_lpf.c $(SRC)/ob_phy_lpf.h $(SRC)/ob_phy_lpf_payloads.h
	$(CC) $(CFLAGS) -o $@ ob_phy_post_lpf_test.c \
		$(SRC)/ob_phy_post_lpf.c $(SRC)/ob_phy_lpf.c

ob_phy_a0be4_test: ob_phy_a0be4_test.c $(SRC)/ob_phy_a0be4.h \
		$(SRC)/ob_phy_a0be4.c $(SRC)/ob_phy_a0be4_vector.h \
		$(SRC)/ob_phy_post_lpf.c $(SRC)/ob_phy_post_lpf.h \
		$(SRC)/ob_phy_lpf.c $(SRC)/ob_phy_lpf.h
	$(CC) $(CFLAGS) -o $@ ob_phy_a0be4_test.c \
		$(SRC)/ob_phy_a0be4.c $(SRC)/ob_phy_post_lpf.c $(SRC)/ob_phy_lpf.c

ob_phy_a6b0f_test: ob_phy_a6b0f_test.c $(SRC)/ob_phy_a6b0f.c \
		$(SRC)/ob_phy_a6b0f.h $(SRC)/ob_phy_a6b0f_vector.h \
		$(SRC)/ob_phy_a0be4.c $(SRC)/ob_phy_post_lpf.c $(SRC)/ob_phy_lpf.c \
		$(SRC)/ob_phy_9e378.c $(SRC)/ob_phy_9e378.h $(SRC)/ob_phy_9e378_vector.h \
		$(SRC)/ob_phy_9eaf9.c $(SRC)/ob_phy_9eaf9.h $(SRC)/ob_phy_9eaf9_vector.h \
		$(SRC)/ob_phy_farrow_crs.c $(SRC)/ob_phy_farrow_crs.h \
		$(SRC)/ob_phy_post_farrow_continuation.c \
		$(SRC)/ob_phy_post_farrow_continuation.h
	$(CC) $(CFLAGS) -o $@ ob_phy_a6b0f_test.c \
		$(SRC)/ob_phy_a6b0f.c $(SRC)/ob_phy_a0be4.c \
		$(SRC)/ob_phy_post_lpf.c $(SRC)/ob_phy_lpf.c $(SRC)/ob_phy_9e378.c \
		$(SRC)/ob_phy_9eaf9.c $(SRC)/ob_phy_farrow_crs.c \
		$(SRC)/ob_phy_post_farrow_continuation.c

ob_phy_9e378_test: ob_phy_9e378_test.c $(SRC)/ob_phy_9e378.c \
		$(SRC)/ob_phy_9e378.h $(SRC)/ob_phy_9e378_vector.h
	$(CC) $(CFLAGS) -o $@ ob_phy_9e378_test.c $(SRC)/ob_phy_9e378.c

ob_phy_9eaf9_test: ob_phy_9eaf9_test.c $(SRC)/ob_phy_9eaf9.c \
		$(SRC)/ob_phy_9eaf9.h $(SRC)/ob_phy_9eaf9_vector.h
	$(CC) $(CFLAGS) -o $@ ob_phy_9eaf9_test.c $(SRC)/ob_phy_9eaf9.c

ob_phy_a7089_cal_test: ob_phy_a7089_cal_test.c $(SRC)/ob_phy_a7089_cal.c \
		$(SRC)/ob_phy_a7089_cal.h
	$(CC) $(CFLAGS) -o $@ ob_phy_a7089_cal_test.c $(SRC)/ob_phy_a7089_cal.c

ob_phy_farrow_crs_test: ob_phy_farrow_crs_test.c $(SRC)/ob_phy_farrow_crs.c \
		$(SRC)/ob_phy_farrow_crs.h $(SRC)/ob_phy_post_lpf.h \
		$(SRC)/ob_phy.h
	$(CC) $(CFLAGS) -o $@ ob_phy_farrow_crs_test.c $(SRC)/ob_phy_farrow_crs.c

ob_phy_post_farrow_continuation_test: ob_phy_post_farrow_continuation_test.c \
		$(SRC)/ob_phy_post_farrow_continuation.c \
		$(SRC)/ob_phy_post_farrow_continuation.h \
		$(SRC)/ob_phy_post_lpf.h $(SRC)/ob_phy.h
	$(CC) $(CFLAGS) -o $@ ob_phy_post_farrow_continuation_test.c \
		$(SRC)/ob_phy_post_farrow_continuation.c

ob_phy_99658_test: ob_phy_99658_test.c $(SRC)/ob_phy_99658.c \
		$(SRC)/ob_phy_99658.h $(SRC)/ob_phy.h
	$(CC) $(CFLAGS) -o $@ ob_phy_99658_test.c $(SRC)/ob_phy_99658.c


ob_phy_9af05_loop_test: ob_phy_9af05_loop_test.c $(SRC)/ob_phy_9af05_loop.c \
		$(SRC)/ob_phy_99658.c \
		$(SRC)/ob_phy_post_farrow_continuation.c \
		$(SRC)/ob_phy_9af05_loop.h $(SRC)/ob_phy_9a121.c \
		$(SRC)/ob_phy_9a121.h $(SRC)/ob_phy_9a539.c \
		$(SRC)/ob_phy_9a539.h $(SRC)/ob_phy.h
	$(CC) $(CFLAGS) -o $@ ob_phy_9af05_loop_test.c \
		$(SRC)/ob_phy_9af05_loop.c $(SRC)/ob_phy_99658.c \
		$(SRC)/ob_phy_9a121.c $(SRC)/ob_phy_9a539.c \
		$(SRC)/ob_phy_post_farrow_continuation.c

ob_phy_9a121_test: ob_phy_9a121_test.c $(SRC)/ob_phy_9a121.c \
		$(SRC)/ob_phy_9a121.h $(SRC)/ob_phy.h
	$(CC) $(CFLAGS) -o $@ ob_phy_9a121_test.c $(SRC)/ob_phy_9a121.c

ob_phy_9a121_active_test: ob_phy_9a121_active_test.c $(SRC)/ob_phy_9a121.c \
		$(SRC)/ob_phy_9a121.h $(SRC)/ob_phy.h
	$(CC) $(CFLAGS) -o $@ ob_phy_9a121_active_test.c $(SRC)/ob_phy_9a121.c

ob_phy_9a539_test: ob_phy_9a539_test.c $(SRC)/ob_phy_9a539.c \
		$(SRC)/ob_phy_9a121.c $(SRC)/ob_phy_99658.c \
		$(SRC)/ob_phy_post_farrow_continuation.c \
		$(SRC)/ob_phy_9a539.h $(SRC)/ob_phy_9a121.h \
		$(SRC)/ob_phy_99658.h $(SRC)/ob_phy_post_farrow_continuation.h \
		$(SRC)/ob_phy.h
	$(CC) $(CFLAGS) -o $@ ob_phy_9a539_test.c $(SRC)/ob_phy_9a539.c \
		$(SRC)/ob_phy_9a121.c $(SRC)/ob_phy_99658.c \
		$(SRC)/ob_phy_post_farrow_continuation.c

ob_phy_96203_test: ob_phy_96203_test.c $(SRC)/ob_phy_96203.c \
		$(SRC)/ob_phy_96203.h $(SRC)/ob_phy_post_lpf.h
	$(CC) $(CFLAGS) -o $@ ob_phy_96203_test.c $(SRC)/ob_phy_96203.c

ob_diag_test: ob_diag_test.c $(SRC)/ob_diag.h $(SRC)/ob_ucode.h \
		$(SRC)/ob_guard.h
		$(CC) $(CFLAGS) -o $@ $<

# Phase 36Z: executes the real wrapper-only decision/sequencing logic
# (src/ob_ai_wrapper_only.h) plus the real profile admission and the real
# 23-way isolated-mode selector.
ob_ai_wrapper_only_test: ob_ai_wrapper_only_test.c \
		$(SRC)/ob_ai_wrapper_only.h $(SRC)/ob_diag.h \
		$(SRC)/ob_ucode.h $(SRC)/ob_chip_profile.h $(PROFILE_OBJS) \
		linux/types.h
	$(CC) $(CFLAGS) -o $@ ob_ai_wrapper_only_test.c $(PROFILE_OBJS)

# Phase 37A: real default-load predicate, retired-mode refusal and the
# 22+1-way isolated-mode selector, including the unchanged 22 modes.
ob_phase37a_admission_test: ob_phase37a_admission_test.c \
		$(SRC)/ob_ai_wrapper_only.h $(SRC)/ob_ucode.h \
		$(SRC)/ob_profile.h $(PROFILE_OBJS) linux/types.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ \
		ob_phase37a_admission_test.c $(PROFILE_OBJS)

# Phase 37B: executes the real rev42_ai_enable_only sequencing/classification
# state machine (src/ob_ai_enable_only.h), the real admission policy and the
# real supported-device registry.
ob_phase37b_enable_test: ob_phase37b_enable_test.c \
		$(SRC)/ob_ai_enable_only.h $(SRC)/ob_ai_wrapper_only.h \
		$(SRC)/ob_diag.h $(SRC)/ob_ucode.h $(SRC)/ob_profile.h \
		$(PROFILE_OBJS) linux/types.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ \
		ob_phase37b_enable_test.c $(PROFILE_OBJS)

# Phase 38A: executes the real wrapper-enable engine AND the real Phase 38A
# gate/one-read/classify contract (src/ob_d11_maccontrol_once.h), plus the real
# 24-way isolated-mode selector.
ob_phase38a_maccontrol_test: ob_phase38a_maccontrol_test.c \
		$(SRC)/ob_d11_maccontrol_once.h $(SRC)/ob_ai_enable_only.h \
		$(SRC)/ob_ai_wrapper_only.h $(SRC)/ob_diag.h $(SRC)/ob_ucode.h \
		$(SRC)/ob_chip_profile.h $(PROFILE_OBJS) linux/types.h
	$(CC) $(CFLAGS) -std=c11 -Werror -pedantic -o $@ \
		ob_phase38a_maccontrol_test.c $(PROFILE_OBJS)

ob_device_loss_trace_test: ob_device_loss_trace_test.c \
		$(SRC)/ob_device_loss_trace.c $(SRC)/ob_device_loss_trace.h \
		$(SRC)/ob_radio.h $(SRC)/ob_guard.h
	$(CC) $(CFLAGS) -o $@ ob_device_loss_trace_test.c \
		$(SRC)/ob_device_loss_trace.c

ob_lifecycle_test: ob_lifecycle_test.c $(SRC)/ob_lifecycle.h
	$(CC) $(CFLAGS) -o $@ $<

ob_runtime_suspend_test: ob_runtime_suspend_test.c $(SRC)/ob_lifecycle.h
	$(CC) $(CFLAGS) -o $@ $<

ob_sprom_access_test: ob_sprom_access_test.c $(SRC)/ob_sprom_policy.h
	$(CC) $(CFLAGS) -o $@ $<

ob_fabric_baseline_test: ob_fabric_baseline_test.c $(SRC)/ob_fabric_baseline.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f ob_math_test ob_channel_fixture_test ob_radio_2069_lookup_test ob_caps_test ob_dma_test ob_dma_fatal_test ob_irq_test ob_rx_test \
		ob_rx_lifecycle_test \
	      ob_tx_sw_test ob_tx_path_test ob_d11_tx_frameid_test ob_rx_sw_test ob_dpc_sw_test ob_datapath_integration_test \
      ob_rx_mac80211_test ob_rx_integration_test ob_tx_status_test ob_rx_status_test ob_rx_fcs_contract_test \
      ob_rx_length_contract_test ob_tx_status_decode_test \
      ob_fw_test ob_ucode_test ob_ucode_core_state_test ob_ucode_d2a_test ob_ucore_prep_test ob_initvals_test ob_fw_chain_test ob_fw_chain_failure_test ob_fw_chain_mode_test ob_d3a0_test \
	      ob_d3a1_test ob_d3a1_fifo530_test ob_d3b_test ob_d11_tail_state_test ob_d11_prefix_admission_test ob_d11_lifecycle_states_test ob_guard_test ob_cflags_test ob_acphy_attach_arm_test ob_radio_test \
	      ob_phy_test ob_acphy_start_test ob_diag_test ob_device_loss_trace_test ob_lifecycle_test \
	      ob_acphy_preradio_mode_test ob_acphy_cal_contract_test ob_radio_local_0413_test \
	      ob_acphy_cal_run_test ob_acphy_cal_stage_test ob_acphy_cal_write_stage_test \
	      ob_acphy_cal_w2_replay_test \
	      ob_phy_tripoint_test \
	      ob_radio_postphase1_test \
	      ob_radio_postphase2_test \
	      ob_phy_postbmac_test \
	      ob_radio_post0413_test \
	      ob_si_powerup_test \
	      ob_runtime_suspend_test ob_fabric_baseline_test ob_sprom_access_test \
	      ob_phy_post_lpf_test ob_phy_a0be4_test ob_phy_9e378_test ob_phy_9eaf9_test \
	      ob_phy_a6b0f_test ob_phy_farrow_crs_test ob_phy_post_farrow_continuation_test \
	      ob_phy_99658_test ob_phy_9af05_loop_test ob_phy_9a121_test \
	      ob_phy_9a121_active_test ob_phy_9a539_test ob_phy_96203_test \
	      ob_phy_txpwr_index_test ob_phy_a7089_txpwr_test \
	      ob_phy_sub92500_test ob_chip_profile_test ob_profile_evidence_test
