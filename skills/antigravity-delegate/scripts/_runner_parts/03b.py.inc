                    "60s",
                    "--sandbox",
                ],
                timeout=90,
                env=sanitize_environment()[0],
            )
            try:
                envelope = parse_json_envelope(probe.stdout)
            except DelegateError as exc:
                envelope = {"status": "INVALID", "error": str(exc)}
            checks["auth_probe"].update(
                {
                    "ok": probe.returncode == 0 and envelope.get("status") == TERMINAL_SUCCESS,
                    "exit_code": probe.returncode,
                    "status": envelope.get("status"),
                    "error": envelope.get("error") or probe.stderr.strip()[-1000:],
                }
            )
            if not checks["auth_probe"]["ok"]:
                exit_code = 1
    except DelegateError as exc:
        checks["agy"] = {"ok": False, "error": str(exc), "code": exc.code}
        exit_code = 1

    settings = Path.home() / ".gemini" / "antigravity-cli" / "settings.json"
    payload = {
        "schema_version": SCHEMA_VERSION,
        "runner_version": VERSION,
        "status": "SUCCESS" if exit_code == 0 else "ERROR",
        "checks": checks,
        "antigravity_settings": {
            "path": str(settings),
            "exists": settings.is_file(),
        },
    }
    output_payload(payload, pretty=args.pretty)
    return exit_code


def copy_agent_templates(destination: Path, *, force: bool, dry_run: bool) -> list[dict[str, str]]:
    source_root = Path(__file__).resolve().parent.parent / "assets" / "antigravity-agents"
    if not source_root.is_dir():
        raise DelegateError(f"Шаблоны агентов не найдены: {source_root}", code="INSTALLATION_ERROR")
    operations: list[dict[str, str]] = []
    for source_file in sorted(source_root.glob("*/agent.md")):
        target = destination / source_file.parent.name / "agent.md"
        if target.exists() and not force:
            raise DelegateError(
                f"Файл уже существует: {target}. Используйте --force для замены.", code="TARGET_EXISTS"
            )
        operations.append({"source": str(source_file), "target": str(target)})
        if not dry_run:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, target)
    return operations


def command_install_agents(args: argparse.Namespace) -> int:
    if args.scope == "global":
        destination = Path.home() / ".gemini" / "config" / "agents"
    else:
        if args.workspace is None:
            raise DelegateError("Для workspace scope укажите --workspace.", code="INVALID_ARGUMENT")
        repo_root, _ = discover_repo(args.workspace)
        destination = repo_root / ".agents" / "agents"
    operations = copy_agent_templates(destination, force=args.force, dry_run=args.dry_run)
    payload = {
        "schema_version": SCHEMA_VERSION,
        "status": "DRY_RUN" if args.dry_run else "SUCCESS",
        "scope": args.scope,
        "destination": str(destination),
        "installed": operations,
    }
    output_payload(payload, pretty=args.pretty)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="antigravity-delegate",
        description="Безопасное делегирование задач из Codex в Google Antigravity CLI.",
    )
    parser.add_argument("--version", action="version", version=VERSION)
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    run_parser = subparsers.add_parser("run", help="Запустить делегированную задачу")
    run_parser.add_argument("--workspace", type=Path, default=Path.cwd())
    run_parser.add_argument("--task")
    run_parser.add_argument("--task-file", type=Path)
    run_parser.add_argument("--profile", choices=sorted(PROFILE_INSTRUCTIONS), required=True)
    run_parser.add_argument("--mode", choices=("read-only", "isolated-write"), default="read-only")
    run_parser.add_argument("--agy-bin", default=os.environ.get("AGY_BIN", "agy"))
    run_parser.add_argument("--model", help="Точный slug из `agy models`; автоматический fallback не выполняется")
    run_parser.add_argument("--agent", help="Имя установленного custom agent из `agy agents`")
    run_parser.add_argument("--effort", choices=("low", "medium", "high"), default="high")
    run_parser.add_argument("--timeout", default=DEFAULT_TIMEOUT)
    run_parser.add_argument("--conversation", help="Явный UUID разговора Antigravity")
    run_parser.add_argument("--worktree-root", type=Path)
    run_parser.add_argument("--result-file", type=Path)
    run_parser.add_argument("--dry-run", action="store_true")
    run_parser.add_argument("--pretty", action="store_true")
    run_parser.set_defaults(handler=command_run)

    doctor_parser = subparsers.add_parser("doctor", help="Проверить зависимости и авторизацию")
    doctor_parser.add_argument("--agy-bin", default=os.environ.get("AGY_BIN", "agy"))
    doctor_parser.add_argument("--probe-auth", action="store_true")
    doctor_parser.add_argument("--pretty", action="store_true")
    doctor_parser.set_defaults(handler=command_doctor)

    agents_parser = subparsers.add_parser("install-agents", help="Установить профили custom agents Antigravity")
    agents_parser.add_argument("--scope", choices=("global", "workspace"), default="global")
    agents_parser.add_argument("--workspace", type=Path)
    agents_parser.add_argument("--force", action="store_true")
    agents_parser.add_argument("--dry-run", action="store_true")
    agents_parser.add_argument("--pretty", action="store_true")
    agents_parser.set_defaults(handler=command_install_agents)

    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.handler(args))
    except DelegateError as exc:
        payload = {
            "schema_version": SCHEMA_VERSION,
            "runner_version": VERSION,
            "status": "ERROR",
            "error": {"code": exc.code, "message": str(exc)},
        }
        output_payload(payload, pretty=getattr(args, "pretty", False))
        return 1
    except KeyboardInterrupt:
        payload = {
            "schema_version": SCHEMA_VERSION,
            "runner_version": VERSION,
            "status": "INTERRUPTED",
        }
        output_payload(payload, pretty=getattr(args, "pretty", False))
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
