"""Independent oracle for the selected sub_a7089 caller suffix.

The caller transformation follows the pinned instructions around 0xaa54f-
0xaa5bf. The nested helper chronology is composed from the separate oracle
for the already implemented historical TX-power child.
"""
from __future__ import annotations

from txpwr_index_oracle import expected_callbacks


def sample_for_byte(raw: int) -> bytes:
    samples = {
        0x80: bytes((0x11, 0x22, 0x33, 0x44, 0x55, 0x66)),
        0x7f: bytes((0x91, 0x82, 0x73, 0x64, 0x55, 0x46)),
        0xff: bytes((0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6)),
    }
    try:
        return samples[raw & 0xff]
    except KeyError as exc:
        raise ValueError(f"no explicit synthetic TABLE sample for {raw:#x}") from exc


def expected_suffix(pi164: int, power_bytes: tuple[int, int],
                    initial_phy_040a: int = 0x1034,
                    initial_phy_019e: int | None = None,
                    phy_0070: int = 0xe000,
                    phy_0640: int = 0x3a00,
                    phy_0840: int = 0x4b00,
                    nested_342: int = 0,
                    acphychipid: int = 0x4352):
    """Return callback records through the selected caller PHY write."""
    pi164 &= 0xffffffff
    if initial_phy_019e is None:
        # The synthetic reset-CCA arm for {2,5,6} clears 0x003c and bit 0
        # in PHY 0x019e before sub_96203. Other tested PI+0x164 values take
        # the alternate reset-CCA sequence, which leaves this fixture intact.
        initial_phy_019e = 0xa580 if pi164 in (2, 5, 6) else 0xa5a4
    events = []
    if pi164 in (2, 3, 5, 6):
        events.extend((
            {"kind": "PHY_READ", "site": 0xaa57b, "reg": 0x040a,
             "table": 0, "offset": 0, "count": 0, "width": 0,
             "value": initial_phy_040a & 0xffff, "payload": []},
            {"kind": "PHY_WRITE", "site": 0xaa57b, "reg": 0x040a,
             "table": 0, "offset": 0, "count": 0, "width": 0,
             "value": ((initial_phy_040a & ~0x0200) | 0x0200) & 0xffff,
             "payload": []},
        ))

    for core, raw in enumerate(power_bytes):
        raw &= 0xff
        signed = raw if raw < 0x80 else raw - 0x100
        child, final_phy = expected_callbacks(
            core_mask=1 << core,
            core_bound=2,
            power_index=signed,
            table_bytes=sample_for_byte(raw),
            initial_phy_019e=initial_phy_019e,
        )
        if final_phy != (initial_phy_019e & 0xffff):
            raise AssertionError("historical child must restore PHY 0x019e")
        events.extend(child)

    if (phy_0070 & 0xe000) == 0xe000:
        events.append({"kind": "PHY_READ", "site": 0x9071a, "reg": 0x0070,
                       "table": 0, "offset": 0, "count": 0, "width": 0,
                       "value": phy_0070 & 0xffff, "payload": []})
        for reg, sample in ((0x0640, phy_0640), (0x0840, phy_0840)):
            events.append({"kind": "PHY_READ", "site": 0x906d7, "reg": reg,
                           "table": 0, "offset": 0, "count": 0, "width": 0,
                           "value": sample & 0xffff, "payload": []})
        final_0070 = phy_0070 & 0xffff
    else:
        events.append({"kind": "PHY_READ", "site": 0x9071a, "reg": 0x0070,
                       "table": 0, "offset": 0, "count": 0, "width": 0,
                       "value": phy_0070 & 0xffff, "payload": []})
        final_0070 = phy_0070 & 0xffff
    events.extend((
        {"kind": "PHY_READ", "site": 0x90760, "reg": 0x0070,
         "table": 0, "offset": 0, "count": 0, "width": 0,
         "value": final_0070, "payload": []},
        {"kind": "PHY_WRITE", "site": 0x90760, "reg": 0x0070,
         "table": 0, "offset": 0, "count": 0, "width": 0,
         "value": final_0070 & ~0xe000, "payload": []},
    ))

    if (pi164 & 0xffffffff) > 1:
        threshold = 0x1c if (pi164 & 0xffffffff) == 3 else 0x80
    elif nested_342 == 0x0203:
        threshold = 0x26
    else:
        threshold = 0x18
    register = 0x1641 if acphychipid in (0x4352, 0x4360, 0xa9c4,
                                          0xaa06, 0x4350) else 0x0641
    events.append({"kind": "PHY_WRITE", "site": 0xaa61e, "reg": register,
                   "table": 0, "offset": 0, "count": 0, "width": 0,
                   "value": (0x7f00 + threshold) & 0xffff, "payload": []})

    # Callback metadata is explicit in the fake interface: PHY/RADIO calls are
    # 16-bit operations and TABLE calls carry the historical element width.
    # Logical-event grouping is intentionally not used here because this oracle
    # independently checks callback chronology, while grouping is an
    # implementation-level counter maintained by the C runner.
    for event in events:
        kind = event["kind"]
        if kind.startswith("PHY_"):
            event["domain"] = "PHY"
            event["width"] = 16
        elif kind.startswith("RADIO_"):
            event["domain"] = "RADIO"
            event["width"] = 16
        elif kind.startswith("TABLE_"):
            event["domain"] = "TABLE"
        else:
            event["domain"] = "UNKNOWN"
    return events
