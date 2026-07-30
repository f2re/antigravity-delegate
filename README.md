# 🛰️ Antigravity Delegate

**Antigravity Delegate** — переносимый Agent Skill и плагин, который позволяет Codex или другому управляющему агенту безопасно передавать ограниченные задачи в **Google Antigravity CLI (`agy`)**.

Основной агент остаётся оркестратором: формулирует задачу, запускает отдельную headless-сессию Antigravity, проверяет структурированный результат, `git diff` и тесты. Antigravity не получает права самостоятельно выполнять `commit`, `merge`, `push`, развёртывание или производственные миграции.

## ✨ Возможности

- 🔍 независимый аудит архитектуры, безопасности, тестов и документации;
- 🧪 повторная проверка решения другой моделью;
- 🧱 `read-only` в одноразовом detached `git worktree`;
- 🛠️ `isolated-write` в отдельной ветке и отдельном worktree;
- 📦 JSON-результат, проверяемый по JSON Schema;
- 🧹 удаление токенов, паролей, SSH-agent и опасных переменных окружения;
- 🚫 отсутствие `shell=True` и `--dangerously-skip-permissions`;
- ✅ `git diff --check`, перечень изменённых файлов и контроль нарушения политики;
- 🩺 диагностика `agy` и OAuth-сессии;
- 🔌 штатная упаковка для Codex Plugin Marketplace и Antigravity Plugin Manager;
- 📥 установка через общий Agent Skills CLI.

## 📋 Требования

Для выполнения делегированных задач нужны:

- Python 3.10 или новее;
- Git с поддержкой `git worktree`;
- установленный `agy`;
- один интерактивный вход в Antigravity на машине пользователя.

```bash
agy --version
agy models
agy agents
```

# 🚀 Установка в Codex

## Вариант 1. Codex Plugin Marketplace — рекомендуемый

Добавьте репозиторий как источник плагинов:

```bash
codex plugin marketplace add f2re/antigravity-delegate --ref main
codex plugin marketplace list
```

Откройте в Codex:

```text
/plugins
```

Выберите marketplace **f2re · Antigravity Delegate**, установите плагин `antigravity-delegate` и начните новую сессию.

Обновление подключённых marketplace:

```bash
codex plugin marketplace upgrade
```

## Вариант 2. Встроенный установщик навыков Codex

В сессии Codex:

```text
$skill-installer установи навык antigravity-delegate из https://github.com/f2re/antigravity-delegate/tree/main/skills/antigravity-delegate
```

## Вариант 3. Agent Skills CLI

Глобальная установка:

```bash
npx skills add f2re/antigravity-delegate \
  --skill antigravity-delegate \
  --agent codex \
  --global \
  --copy \
  --yes
```

Установка только в текущий проект:

```bash
npx skills add f2re/antigravity-delegate \
  --skill antigravity-delegate \
  --agent codex \
  --copy \
  --yes
```

Проверка в новой сессии Codex:

```text
/skills
$antigravity-delegate проведи независимый read-only аудит архитектуры проекта
```

# 🪐 Установка в Antigravity

## Вариант 1. Штатный Antigravity Plugin Manager — рекомендуемый

Актуальные версии `agy` поддерживают установку локального или удалённого plugin-пакета:

```bash
agy plugin install https://github.com/f2re/antigravity-delegate.git
agy plugin list
```

Если используемая версия CLI принимает только локальный путь:

```bash
git clone --depth 1 https://github.com/f2re/antigravity-delegate.git
agy plugin install ./antigravity-delegate
```

После установки перезапустите `agy` и проверьте:

```text
/skills
/agents
```

Плагин регистрирует:

- навык `/antigravity-delegate`;
- профили `architecture-reviewer`, `security-reviewer`, `test-auditor`, `researcher`, `implementation`.

Управление:

```bash
agy plugin disable antigravity-delegate
agy plugin enable antigravity-delegate
agy plugin uninstall antigravity-delegate
```

