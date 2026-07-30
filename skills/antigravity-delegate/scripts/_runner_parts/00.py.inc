#!/usr/bin/env python3
"""Безопасный мост Codex -> Google Antigravity CLI.

Модуль не использует внешние зависимости и рассчитан на Python 3.10+.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from typing import Any, Iterable, Mapping, Sequence


VERSION = "0.1.0"
SCHEMA_VERSION = 1
DEFAULT_TIMEOUT = "15m"
MAX_TIMEOUT_SECONDS = 2 * 60 * 60
MAX_TASK_CHARS = 100_000
TERMINAL_SUCCESS = "SUCCESS"

PROFILE_INSTRUCTIONS: dict[str, str] = {
    "architecture-reviewer": (
        "Проведи независимый аудит архитектуры и фактических связей между модулями. "
        "Проверяй границы ответственности, связность, дублирование, обратную совместимость, "
        "обработку ошибок и технический долг. Для каждого вывода указывай путь и строку."
    ),
    "security-reviewer": (
        "Проведи безопасный аудит кода, зависимостей, прав доступа, обработки входных данных, "
        "секретов и опасных операций. Не читай реальные секреты и не выполняй эксплуатацию. "
        "Для каждого риска указывай доказательство, воздействие и минимальное исправление."
    ),
    "test-auditor": (
        "Сопоставь требования, реализацию, тесты и CI. Найди непротестированные ветви, "
        "ложноположительные проверки и невоспроизводимые сценарии. Отделяй пробел покрытия "
        "от подтверждённого дефекта."
    ),
    "researcher": (
        "Проведи техническое исследование. Предпочитай официальную документацию, спецификации "
        "и исходный код. Разделяй подтверждённые факты, выводы и предположения; укажи источники "
        "и то, что подтвердить не удалось."
    ),
    "implementation": (
        "Реализуй только ограниченную поставленную задачу в изолированном git worktree. "
        "Следуй существующему стилю и принципу минимального изменения. Не выполняй commit, "
        "push, deploy, производственные миграции и разрушительные команды."
    ),
}

SENSITIVE_ENV_EXACT = {
    "ANTHROPIC_API_KEY",
    "AWS_ACCESS_KEY_ID",
    "AWS_SECRET_ACCESS_KEY",
    "AWS_SESSION_TOKEN",
    "AZURE_CLIENT_SECRET",
    "CI_JOB_TOKEN",
    "DATABASE_URL",
    "DOCKER_AUTH_CONFIG",
    "GITHUB_TOKEN",
    "GH_TOKEN",
    "GOOGLE_APPLICATION_CREDENTIALS",
    "NPM_TOKEN",
    "OPENAI_API_KEY",
    "PGPASSWORD",
    "PYPI_TOKEN",
    "SSH_AUTH_SOCK",
    "BASH_ENV",
    "ENV",
    "GIT_ASKPASS",
    "GIT_CONFIG_COUNT",
    "GIT_CONFIG_PARAMETERS",
    "GIT_DIR",
    "GIT_EXTERNAL_DIFF",
    "GIT_INDEX_FILE",
    "GIT_OBJECT_DIRECTORY",
    "GIT_WORK_TREE",
    "LD_PRELOAD",
    "NODE_OPTIONS",
    "PYTHONSTARTUP",
    "SSH_ASKPASS",
}
SENSITIVE_ENV_PARTS = ("PASSWORD", "PASSPHRASE", "PRIVATE_KEY", "ACCESS_TOKEN", "API_TOKEN")
TIMEOUT_RE = re.compile(r"^(?P<value>[1-9][0-9]*)(?P<unit>[smh])$")
SAFE_NAME_RE = re.compile(r"[^a-z0-9._-]+")


class DelegateError(RuntimeError):
    """Контролируемая ошибка runner."""

    def __init__(self, message: str, *, code: str = "RUNNER_ERROR") -> None:
        super().__init__(message)
        self.code = code


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")


def parse_timeout(value: str) -> int:
    match = TIMEOUT_RE.fullmatch(value.strip())
    if not match:
        raise DelegateError("Тайм-аут должен иметь вид 30s, 10m или 1h.", code="INVALID_ARGUMENT")
    amount = int(match.group("value"))
    multiplier = {"s": 1, "m": 60, "h": 3600}[match.group("unit")]
    seconds = amount * multiplier
    if seconds < 10:
        raise DelegateError("Минимальный тайм-аут — 10 секунд.", code="INVALID_ARGUMENT")
    if seconds > MAX_TIMEOUT_SECONDS:
        raise DelegateError("Максимальный тайм-аут — 2 часа.", code="INVALID_ARGUMENT")
    return seconds


def run_process(
    args: Sequence[str],
    *,
    cwd: Path | None = None,
    timeout: float = 30,
    env: Mapping[str, str] | None = None,
    stdin_devnull: bool = True,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(args),
        cwd=str(cwd) if cwd else None,
        env=dict(env) if env is not None else None,
        stdin=subprocess.DEVNULL if stdin_devnull else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        check=False,
    )


def git(repo: Path, *args: str, timeout: float = 30) -> subprocess.CompletedProcess[str]:
    hooks_dir = Path(tempfile.gettempdir()) / "antigravity-delegate-empty-hooks"
    hooks_dir.mkdir(parents=True, exist_ok=True)
    environment, _ = sanitize_environment()
    environment["GIT_LFS_SKIP_SMUDGE"] = "1"
    environment["GIT_PAGER"] = "cat"
    command = [
        "git",
        "--no-pager",
        "-c",
        f"core.hooksPath={hooks_dir}",
        "-c",
        "core.fsmonitor=false",
        "-c",
        "diff.external=",
        "-c",
        "credential.helper=",
        "-C",
        str(repo),
        *args,
    ]
    return run_process(command, timeout=timeout, env=environment)


def require_git() -> str:
    binary = shutil.which("git")
    if not binary:
        raise DelegateError("Git не найден в PATH.", code="MISSING_DEPENDENCY")
    return binary


def resolve_binary(value: str) -> str:
    candidate = Path(value).expanduser()
    if candidate.parent != Path(".") or candidate.is_absolute():
