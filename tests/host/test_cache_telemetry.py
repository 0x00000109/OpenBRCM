#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host tests for the OpenBRCM DeepSeek cache layer (offline, deterministic).

Covers the canonical cache-telemetry model: hash contract, frozen-epoch
snapshot semantics, append-only STATE DELTA rendering, JSONL parsing, aggregate
hit/miss maths, graceful handling of zero-hit / missing / partial usage,
volatility detection, compaction epoch reset and the mock benchmark path.
"""
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.abspath(os.path.join(_HERE, "..", ".."))
_SCRIPTS = os.path.join(_ROOT, "scripts")

_spec = importlib.util.spec_from_file_location(
    "cache_telemetry", os.path.join(_SCRIPTS, "cache_telemetry.py")
)
ct = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ct)

_BS = os.path.join(_SCRIPTS, "deepseek-cache-benchmark.py")


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def rec(**over):
    r = {
        "kind": "request",
        "seq": 1,
        "session": "sess0001",
        "blocker_id": "b1",
        "request_kind": "agent",
        "model": "deepseek/deepseek-flash",
        "protocol": "openai-chat-completions",
        "prompt_tokens": 82537,
        "input_tokens": 617,
        "cache_hit_tokens": 81920,
        "cache_miss_tokens": 617,
        "cache_hit_ratio": 81920 / 82537,
        "output_tokens": 325,
        "reasoning_tokens": 1475,
        "stable_system_hash": "sys0001",
        "tools_hash": "tools01",
        "tool_order_hash": "order01",
        "frozen_context_hash": "frz0001",
        "current_context_file_hash": "ctx0001",
        "model_fingerprint": "mfp0001",
        "user_id_state": "absent",
        "compaction_generation": 0,
    }
    r.update(over)
    return r


class TestHashes(unittest.TestCase):
    def test_hash_parts_deterministic(self):
        self.assertEqual(ct.hash_parts(["a", "b"]), ct.hash_parts(["a", "b"]))
        self.assertNotEqual(ct.hash_parts(["a", "b"]), ct.hash_parts(["b", "a"]))

    def test_stable_system_hash_is_stable(self):
        sys_a = ["governance", "tool-first"]
        self.assertEqual(ct.stable_system_hash(sys_a), ct.stable_system_hash(sys_a))

    def test_tail_does_not_alter_prefix_hash(self):
        prefix = ["governance", "evidence rules", "one blocker"]
        h1 = ct.stable_system_hash(prefix)
        # a dynamic tail (timestamp / turn) is a SEPARATE part and must never be
        # folded into the stable prefix hash.
        tail = prefix + ["2026-09-25T00:00:00Z turn=7"]
        self.assertNotEqual(ct.stable_system_hash(tail), h1)
        self.assertEqual(ct.stable_system_hash(prefix), h1)

    def test_tools_hash_order_independent_and_deterministic(self):
        a = [("read", "h1"), ("bash", "h2")]
        b = [("bash", "h2"), ("read", "h1")]
        self.assertEqual(ct.tools_hash(a), ct.tools_hash(b))
        self.assertEqual(ct.tools_hash(a), ct.tools_hash(a))

    def test_tool_order_hash_order_sensitive(self):
        self.assertNotEqual(
            ct.tool_order_hash(["read", "bash"]),
            ct.tool_order_hash(["bash", "read"]),
        )


class TestFrozenEpoch(unittest.TestCase):
    def test_changing_head_does_not_replace_snapshot(self):
        store = ct.FrozenEpochStore()
        holder = {"ctx": b"v1", "state": b"s1"}
        snap1 = store.snapshot("sess", "b1", lambda: (holder["ctx"], holder["state"]))
        holder["ctx"] = b"v2"  # HEAD/docs changed mid-blocker
        snap2 = store.snapshot("sess", "b1", lambda: (holder["ctx"], holder["state"]))
        self.assertIs(snap1, snap2)
        self.assertEqual(snap2["current_context_sha256"], ct.sha256_hex(b"v1"))

    def test_next_session_gets_new_snapshot(self):
        store = ct.FrozenEpochStore()
        snap1 = store.snapshot("sess1", "b1", lambda: (b"v1", b"s1"))
        snap2 = store.snapshot("sess2", "b1", lambda: (b"v2", b"s2"))
        self.assertNotEqual(snap1["current_context_sha256"], snap2["current_context_sha256"])

    def test_blocker_change_starts_new_epoch(self):
        store = ct.FrozenEpochStore()
        snap1 = store.snapshot("sess1", "b1", lambda: (b"v1", b"s1"))
        snap2 = store.snapshot("sess1", "b2", lambda: (b"v2", b"s2"))
        self.assertNotEqual(snap1["current_context_sha256"], snap2["current_context_sha256"])

    def test_state_delta_is_append_only_text(self):
        store = ct.FrozenEpochStore()
        store.snapshot("sess1", "b1", lambda: (b"v1", b"s1"))
        delta = store.state_delta({
            "commit": ("A", "B"),
            "facts": {"d4.foo": ("UNKNOWN", "PROVEN")},
            "blockers": {"d4.foo": ("OPEN", "CLOSED")},
        })
        self.assertIn("STATE DELTA:", delta)
        self.assertIn("commit: A -> B", delta)
        self.assertIn("fact d4.foo: UNKNOWN -> PROVEN", delta)
        self.assertIn("blocker d4.foo: OPEN -> CLOSED", delta)
        # prior frozen snapshot is unchanged by appending a delta
        self.assertEqual(
            store.snapshot("sess1", "b1", lambda: (b"MUTATED", b"MUTATED"))[
                "current_context_sha256"
            ],
            ct.sha256_hex(b"v1"),
        )


class TestParsingAndAggregation(unittest.TestCase):
    def _write(self, lines):
        fd, path = tempfile.mkstemp(suffix=".jsonl")
        with os.fdopen(fd, "w") as fh:
            fh.write("\n".join(lines))
        self.addCleanup(os.unlink, path)
        return path

    def test_jsonl_parser_skips_malformed(self):
        path = self._write([
            json.dumps(rec(seq=1)),
            "not json{{{",
            "",
            json.dumps(rec(seq=2, cache_hit_tokens=1, cache_miss_tokens=9)),
        ])
        records = ct.load_records(path)
        self.assertEqual(len(records), 2)

    def test_aggregate_hit_miss(self):
        records = [rec(seq=1), rec(seq=2, cache_hit_tokens=1, cache_miss_tokens=9)]
        s = ct.summarize(records)
        self.assertEqual(s["total_requests"], 2)
        self.assertEqual(s["total_cache_hit_tokens"], 81921)
        self.assertEqual(s["total_cache_miss_tokens"], 626)
        self.assertAlmostEqual(s["aggregate_hit_ratio"], 81921 / (81921 + 626))

    def test_zero_hit_request(self):
        s = ct.summarize([rec(cache_hit_tokens=0, cache_miss_tokens=100)])
        self.assertEqual(s["aggregate_hit_ratio"], 0.0)

    def test_missing_usage_fields_graceful(self):
        r = rec()
        for k in ("cache_hit_tokens", "cache_miss_tokens", "input_tokens",
                  "output_tokens", "reasoning_tokens"):
            r.pop(k)
        s = ct.summarize([r])
        self.assertEqual(s["total_requests"], 1)
        self.assertIsNone(s["aggregate_hit_ratio"])

    def test_partial_streaming_record_not_double_counted(self):
        # The plugin only records when time.completed + tokens exist; a partial
        # line lacks tokens and must not inflate totals.
        records = [rec(seq=1), {"kind": "request", "seq": 2, "session": "sess0001"}]
        s = ct.summarize(records)
        self.assertEqual(s["total_requests"], 2)
        self.assertEqual(s["total_cache_hit_tokens"], 81920)

    def test_first_vs_subsequent(self):
        records = [
            rec(seq=1, cache_hit_tokens=0, cache_miss_tokens=800),
            rec(seq=2, cache_hit_tokens=760, cache_miss_tokens=40),
        ]
        s = ct.summarize(records)
        self.assertEqual(s["first_request_hit_ratio"], 0.0)
        self.assertAlmostEqual(s["subsequent_hit_ratio"], 760 / 800)


class TestVolatility(unittest.TestCase):
    def test_user_id_volatility(self):
        records = [
            rec(seq=1, user_id_state="absent"),
            rec(seq=2, user_id_state="present"),
        ]
        self.assertEqual(ct.detect_user_id_volatility(records), ["absent", "present"])
        self.assertEqual(ct.detect_user_id_volatility([rec()]), ["absent"])

    def test_tool_order_volatility(self):
        self.assertFalse(ct.detect_tool_order_volatility([rec(seq=1), rec(seq=2)]))
        self.assertTrue(ct.detect_tool_order_volatility(
            [rec(seq=1, tool_order_hash="a"), rec(seq=2, tool_order_hash="b")]
        ))

    def test_compaction_detection(self):
        records = [
            rec(seq=1),
            {"kind": "compaction", "seq": 2, "session": "sess0001",
             "compaction_generation": 1, "epoch_reset": True},
            rec(seq=3, compaction_generation=1),
        ]
        self.assertEqual(ct.detect_compaction_events(records), [2])

    def test_fragmentation_flags_structural_change(self):
        records = [
            rec(seq=1, cache_hit_tokens=800, cache_miss_tokens=200),
            rec(seq=2, cache_hit_tokens=50, cache_miss_tokens=950,
                current_context_file_hash="ctx0002"),
        ]
        frag = ct.detect_fragmentation(records)
        self.assertEqual(len(frag), 1)
        self.assertIn("CONTEXT_SNAPSHOT_CHANGED", frag[0]["structural_changes"])
        self.assertEqual(frag[0]["causality"], "NOT_CLAIMED")


class TestNoSecretsInSchema(unittest.TestCase):
    ALLOWED = {
        "ts", "kind", "seq", "session", "blocker_id", "request_kind", "mode",
        "protocol", "provider", "model", "prompt_tokens", "input_tokens",
        "cache_hit_tokens", "cache_miss_tokens", "cache_hit_ratio",
        "output_tokens", "reasoning_tokens", "system_fingerprint",
        "stable_system_hash", "tools_hash", "tool_order_hash", "tool_count",
        "frozen_context_hash", "current_context_file_hash", "model_fingerprint",
        "user_id_state", "compaction_generation", "note", "epoch_reset",
    }

    def test_record_has_no_prompt_or_secret_fields(self):
        blob = json.dumps(rec())
        self.assertNotIn("Authorization", blob)
        self.assertNotIn("Bearer", blob)
        self.assertNotIn("sk-", blob)
        self.assertNotIn("apiKey", blob)
        self.assertNotIn("messages", blob)

    def test_record_schema_is_bounded(self):
        self.assertTrue(set(rec().keys()).issubset(self.ALLOWED))


class TestBenchmarkMock(unittest.TestCase):
    def test_mock_runs_and_emits_three_probes(self):
        r = subprocess.run(
            [sys.executable, _BS, "--mock", "--max-requests", "3"],
            capture_output=True, text=True,
        )
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("STABLE-PREFIX", read(_BS))
        self.assertIn("probe", r.stdout)
        self.assertGreaterEqual(r.stdout.count("A "), 1)

    def test_live_without_key_reports_not_run(self):
        env = dict(os.environ)
        env.pop("DEEPSEEK_API_KEY", None)
        r = subprocess.run(
            [sys.executable, _BS], capture_output=True, text=True, env=env,
        )
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("LIVE BENCHMARK NOT RUN", r.stdout)


if __name__ == "__main__":
    unittest.main()
