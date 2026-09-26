# GDZLauncher
Лаунчер для Minecraft 1.7.10 с Forge: скачивает игру и Java 8, синхронизирует моды с сервером проекта
и обновляется сам. Linux и Windows.

---
## Роадмап

- [x] Кроссплатформенность
	- [x] Linux
		- [x] WebKitGtk
		- [x] Работа с майном
		- [x] Система обновления
	- [x] Windows
		- [x] Webview2
		- [x] Работа с майном
		- [x] Система обновления
- [x] Интерфейс (встроен в бинарник из `src/ui/index.html`)
- [ ] Сборка
	- [x] Автоматическая сборка в GitHub Actions
		- [x] Linux
		- [x] Windows
		- [x] Сквозной тест (tests/e2e)
		- [x] Релиз по тегу с готовым `launcher.json`
	- [ ] Упаковка
		- [x] Linux (`.tar.gz`)
		- [x] Windows (`.zip`)
		- [ ] Установщики
	- [x] Компиляция кода
		- [x] Linux
		- [x] Windows (MSVC)
- [x] Запуск майна
	- [x] Minecraft 1.7.10 + Forge 10.13.4.1614
	- [x] Java 8 от Mojang скачивается автоматически
	- [x] Синхронизация модов и конфигов с сервером
	- [x] Запуск без сети из скачанных файлов
	- [x] Пользовательские настройки
- [x] Конфигурация при сборке
	- [x] Установка адреса сервера API с каналом новостей и обновления
	- [x] Лок сервера на который ходит клиент игры
- [x] Дока по API для лаунчера ([docs/API.md](docs/API.md))
- [ ] Вход через аккаунт (сейчас офлайн-профиль: подходит для серверов с `online-mode=false`)

---
## Как это работает

1. **Сборка проекта.** Лаунчер скачивает `client.json` с сервера проекта и приводит папку игры к списку:
   докачивает недостающие и изменённые моды, удаляет лишние из папок синхронизации.
2. **Java 8.** Скачивается от Mojang (`jre-legacy`), если в настройках не указан свой путь.
3. **Minecraft и Forge.** Библиотеки, клиент, ресурсы и нативные файлы проверяются по SHA-1 и докачиваются.
4. **Запуск.** Игра стартует отдельным процессом и сразу подключается к серверу проекта.
5. **Обновление лаунчера.** По `launcher.json`: на Windows через `Updater.exe`, на Linux заменой бинарника.

Формат `client.json` и `launcher.json`, хранение данных и зеркала: [docs/API.md](docs/API.md).

---
## Сборка

Быстрый способ — скрипты из папки `scripts` (результат складывается в `dist/`):

| Скрипт | Что собирает |
| --- | --- |
| `scripts/build-linux.sh` | Linux: `dist/GDZLauncher-linux-x86_64.tar.gz` |
| `scripts/build-windows.bat` | Windows, MSVC (Visual Studio 2022): `dist\GDZLauncher\` |
| `scripts/build-windows-mingw.sh` | Windows из Linux, MinGW-w64: `dist/GDZLauncher-windows-x64.zip` |

Адрес сервера проекта и игрового сервера передаются переменными окружения `GDZ_API_URL` и `GDZ_SERVER_ADDRESS`.
Ниже то же самое вручную.

### Linux
**Зависимости:**
> - cmake
> - g++
> - pkg-config
> - libgtk-3-dev
> - libwebkit2gtk-4.1-dev
> - libcurl4-openssl-dev
> - Прямые руки

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DGDZ_API_URL=https://example.org/launcher/ \
      -DGDZ_SERVER_ADDRESS=play.example.org:25565
cmake --build build
./build/build/launcher
```

У игроков должны быть установлены библиотеки `libwebkit2gtk-4.1` и `libcurl` (есть в любом современном
дистрибутиве), а для LWJGL 2 в Minecraft 1.7.10 — утилита `xrandr`.

### Windows (MSVC)
**Зависимости:** Visual Studio 2022 (MSVC) и cmake.

```bat
cmake -S . -B build -A x64 -DGDZ_API_URL=https://example.org/launcher/ -DGDZ_SERVER_ADDRESS=play.example.org:25565
cmake --build build --config Release
```

Результат: `build\build\Release\Updater.exe` и `build\build\Release\launcher.dll`. Их нужно держать в одной папке
и запускать `Updater.exe`: он проверяет WebView2, обновляет лаунчер и запускает `launcher.dll`.

### Windows из Linux (MinGW-w64)
```bash
sudo apt install g++-mingw-w64-x86-64 cmake
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```
Результат: `build-win/build/Updater.exe` и `build-win/build/launcher.dll`, слинкованные статически
(нужны только системные библиотеки Windows). Заголовки WebView2 скачиваются с nuget.org автоматически;
без доступа к нему путь к распакованному пакету `Microsoft.Web.WebView2` передаётся через `-DMSWebView2_ROOT=...`.

### Опции CMake

