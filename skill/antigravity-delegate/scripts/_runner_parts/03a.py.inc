            stderr_text = ""
        else:
            try:
                completed = run_process(
                    command,
                    cwd=execution_workspace,
                    timeout=timeout_seconds + 30,
                    env=environment,
                )
                agent_exit_code = completed.returncode
                stderr_text = completed.stderr
                try:
                    agent_result = parse_json_envelope(completed.stdout)
                except DelegateError as exc:
                    agent_result = {
                        "status": "ERROR",
                        "error": str(exc),
                        "raw_stdout_tail": completed.stdout[-2000:],
                    }
            except subprocess.TimeoutExpired as exc:
                timed_out = True
                stderr_text = (exc.stderr or "") if isinstance(exc.stderr, str) else ""
                status = "TIMEOUT"
                result_exit_code = 4
            except (OSError, subprocess.SubprocessError) as exc:
                agent_exit_code = 1
                agent_result = {"status": "ERROR", "error": str(exc)}
                stderr_text = str(exc)

        files = changed_files(execution_root)
        diff_result = diff_check(execution_root)

        if not timed_out and not args.dry_run:
            agent_status = str((agent_result or {}).get("status", "INVALID"))
            if args.mode == "read-only" and files:
                status = "POLICY_VIOLATION"
                result_exit_code = 3
            elif agent_exit_code == 0 and agent_status == TERMINAL_SUCCESS:
                status = "SUCCESS"
                result_exit_code = 0
            else:
                status = "AGENT_ERROR"
                result_exit_code = 1

        if args.mode == "isolated-write":
            if files:
                worktree_retained = True
            else:
                cleanup_warnings.extend(remove_worktree(repo_root, execution_root, branch=branch))
                execution_root = None
                branch = None
        removed_environment = removed_env
        safe_command = redacted_command(command)
    finally:
        if args.mode == "read-only" and execution_root is not None:
            cleanup_warnings.extend(remove_worktree(repo_root, execution_root))
            execution_root = None
        if cleanup_parent is not None:
            shutil.rmtree(cleanup_parent, ignore_errors=True)

    payload: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "runner_version": VERSION,
        "status": status,
        "mode": args.mode,
        "profile": args.profile,
        "source_repository": str(repo_root),
        "source_workspace": str((repo_root / relative_workspace).resolve()),
        "execution_worktree": str(execution_root) if execution_root else None,
        "worktree_retained": worktree_retained,
        "branch": branch,
        "changed_files": files,
        "diff_check": diff_result,
        "agy_exit_code": agent_exit_code,
        "agy_result": agent_result,
        "agy_stderr_tail": stderr_text[-4000:],
        "command": safe_command,
        "removed_environment_variables": removed_environment,
        "task": {
            "sha256": hashlib.sha256(task.encode("utf-8")).hexdigest(),
            "characters": len(task),
        },
        "safety": {
            "source_repo_was_clean": True,
            "sandbox_forced": True,
            "stdin_closed": True,
            "shell_used": False,
            "dangerously_skip_permissions_supported": False,
            "read_only_changes_detected": bool(args.mode == "read-only" and files),
        },
        "cleanup_warnings": cleanup_warnings,
        "started_at": started_at,
        "finished_at": utc_now(),
        "duration_seconds": round(time.monotonic() - started_monotonic, 3),
    }
    if args.result_file:
        write_private_json(args.result_file, payload)
    output_payload(payload, pretty=args.pretty)
    return result_exit_code


def command_doctor(args: argparse.Namespace) -> int:
    checks: dict[str, Any] = {
        "python": {
            "ok": sys.version_info >= (3, 10),
            "version": sys.version.split()[0],
        },
        "git": {"ok": False},
        "agy": {"ok": False},
        "auth_probe": {"performed": False},
    }
    exit_code = 0
    git_bin = shutil.which("git")
    if git_bin:
        completed = run_process([git_bin, "--version"], timeout=10)
        checks["git"] = {
            "ok": completed.returncode == 0,
            "binary": git_bin,
            "version": completed.stdout.strip(),
            "error": completed.stderr.strip(),
        }
    else:
        checks["git"] = {"ok": False, "error": "git не найден"}
        exit_code = 1

    try:
        agy_bin = resolve_binary(args.agy_bin)
        completed = run_process([agy_bin, "--version"], timeout=15, env=sanitize_environment()[0])
        checks["agy"] = {
            "ok": completed.returncode == 0,
            "binary": agy_bin,
            "version": completed.stdout.strip() or completed.stderr.strip(),
            "exit_code": completed.returncode,
        }
        if completed.returncode != 0:
            exit_code = 1
        if args.probe_auth:
            checks["auth_probe"]["performed"] = True
            probe = run_process(
                [
                    agy_bin,
                    "-p",
                    "Ответь ровно: OK",
                    "--output-format",
                    "json",
                    "--print-timeout",
