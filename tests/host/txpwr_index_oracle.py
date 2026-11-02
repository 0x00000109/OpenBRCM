"""Independent callback oracle transcribed from pinned instruction order.

This module does not import or parse the C implementation. TABLE_READ packing
uses the byte loads, shifts and AX stores at vendor PCs 0x986ff..0x9873b.
"""
from __future__ import annotations


def _le16(value: int) -> list[int]:
    return [value & 0xff, (value >> 8) & 0xff]


def expected_callbacks(core_mask: int, core_bound: int, power_index: int,
                       table_bytes: bytes, initial_phy_019e: int = 0xa5a4):
    if len(table_bytes) != 6:
        raise ValueError("the historical TABLE_READ consumes six bytes")
    events = []
    phy = initial_phy_019e & 0xffff

    def phy_read(site: int) -> None:
        events.append({"kind": "PHY_READ", "site": site, "reg": 0x019e,
                       "table": 0, "offset": 0, "count": 0, "width": 0,
                       "value": phy, "payload": []})

    def phy_write(site: int, value: int) -> None:
        nonlocal phy
        phy = value & 0xffff
        events.append({"kind": "PHY_WRITE", "site": site, "reg": 0x019e,
                       "table": 0, "offset": 0, "count": 0, "width": 0,
                       "value": phy, "payload": []})

    def rmw_bit1(site: int, saved_bit: int | None = None) -> None:
        phy_read(site)
        old = phy
        bit = 1 if saved_bit is None else saved_bit
        phy_write(site, (old & ~0x0002) | (bit << 1))

    saved_top = (initial_phy_019e >> 1) & 1
    phy_read(0x9ce1e)
    rmw_bit1(0x9ce39)

    for core in range(core_bound):
        if not (core_mask & (1 << core)):
            continue

        saved_9868f = (phy >> 1) & 1
        phy_read(0x986ac)
        rmw_bit1(0x986c6)
        events.append({"kind": "TABLE_READ", "site": 0x986e6, "reg": 0,
                       "table": 0x20, "offset": power_index, "count": 1,
                       "width": 48, "value": 0,
                       "payload": list(table_bytes)})
        d0, d1, d2, d3, d4, d5 = table_bytes
        word0 = (d1 + (d2 << 8)) & 0xffff
        word2 = (d3 + (d4 << 8)) & 0xffff
        word4 = d5 & 0xffff
        word8 = d0 & 0xffff
        rmw_bit1(0x9873f, saved_9868f)

        for site, offset, value in (
            (0x9cea3, 0x100 + core, word0),
            (0x9cec7, 0x103 + core, word2),
            (0x9ceeb, 0x106 + core, word4),
        ):
            events.append({"kind": "TABLE_WRITE", "site": site, "reg": 0,
                           "table": 7, "offset": offset, "count": 1,
                           "width": 16, "value": 0,
                           "payload": _le16(value)})

        offsets = ((0x63, 0x73), (0x67, 0x77),
                   (0x6b, 0x7b), (0x6f, 0x7f))[core]
        saved_9c4e4 = (phy >> 1) & 1
        phy_read(0x9c535)
        rmw_bit1(0x9c54f)
        for site, offset in ((0x9c573, offsets[0]), (0x9c594, offsets[1])):
            events.append({"kind": "TABLE_WRITE", "site": site, "reg": 0,
                           "table": 0x0c, "offset": offset, "count": 1,
                           "width": 16, "value": 0,
                           "payload": _le16(word8)})
        rmw_bit1(0x9c5b1, saved_9c4e4)

    rmw_bit1(0x9cf3f, saved_top)
    return events, phy
