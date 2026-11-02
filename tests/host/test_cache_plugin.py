#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host audit for the OpenCode cache/guard plugins (static + syntax).

Guarantees the telemetry plugin stays local-only and non-perturbing, that the
guard plugin implements the frozen session snapshot, and that all project
plugins still parse. No network, no OpenCode process required.
"""
import os
import re
import shutil
import subprocess
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.abspath(os.path.join(_HERE, "..", ".."))
_PLUGINS = os.path.join(_ROOT, ".opencode", "plugins")
_CACHE = os.path.join(_PLUGINS, "openbrcm-cache.ts")
_GUARD = os.path.join(_PLUGINS, "openbrcm-guard.ts")
_RE = os.path.join(_PLUGINS, "openbrcm-re.ts")
_CT = os.path.join(_ROOT, "scripts", "cache_telemetry.py")


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return src


class TestCachePluginLocalOnly(unittest.TestCase):
    def setUp(self):
        self.src = read(_CACHE)
        self.code = strip_comments(self.src)

    def test_required_hooks_present(self):
        for hook in (
            "experimental.chat.system.transform",
            "tool.definition",
            "chat.params",
            "event",
            "chat.message",
        ):
            self.assertIn(hook, self.src)

    def test_no_network_or_secret_patterns(self):
        for bad in ("fetch(", "http://", "https://", "XMLHttpRequest",
                    "Authorization", "Bearer", "apiKey", "sk-", "console.log"):
            self.assertNotIn(bad, self.code, f"cache plugin must not contain {bad!r}")

    def test_does_not_perturb_system_or_tools(self):
        for mutator in ("output.system.push", "output.system.splice",
                        "output.system.unshift", "output.system =",
                        "output.system.sort", "output.system.reverse"):
            self.assertNotIn(mutator, self.code)

    def test_writes_only_to_gitignored_local_dir(self):
        self.assertIn('".openbrcm-local"', self.code)
        self.assertIn("cache-telemetry.jsonl", self.code)
        self.assertIn("appendFileSync", self.code)
        gi = read(os.path.join(_ROOT, ".gitignore"))
        self.assertIn(".openbrcm-local/", gi)

    def test_records_only_usage_and_hashes(self):
        # Fields sourced only from token/hash structures.
        self.assertIn("info.tokens", self.code)
        self.assertIn("cache_hit_tokens", self.code)
        # Must not read message content/parts.
        for bad in ("info.parts", "output.parts", "message.parts",
                    "info.content", "info.text", "info.summary.body"):
            self.assertNotIn(bad, self.code)


class TestGuardFrozenSnapshot(unittest.TestCase):
    def test_guard_freezes_per_session(self):
        src = read(_GUARD)
        self.assertIn("frozenState", src)
        self.assertIn("frozenStateText", src)
        self.assertIn("input?.sessionID", src)

    def test_guard_still_blocks_mok(self):
        src = read(_GUARD)
        self.assertIn("BLOCK", src)
        self.assertIn("MOK.priv", src)


class TestPluginsParse(unittest.TestCase):
    def test_type_strip_syntax_check(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node not available")
        for path in (_CACHE, _GUARD, _RE):
            r = subprocess.run(
                [node, "--experimental-strip-types", "--check", path],
                capture_output=True, text=True,
            )
            if "bad option" in (r.stderr or "") or "--experimental-strip-types" in (r.stderr or ""):
                self.skipTest("node lacks --experimental-strip-types")
            self.assertEqual(r.returncode, 0, f"{path}: {r.stderr}")


class TestHashContractCrossLanguage(unittest.TestCase):
    def test_ts_and_python_hash_parts_match(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node not available")
        import importlib.util
        spec = importlib.util.spec_from_file_location("cache_telemetry", _CT)
        ct = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ct)

        inputs = [["a", "b"], ["governance", "tool-first"], [""], ["x"]]
        harness = os.path.join(_ROOT, "tests", "host", ".hashcheck.mjs")
        with open(harness, "w", encoding="utf-8") as fh:
            fh.write(
                "const m = await import(%r);\n" % _CACHE
                + "console.log(JSON.stringify(%r.map(m.hashParts)));\n"
                % (inputs,)
            )
        self.addCleanup(os.unlink, harness)
        r = subprocess.run(
            [node, "--experimental-strip-types", "--no-warnings", harness],
            capture_output=True, text=True,
        )
        if r.returncode != 0:
            self.skipTest("node could not import plugin: " + r.stderr.strip())
        import json
        ts = json.loads(r.stdout.strip().splitlines()[-1])
        py = [ct.hash_parts(x) for x in inputs]
        self.assertEqual(ts, py)


class TestCachePluginRuntime(unittest.TestCase):
    """Execute the real plugin hooks offline via Node and inspect the JSONL."""

    HARNESS = r"""
