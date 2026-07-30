from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "skill" / "antigravity-delegate" / "scripts" / "antigravity_delegate.py"

spec = importlib.util.spec_from_file_location("antigravity_delegate", RUNNER)
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def run(args: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=cwd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
    )


class GitFixture:
    def __init__(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / "project"
        self.root.mkdir()
        self._git("init", "-b", "main")
        self._git("config", "user.email", "tests@example.invalid")
        self._git("config", "user.name", "Tests")
        (self.root / "README.md").write_text("fixture\n", encoding="utf-8")
        self._git("add", "README.md")
        self._git("commit", "-m", "initial")

    def _git(self, *args: str) -> subprocess.CompletedProcess[str]:
        completed = run(["git", "-C", str(self.root), *args])
        if completed.returncode != 0:
            raise AssertionError(completed.stderr)
        return completed

    def close(self) -> None:
        run(["git", "-C", str(self.root), "worktree", "prune"])
        self.temp.cleanup()


class DelegateUnitTests(unittest.TestCase):
    def test_timeout_parser(self) -> None:
        self.assertEqual(module.parse_timeout("30s"), 30)
        self.assertEqual(module.parse_timeout("15m"), 900)
        self.assertEqual(module.parse_timeout("1h"), 3600)
        with self.assertRaises(module.DelegateError):
            module.parse_timeout("9s")
        with self.assertRaises(module.DelegateError):
            module.parse_timeout("3h")
        with self.assertRaises(module.DelegateError):
            module.parse_timeout("15")

    def test_environment_is_sanitized(self) -> None:
        clean, removed = module.sanitize_environment(
            {
                "PATH": "/usr/bin",
                "HOME": "/tmp/home",
                "OPENAI_API_KEY": "secret",
                "MY_ACCESS_TOKEN": "secret",
                "LD_PRELOAD": "/tmp/evil.so",
                "GIT_CONFIG_COUNT": "1",
                "NORMAL": "value",
            }
        )
        self.assertEqual(clean["NORMAL"], "value")
        self.assertNotIn("OPENAI_API_KEY", clean)
        self.assertNotIn("MY_ACCESS_TOKEN", clean)
        self.assertNotIn("LD_PRELOAD", clean)
        self.assertNotIn("GIT_CONFIG_COUNT", clean)
        self.assertIn("OPENAI_API_KEY", removed)
        self.assertIn("MY_ACCESS_TOKEN", removed)

    def test_command_forces_sandbox_and_json(self) -> None:
        command = module.build_agy_command(
            agy_bin="agy",
            prompt="secret task",
            schema_path=Path("/tmp/schema.json"),
            timeout="15m",
            effort="high",
            model="gemini-test",
            agent=None,
            conversation=None,
        )
        self.assertIn("--sandbox", command)
        self.assertIn("--output-format", command)
        self.assertIn("json", command)
        self.assertNotIn("--dangerously-skip-permissions", command)
        self.assertEqual(module.redacted_command(command)[2], "<task redacted>")

    def test_prompt_contains_mode_restrictions(self) -> None:
        prompt = module.build_prompt("Проверить код", "security-reviewer", "read-only")
        self.assertIn("READ_ONLY", prompt)
        self.assertIn("Не изменяй", prompt)
        self.assertIn("не читай `.env`", prompt)


class DelegateIntegrationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture = GitFixture()
        self.fake_dir = Path(self.fixture.temp.name) / "bin"
        self.fake_dir.mkdir()
        self.fake_agy = self.fake_dir / "agy"
        self.fake_agy.write_text(
            textwrap.dedent(
                f"""\
                #!{sys.executable}
                import json
                from pathlib import Path
                import sys

                args = sys.argv[1:]
                if args == ["--version"]:
                    print("agy fake 1.1.8")
                    raise SystemExit(0)
                if args == ["models"]:
                    print("fake-model Fake Model")
                    raise SystemExit(0)
                prompt = args[args.index("-p") + 1]
                if "MALFORMED_OUTPUT" in prompt:
                    print("not-json")
                    raise SystemExit(0)
                if "WRITE_FILE_FOR_TEST" in prompt:
                    Path("generated.txt").write_text("generated\\n", encoding="utf-8")
                structured = {{
                    "summary": "ok",
                    "findings": [],
                    "changed_files": ["generated.txt"] if Path("generated.txt").exists() else [],
                    "tests": [],
                    "risks": [],
                    "next_steps": []
                }}
                print(json.dumps({{
                    "conversation_id": "fake-conversation",
                    "status": "SUCCESS",
                    "response": json.dumps(structured, ensure_ascii=False),
                    "structured_output": structured,
                    "duration_seconds": 0.01,
                    "num_turns": 1,
                    "usage": {{"total_tokens": 1}}
                }}, ensure_ascii=False))
                """
            ),
            encoding="utf-8",
        )
        self.fake_agy.chmod(self.fake_agy.stat().st_mode | stat.S_IXUSR)

    def tearDown(self) -> None:
        self.fixture.close()

    def invoke(self, *extra: str) -> subprocess.CompletedProcess[str]:
        return run(
            [
                sys.executable,
                str(RUNNER),
                "run",
                "--workspace",
                str(self.fixture.root),
                "--agy-bin",
                str(self.fake_agy),
                "--timeout",
                "30s",
                *extra,
            ]
        )

    def test_read_only_discards_agent_writes_and_reports_violation(self) -> None:
        completed = self.invoke(
            "--profile",
            "security-reviewer",
            "--mode",
            "read-only",
            "--task",
            "WRITE_FILE_FOR_TEST",
        )
        self.assertEqual(completed.returncode, 3, completed.stdout + completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["status"], "POLICY_VIOLATION")
        self.assertEqual(payload["changed_files"], ["generated.txt"])
        self.assertFalse((self.fixture.root / "generated.txt").exists())
        self.assertIsNone(payload["execution_worktree"])
        self.assertTrue(payload["safety"]["read_only_changes_detected"])

    def test_isolated_write_retains_separate_worktree(self) -> None:
        completed = self.invoke(
            "--profile",
            "implementation",
            "--mode",
            "isolated-write",
            "--task",
            "WRITE_FILE_FOR_TEST",
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["status"], "SUCCESS")
        self.assertTrue(payload["worktree_retained"])
        self.assertTrue(payload["branch"].startswith("agy/implementation-"))
        worktree = Path(payload["execution_worktree"])
        self.assertTrue((worktree / "generated.txt").is_file())
        self.assertFalse((self.fixture.root / "generated.txt").exists())
        self.assertEqual(module.git_status(self.fixture.root), [])
        run(["git", "-C", str(self.fixture.root), "worktree", "remove", "--force", str(worktree)])
        run(["git", "-C", str(self.fixture.root), "branch", "-D", payload["branch"]])

    def test_malformed_agent_output_is_reported_without_touching_source(self) -> None:
        completed = self.invoke(
            "--profile",
            "researcher",
            "--mode",
            "read-only",
            "--task",
            "MALFORMED_OUTPUT",
        )
        self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["status"], "AGENT_ERROR")
        self.assertEqual(payload["agy_result"]["status"], "ERROR")
        self.assertIn("not-json", payload["agy_result"]["raw_stdout_tail"])
        self.assertEqual(module.git_status(self.fixture.root), [])

    def test_write_mode_requires_implementation_profile(self) -> None:
        completed = self.invoke(
            "--profile",
            "test-auditor",
            "--mode",
            "isolated-write",
            "--task",
            "Проверить",
        )
        self.assertEqual(completed.returncode, 1)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["error"]["code"], "INVALID_ARGUMENT")

    def test_clean_write_run_removes_unused_worktree(self) -> None:
        completed = self.invoke(
            "--profile",
            "implementation",
            "--mode",
            "isolated-write",
            "--task",
            "NO_WRITE",
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertFalse(payload["worktree_retained"])
        self.assertIsNone(payload["execution_worktree"])
        self.assertIsNone(payload["branch"])

    def test_dirty_source_repository_is_rejected(self) -> None:
        (self.fixture.root / "dirty.txt").write_text("dirty", encoding="utf-8")
        completed = self.invoke(
            "--profile",
            "researcher",
            "--mode",
            "read-only",
            "--task",
            "Проверить",
        )
        self.assertEqual(completed.returncode, 1)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["error"]["code"], "DIRTY_REPOSITORY")
        self.assertIn("dirty.txt", payload["error"]["message"])

    def test_dry_run_does_not_call_agent(self) -> None:
        broken = self.fake_dir / "broken-agy"
        shutil.copy2(self.fake_agy, broken)
        broken.chmod(broken.stat().st_mode & ~stat.S_IXUSR)
        completed = run(
            [
                sys.executable,
                str(RUNNER),
                "run",
                "--workspace",
                str(self.fixture.root),
                "--agy-bin",
                str(self.fake_agy),
                "--profile",
                "researcher",
                "--mode",
                "read-only",
                "--task",
                "Проверить",
                "--dry-run",
            ]
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        payload = json.loads(completed.stdout)
        self.assertEqual(payload["status"], "DRY_RUN")
        self.assertEqual(payload["command"][2], "<task redacted>")


if __name__ == "__main__":
    unittest.main()
