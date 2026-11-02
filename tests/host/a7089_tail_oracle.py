"""Independent instruction-derived oracle for the sub_a7089 caller tail."""
from __future__ import annotations


def _event(kind: str, domain: str, site: int, reg: int = 0,
           value: int = 0) -> dict:
    return {
        "kind": kind, "domain": domain, "site": site, "reg": reg,
        "table": 0, "offset": 0, "count": 0,
        "width": 16 if domain in {"PHY", "RADIO"} else 0,
        "value": value, "payload": [],
    }


def expected_carriersearch_events(profile_pi164: int, chanspec: int,
                                  nested_902: int,
                                  nested_904: int,
                                  counter_before: int = 1,
                                  classifier_old: int = 0xa500,
                                  per_core_old: tuple[int, int] = (0, 0)
                                  ) -> list[dict]:
    """Instruction-derived events for the selected arg-0 carrier child.

    This child oracle is independent of the caller-prefix model, so it can
    validate PI+0x164==0 even where an earlier reset-CCA branch differs.
    All supplied register and nested words are explicit synthetic samples.
    """
    if profile_pi164 < 0 or profile_pi164 > 0xffffffff:
        raise ValueError("PI+0x164 out of range")
    if not 0 <= chanspec <= 0xffff:
        raise ValueError("chanspec out of range")
    if not 0 <= nested_902 <= 0xffff or not 0 <= nested_904 <= 0xffff:
        raise ValueError("nested word out of range")
    if not 0 <= counter_before <= 0xffff:
        raise ValueError("counter out of range")
    if len(per_core_old) != 2 or any(not 0 <= x <= 0xffff
                                     for x in per_core_old):
        raise ValueError("per-core samples must be two u16 values")
    if ((counter_before - 1) & 0xffff) != 0:
        return []

    mask = 7 if (chanspec & 0xc000) == 0 else 6
    events = [
        _event("PHY_READ", "PHY", 0x91273, 0x0140, classifier_old),
        _event("PHY_WRITE", "PHY", 0x91293, 0x0140,
               (classifier_old & ~mask) | (7 & mask)),
    ]
    for site, reg, old in ((0x8faae, 0x02ed, 0),
                           (0x8fac5, 0x02f1, 0),
                           (0x8fadc, 0x02f5, 0),
                           (0x8fb30, 0x02f9, 0)):
        events.extend((_event("PHY_READ", "PHY", site, reg, old),
                       _event("PHY_WRITE", "PHY", site, reg,
                              (old & ~0x10) | 0x10)))
    for core, reg in enumerate((0x06d4, 0x08d4)):
        if profile_pi164 == 0:
            events.append(_event("PHY_WRITE", "PHY", 0x97a16,
                                 reg + 6, nested_902))
        else:
            events.extend((_event("PHY_READ", "PHY", 0x97a2e, reg,
                                  per_core_old[core]),
                           _event("PHY_WRITE", "PHY", 0x97a2e, reg,
                                  per_core_old[core] & 0xbfff)))
    events.append(_event("PHY_WRITE", "PHY", 0x97b2e, 0x0339,
                         nested_904))
    return events


def _post_result(out: dict, saved_fa0: int,
                 lookup_mask: int | None) -> dict:
    if lookup_mask is not None:
        out.update({
            "txpwrctrl_nonzero_argument": 0,
            "txpwrctrl_nonzero_path": 0,
            "txpwrctrl_nonzero_store": 0,
            "txpwrctrl_core_updates": 0,
            "pi_fa0_after_txpwrctrl": 0,
            "post_aa63e_entered": 0,
            "post_aa63e_lookup_nonnull_mask": 0,
            "post_aa63e_radio_poll_selected": 0,
            "post_aa63e_poll_satisfied": 0,
            "post_aa63e_poll_exhausted": 0,
            "post_aa63e_poll_attempts": 0,
            "unsupported_site": (out["terminal_boundary"]
                                 if out["status"] == "unsupported" else 0),
            "post_aa63e_nested_gate_nonzero": 0,
            "post_aa63e_rxgainerr_completed": 0,
            "post_aa63e_0400_selected": 0,
            "post_aa63e_carriersearch_entered": 0,
            "post_aa63e_carriersearch_effects_selected": 0,
            "post_aa63e_carriersearch_completed": 0,
            "post_aa63e_carriersearch_count_after": 0,
            "post_aa63e_classifier_mask": 0,
            "post_aa63e_rxgainerr_band": 0,
            "post_aa63e_rxgainerr_return": 0,
            "post_aa63e_cores_completed": 0,
            "post_aa63e_rxgainerr_values": [0, 0],
            "post_aa63e_sub8f086_returns": [0, 0],
            "post_aa63e_sub8f086_output": [[0] * 6, [0] * 6],
            "nested_gainerr_state_after": [0, 0, 0, 0, 0],
        })
    return out