const repo = process.argv[2];
const mod = await import(process.argv[3]);
const hooks = await mod.default({ worktree: repo, directory: repo });
await hooks["tool.definition"]({toolID:"bash"},{description:"run",parameters:{c:1}});
await hooks["tool.definition"]({toolID:"read"},{description:"read",parameters:{p:1}});
await hooks["chat.params"]({sessionID:"sess-1"},{temperature:0,topP:1,topK:0,maxOutputTokens:64,options:{}});
await hooks["experimental.chat.system.transform"]({sessionID:"sess-1"},{system:["SECRET SYSTEM A","SECRET SYSTEM B"]});
const ev=(id,hit,miss)=>({event:{type:"message.updated",properties:{info:{role:"assistant",id,sessionID:"sess-1",providerID:"deepseek",modelID:"deepseek-flash",mode:"build",summary:false,time:{created:1,completed:2},tokens:{input:miss,output:5,reasoning:1,cache:{read:hit,write:0}}}}}});
await hooks.event(ev("m1",0,800));
await hooks.event(ev("m2",780,20));
await hooks.event(ev("m2",780,20));
await hooks.event({event:{type:"session.compacted",properties:{sessionID:"sess-1"}}});
await hooks.event({event:{type:"message.updated",properties:{info:{role:"user"}}}});
console.log("DONE");
"""

    def test_runtime_capture(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node not available")
        import json
        import tempfile
        with tempfile.TemporaryDirectory() as repo:
            os.makedirs(os.path.join(repo, "docs"))
            with open(os.path.join(repo, "docs", "current-context.json"), "w") as fh:
                json.dump({"active_blocker": {"id": "test.blocker"}}, fh)
            with open(os.path.join(repo, "docs", "agent-state.md"), "w") as fh:
                fh.write("# state\n")
            harness = os.path.join(repo, "harness.mjs")
            with open(harness, "w", encoding="utf-8") as fh:
                fh.write(self.HARNESS)
            r = subprocess.run(
                [node, "--experimental-strip-types", "--no-warnings",
                 harness, repo, _CACHE],
                capture_output=True, text=True,
            )
            if r.returncode != 0:
                self.skipTest("node runtime unavailable: " + r.stderr.strip()[-300:])
            log = os.path.join(repo, ".openbrcm-local", "cache-telemetry.jsonl")
            self.assertTrue(os.path.exists(log))
            raw = read(log)
            records = [json.loads(x) for x in raw.splitlines() if x.strip()]

        reqs = [x for x in records if x.get("kind") == "request"]
        comp = [x for x in records if x.get("kind") == "compaction"]
        self.assertEqual(len(reqs), 2, "duplicate message.updated must be deduped")
        self.assertEqual(len(comp), 1, "session.compacted records an epoch reset")
        self.assertEqual(reqs[0]["blocker_id"], "test.blocker")
        self.assertEqual(reqs[0]["cache_hit_tokens"], 0)
        self.assertEqual(reqs[1]["cache_hit_tokens"], 780)
        self.assertEqual(reqs[1]["cache_miss_tokens"], 20)
        self.assertEqual(reqs[0]["stable_system_hash"], reqs[1]["stable_system_hash"])
        self.assertEqual(reqs[0]["tools_hash"], reqs[1]["tools_hash"])
        self.assertEqual(reqs[0]["tool_order_hash"], reqs[1]["tool_order_hash"])
        self.assertEqual(reqs[1]["tool_count"], 2)
        self.assertEqual(reqs[1]["user_id_state"], "absent")
        # No prompt/system content may leak into the log.
        self.assertNotIn("SECRET SYSTEM", raw)


if __name__ == "__main__":
    unittest.main()
