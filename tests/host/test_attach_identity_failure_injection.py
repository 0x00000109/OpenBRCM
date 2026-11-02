#!/usr/bin/env python3
"""Static fail-closed checks for each pre-ucode attach identity boundary."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
LIFECYCLE = (ROOT / "src/ob_lifecycle.c").read_text(encoding="utf-8")


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
    probe = function_body(CORE, "int ob_probe(")
    normal = probe[probe.rfind("ret = ob_si_probe(hw);"):]
    attach_call = normal.index("ret = ob_acphy_attach_identity(hw);")
    fw_call = normal.index("ret = ob_fw_probe(hw);")
    ucode_call = normal.index("ret = ob_d11_post_ucode_init(hw);")
    assert attach_call < fw_call < ucode_call
    assert normal.index("if (ret)\n\t\treturn ret;", attach_call) < fw_call

    attach = function_body(LIFECYCLE, "int ob_acphy_attach_identity(")
    assert attach.count("ob_radio_prepare_for_identity(hw)") == 1
    assert attach.index("if (ret)\n\t\treturn ret;") < attach.index(
        "phyver = ob_d11_read16(hw, 0x03e0u)"
    )
    assert attach.index("if (hw->dev_lost || phyver == 0xffffu)") < attach.index(
        "ob_radio_read_identity(hw, &radio)"
    )
    radio_error = min(
        pos for pos in (
            attach.find("if (ret)\n\t\treturn ret;", attach.index(
                "ob_radio_read_identity(hw, &radio)"
            )),
            attach.find("if (ret) {", attach.index(
                "ob_radio_read_identity(hw, &radio)"
            )),
        ) if pos >= 0
    )
    assert radio_error < attach.index("if (!radio.id_is_2069")
    assert attach.index("if (!radio.id_is_2069") < attach.index(
        "OB_D11_ATTACH_IDENTITY_READY"
    )
    assert "ob_ucode_run_d2a" not in attach

    post = function_body(LIFECYCLE, "int ob_d11_post_ucode_init(")
    assert "OB_D11_ATTACH_IDENTITY_READY" in post
    assert "OB_D11_CORE_READY" not in post
    print("test_attach_identity_failure_injection: PASS")


if __name__ == "__main__":
    main()
