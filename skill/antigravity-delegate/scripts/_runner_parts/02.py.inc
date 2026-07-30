def build_prompt(task: str, profile: str, mode: str) -> str:
    profile_text = PROFILE_INSTRUCTIONS[profile]
    mode_text = (
        "Режим READ_ONLY. Ты работаешь в одноразовом detached git worktree. "
        "Не изменяй, не создавай, не удаляй и не переименовывай файлы. Любая запись будет "
        "зафиксирована как нарушение политики и отброшена."
        if mode == "read-only"
        else
        "Режим ISOLATED_WRITE. Ты работаешь в отдельной ветке и отдельном git worktree. "
        "Можно менять только файлы, необходимые для задачи. Не выполняй commit, push, merge, "
        "reset, clean, deploy и производственные миграции."
    )
    return f"""Ты — внешний субагент Antigravity, запущенный Codex через безопасный runner.

Профиль:
{profile_text}

Режим:
{mode_text}

Обязательные ограничения безопасности:
- работай только внутри текущего рабочего дерева;
- не читай `.env`, приватные ключи, токены, keyring, SSH-конфигурацию и пользовательские каталоги вне проекта;
- не передавай исходный код или данные во внешние сервисы без прямой необходимости задачи и разрешённой политики;
- не запускай разрушительные команды;
- не выдавай предположение за подтверждённый факт;
- заверши работу структурированным результатом по переданной JSON Schema;
- в доказательствах используй `путь:строка`, когда это применимо;
- перечисли реально выполненные проверки и всё, что проверить не удалось.

Задача:
{task}
"""


def parse_json_envelope(stdout: str) -> dict[str, Any]:
    text = stdout.strip()
    if not text:
        raise DelegateError("Antigravity не вернул JSON в stdout.", code="INVALID_AGENT_OUTPUT")
    try:
        value = json.loads(text)
        if not isinstance(value, dict):
            raise ValueError("root is not object")
        return value
    except (json.JSONDecodeError, ValueError):
        for line in reversed(text.splitlines()):
            candidate = line.strip()
            if not candidate.startswith("{"):
                continue
            try:
                value = json.loads(candidate)
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict):
                return value
    raise DelegateError("Не удалось разобрать JSON-ответ Antigravity.", code="INVALID_AGENT_OUTPUT")


def write_private_json(path: Path, payload: Mapping[str, Any]) -> None:
    resolved = path.expanduser().resolve()
    resolved.parent.mkdir(parents=True, exist_ok=True)
    resolved.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    try:
        resolved.chmod(stat.S_IRUSR | stat.S_IWUSR)
    except OSError:
        pass


def build_agy_command(
    *,
    agy_bin: str,
    prompt: str,
    schema_path: Path,
    timeout: str,
    effort: str,
    model: str | None,
    agent: str | None,
    conversation: str | None,
) -> list[str]:
    command = [
        agy_bin,
        "-p",
        prompt,
        "--output-format",
        "json",
        "--json-schema",
        str(schema_path),
        "--effort",
        effort,
        "--print-timeout",
        timeout,
        "--sandbox",
    ]
    if model:
        command.extend(["--model", model])
    if agent:
        command.extend(["--agent", agent])
    if conversation:
        command.extend(["--conversation", conversation])
    return command


def redacted_command(command: Sequence[str]) -> list[str]:
    result = list(command)
    try:
        prompt_index = result.index("-p") + 1
        result[prompt_index] = "<task redacted>"
    except (ValueError, IndexError):
        pass
    return result


def output_payload(payload: Mapping[str, Any], *, pretty: bool) -> None:
    json.dump(payload, sys.stdout, ensure_ascii=False, indent=2 if pretty else None)
    sys.stdout.write("\n")


def command_run(args: argparse.Namespace) -> int:
    started_at = utc_now()
    started_monotonic = time.monotonic()
    task = load_task(args.task, args.task_file)
    timeout_seconds = parse_timeout(args.timeout)
    if args.mode == "isolated-write" and args.profile != "implementation":
        raise DelegateError(
            "Режим isolated-write разрешён только для профиля implementation.",
            code="INVALID_ARGUMENT",
        )
    repo_root, relative_workspace = discover_repo(args.workspace)
    require_clean_repo(repo_root)
    agy_bin = resolve_binary(args.agy_bin)
    script_dir = Path(__file__).resolve().parent
    schema_path = script_dir.parent / "schemas" / "delegated-result.schema.json"
    if not schema_path.is_file():
        raise DelegateError(f"JSON Schema не найдена: {schema_path}", code="INSTALLATION_ERROR")

    execution_root: Path | None = None
    cleanup_parent: Path | None = None
    branch: str | None = None
    worktree_retained = False
    cleanup_warnings: list[str] = []
    agent_result: dict[str, Any] | None = None
    agent_exit_code: int | None = None
    stderr_text = ""
    status = "RUNNER_ERROR"
    result_exit_code = 1
    files: list[str] = []
    diff_result: dict[str, Any] = {"ok": True, "output": ""}
    timed_out = False

    try:
        if args.mode == "read-only":
            execution_root, cleanup_parent = create_readonly_worktree(repo_root)
        else:
            execution_root, branch = create_write_worktree(repo_root, args.profile, args.worktree_root)
        protect_worktree_metadata(execution_root)

        execution_workspace = (execution_root / relative_workspace).resolve()
        if not execution_workspace.is_dir():
            raise DelegateError(
                f"Подкаталог рабочего пространства отсутствует в worktree: {relative_workspace}",
                code="INVALID_WORKSPACE",
            )

        prompt = build_prompt(task, args.profile, args.mode)
        command = build_agy_command(
            agy_bin=agy_bin,
            prompt=prompt,
            schema_path=schema_path,
            timeout=args.timeout,
            effort=args.effort,
            model=args.model,
            agent=args.agent,
            conversation=args.conversation,
        )
        environment, removed_env = sanitize_environment()

        if args.dry_run:
            status = "DRY_RUN"
            result_exit_code = 0
            agent_result = None
