#!/usr/bin/env python3
"""Атомарный установщик Antigravity Delegate для Codex и Antigravity."""

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
TARGET_USER_ROOTS = {
    "codex": Path(".agents") / "skills",
    "antigravity-cli": Path(".gemini") / "antigravity-cli" / "skills",
    "antigravity-ide": Path(".gemini") / "config" / "skills",
}


class InstallError(RuntimeError):
    pass


def remove_path(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.exists():
        shutil.rmtree(path)


def atomic_install(
    source: Path,
    target: Path,
    *,
    mode: str,
    force: bool,
    dry_run: bool,
) -> None:
    occupied = target.exists() or target.is_symlink()
    if occupied and not force:
        raise InstallError(f"Каталог уже существует: {target}. Используйте --force для обновления.")
    if dry_run:
        return

    target.parent.mkdir(parents=True, exist_ok=True)
    staging_root = Path(tempfile.mkdtemp(prefix=f".{SKILL_NAME}-", dir=target.parent))
    staged = staging_root / SKILL_NAME
    backup = target.parent / f".{SKILL_NAME}.backup"

    try:
        if mode == "link":
            staged.symlink_to(source.resolve(), target_is_directory=True)
        else:
            shutil.copytree(source, staged)
            for script in (staged / "scripts").glob("*.py"):
                script.chmod(script.stat().st_mode | stat.S_IXUSR)

        if backup.exists() or backup.is_symlink():
            remove_path(backup)
        if occupied:
            target.rename(backup)
        staged.rename(target)
        if backup.exists() or backup.is_symlink():
            remove_path(backup)
    except Exception:
        if not target.exists() and not target.is_symlink() and (backup.exists() or backup.is_symlink()):
            backup.rename(target)
        raise
    finally:
        shutil.rmtree(staging_root, ignore_errors=True)


def destination_for(target_name: str, scope: str, repo: Path | None) -> Path:
    if scope == "repo":
        if repo is None:
            raise InstallError("Для --scope repo требуется --repo.")
        return repo.expanduser().resolve() / ".agents" / "skills" / SKILL_NAME

    home = Path.home()
    return home / TARGET_USER_ROOTS[target_name] / SKILL_NAME


def install_agents(
    skill_target: Path,
    scope: str,
    repo: Path | None,
    *,
    force: bool,
    dry_run: bool,
) -> list[str]:
    source_root = skill_target / "assets" / "antigravity-agents"
    if scope == "global":
        destination = Path.home() / ".gemini" / "config" / "agents"
    else:
        if repo is None:
            raise InstallError("Для workspace-агентов требуется --repo.")
        destination = repo.expanduser().resolve() / ".agents" / "agents"

    installed: list[str] = []
    for source in sorted(source_root.glob("*/agent.md")):
        target = destination / source.parent.name / "agent.md"
        if (target.exists() or target.is_symlink()) and not force:
            raise InstallError(f"Агент уже существует: {target}. Используйте --force.")
        installed.append(str(target))
        if not dry_run:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    return installed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Установить Antigravity Delegate для Codex или Antigravity"
    )
    parser.add_argument(
        "--target",
        choices=tuple(TARGET_USER_ROOTS),
        default="codex",
        help="Целевая среда пользовательской установки",
    )
    parser.add_argument("--scope", choices=("user", "repo"), default="user")
    parser.add_argument("--repo", type=Path, help="Корень проекта для scope=repo")
    parser.add_argument("--mode", choices=("copy", "link"), default="copy")
    parser.add_argument(
        "--install-agents",
        choices=("none", "global", "workspace"),
        default="none",
        help="Дополнительно установить профили Antigravity",
    )
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--pretty", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    repo_root = Path(__file__).resolve().parent.parent
    source = repo_root / "skills" / SKILL_NAME
    if not (source / "SKILL.md").is_file():
        raise InstallError(f"Исходный skill не найден: {source}")

    target = destination_for(args.target, args.scope, args.repo)
    atomic_install(
        source,
        target,
        mode=args.mode,
        force=args.force,
        dry_run=args.dry_run,
    )

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
        "target": args.target,
        "scope": args.scope,
        "mode": args.mode,
        "skill": str(target),
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
