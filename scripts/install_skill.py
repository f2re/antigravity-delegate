#!/usr/bin/env python3
"""Установщик skill antigravity-delegate для Codex."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import stat
import sys
import tempfile
from typing import Sequence


SKILL_NAME = "antigravity-delegate"


class InstallError(RuntimeError):
    pass


def atomic_copytree(source: Path, target: Path, *, force: bool, dry_run: bool) -> None:
    if target.exists() and not force:
        raise InstallError(f"Каталог уже существует: {target}. Используйте --force для обновления.")
    if dry_run:
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    staging_root = Path(tempfile.mkdtemp(prefix=f".{SKILL_NAME}-", dir=target.parent))
    staged = staging_root / SKILL_NAME
    backup = target.parent / f".{SKILL_NAME}.backup"
    try:
        shutil.copytree(source, staged)
        for script in (staged / "scripts").glob("*.py"):
            script.chmod(script.stat().st_mode | stat.S_IXUSR)
        if backup.exists():
            shutil.rmtree(backup)
        if target.exists():
            target.rename(backup)
        staged.rename(target)
        if backup.exists():
            shutil.rmtree(backup)
    except Exception:
        if not target.exists() and backup.exists():
            backup.rename(target)
        raise
    finally:
        shutil.rmtree(staging_root, ignore_errors=True)


def install_agents(skill_target: Path, scope: str, repo: Path | None, *, force: bool, dry_run: bool) -> list[str]:
    source_root = skill_target / "assets" / "antigravity-agents"
    if scope == "global":
        destination = Path.home() / ".gemini" / "config" / "agents"
    else:
        if repo is None:
            raise InstallError("Для workspace-агентов требуется --repo.")
        destination = repo.resolve() / ".agents" / "agents"
    installed: list[str] = []
    for source in sorted(source_root.glob("*/agent.md")):
        target = destination / source.parent.name / "agent.md"
        if target.exists() and not force:
            raise InstallError(f"Агент уже существует: {target}. Используйте --force.")
        installed.append(str(target))
        if not dry_run:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    return installed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Установить antigravity-delegate для Codex")
    parser.add_argument("--scope", choices=("user", "repo"), default="user")
    parser.add_argument("--repo", type=Path, help="Корень целевого репозитория для scope=repo")
    parser.add_argument("--install-agents", choices=("none", "global", "workspace"), default="none")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--pretty", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = Path(__file__).resolve().parent.parent
    source = root / "skill" / SKILL_NAME
    if not (source / "SKILL.md").is_file():
        raise InstallError(f"Исходный skill не найден: {source}")
    if args.scope == "user":
        target = Path.home() / ".agents" / "skills" / SKILL_NAME
    else:
        if args.repo is None:
            raise InstallError("Для --scope repo требуется --repo.")
        target = args.repo.expanduser().resolve() / ".agents" / "skills" / SKILL_NAME
    atomic_copytree(source, target, force=args.force, dry_run=args.dry_run)
    agents: list[str] = []
    if args.install_agents != "none":
        agents = install_agents(
            source if args.dry_run else target,
            args.install_agents,
            args.repo,
            force=args.force,
            dry_run=args.dry_run,
        )
    payload = {
        "status": "DRY_RUN" if args.dry_run else "SUCCESS",
        "skill": str(target),
        "scope": args.scope,
        "agents": agents,
    }
    json.dump(payload, sys.stdout, ensure_ascii=False, indent=2 if args.pretty else None)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except InstallError as exc:
        json.dump({"status": "ERROR", "error": str(exc)}, sys.stdout, ensure_ascii=False)
        sys.stdout.write("\n")
        raise SystemExit(1)
