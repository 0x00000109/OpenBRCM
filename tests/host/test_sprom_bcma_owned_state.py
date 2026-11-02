#!/usr/bin/env python3
"""Static contract tests for BCMA-owned normal SPROM state."""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SI = (ROOT / "src" / "ob_si.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "src" / "ob_core.h").read_text(encoding="utf-8")
MAC = (ROOT / "src" / "ob_mac80211.c").read_text(encoding="utf-8")


def function_body(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : pos + 1]
    raise AssertionError(f"unterminated function: {signature}")


def test_normal_probe_delegates_to_cached_consumer():
    probe = function_body(SI, "int ob_si_probe(struct ob_hw *hw)")
    assert "ob_si_consume_cached_sprom(hw)" in probe
    assert "if (!hw->mac_valid)" not in probe
    for forbidden in (
        "ob_si_read_mac(hw",
        "ob_si_cc_read16",
        "ob_si_sprom_diag",
        "ob_si_dump_sprom",
        "OB_CC_SPROM",
    ):
        assert forbidden not in probe


def test_powerup_has_no_normal_raw_sprom_dependency():
    powerup = function_body(SI, "int ob_si_powerup(struct ob_hw *hw)")
    assert "ob_si_dump_sprom" not in powerup
    assert "ob_si_sprom_read_required" not in powerup


def test_cached_consumer_reads_validated_rev11_board_data():
    consumer = function_body(SI, "static int ob_si_consume_cached_sprom(")
    assert "hw->bus->sprom" in consumer
    assert "bcma_read16" not in consumer
    assert "OB_CC_SPROM" not in consumer
    assert "OB_MAC_SOURCE_UNKNOWN" in consumer
    assert "OB_MAC_SOURCE_BCMA_PARSED" in consumer
    assert "ob_si_read_mac(hw, ext_mac)" in consumer
    assert "ext_mac" in consumer
    assert "sprom->boardflags3" not in consumer


def test_normal_rev11_boardflags3_comes_from_crc_validated_image():
    consumer = function_body(SI, "static int ob_si_consume_cached_sprom(")
    assert "ob_d3b_clear_rev11_board(&hw->board)" in consumer
    assert "ob_si_read_mac(hw, ext_mac)" in consumer
    assert "boardflags3 remains invalid" in consumer
    assert "boardflags3=0x%08x" in consumer
    assert "hw->mac_valid = false" in consumer
    assert "OB_BOARDFLAGS3_SOURCE_VALIDATED_REV11" not in consumer


def test_raw_rev11_boardflags3_has_crc_validated_source_marker():
    d3b = (ROOT / "src" / "ob_d3b.h").read_text(encoding="utf-8")
    decoder = function_body(d3b, "static inline bool ob_d3b_decode_rev11_board(")
    assert "OB_BOARDFLAGS3_SOURCE_VALIDATED_REV11" in decoder
    assert "ob_d3b_clear_rev11_board(out)" in decoder


def test_mac_provenance_is_explicit_and_rev11_not_repaired_from_il0mac():
    assert "enum ob_mac_source" in CORE_H
    assert "mac_source" in CORE_H
    assert "OB_MAC_SOURCE_REV11_CORRECTED" in CORE_H
    consumer = function_body(SI, "static int ob_si_consume_cached_sprom(")
    assert re.search(r"revision\s*==\s*11", consumer)
    assert "il0mac" in consumer
    assert "revision != 11" in consumer


def test_registration_fails_closed_without_valid_mac():
    register = function_body(MAC, "int ob_mac80211_register(struct ob_hw *ob)")
    assert "!ob->mac_valid" in register
    assert "return -ENODATA" in register
    assert "using mac80211 default address" not in register


def test_pre_registration_bringup_is_not_mac_gated():
    consumer = function_body(SI, "static int ob_si_consume_cached_sprom(")
    assert "return 0;" in consumer
    assert "-ENODATA" not in consumer


def test_raw_sprom_reader_is_shared_by_normal_and_isolated_board_paths():
    assert "int ob_si_sprom_evidence(struct ob_hw *hw)" in SI
    assert "static int ob_si_sprom_diag(struct ob_hw *hw)" in SI
    d3a1 = function_body(SI, "int ob_si_prepare_board_data_for_d3a1(")
    consumer = function_body(SI, "static int ob_si_consume_cached_sprom(")
    assert "ob_si_read_mac(hw" in d3a1
    assert "ob_si_read_mac(hw" in consumer
    assert "ob_si_read_mac(hw" not in function_body(SI, "int ob_si_probe(")


def test_no_new_raw_bar_or_non_bcma_access_path():
    consumer = function_body(SI, "static int ob_si_consume_cached_sprom(")
    assert "ioread" not in consumer
    assert "readl" not in consumer
    assert "pci_read_config" not in consumer
    assert "bcma_read" not in consumer


if __name__ == "__main__":
    tests = [value for name, value in globals().items()
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
    print(f"{len(tests)} SPROM ownership tests passed")
