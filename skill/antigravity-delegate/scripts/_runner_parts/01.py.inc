        resolved = candidate.resolve()
        if not resolved.is_file():
            raise DelegateError(f"Исполняемый файл agy не найден: {resolved}", code="MISSING_DEPENDENCY")
        if os.name != "nt" and not os.access(resolved, os.X_OK):
            raise DelegateError(f"Файл agy не является исполняемым: {resolved}", code="MISSING_DEPENDENCY")
        return str(resolved)
    found = shutil.which(value)
    if not found:
        raise DelegateError(
            "Antigravity CLI (agy) не найден в PATH. Установите его и один раз выполните интерактивный вход.",
            code="MISSING_DEPENDENCY",
        )
    return found


def discover_repo(workspace: Path) -> tuple[Path, Path]:
    require_git()
    resolved = workspace.expanduser().resolve()
    if not resolved.exists() or not resolved.is_dir():
        raise DelegateError(f"Рабочий каталог не существует: {resolved}", code="INVALID_WORKSPACE")
    result = git(resolved, "rev-parse", "--show-toplevel")
    if result.returncode != 0:
        raise DelegateError("Рабочий каталог должен находиться внутри Git-репозитория.", code="INVALID_WORKSPACE")
    repo_root = Path(result.stdout.strip()).resolve()
    try:
        relative = resolved.relative_to(repo_root)
    except ValueError as exc:
        raise DelegateError("Не удалось определить путь внутри репозитория.", code="INVALID_WORKSPACE") from exc
    head = git(repo_root, "rev-parse", "--verify", "HEAD")
    if head.returncode != 0:
        raise DelegateError("Репозиторий должен содержать хотя бы один commit.", code="INVALID_WORKSPACE")
    return repo_root, relative


def git_status(repo: Path) -> list[str]:
    result = git(repo, "status", "--porcelain=v1", "--untracked-files=all")
    if result.returncode != 0:
        raise DelegateError(f"Не удалось получить git status: {result.stderr.strip()}", code="GIT_ERROR")
    return [line for line in result.stdout.splitlines() if line.strip()]


def require_clean_repo(repo: Path) -> None:
    dirty = git_status(repo)
    if dirty:
        preview = "\n".join(dirty[:20])
        suffix = "\n…" if len(dirty) > 20 else ""
        raise DelegateError(
            "Исходный репозиторий содержит незакоммиченные изменения. "
            "Runner намеренно не смешивает их с работой субагента:\n" + preview + suffix,
            code="DIRTY_REPOSITORY",
        )


def sanitize_name(value: str) -> str:
    normalized = SAFE_NAME_RE.sub("-", value.lower()).strip("-._")
    return normalized[:40] or "job"


def create_readonly_worktree(repo: Path) -> tuple[Path, Path]:
    parent = Path(tempfile.mkdtemp(prefix="antigravity-delegate-read-"))
    worktree = parent / "worktree"
    result = git(repo, "worktree", "add", "--detach", str(worktree), "HEAD", timeout=60)
    if result.returncode != 0:
        shutil.rmtree(parent, ignore_errors=True)
        raise DelegateError(f"Не удалось создать временный worktree: {result.stderr.strip()}", code="GIT_ERROR")
    return worktree, parent


def create_write_worktree(repo: Path, profile: str, root_override: Path | None) -> tuple[Path, str]:
    job_id = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%S") + "-" + secrets.token_hex(3)
    branch = f"agy/{sanitize_name(profile)}-{job_id}"
    root = (
        root_override.expanduser().resolve()
        if root_override
        else repo.parent / f".{repo.name}-agy-worktrees"
    )
    root.mkdir(parents=True, exist_ok=True)
    try:
        root.chmod(stat.S_IRWXU)
    except OSError:
        pass
    worktree = root / job_id
    result = git(repo, "worktree", "add", "-b", branch, str(worktree), "HEAD", timeout=60)
    if result.returncode != 0:
        raise DelegateError(f"Не удалось создать изолированный worktree: {result.stderr.strip()}", code="GIT_ERROR")
    return worktree, branch


def protect_worktree_metadata(worktree: Path) -> None:
    git_pointer = worktree / ".git"
    if not git_pointer.exists():
        return
    try:
        mode = git_pointer.stat().st_mode
        git_pointer.chmod(mode & ~(stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH))
    except OSError:
        pass


def remove_worktree(repo: Path, worktree: Path, *, branch: str | None = None) -> list[str]:
    warnings: list[str] = []
    result = git(repo, "worktree", "remove", "--force", str(worktree), timeout=60)
    if result.returncode != 0:
        warnings.append(f"Не удалось удалить worktree {worktree}: {result.stderr.strip()}")
    if branch:
        result = git(repo, "branch", "-D", branch, timeout=30)
        if result.returncode != 0:
            warnings.append(f"Не удалось удалить временную ветку {branch}: {result.stderr.strip()}")
    return warnings


def changed_files(worktree: Path) -> list[str]:
    lines = git_status(worktree)
    files: list[str] = []
    for line in lines:
        payload = line[3:] if len(line) >= 4 else line
        if " -> " in payload:
            payload = payload.split(" -> ", 1)[1]
        files.append(payload.strip().strip('"'))
    return files


def diff_check(worktree: Path) -> dict[str, Any]:
    result = git(worktree, "diff", "--check", timeout=60)
    return {
        "ok": result.returncode == 0,
        "output": (result.stdout + result.stderr).strip()[:4000],
    }


def sanitize_environment(source: Mapping[str, str] | None = None) -> tuple[dict[str, str], list[str]]:
    raw = dict(os.environ if source is None else source)
    clean: dict[str, str] = {}
    removed: list[str] = []
    for key, value in raw.items():
        upper = key.upper()
        sensitive = upper in SENSITIVE_ENV_EXACT or any(part in upper for part in SENSITIVE_ENV_PARTS)
        if sensitive:
            removed.append(key)
        else:
            clean[key] = value
    path_value = clean.get("PATH")
    if path_value:
        safe_entries: list[str] = []
        for entry in path_value.split(os.pathsep):
            if not entry:
                continue
            candidate = Path(entry).expanduser()
            if not candidate.is_absolute():
                continue
            normalized = str(candidate)
            if normalized not in safe_entries:
                safe_entries.append(normalized)
        clean["PATH"] = os.pathsep.join(safe_entries)
    clean.setdefault("NO_COLOR", "1")
    return clean, sorted(removed)


def load_task(task: str | None, task_file: Path | None) -> str:
    if bool(task) == bool(task_file):
        raise DelegateError("Укажите ровно один источник: --task или --task-file.", code="INVALID_ARGUMENT")
    if task_file:
        resolved = task_file.expanduser().resolve()
        if not resolved.is_file():
            raise DelegateError(f"Файл задания не найден: {resolved}", code="INVALID_ARGUMENT")
        content = resolved.read_text(encoding="utf-8")
    else:
        content = task or ""
    content = content.strip()
    if not content:
        raise DelegateError("Задание не может быть пустым.", code="INVALID_ARGUMENT")
    if "\x00" in content:
        raise DelegateError("Задание содержит недопустимый нулевой байт.", code="INVALID_ARGUMENT")
    if len(content) > MAX_TASK_CHARS:
        raise DelegateError(
            f"Задание превышает ограничение {MAX_TASK_CHARS} символов.", code="INVALID_ARGUMENT"
        )
    return content


