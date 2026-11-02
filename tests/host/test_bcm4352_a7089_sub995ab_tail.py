#!/usr/bin/env python3
"""Compare the sub_995ab tail with an independent instruction oracle."""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/host"))
from a7089_tail_oracle import expected_carriersearch_events, expected_tail

BINARY = ROOT / "tests/host/ob_phy_a7089_txpwr_test"


class Sub995abTailTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "-C", "tests/host", "ob_phy_a7089_txpwr_test"],
                       cwd=ROOT, check=True, capture_output=True, text=True)

    def run_tail(self, pi_fa2: int, chanspec: int, nested: tuple[int, ...],
                 context_31: int, saved_fa0: int,
                 mac_state: str = "enabled",
                 phy_0070: int = 0xe000,
                 post_aa63e_lookup_mask: int | None = None,
                 radio_sample: int = 0x0100,
                 nested_32c_snapshot: int = 0,
                 rxgainerr_return: int = 0,
                 pi_a7: int = 0,
                 pi_a4: int = 0,
                 core_candidate_counts: tuple[tuple[int, ...],
                                              tuple[int, ...]] | None = None,
                 carriersearch_count: int | None = None,
                 nested_902: int = 0x2468,
                 nested_904: int = 0x1357,
                 profile_pi164: int = 2,
                 rfseq_sample: int | None = None) -> dict:
        command = [str(BINARY), "--tail", str(profile_pi164), "0x80", "0x7f",
                   hex(pi_fa2), hex(chanspec),
                   *(hex(value) for value in nested), hex(context_31),
                   hex(saved_fa0), mac_state]
        if phy_0070 != 0xe000 or post_aa63e_lookup_mask is not None:
            command.append(hex(phy_0070))
        if post_aa63e_lookup_mask is not None:
            command.append(hex(post_aa63e_lookup_mask))
            command.append(hex(radio_sample))
            command.extend((hex(nested_32c_snapshot), hex(rxgainerr_return),
                            hex(pi_a7), hex(pi_a4)))
            if rfseq_sample is not None:
                command.append(hex(rfseq_sample))
            if core_candidate_counts is not None:
                command.extend(hex(count) for row in core_candidate_counts
                               for count in row)
            if carriersearch_count is not None:
                command.extend((hex(carriersearch_count), hex(nested_902),
                                hex(nested_904)))
        completed = subprocess.run(command, cwd=ROOT, check=True,
                                   capture_output=True, text=True)
        return json.loads(completed.stdout)

    def assert_matches(self, *args, **kwargs):
        actual = self.run_tail(*args, **kwargs)
        expected = expected_tail(*args, **kwargs)
        self.assertEqual(actual, expected)

    def test_core_one_uses_its_strided_candidate_count_row(self):
        counts = ((1, 1, 1, 1, 1, 1), (2, 1, 1, 1, 1, 1))
        actual = self.run_tail(0, 0, (0, 0, 0, 0), 0, 1,
                               post_aa63e_lookup_mask=1,
                               core_candidate_counts=counts)
        expected = expected_tail(0, 0, (0, 0, 0, 0), 0, 1,
                                 post_aa63e_lookup_mask=1,
                                 core_candidate_counts=counts)
        self.assertEqual(actual, expected)
        self.assertEqual(actual["post_aa63e_sub8f086_returns"], [19, 20])
        self.assertEqual(actual["nested_gainerr_state_after"],
                         [0xaa, 0x62, 0x65, 0xdd, 0xee])

    def test_zero_argument_mutates_four_bytes_then_uses_saved_arg_zero(self):
        self.assert_matches(0, 0x0000, (1, 2, 3, 4), 0, 0, "enabled")

    def test_nonzero_zero_mask_f8_vector_stops_mutation_at_bound_two(self):
        self.assert_matches(1, 0x0000, (0, 0, 0, 0x44), 1, 0, "enabled")

    def test_nonzero_c000_mask_selects_zero_vector(self):
        self.assert_matches(1, 0xc123, (1, 2, 3, 4), 0, 0, "suspended")

    def test_nonzero_4000_mask_skips_sub99528(self):
        self.assert_matches(1, 0x4001, (1, 2, 3, 4), 1, 0, "enabled")

    def test_nonzero_8000_mask_skips_sub99528(self):
        self.assert_matches(1, 0x8001, (1, 2, 3, 4), 1, 0, "unknown")

    def test_changed_zero_vector_suspends_then_stops_before_sub9949f(self):
        self.assert_matches(0, 0, (1, 2, 3, 4), 1, 0, "enabled")

    def test_unchanged_zero_vector_does_not_suspend_already_enabled_mac(self):
        self.assert_matches(0, 0, (0, 0, 0, 0), 1, 0, "enabled")

    def test_saved_pi_fa0_one_selects_nonzero_txpwrctrl_through_aa63e(self):
        self.assert_matches(0, 0, (0, 0, 0, 0), 0, 1, "enabled")
        result = expected_tail(0, 0, (0, 0, 0, 0), 0, 1)
        self.assertEqual(result["logical_event_count"], 92)
        self.assertEqual(result["callback_count"], 120)
        self.assertEqual(result["terminal_boundary"], 0xaa63e)

    def test_distinct_nonzero_bytes_share_branch_except_argument_one_store(self):
        one = self.run_tail(0, 0, (0, 0, 0, 0), 0, 1,
                            post_aa63e_lookup_mask=1)
        two = self.run_tail(0, 0, (0, 0, 0, 0), 0, 2,
                            post_aa63e_lookup_mask=1)
        maximum = self.run_tail(0, 0, (0, 0, 0, 0), 0, 0xff,
                                post_aa63e_lookup_mask=1)
        self.assertEqual(one["events"], two["events"])
        self.assertEqual(two["events"], maximum["events"])
        self.assertEqual(one["txpwrctrl_nonzero_store"], 1)
        self.assertEqual(two["txpwrctrl_nonzero_store"], 0)
        self.assertEqual(two["pi_fa0_after_txpwrctrl"], 0)
        self.assertEqual(maximum["txpwrctrl_nonzero_argument"], 0xff)

    def test_class0_lookup_match_runs_sub93e47_poll_before_aa664(self):
        self.assert_matches(0, 0, (0, 0, 0, 0), 0, 1,
                            post_aa63e_lookup_mask=1,
                            radio_sample=0x0100)
        result = self.run_tail(0, 0, (0, 0, 0, 0), 0, 1,
                               post_aa63e_lookup_mask=1,
                               radio_sample=0x0100)
        self.assertEqual(result["terminal_boundary"], 0xaa781)
        self.assertEqual(result["post_aa63e_poll_attempts"], 1)
        self.assertEqual(result["post_aa63e_poll_satisfied"], 1)
        poll_events = [event for event in result["events"]
                       if event["site"] in (0x93e61, 0x93e95)]
        self.assertEqual([event["kind"] for event in poll_events],
                         ["DELAY", "RADIO_READ"])
        self.assertEqual(poll_events[0]["value"], 10)
        self.assertEqual(poll_events[1]["reg"], 0x090b)
        self.assertEqual(result["post_aa63e_rxgainerr_completed"], 1)
        self.assertEqual(result["post_aa63e_cores_completed"], 2)
        self.assertEqual(result["nested_gainerr_state_after"],
                         [0xaa, 0x62, 0x67, 0xdd, 0xee])

    def test_class0_lookup_row_polls_then_reaches_carriersearch(self):
        self.assert_matches(0, 0, (0, 0, 0, 0), 0, 2,
                            post_aa63e_lookup_mask=1)
        result = self.run_tail(0, 0, (0, 0, 0, 0), 0, 2,
                               post_aa63e_lookup_mask=1)
        self.assertEqual(result["terminal_boundary"], 0xaa781)
        self.assertEqual(result["post_aa63e_entered"], 1)
        self.assertEqual(result["post_aa63e_radio_poll_selected"], 1)
        self.assertEqual(result["post_aa63e_poll_attempts"], 1)
        self.assertEqual(result["tail_callbacks"], 27)

    def test_other_lookup_output_slots_are_unsupported_for_class0_profile(self):
        for mask in (0x02, 0x04, 0x08):
            actual = self.run_tail(0, 0, (0, 0, 0, 0), 0, 2,
                                   post_aa63e_lookup_mask=mask)
            self.assertEqual(actual["status"], "invalid", hex(mask))
            self.assertEqual(actual["events"], [], hex(mask))
            self.assertEqual(actual["callback_count"], 0, hex(mask))

    def test_aa63e_rejects_unreachable_lookup_output_states(self):
        for mask in (0x00, 0x03, 0x10):
            actual = self.run_tail(0, 0, (0, 0, 0, 0), 0, 2,
                                   post_aa63e_lookup_mask=mask)
            self.assertEqual(actual["status"], "invalid", hex(mask))
            self.assertEqual(actual["events"], [], hex(mask))
            self.assertEqual(actual["callback_count"], 0, hex(mask))

    def test_sub93e47_poll_exhaustion_continues_through_caller_return(self):
        self.assert_matches(0, 0, (0, 0, 0, 0), 0, 2,
                            post_aa63e_lookup_mask=1,
                            radio_sample=0)
        result = self.run_tail(0, 0, (0, 0, 0, 0), 0, 2,
                               post_aa63e_lookup_mask=1,
                               radio_sample=0)
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["unsupported_site"], 0)
        self.assertEqual(result["terminal_boundary"], 0xaa781)
        self.assertEqual(result["post_aa63e_poll_attempts"], 100)
        self.assertEqual(result["post_aa63e_poll_exhausted"], 1)
        self.assertEqual(result["tail_callbacks"], 225)

    def test_nonzero_entry_snapshot_matching_state_reaches_supported_suffix(self):
        self.assert_matches(0, 0, (0, 0, 0, 0), 0, 1,
                            post_aa63e_lookup_mask=1,
                            nested_32c_snapshot=1, pi_a7=0, pi_a4=0)
        result = self.run_tail(0, 0, (0, 0, 0, 0), 0, 1,
                               post_aa63e_lookup_mask=1,
                               nested_32c_snapshot=1, pi_a7=0, pi_a4=0)
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["terminal_boundary"], 0xaa781)
        self.assertEqual(result["events"][0]["site"], 0x9077c)
        self.assertIn({"kind": "PHY_READ", "domain": "PHY",
                       "site": 0xaa672, "reg": 0x000b, "table": 0,
                       "offset": 0, "count": 0, "width": 16,
                       "value": 0, "payload": []}, result["events"])
        self.assertFalse(any(event["site"] == 0xaa6a5
                             for event in result["events"]))
        self.assertEqual(result["post_aa63e_carriersearch_completed"], 1)

    def test_nonzero_entry_snapshot_runs_supported_rxcore_setstate(self):
        # This fixture supplies shared+0x31 != 0, which is the arm the pinned
        # function actually executes its register sequence on (the zero arm is
        # software-only), plus a synthetic set RFSEQ completion sample.  The
        # historical selected branch executes suspend, both force_rfseq calls
        # and the MAC enable before returning at 0xaa781.
        result = self.run_tail(0, 0, (0, 0, 0, 0), 1, 1,
                               post_aa63e_lookup_mask=1,
                               nested_32c_snapshot=1, pi_a7=1, pi_a4=1,
                               rfseq_sample=3)
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["terminal_boundary"], 0xaa781)
        sites = [event["site"] for event in result["events"]]
        self.assertIn(0x97d13, sites)
        self.assertIn(0x97d20, sites)
        self.assertIn(0x97d50, sites)
        self.assertIn(0x97e1d, sites)
        self.assertLess(sites.index(0x97d13), sites.index(0x97d20))
        self.assertGreater(sites.index(0x97e1d), sites.index(0x97e10))
        self.assertEqual(sites.count(0x97ca6), 2)
        rfseq_starts = [i for i, site in enumerate(sites) if site == 0x97c1f]
        self.assertEqual(len(rfseq_starts), 2)
        self.assertLess(rfseq_starts[0], rfseq_starts[1])
        self.assertEqual(result["callback_count"], 187)
        self.assertEqual(result["logical_event_count"], 140)
        self.assertEqual(result["unsupported_site"], 0)

    def test_carriersearch_selected_effects_match_independent_oracle(self):
        self.assert_matches(0, 0xc000, (0, 0, 0, 0), 0, 2,
                            post_aa63e_lookup_mask=1,
                            carriersearch_count=1,
                            nested_902=0x2468,
                            nested_904=0x9abc)
        result = self.run_tail(0, 0xc000, (0, 0, 0, 0), 0, 2,
                               post_aa63e_lookup_mask=1,
                               carriersearch_count=1,
                               nested_902=0x2468,
                               nested_904=0x9abc)
        self.assertEqual(result["terminal_boundary"], 0xaa781)
        self.assertEqual(result["post_aa63e_carriersearch_count_after"], 0)
        self.assertEqual(result["post_aa63e_carriersearch_effects_selected"], 1)
        self.assertEqual(result["post_aa63e_classifier_mask"], 6)
        self.assertEqual(result["tail_callbacks"], 42)
        self.assertEqual(result["logical_event_count"], 117)
        self.assertEqual(result["callback_count"], 156)

    def test_carriersearch_pi164_zero_uses_direct_per_core_writes(self):
        result = self.run_tail(0, 0xc000, (0, 0, 0, 0), 0, 2,
                               post_aa63e_lookup_mask=1,
                               carriersearch_count=1,
                               nested_902=0x2468,
                               nested_904=0x9abc,
                               profile_pi164=0)
        self.assertEqual(result["terminal_boundary"], 0xaa781)
        self.assertEqual(result["logical_event_count"], 101)
        self.assertEqual(result["callback_count"], 130)
        self.assertEqual(result["post_aa63e_0400_selected"], 0)
        carrier_start = next(i for i, event in enumerate(result["events"])
                             if event["site"] == 0x91273)
        self.assertEqual(
            result["events"][carrier_start:],
            expected_carriersearch_events(0, 0xc000, 0x2468, 0x9abc))
        direct = [event for event in result["events"]
                  if event["site"] == 0x97a16]
        self.assertEqual([(event["reg"], event["value"]) for event in direct],
                         [(0x06da, 0x2468), (0x08da, 0x2468)])

    def test_rxgainerr_true_branch_clears_nested_211_bytes(self):
        self.assert_matches(0, 0, (0, 0, 0, 0), 0, 1,
                            post_aa63e_lookup_mask=1,
                            rxgainerr_return=1)
        result = self.run_tail(0, 0, (0, 0, 0, 0), 0, 1,
                               post_aa63e_lookup_mask=1,
                               rxgainerr_return=1)
        self.assertEqual(result["post_aa63e_rxgainerr_return"], 1)
        self.assertEqual(result["nested_gainerr_state_after"],
                         [0, 0, 0xcc, 0xdd, 0xee])
        self.assertEqual(result["post_aa63e_sub8f086_returns"], [0, 0])

    def test_non_equal_phy_sample_keeps_explicit_nested_bytes_for_core_loop(self):
        result = self.run_tail(0, 0, (0, 0, 0, 0), 0, 2,
                               phy_0070=0x1234,
                               post_aa63e_lookup_mask=1)
        core_events = [event for event in result["events"]
                       if event["site"] == 0x8f414]
        self.assertEqual([event["reg"] for event in core_events],
                         [0x0644, 0x0644, 0x0844, 0x0844])
        self.assertEqual([event["value"] for event in core_events],
                         [0, 0, 0, 0])

    def test_initially_suspended_mac_is_still_delegated_when_change_requires_it(self):
        self.assert_matches(0, 0, (1, 2, 3, 4), 1, 0, "suspended")

    def test_unequal_phy_sample_preserves_the_alternate_join_counts(self):
        self.assert_matches(1, 0x4001, (1, 2, 3, 4), 1, 0,
                            "enabled", 0x1234)


if __name__ == "__main__":
    unittest.main()
