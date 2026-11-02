#!/usr/bin/env python3
"""Static ordering checks for the pre-ucode attach identity lifecycle."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
LIFECYCLE = (ROOT / "src/ob_lifecycle.c").read_text(encoding="utf-8")
HEADER = (ROOT / "src/ob_lifecycle.h").read_text(encoding="utf-8")
RADIO = (ROOT / "src/ob_radio.c").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:end]
    raise AssertionError(f"unterminated function: {signature}")


def main() -> None:
    assert "OB_D11_ATTACH_IDENTITY_READY" in HEADER
    assert "state == OB_D11_CORE_READY" in HEADER
    assert "next == OB_D11_ATTACH_IDENTITY_READY" in HEADER
    assert "state == OB_D11_ATTACH_IDENTITY_READY" in HEADER
    assert "next == OB_D11_UCODE_LOADED" in HEADER
    assert "OB_D11_ACPHY_IDENTITY_READY" not in HEADER

    probe = function_body(CORE, "int ob_probe(")
    probe = probe[probe.rfind("ret = ob_si_probe(hw);"):]
    ordered = [
        "ob_si_probe(hw)",
        "ob_acphy_attach_identity(hw)",
        "ob_fw_probe(hw)",
        "ob_d11_post_ucode_init(hw)",
    ]
    positions = [probe.index(token) for token in ordered]
    assert positions == sorted(positions), "normal lifecycle order changed"
    assert probe.index("ob_acphy_attach_identity(hw)") < probe.index(
        "ob_d11_post_ucode_init(hw)"
    )

    attach = function_body(LIFECYCLE, "int ob_acphy_attach_identity(")
    assert "hw->d11_state != OB_D11_CORE_READY" in attach
    assert "ob_radio_prepare_for_identity(hw)" in attach
    assert "phyver = ob_d11_read16(hw, 0x03e0u)" in attach
    assert "ob_radio_read_identity(hw, &radio)" in attach
    assert "OB_D11_ATTACH_IDENTITY_READY" in attach
    assert "ob_d11_post_ucode_init" not in attach

    post = function_body(LIFECYCLE, "int ob_d11_post_ucode_init(")
    assert "hw->d11_state != OB_D11_ATTACH_IDENTITY_READY" in post
    assert "OB_D11_POST_UCODE_READY" in post

    assert "udelay(150)" not in RADIO
    assert RADIO.count("bcma_core_enable(hw->core, 0x4u)") == 1
    print("test_attach_identity_lifecycle: PASS")


if __name__ == "__main__":
    main()
