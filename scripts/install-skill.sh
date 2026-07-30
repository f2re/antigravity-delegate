#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${PYTHON_BIN:-}"

if [[ -z "$PYTHON_BIN" ]]; then
  if command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN="python3"
  elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN="python"
  else
    echo "Не найден Python 3.10 или новее." >&2
    exit 1
  fi
fi

usage() {
  cat <<'HELP'
Установка Antigravity Delegate

Использование:
  ./scripts/install-skill.sh [параметры]

Параметры:
  --target <codex|antigravity-cli|antigravity-ide>
  --user
  --project <путь>
  --copy
  --link
  --install-agents <none|global|workspace>
  --force
  --dry-run
  --help

Примеры:
  ./scripts/install-skill.sh --target codex --user --copy
  ./scripts/install-skill.sh --target antigravity-cli --user --copy
  ./scripts/install-skill.sh --target codex --project /путь/к/проекту --copy
HELP
}

ARGS=(--pretty)
while (($#)); do
  case "$1" in
    --target)
      [[ $# -ge 2 ]] || { echo "После --target требуется значение." >&2; exit 2; }
      ARGS+=(--target "$2")
      shift 2
      ;;
    --user)
      ARGS+=(--scope user)
      shift
      ;;
    --project)
      [[ $# -ge 2 ]] || { echo "После --project требуется путь." >&2; exit 2; }
      ARGS+=(--scope repo --repo "$2")
      shift 2
      ;;
    --copy)
      ARGS+=(--mode copy)
      shift
      ;;
    --link)
      ARGS+=(--mode link)
      shift
      ;;
    --install-agents)
      [[ $# -ge 2 ]] || { echo "После --install-agents требуется значение." >&2; exit 2; }
      ARGS+=(--install-agents "$2")
      shift 2
      ;;
    --force|--dry-run)
      ARGS+=("$1")
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Неизвестный параметр: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

exec "$PYTHON_BIN" "$SCRIPT_DIR/install_skill.py" "${ARGS[@]}"