## Вариант 2. Agent Skills CLI для Antigravity CLI

```bash
npx skills add f2re/antigravity-delegate \
  --skill antigravity-delegate \
  --agent antigravity-cli \
  --global \
  --copy \
  --yes
```

Этот способ устанавливает сам навык. Корневые custom agents автоматически добавляются только при установке полного Antigravity-плагина.

## Вариант 3. Встроенный установщик репозитория

Linux и macOS:

```bash
git clone --depth 1 https://github.com/f2re/antigravity-delegate.git
cd antigravity-delegate

./scripts/install-skill.sh \
  --target antigravity-cli \
  --user \
  --copy
```

Windows PowerShell:

```powershell
git clone --depth 1 https://github.com/f2re/antigravity-delegate.git
Set-Location antigravity-delegate

pwsh ./scripts/install-skill.ps1 `
  -Target antigravity-cli `
  -Scope User `
  -Mode Copy
```

# 📦 Универсальный установщик

Python-установщик поддерживает три пользовательских назначения:

| `--target` | Каталог |
|---|---|
| `codex` | `~/.agents/skills/antigravity-delegate` |
| `antigravity-cli` | `~/.gemini/antigravity-cli/skills/antigravity-delegate` |
| `antigravity-ide` | `~/.gemini/config/skills/antigravity-delegate` |

Codex:

```bash
python3 scripts/install_skill.py \
  --target codex \
  --scope user \
  --mode copy \
  --pretty
```

Antigravity CLI:

```bash
python3 scripts/install_skill.py \
  --target antigravity-cli \
  --scope user \
  --mode copy \
  --pretty
```

Только для одного проекта — путь одинаков для Codex и Antigravity:

```bash
python3 scripts/install_skill.py \
  --target codex \
  --scope repo \
  --repo /путь/к/проекту \
  --mode copy \
  --pretty
```

Результат:

```text
<проект>/.agents/skills/antigravity-delegate/
```

Для разработки можно использовать символическую ссылку:

```bash
python3 scripts/install_skill.py \
  --target codex \
  --scope user \
  --mode link \
  --force \
  --pretty
```

Предварительная проверка без записи:

```bash
python3 scripts/install_skill.py \
  --target codex \
  --scope user \
  --dry-run \
  --pretty
```

Существующая установка не заменяется без `--force`.

# 🤖 Профили Antigravity

В комплект включены пять ролей:

| Профиль | Назначение |
|---|---|
| `architecture-reviewer` | архитектура, связность, технический долг |
| `security-reviewer` | безопасность, права, секреты, опасные операции |
| `test-auditor` | тесты, CI, воспроизводимость и пробелы покрытия |
| `researcher` | документация, спецификации и проверка источников |
| `implementation` | ограниченное изменение кода в изолированном worktree |

При штатной установке Antigravity-плагина профили берутся из `agents/`.

При установке только skill их можно скопировать отдельно:

```bash
RUNNER="$HOME/.agents/skills/antigravity-delegate/scripts/antigravity_delegate.py"

python3 "$RUNNER" install-agents \
  --scope global \
  --pretty
```

Для конкретного проекта:

```bash
python3 "$RUNNER" install-agents \
  --scope workspace \
  --workspace /путь/к/проекту \
  --pretty
```

# 🩺 Диагностика

Для Codex-установки:

```bash
RUNNER="$HOME/.agents/skills/antigravity-delegate/scripts/antigravity_delegate.py"

python3 "$RUNNER" doctor --pretty
python3 "$RUNNER" doctor --probe-auth --pretty
```

Для Antigravity CLI skill-установки:

```bash
RUNNER="$HOME/.gemini/antigravity-cli/skills/antigravity-delegate/scripts/antigravity_delegate.py"

