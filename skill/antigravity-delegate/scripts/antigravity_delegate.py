#!/usr/bin/env python3
"""Проверяемый загрузчик безопасного runner Antigravity Delegate."""

from __future__ import annotations

import hashlib
from pathlib import Path

_EXPECTED_SHA256 = "a80b3d4c03e332b77c679071e52ea4c5cb6aaafb5994f0b1417fdac7beea59f1"
_PARTS_DIR = Path(__file__).resolve().parent / "_runner_parts"
_PARTS = sorted(_PARTS_DIR.glob("*.py.inc"))
if not _PARTS:
    raise RuntimeError(f"Не найдены части runner: {_PARTS_DIR}")
_SOURCE = "".join(path.read_text(encoding="utf-8") for path in _PARTS)
_ACTUAL_SHA256 = hashlib.sha256(_SOURCE.encode("utf-8")).hexdigest()
if _ACTUAL_SHA256 != _EXPECTED_SHA256:
    raise RuntimeError(
        "Нарушена целостность runner Antigravity Delegate: "
        f"ожидался {_EXPECTED_SHA256}, получен {_ACTUAL_SHA256}"
    )
exec(compile(_SOURCE, str(Path(__file__).resolve()), "exec"), globals(), globals())