def expected_tail(pi_fa2: int, chanspec: int, nested: tuple[int, ...],
                  context_31: int, saved_fa0: int,
                  mac_state: str = "enabled",
                  initial_phy_0070: int = 0xe000,
                  post_aa63e_lookup_mask: int | None = None,
                  radio_sample: int = 0x0100,
                  nested_32c_snapshot: int = 0,
                  rxgainerr_return: int = 0,
                  pi_a7: int = 0,
                  pi_a4: int = 0,
                  core_candidate_counts: tuple[tuple[int, ...],
                                               tuple[int, ...]] | None = None,
                  phy_000b_sample: int = 0,
                  phy_0640: int = 0x3a00,
                  phy_0840: int = 0x4b00,
                  carriersearch_count: int = 2,
                  nested_902: int = 0x2468,
                  nested_904: int = 0x1357) -> dict:
    """Model caller-visible mutations and callback chronology.

    All values are test fixtures. The source-derived helper behavior uses the
    saved unsigned PI byte and the two-core BCM4352 AC attach bound. The
    optional lookup mask represents the selected class-0 sub_8e9b1 output
    slot at caller stack ``[rbp-0x40]`` and must be one when that continuation
    is modeled. Other caller slots require their upstream row route. The
    RADIO sample is supplied by the fake backend and is never promoted to a
    hardware fact.
    """
    if len(nested) != 4:
        raise ValueError("nested+0x456 fixture must contain four bytes")
    for value in (pi_fa2, context_31, saved_fa0, *nested):
        if not 0 <= value <= 0xff:
            raise ValueError("byte input out of range")
    if not 0 <= chanspec <= 0xffff:
        raise ValueError("PI+0x17e input out of range")
    if not 0 <= initial_phy_0070 <= 0xffff or not 0 <= radio_sample <= 0xffff:
        raise ValueError("register fixture out of range")
    if not 0 <= phy_0640 <= 0xffff or not 0 <= phy_0840 <= 0xffff:
        raise ValueError("PHY fixture out of range")
    if not 0 <= carriersearch_count <= 0xffff:
        raise ValueError("nested+0x00c low-word fixture out of range")
    if not 0 <= nested_902 <= 0xffff or not 0 <= nested_904 <= 0xffff:
        raise ValueError("nested word fixture out of range")
    if core_candidate_counts is None:
        core_candidate_counts = ((1,) * 6, (1,) * 6)
    if len(core_candidate_counts) != 2 or any(
            len(row) != 6 or any(not 1 <= count <= 6 for count in row)
            for row in core_candidate_counts):
        raise ValueError("each core needs six explicit candidate limits in 1..6")
    if mac_state not in {"unknown", "enabled", "suspended"}:
        raise ValueError("invalid initial MAC state")
    if post_aa63e_lookup_mask is not None and post_aa63e_lookup_mask != 1:
        raise ValueError("only class-0 lookup output slot -0x40 is supported")

    # sub_995ab @0x995d1..0x995fb selects a zero vector or four 0xf8 bytes.
    chanspec_mask = chanspec & 0xc000
    if pi_fa2 == 0 or chanspec_mask == 0xc000:
        source = (0, 0, 0, 0)
    elif chanspec_mask == 0:
        source = (0xf8, 0xf8, 0xf8, 0xf8)
    else:
        source = None

    updated = list(nested)
    changed = False
    child_return = None
    base_logical = 89 if (initial_phy_0070 & 0xe000) == 0xe000 else 87
    base_callbacks = 114 if (initial_phy_0070 & 0xe000) == 0xe000 else 112
    if source is not None:
        for index, byte in enumerate(source):
            if byte == 0:
                if updated[index] != 0:
                    updated[index] = 0
                    changed = True
                continue
            # The admitted BCM4352 AC profile has PI+0x168 == 2.
            if index > 2:
                child_return = -2
                break
            if updated[index] != byte:
                updated[index] = byte
                changed = True

    events: list[dict] = []
    state = mac_state
    status = "complete"
    boundary = 0xaa63e
    if child_return != -2 and changed and context_31 != 0:
        events.append(_event("MAC_SUSPEND", "PLATFORM", 0x9958b))
        state = "suspended"
        out = {
            "events": events, "status": "unsupported",
            "terminal_boundary": 0x99593, "nested_456": updated,
            "mac_state": state, "sub99528_return": None,
            "tail_logical_events": 1, "tail_callbacks": 1,
            "logical_event_count": base_logical + 1,
            "callback_count": base_callbacks + 1,
        }
        return _post_result(out, saved_fa0, post_aa63e_lookup_mask)

    if child_return is None and source is not None:
        # Common epilogue @0x995a5 zeros EAX on an ordinary child return.
        child_return = 0

    tail_logical = 0
    tail_callbacks = 0
    core_updates = 0
    nonzero_store = 0
    pi_fa0_after = 0
    nonzero_argument = 0

    # The second helper sees persistent PHY[0x0070] after the first arg-zero
    # invocation cleared its field. That first invocation either populated the
    # two nested bytes from PHY[0x0640]/[0x0840], or retained their zero seed.
    post_first_sample = initial_phy_0070 & 0xffff & ~0xe000
    if saved_fa0 == 0:
        events.extend((
            _event("PHY_READ", "PHY", 0x9071a, 0x0070, post_first_sample),
            _event("PHY_READ", "PHY", 0x90760, 0x0070, post_first_sample),
            _event("PHY_WRITE", "PHY", 0x90760, 0x0070, post_first_sample),
        ))
        tail_logical += 2
        tail_callbacks += 3
    else:
        nonzero_argument = saved_fa0
        if saved_fa0 == 1:
            # cmp sil,1 / ja at 0x906e7 stores only for the value one.
            nonzero_store = 1
            pi_fa0_after = 1

        restored = (post_first_sample & ~0xe000) | 0xe000
        events.extend((
            _event("PHY_READ", "PHY", 0x9077c, 0x0070, post_first_sample),
            _event("PHY_WRITE", "PHY", 0x9077c, 0x0070, restored),
        ))
        tail_logical += 1
        tail_callbacks += 2

        if (initial_phy_0070 & 0xe000) == 0xe000:
            nested_tx = ((phy_0640 & 0x7f00) >> 8,
                         (phy_0840 & 0x7f00) >> 8)
        else:
            nested_tx = (0, 0)
        for core, (reg, byte) in enumerate(zip((0x0644, 0x0844), nested_tx)):
            if byte == 0x80:
                continue
            old = 0  # both fake register cells are zero in the admitted fixture
            write = (old & ~0x007f) | (byte & 0x007f)
            events.extend((
                _event("PHY_READ", "PHY", 0x8f414, reg, old),
                _event("PHY_WRITE", "PHY", 0x8f414, reg, write),
            ))
            tail_logical += 1
            tail_callbacks += 2
            core_updates += 1

    post_entered = 0
    post_mask = 0
    poll_selected = 0
    poll_satisfied = 0
    poll_exhausted = 0
    poll_attempts = 0
    nested_gate_nonzero = 0
    rxgainerr_completed = 0
    phy0400_selected = 0
    rxgainerr_band = 0
    rxgainerr_flag = 0
    cores_completed = 0
    rxgainerr_values = [0, 0]
    sub8f086_returns = [0, 0]
    sub8f086_output = [[0] * 6, [0] * 6]
    nested_gainerr = [0xaa, 0xbb, 0xcc, 0xdd, 0xee]
    carriersearch_entered = 0
    carriersearch_effects_selected = 0
    carriersearch_completed = 0
    carriersearch_count_after = 0
    classifier_mask = 0
    if post_aa63e_lookup_mask is not None:
        post_entered = 1
        post_mask = post_aa63e_lookup_mask
        if post_mask:
            poll_selected = 1
            for attempt in range(1, 101):
                poll_attempts = attempt
                events.append(_event("DELAY", "DELAY", 0x93e61, value=10))
                events.append(_event("RADIO_READ", "RADIO", 0x93e95,
                                     0x090b, radio_sample))
                tail_logical += 2
                tail_callbacks += 2
                if radio_sample & 0x0100:
                    poll_satisfied = 1
                    break
            else:
                poll_exhausted = 1

        nested_gate_nonzero = int(nested_32c_snapshot != 0)
        if nested_gate_nonzero:
            events.append(_event("PHY_READ", "PHY", 0xaa672, 0x000b,
                                 phy_000b_sample))
            tail_logical += 1
            tail_callbacks += 1
            bits = phy_000b_sample & 7
            mask = ((1 << bits) - 1) & 0xff
            if pi_a7 != mask or pi_a4 != pi_a7:
                # wlc_phy_rxcore_setstate_acphy. shared+0x31 == 0 is the
                # software-only arm (shared+0xa7 store, no PHY access, no MAC
                # callback), so control continues in the caller. The nonzero
                # arm runs the reference-counted MAC suspend, the recovered
                # register sequence and the MAC enable; this oracle does not
                # model it, and no assert_matches fixture selects it.
                if context_31 != 0:
                    raise NotImplementedError(
                        "nonzero shared+0x31 selects the RX-core hardware "
                        "arm, which this oracle does not model")
        if boundary != 0xaa6a5:
            # The selected BCM4352 synthetic PI+0x164=2 resetCCA arm. Its
            # saved PHY samples come from the persistent fake state left by
            # the prefix: PHY[1]=0x1234 and PHY[0x019e]=0xa580.
            events.extend((
                _event("FGC", "PLATFORM", 0x9742b, value=1),
                _event("PHY_READ", "PHY", 0x97438, 0x0001, 0x1234),
                _event("PHY_WRITE", "PHY", 0x97450, 0x0001, 0x5234),
                _event("DELAY", "DELAY", 0x97455, value=1),
                _event("PHY_READ", "PHY", 0x9746e, 0x019e, 0xa580),
                _event("PHY_WRITE", "PHY", 0x9746e, 0x019e, 0xa580),
                _event("DELAY", "DELAY", 0x97478, value=1),
                _event("PHY_READ", "PHY", 0x9748f, 0x019e, 0xa580),
                _event("PHY_WRITE", "PHY", 0x9748f, 0x019e, 0xa581),
                _event("DELAY", "DELAY", 0x97499, value=1),
                _event("FGC", "PLATFORM", 0x974a8, value=0),
                _event("DELAY", "DELAY", 0x974b2, value=1),
                _event("PHY_WRITE", "PHY", 0x974c2, 0x0001, 0x1234),
                _event("DELAY", "DELAY", 0x974d2, value=1),
                _event("PHY_READ", "PHY", 0x974e6, 0x019e, 0xa581),
                _event("PHY_WRITE", "PHY", 0x974e6, 0x019e, 0xa580),
                _event("DELAY", "DELAY", 0x97555, value=2),
            ))
            tail_logical += 14
            tail_callbacks += 17

            # PI+0x160=0x0b selects the AC-PHY byte rows. The low byte of
            # the explicit chanspec chooses one of the five historical spans.
            channel = chanspec & 0xff
            if channel <= 0x0e:
                rxgainerr_band = 0
            elif channel <= 0x30:
                rxgainerr_band = 1
            elif channel <= 0x40:
                rxgainerr_band = 2
            elif channel <= 0x80:
                rxgainerr_band = 3
            else:
                rxgainerr_band = 4
            rxgainerr_values = [-2, 3]
            rxgainerr_flag = rxgainerr_return
            rxgainerr_completed = 1
            if rxgainerr_flag:
                nested_gainerr[0] = 0
                nested_gainerr[1] = 0
                cores_completed = 2
            else:
                # Reconstruct sub_8f086 from the selected byte loop: each
                # core has its own six limits, and the second core starts at
                # nested+0x6c2 due to the historical 0x78 stride.
                initial = [0x2b, 0x2b, 0x2b, 0x34, 0x34, 0x64]
                for core in range(2):
                    base = 2
                    target = base + 0x45
                    accumulator = 0
                    generated = list(initial)
                    # The synthetic coefficient matrix has x=(i+1) and
                    # y=(i+5) for candidate zero. Core one also has x=2/y=6
                    # at output 0, candidate 1; all other nonzero candidates
                    # are zero-initialized.
                    x_values = [i + 1 for i in range(6)]
                    y_values = [i + 5 for i in range(6)]
                    for i in range(6):
                        if i == 3 and core % 3 == 2:
                            target = min(target + (1 if target % 3 == 2 else 0), 30)
                        residual_byte = ((target & 0xff) -
                                         sum(x_values[1:])) & 0xff
                        residual = (residual_byte if residual_byte < 0x80
                                    else residual_byte - 0x100)
                        found = False
                        for candidate in range(core_candidate_counts[core][i], 0, -1):
                            index = candidate - 1
                            if i == 0 and core == 1 and index == 1:
                                x_value, y_value = 2, 6
                            elif index == 0:
                                x_value, y_value = x_values[i], y_values[i]
                            else:
                                x_value, y_value = 0, 0
                            if (y_value != y_values[i] and
                                    (x_value > residual or
                                     x_value + accumulator > generated[i])):
                                continue
                            generated[i] = y_value
                            accumulator += x_value
                            target -= x_value
                            found = True
                            break
                        if not found:
                            accumulator = base
                            generated = [0] * 6
                            break
                    sub8f086_output[core] = generated
                    sub8f086_returns[core] = (accumulator - base) & 0xff
                    adjusted = (sub8f086_returns[core] * 2 - 4) & 0xff
                    nested_gainerr[1 + core] = (
                        [0xfe, 3][core] - 0x7a - adjusted) & 0xff
                cores_completed = 2

            # PI+0x164 is fixed to the explicit synthetic value 2 by this
            # runner invocation, selecting PHY[0x0400] RMW mask 1/value 0.
            phy0400_selected = 1
            events.append(_event("PHY_READ", "PHY", 0xaa74d, 0x0400,
                                 0x1235))
            events.append(_event("PHY_WRITE", "PHY", 0xaa74d, 0x0400,
                                 0x1234))
            tail_logical += 1
            tail_callbacks += 2
            carriersearch_entered = 1
            carriersearch_count_after = (carriersearch_count - 1) & 0xffff
            if carriersearch_count_after == 0:
                carriersearch_effects_selected = 1
                classifier_mask = 7 if (chanspec & 0xc000) < 1 else 6
                classifier_old = 0xa500
                classifier_new = ((classifier_old & ~classifier_mask) |
                                  (7 & classifier_mask)) & 0xffff
                events.extend((
                    _event("PHY_READ", "PHY", 0x91273, 0x0140,
                           classifier_old),
                    _event("PHY_WRITE", "PHY", 0x91293, 0x0140,
                           classifier_new),
                ))
                tail_logical += 1
                tail_callbacks += 2

                for site, reg in ((0x8faae, 0x02ed),
                                  (0x8fac5, 0x02f1),
                                  (0x8fadc, 0x02f5),
                                  (0x8fb30, 0x02f9)):
                    events.extend((
                        _event("PHY_READ", "PHY", site, reg, 0),
                        _event("PHY_WRITE", "PHY", site, reg, 0x0010),
                    ))
                tail_logical += 4
                tail_callbacks += 8

                for core in range(2):
                    if core == 0:
                        reg = 0x06d4
                    else:
                        reg = 0x08d4
                    events.extend((
                        _event("PHY_READ", "PHY", 0x97a2e, reg, 0),
                        _event("PHY_WRITE", "PHY", 0x97a2e, reg, 0),
                    ))
                    tail_logical += 1
                    tail_callbacks += 2

                events.append(_event("PHY_WRITE", "PHY", 0x97b2e,
                                     0x0339, nested_904))
                tail_logical += 1
                tail_callbacks += 1
            carriersearch_completed = 1
            status = "complete"
            boundary = 0xaa781

    out = {
        "events": events, "status": status,
        "terminal_boundary": boundary, "nested_456": updated,
        "mac_state": state, "sub99528_return": child_return,
        "tail_logical_events": tail_logical,
        "tail_callbacks": tail_callbacks,
        "logical_event_count": base_logical + tail_logical,
        "callback_count": base_callbacks + tail_callbacks,
    }
    if post_aa63e_lookup_mask is not None:
        out.update({
            "txpwrctrl_nonzero_argument": nonzero_argument,
            "txpwrctrl_nonzero_path": int(saved_fa0 != 0),
            "txpwrctrl_nonzero_store": nonzero_store,
            "txpwrctrl_core_updates": core_updates,
            "pi_fa0_after_txpwrctrl": pi_fa0_after,
            "post_aa63e_entered": post_entered,
            "post_aa63e_lookup_nonnull_mask": post_mask,
            "post_aa63e_radio_poll_selected": poll_selected,
            "post_aa63e_poll_satisfied": poll_satisfied,
            "post_aa63e_poll_exhausted": poll_exhausted,
            "post_aa63e_poll_attempts": poll_attempts,
            "post_aa63e_nested_gate_nonzero": nested_gate_nonzero,
            "post_aa63e_rxgainerr_completed": rxgainerr_completed,
            "post_aa63e_0400_selected": phy0400_selected,
            "post_aa63e_carriersearch_entered": carriersearch_entered,
            "post_aa63e_carriersearch_effects_selected": carriersearch_effects_selected,
            "post_aa63e_carriersearch_completed": carriersearch_completed,
            "post_aa63e_carriersearch_count_after": carriersearch_count_after,
            "post_aa63e_classifier_mask": classifier_mask,
            "post_aa63e_rxgainerr_band": rxgainerr_band,
            "post_aa63e_rxgainerr_return": rxgainerr_flag,
            "post_aa63e_cores_completed": cores_completed,
            "post_aa63e_rxgainerr_values": rxgainerr_values,
            "post_aa63e_sub8f086_returns": sub8f086_returns,
            "post_aa63e_sub8f086_output": sub8f086_output,
            "nested_gainerr_state_after": nested_gainerr,
            "unsupported_site": boundary if status == "unsupported" else 0,
        })
    return out