python3 "$RUNNER" doctor --pretty
python3 "$RUNNER" doctor --probe-auth --pretty
```

Если headless-вход не подтверждён, один раз запустите `agy` вручную и завершите официальный OAuth-вход в браузере. Не передавайте токены в чат или в текст задания.

# ▶️ Запуск

## Аудит без изменения файлов

```bash
python3 "$RUNNER" run \
  --workspace /путь/к/проекту \
  --profile architecture-reviewer \
  --mode read-only \
  --task "Проведи аудит импорта XLSX. Укажи дефекты как путь:строка." \
  --timeout 15m \
  --pretty
```

Runner:

1. требует чистый и закоммиченный исходный репозиторий;
2. создаёт временный detached worktree;
3. запускает `agy -p` с JSON Schema и `--sandbox`;
4. проверяет фактические изменения;
5. удаляет worktree;
6. возвращает `POLICY_VIOLATION`, если агент записал файл.

## Ограниченное изменение кода

```bash
python3 "$RUNNER" run \
  --workspace /путь/к/проекту \
  --profile implementation \
  --mode isolated-write \
  --task-file /tmp/task.txt \
  --timeout 30m \
  --pretty
```

Изменения остаются только в отдельном каталоге и ветке:

```json
{
  "execution_worktree": "/путь/.project-agy-worktrees/20260730-...",
  "branch": "agy/implementation-20260730-...",
  "changed_files": ["src/example.py"]
}
```

Основной агент или оператор обязан самостоятельно прочитать полный diff и выполнить тесты. Runner не выполняет `commit`, `merge`, `push`, deploy и миграции.

# 🔐 Безопасность

- каждый запуск `agy` выполняется вне исходного checkout;
- команда передаётся списком аргументов, без shell-интерпретации;
- `stdin` закрыт;
- `--sandbox` включается принудительно;
- `--dangerously-skip-permissions` отсутствует;
- из окружения удаляются ключи, токены, пароли, `SSH_AUTH_SOCK`, `LD_PRELOAD` и Git-переменные внедрения;
- отключаются Git hooks, внешний diff, credential helper, fsmonitor и LFS smudge;
- текст задания не включается в итоговый JSON — сохраняются длина и SHA-256;
- запись в `read-only` считается нарушением политики;
- режим записи разрешён только профилю `implementation`;
- исходный репозиторий должен быть чистым;
- установка skill не запускает `agy` и не выполняет произвольные post-install скрипты.

Консервативный пример разрешений: [`examples/permissions.settings.fragment.json`](examples/permissions.settings.fragment.json).

Полная модель угроз: [`SECURITY.md`](SECURITY.md).

# 🧪 Проверка репозитория

```bash
python3 -m compileall -q scripts skills skill tests
python3 -m unittest discover -s tests -v
```

CI дополнительно проверяет:

- корректность Codex- и Antigravity-манифестов;
- наличие канонического `skills/antigravity-delegate`;
- идентичность временного compatibility-mirror `skill/`;
- совпадение встроенных профилей и plugin-профилей `agents/`.

# 🗂️ Структура

```text
antigravity-delegate/
├── .codex-plugin/plugin.json
├── .agents/plugins/marketplace.json
├── plugin.json
├── agents/
│   ├── architecture-reviewer/agent.md
│   ├── security-reviewer/agent.md
│   ├── test-auditor/agent.md
│   ├── researcher/agent.md
│   └── implementation/agent.md
├── skills/antigravity-delegate/
│   ├── SKILL.md
│   ├── agents/openai.yaml
│   ├── scripts/
│   ├── schemas/
│   ├── references/
│   └── assets/antigravity-agents/
├── skill/antigravity-delegate/      # compatibility-mirror версии 0.1
├── scripts/
└── tests/
```

Канонический источник для установщиков — `skills/antigravity-delegate`. Каталог `skill/` временно сохранён для обратной совместимости и проверяется CI на побайтовое совпадение.

## 📄 Лицензия

MIT. См. [`LICENSE`](LICENSE).
