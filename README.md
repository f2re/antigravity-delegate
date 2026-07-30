# 🛰️ Antigravity Delegate

Безопасный **Codex skill**, который передаёт ограниченные задачи внешнему агенту **Google Antigravity CLI (`agy`)** и возвращает Codex структурированный JSON-результат.

Codex остаётся оркестратором: формулирует задачу, запускает Antigravity, проверяет вывод, `git diff` и тесты. Antigravity не получает право самостоятельно публиковать изменения.

## ✨ Возможности

- 🔍 аудит архитектуры, безопасности, тестов и документации;
- 🧪 независимая повторная проверка решения другим агентом;
- 🧱 режим `read-only` в одноразовом detached `git worktree`;
- 🛠️ режим `isolated-write` в отдельной ветке и отдельном worktree;
- 📦 машинно-проверяемый JSON по JSON Schema;
- 🧹 фильтрация токенов, паролей, SSH-agent и опасных переменных окружения;
- 🚫 отсутствие `shell=True` и `--dangerously-skip-permissions`;
- ✅ проверка `git diff --check`, перечень изменённых файлов и контроль политики;
- 🩺 команды диагностики и установки профилей Antigravity.

## 📋 Требования

- Python 3.10 или новее;
- Git с поддержкой `git worktree`;
- установленный `agy`;
- один интерактивный вход в Antigravity на машине пользователя.

Проверьте CLI:

```bash
agy --version
agy models
agy agents
```

## 📥 Установка skill

```bash
git clone https://github.com/f2re/antigravity-delegate.git
cd antigravity-delegate
python3 scripts/install_skill.py --scope user --pretty
```

Skill устанавливается в:

```text
~/.agents/skills/antigravity-delegate
```

Установка только для одного репозитория:

```bash
python3 scripts/install_skill.py \
  --scope repo \
  --repo /путь/к/проекту \
  --pretty
```

Проверка без записи:

```bash
python3 scripts/install_skill.py --scope user --dry-run --pretty
```

Существующая установка не перезаписывается без явного `--force`.

## 🤖 Профили Antigravity

В комплект включены пять ролей:

| Профиль | Назначение |
|---|---|
| `architecture-reviewer` | архитектура, связность, технический долг |
| `security-reviewer` | безопасность, права, секреты, опасные операции |
| `test-auditor` | тесты, CI, воспроизводимость и пробелы покрытия |
| `researcher` | документация, спецификации и проверка источников |
| `implementation` | ограниченное изменение кода в изолированном worktree |

Установка профилей глобально:

```bash
RUNNER="$HOME/.agents/skills/antigravity-delegate/scripts/antigravity_delegate.py"
python3 "$RUNNER" install-agents --scope global --pretty
```

Для конкретного проекта:

```bash
python3 "$RUNNER" install-agents \
  --scope workspace \
  --workspace /путь/к/проекту \
  --pretty
```

## 🩺 Диагностика

```bash
python3 "$RUNNER" doctor --pretty
```

Проверка реальной headless-авторизации:

```bash
python3 "$RUNNER" doctor --probe-auth --pretty
```

Если проверка входа не проходит, один раз запустите `agy` вручную и завершите OAuth-вход в браузере.

## ▶️ Запуск аудита без изменения файлов

```bash
python3 "$RUNNER" run \
  --workspace /путь/к/проекту \
  --profile architecture-reviewer \
  --mode read-only \
  --task "Проведи аудит импорта XLSX. Укажи дефекты как путь:строка." \
  --timeout 15m \
  --pretty
```

В режиме `read-only` runner:

1. требует чистый и закоммиченный исходный репозиторий;
2. создаёт временный detached worktree;
3. запускает `agy -p ... --output-format json --json-schema ... --sandbox`;
4. проверяет изменения;
5. удаляет worktree;
6. возвращает `POLICY_VIOLATION`, если агент что-либо записал.

Для длинного задания используйте файл:

```bash
python3 "$RUNNER" run \
  --workspace . \
  --profile security-reviewer \
  --mode read-only \
  --task-file /tmp/security-audit.txt \
  --pretty
```

## 🛠️ Запуск ограниченного изменения кода

```bash
python3 "$RUNNER" run \
  --workspace /путь/к/проекту \
  --profile implementation \
  --mode isolated-write \
  --task-file /tmp/task.txt \
  --timeout 30m \
  --pretty
```

Изменения остаются только в каталоге и ветке, указанных в полях:

```json
{
  "execution_worktree": "/путь/.project-agy-worktrees/20260730-...",
  "branch": "agy/implementation-20260730-...",
  "changed_files": ["src/example.py"]
}
```

Codex или оператор обязан самостоятельно проверить этот worktree, выполнить тесты и только затем перенести изменения. Runner не выполняет `commit`, `merge`, `push`, `deploy` и миграции.

## 🧠 Вызов из Codex

```text
$antigravity-delegate

Передай Antigravity независимый read-only аудит безопасности текущего проекта.
Проверь права доступа, секреты, внешние команды и обработку входных данных.
После ответа самостоятельно проверь каждый вывод и не изменяй файлы.
```

Для автоматического применения политики можно добавить в `AGENTS.md` фрагмент из [`examples/AGENTS.md.fragment.md`](examples/AGENTS.md.fragment.md).

## 🔐 Безопасность

Основные ограничения runner:

- каждый запуск выполняется не в исходном checkout, а в отдельном worktree;
- `stdin` закрыт, команда запускается списком аргументов без shell-интерпретации;
- принудительно добавляется `--sandbox`;
- опасный режим пропуска разрешений не поддерживается;
- из окружения удаляются ключи, токены, пароли, `SSH_AUTH_SOCK`, `LD_PRELOAD` и Git-переменные внедрения;
- отключаются Git hooks, внешний diff, credential helper, fsmonitor и LFS smudge;
- текст задания не печатается в итоговом JSON: сохраняются только длина и SHA-256;
- запись в `read-only` считается нарушением политики;
- режим записи доступен только профилю `implementation`;
- исходный репозиторий должен быть чистым.

Дополнительно настройте разрешения самого Antigravity. Консервативный пример: [`examples/permissions.settings.fragment.json`](examples/permissions.settings.fragment.json). Не копируйте его вслепую: правила должны соответствовать версии CLI и вашему проекту.

Полная модель угроз: [`SECURITY.md`](SECURITY.md).

## 🧪 Проверка проекта

```bash
python3 -m compileall -q scripts skill tests
python3 -m unittest discover -s tests -v
```

Тесты используют поддельный исполняемый файл `agy` и проверяют изоляцию worktree, нарушение `read-only`, очистку окружения, JSON-контракт и установщик. Реальный OAuth-вход проверяется командой `doctor --probe-auth` на целевой машине.

## 🗂️ Структура

```text
skill/antigravity-delegate/
├── SKILL.md
├── agents/openai.yaml
├── scripts/antigravity_delegate.py
├── schemas/delegated-result.schema.json
├── references/SECURITY.md
└── assets/antigravity-agents/

scripts/install_skill.py
tests/
examples/
```

## 📄 Лицензия

MIT. См. [`LICENSE`](LICENSE).
