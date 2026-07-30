from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CANONICAL = ROOT / "skills" / "antigravity-delegate"
LEGACY = ROOT / "skill" / "antigravity-delegate"
PLUGIN_AGENTS = ROOT / "agents"
EMBEDDED_AGENTS = CANONICAL / "assets" / "antigravity-agents"
INSTALLER_PATH = ROOT / "scripts" / "install_skill.py"


def relative_files(root: Path) -> list[Path]:
    return sorted(
        path.relative_to(root)
        for path in root.rglob("*")
        if path.is_file()
        and "__pycache__" not in path.parts
        and path.suffix not in {".pyc", ".pyo"}
    )


class PackagingTests(unittest.TestCase):
    def test_codex_plugin_manifest(self) -> None:
        manifest = json.loads((ROOT / ".codex-plugin" / "plugin.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["name"], "antigravity-delegate")
        self.assertEqual(manifest["skills"], "./skills/")
        self.assertEqual(manifest["version"], "0.2.0")

        marketplace = json.loads(
            (ROOT / ".agents" / "plugins" / "marketplace.json").read_text(encoding="utf-8")
        )
        self.assertEqual(marketplace["plugins"][0]["name"], "antigravity-delegate")
        self.assertEqual(marketplace["plugins"][0]["source"]["path"], "./")

    def test_antigravity_plugin_manifest(self) -> None:
        manifest = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8"))
        self.assertEqual(
            manifest["$schema"],
            "https://antigravity.google/schemas/v1/plugin.json",
        )
        self.assertEqual(manifest["name"], "antigravity-delegate")
        self.assertEqual(set(manifest), {"$schema", "name", "description"})

    def test_canonical_skill_is_complete(self) -> None:
        self.assertTrue((CANONICAL / "SKILL.md").is_file())
        self.assertTrue((CANONICAL / "scripts" / "antigravity_delegate.py").is_file())
        self.assertTrue((CANONICAL / "schemas" / "delegated-result.schema.json").is_file())
        self.assertTrue((CANONICAL / "agents" / "openai.yaml").is_file())
        self.assertTrue((CANONICAL / "SKILL.md").read_text(encoding="utf-8").startswith("---\n"))

    def test_legacy_layout_is_exact_mirror(self) -> None:
        canonical_files = relative_files(CANONICAL)
        legacy_files = relative_files(LEGACY)
        self.assertEqual(canonical_files, legacy_files)
        for relative in canonical_files:
            self.assertEqual(
                (CANONICAL / relative).read_bytes(),
                (LEGACY / relative).read_bytes(),
                str(relative),
            )

    def test_antigravity_plugin_agents_match_embedded_profiles(self) -> None:
        plugin_files = relative_files(PLUGIN_AGENTS)
        embedded_files = relative_files(EMBEDDED_AGENTS)
        self.assertEqual(plugin_files, embedded_files)
        for relative in plugin_files:
            self.assertEqual(
                (PLUGIN_AGENTS / relative).read_bytes(),
                (EMBEDDED_AGENTS / relative).read_bytes(),
                str(relative),
            )

    def test_installer_resolves_all_supported_targets(self) -> None:
        spec = importlib.util.spec_from_file_location("install_skill", INSTALLER_PATH)
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            self.assertEqual(
                module.destination_for("codex", "repo", repo),
                repo / ".agents" / "skills" / "antigravity-delegate",
            )

        self.assertEqual(
            module.TARGET_USER_ROOTS["codex"],
            Path(".agents") / "skills",
        )
        self.assertEqual(
            module.TARGET_USER_ROOTS["antigravity-cli"],
            Path(".gemini") / "antigravity-cli" / "skills",
        )
        self.assertEqual(
            module.TARGET_USER_ROOTS["antigravity-ide"],
            Path(".gemini") / "config" / "skills",
        )


if __name__ == "__main__":
    unittest.main()
