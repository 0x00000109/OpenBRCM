#!/usr/bin/env python3
"""Host-only checks for the project-local Codex research preflight hook."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
HOOK = ROOT / ".codex" / "hooks" / "research-session-start.py"
HOOK_CONFIG = ROOT / ".codex" / "hooks.json"
LEDGER = ROOT / "docs" / "artifact-ledger.json"


def run_hook(event: dict) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(HOOK)],
        input=json.dumps(event),
        text=True,
        capture_output=True,
        cwd=ROOT,
        check=False,
    )


class ResearchCodexHookTests(unittest.TestCase):
    def test_project_hook_config_uses_supported_session_start_schema(self):
        config = json.loads(HOOK_CONFIG.read_text())
        groups = config["hooks"]["SessionStart"]
        self.assertEqual(len(groups), 1)
        self.assertEqual(groups[0]["matcher"], "startup|resume|clear|compact")
        handler = groups[0]["hooks"][0]
        self.assertEqual(handler["type"], "command")
        self.assertIn("research-session-start.py", handler["command"])
        self.assertEqual(handler["timeout"], 20)

    def test_session_start_supplies_registry_context_without_mutating_ledger(self):
        before = hashlib.sha256(LEDGER.read_bytes()).hexdigest()
        result = run_hook({"hook_event_name": "SessionStart", "source": "startup", "cwd": str(ROOT)})
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        context = payload["hookSpecificOutput"]["additionalContext"]
        self.assertIn("read AGENTS.md and docs/current-context.json", context)
        self.assertIn("research_registry=docs/artifact-ledger.json entries=", context)
        self.assertIn("status=PASS", context)
        self.assertEqual(hashlib.sha256(LEDGER.read_bytes()).hexdigest(), before)

    def test_session_start_reports_registry_and_historical_source_freshness_separately(self):
        result = run_hook({"hook_event_name": "SessionStart", "source": "startup", "cwd": str(ROOT)})
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        context = payload["hookSpecificOutput"]["additionalContext"]
        self.assertIn("research_registry=docs/artifact-ledger.json", context)
        self.assertRegex(context, r"current_context_freshness=(PASS|FAIL_CLOSED)")

    def test_session_outside_repository_is_ignored(self):
        result = run_hook({"hook_event_name": "SessionStart", "cwd": "/tmp"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "")

    def test_malformed_event_warns_without_blocking_session_start(self):
        result = subprocess.run(
            [sys.executable, str(HOOK)], input="not-json", text=True,
            capture_output=True, cwd=ROOT, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertIn("could not read Codex hook input", payload["systemMessage"])


if __name__ == "__main__":
    unittest.main()
