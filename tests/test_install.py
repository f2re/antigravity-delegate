from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "scripts" / "install_skill.py"


class InstallTests(unittest.TestCase):
    def test_repo_scope_install_and_refuse_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp) / "repo"
            repo.mkdir()
            first = subprocess.run(
                [
                    sys.executable,
                    str(INSTALLER),
                    "--scope",
                    "repo",
                    "--repo",
                    str(repo),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
            payload = json.loads(first.stdout)
            target = Path(payload["skill"])
            self.assertTrue((target / "SKILL.md").is_file())
            self.assertTrue((target / "scripts" / "antigravity_delegate.py").is_file())

            second = subprocess.run(
                [
                    sys.executable,
                    str(INSTALLER),
                    "--scope",
                    "repo",
                    "--repo",
                    str(repo),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            self.assertEqual(second.returncode, 1)
            self.assertEqual(json.loads(second.stdout)["status"], "ERROR")

            forced = subprocess.run(
                [
                    sys.executable,
                    str(INSTALLER),
                    "--scope",
                    "repo",
                    "--repo",
                    str(repo),
                    "--force",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            self.assertEqual(forced.returncode, 0, forced.stdout + forced.stderr)

    def test_dry_run_does_not_create_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp) / "repo"
            repo.mkdir()
            completed = subprocess.run(
                [
                    sys.executable,
                    str(INSTALLER),
                    "--scope",
                    "repo",
                    "--repo",
                    str(repo),
                    "--dry-run",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            self.assertEqual(completed.returncode, 0)
            payload = json.loads(completed.stdout)
            self.assertEqual(payload["status"], "DRY_RUN")
            self.assertFalse((repo / ".agents").exists())


if __name__ == "__main__":
    unittest.main()