| Опция | Назначение |
| --- | --- |
| `GDZ_API_URL` | Адрес API проекта (`client.json`, `launcher.json`). Пусто: чистый Forge без модов, новостей и обновлений. |
| `GDZ_SERVER_ADDRESS` | Сервер, к которому игра подключается при запуске (`host` или `host:port`). |
| `GDZ_*_URL` | Зеркала серверов Mojang и Forge, см. [docs/API.md](docs/API.md). |
| `GDZ_E2E_HARNESS` | Только для сквозного теста. |

### Автокомплит (YCM)
```bash
./setup_autocomplete.sh
```
Создаёт `compile_commands.json` для Linux-сборки. Файлы из `src/windows` он не покрывает.

---
## Выкладка сборки и обновлений

```bash
# моды и конфиги: папка pack повторяет папку игры (pack/mods, pack/config ...)
python3 tools/make_manifest.py client --dir ./pack --publish ./site \
    --sync mods --once "config/**" --once options.txt --server play.example.org:25565

# новая версия лаунчера (файлы из релиза или своей сборки)
python3 tools/make_manifest.py launcher --version 0.3.0 --publish ./site \
    --windows-dir ./GDZLauncher-windows --linux-bin ./GDZLauncher-linux/launcher --news news.json
```

Содержимое `./site` выкладывается по адресу `GDZ_API_URL` на любой статический хостинг.

---
## Автоматические сборки

Файл `.github/workflows/build.yml`:

- на каждый push и pull request в `main` собирает Linux и Windows и прогоняет сквозной тест.
  Архивы `GDZLauncher-linux-x86_64.tar.gz` и `GDZLauncher-windows-x64.zip` лежат в артефактах запуска;
- при создании тега `v*` публикует релиз: архивы, отдельные файлы для самообновления, `launcher.json`
  со ссылками на них и `SHA256SUMS.txt`. Тег должен совпадать с версией в `CMakeLists.txt`;
- запускается вручную через вкладку Actions (Run workflow).

Адрес API и сервер для CI задаются переменными репозитория `GDZ_API_URL` и `GDZ_SERVER_ADDRESS`
(Settings → Secrets and variables → Actions → Variables).

Выпуск новой версии: поменять версию в `project(GDZLauncher VERSION ...)` и в блоке VERSIONINFO
`src/windows/updater_res.rc`, закоммитить, `git tag v0.3.0 && git push origin v0.3.0`, затем выложить
`launcher.json` из релиза в корень API.

---
## Как выложить на GitHub

1. Создайте пустой репозиторий на GitHub (без README и .gitignore).
2. В папке проекта:
   ```bash
   git init
   git add .
   git commit -m "GDZLauncher 0.2.0"
   git branch -M main
   git remote add origin https://github.com/<логин>/<репозиторий>.git
   git push -u origin main
   ```
   Без командной строки: GitHub Desktop → File → Add local repository → Publish repository.

   Адрес сервера проекта (необязательно): Settings → Secrets and variables → Actions → Variables —
   `GDZ_API_URL` и `GDZ_SERVER_ADDRESS`. Без них собирается лаунчер с чистым Forge.
3. Сборка запускается сама после push. Готовые файлы: вкладка **Actions** → последний запуск
   **C++ Multi-platform CI** → внизу раздел **Artifacts**:
   `launcher-windows` (MSVC), `launcher-windows-mingw` (MinGW) и `launcher-linux`.
   Запустить вручную: Actions → C++ Multi-platform CI → Run workflow.
4. Первый релиз: `git tag v0.2.0 && git push origin v0.2.0`. Через несколько минут во вкладке Releases
   появятся сборки для Windows и Linux и `launcher.json`.

---
## Тесты

```bash
cmake -S . -B build-e2e -DCMAKE_BUILD_TYPE=Release -DGDZ_E2E_HARNESS=ON && cmake --build build-e2e
python3 tests/e2e/run_e2e.py --launcher build-e2e/build/launcher
```

Тест поднимает локальный сервер, изображающий Mojang, Forge, Java и сервер проекта (с настоящим JSON
версии 1.7.10 и настоящей JRE 8, но с заглушками вместо файлов игры), и проверяет: первый запуск,
синхронизацию модов, восстановление файлов, запуск без сети, отмену загрузки и самообновление.
Нужны Java 8 (JDK) и `xvfb-run`.

---
## Структура

- `src/shared` — общий код: окно и привязки интерфейса (`core`), установка и запуск игры (`game`),
  самообновление (`selfupdate`), сеть (`net`), SHA-1, распаковка архивов, настройки.
- `src/linux` — точка входа, сеть на libcurl, запуск процессов для Linux.
- `src/windows` — `Updater.exe` (WebView2, обновление, загрузка DLL), `launcher.dll`, сеть на WinHTTP, процессы.
- `src/ui/index.html` — интерфейс. `src/profiles/forge-1.7.10.json` — библиотеки Forge. Оба встраиваются в бинарник.
- `tools/make_manifest.py` — генератор `client.json` и `launcher.json`.
- `scripts/` — скрипты сборки для Linux и Windows.
- `.github/workflows/build.yml` — сборки, тесты и релизы в GitHub Actions.
- `tests/e2e` — сквозной тест.
