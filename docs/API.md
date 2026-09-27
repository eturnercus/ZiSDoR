# API лаунчера

Лаунчеру не нужен собственный сервер с кодом: достаточно статического хостинга (nginx, Caddy,
GitHub Pages, S3, любой CDN). Все файлы API лежат по одному базовому адресу, который задаётся
при сборке лаунчера:

```bash
cmake -S . -B build -DGDZ_API_URL=https://example.org/launcher/ -DGDZ_SERVER_ADDRESS=play.example.org:25565
```

Если `GDZ_API_URL` не задан, лаунчер запускает чистый Forge 1.7.10 без модов, новостей и обновлений.

| Файл | Зачем |
| --- | --- |
| `<API>/client.json` | Список файлов сборки: моды, конфиги, ресурс-паки. Загружается перед каждым запуском. |
| `<API>/files/...` | Сами файлы сборки (адреса указаны в `client.json`). |
| `<API>/launcher.json` | Актуальная версия лаунчера, файлы для самообновления и новости. |
| `<API>/bin/...` | Файлы лаунчера для самообновления (адреса указаны в `launcher.json`). |

Оба JSON-файла удобно генерировать скриптом `tools/make_manifest.py` (примеры ниже).
Рекомендуется HTTPS: целостность файлов проверяется по SHA-1, но сами манифесты
приходят по сети и должны быть защищены от подмены.

---

## client.json

```json
{
  "formatVersion": 1,
  "name": "Моя сборка",
  "minecraft": "1.7.10",
  "server": "play.example.org:25565",
  "syncDirs": ["mods"],
  "ignore": ["mods/optifine*.jar"],
  "jvmArgs": ["-XX:+UseG1GC"],
  "files": [
    { "path": "mods/industrialcraft.jar", "url": "files/mods/industrialcraft.jar", "sha1": "…", "size": 1234567 },
    { "path": "config/forge.cfg", "url": "files/config/forge.cfg", "sha1": "…", "size": 2048, "mode": "once" }
  ]
}
```

| Поле | Обязательно | Описание |
| --- | --- | --- |
| `formatVersion` | да | Всегда `1`. |
| `minecraft` | нет | Версия игры. Лаунчер запускает только `1.7.10` и откажется работать с другой. |
| `name` | нет | Название сборки. |
| `server` | нет | `host` или `host:port` (порт по умолчанию 25565): игра сразу подключается к серверу. `GDZ_SERVER_ADDRESS`, заданный при сборке лаунчера, имеет приоритет. |
| `syncDirs` | нет | Папки (относительно папки игры), где удаляется всё, чего нет в `files`. Обычно `["mods"]`. Не указывайте папки с сохранениями и настройками игрока. |
| `ignore` | нет | Шаблоны файлов, которые синхронизация не удаляет (например, разрешённые клиентские моды). `*` и `?` не переходят через `/`, `**` — переходит. |
| `jvmArgs` | нет | Дополнительные аргументы Java. |
| `baseUrl` | нет | Базовый адрес для относительных `url` (относительно `GDZ_API_URL`). По умолчанию сам `GDZ_API_URL`. |
| `files[].path` | да | Путь в папке игры через `/`. Запрещены абсолютные пути, `..`, `\`, `:`, пробел или точка в конце имени. |
| `files[].url` | нет | Абсолютный (`https://…`) или относительный адрес. По умолчанию равен `path`. |
| `files[].sha1` | да | SHA-1 файла (40 шестнадцатеричных символов). |
| `files[].size` | нет | Размер в байтах (ускоряет проверку). |
| `files[].mode` | нет | `sync` (по умолчанию): файл всегда приводится к версии сервера. `once`: скачивается, только если его нет (конфиги, которые игрок может менять). |

Как лаунчер применяет `client.json` при каждом запуске:

1. Скачивает `client.json`. Если сервер недоступен, использует сохранённую копию (запуск без сети).
2. Проверяет каждый файл по размеру и SHA-1, недостающие и изменённые скачивает заново.
3. В папках `syncDirs` удаляет файлы, которых нет в списке (кроме `ignore`) и опустевшие подпапки.

Генерация из папки, которая повторяет папку игры (`pack/mods/…`, `pack/config/…`):

```bash
python3 tools/make_manifest.py client --dir ./pack --publish ./site \
    --name "Моя сборка" --server play.example.org:25565 \
    --sync mods --ignore "mods/optifine*.jar" --once "config/**" --once options.txt
```

Содержимое `./site` (`client.json` и `files/`) выкладывается в корень API.

---

## launcher.json

```json
{
  "version": "0.002",
  "news": [
    { "title": "Сервер открыт", "date": "26.09.2026", "text": "Текст новости" }
  ],
  "windows": [
    { "path": "GDZLauncher.exe", "url": "bin/windows/GDZLauncher.exe", "sha1": "…", "size": 1737728 }
  ],
  "linux": [
    { "path": "GDZLauncher-x86_64.AppImage", "url": "bin/linux/GDZLauncher-x86_64.AppImage", "sha1": "…", "size": 1223160 },
    { "path": "launcher", "url": "bin/linux/launcher", "sha1": "…", "size": 1150000 }
  ]
}
```

| Поле | Описание |
| --- | --- |
| `version` | Версия лаунчера на сервере. Должна совпадать с `project(GDZLauncher VERSION …)` той сборки, чьи файлы указаны ниже. |
| `news` | До 10 новостей для блока «Вестник»: `title`, `date` (произвольная строка), `text` (переносы строк сохраняются). |
| `windows`, `linux` | Файлы лаунчера для платформы. `path` — только имя файла, без папок. |

Имена основного файла зарезервированы и сопоставляются с запущенным файлом, даже если пользователь
его переименовал (например, `GDZLauncher (1).exe`):

| `path` | Что обновляется |
| --- | --- |
| `GDZLauncher.exe` | Windows: запущенный exe. |
| `GDZLauncher-x86_64.AppImage` | Linux, запуск из AppImage: сам файл `.AppImage`. |
| `launcher` | Linux, запуск обычного бинарника (собранного самостоятельно). Необязательная запись. |

Лаунчер в AppImage пропускает запись `launcher`, обычный бинарник пропускает запись AppImage.

Обновление предлагается, только если `version` отличается от версии установленного лаунчера **и**
хотя бы один файл отличается по SHA-1. Поэтому локальная сборка разработчика с той же версией
никогда не будет перезаписана файлами с сервера, а откат на прошлую версию тоже возможен.

Как устанавливается обновление:

* **Windows.** `GDZLauncher.exe` проверяет `launcher.json` при каждом старте, ещё до открытия интерфейса.
  Новый exe скачивается рядом, текущий переименовывается в `GDZLauncher.exe.old` (Windows разрешает
  переименовать запущенный файл), и лаунчер перезапускается. Кнопка «Обновить» в интерфейсе делает то же.
* **Linux.** Так же заменяется файл `.AppImage` (или обычный бинарник) и лаунчер перезапускается.
* Папка с лаунчером должна быть доступна для записи (не `Program Files`).

В обоих случаях файл сначала скачивается в `*.new` и проверяется по SHA-1, а старый сохраняется как
`*.old` до следующего запуска. Без сети лаунчер просто запускает установленную версию.

Генерация:

```bash
python3 tools/make_manifest.py launcher --version 0.002 --publish ./site \
    --windows-exe ./GDZLauncher.exe --linux-appimage ./GDZLauncher-x86_64.AppImage --news news.json
```

Сборка в GitHub Actions сама прикладывает к релизу `launcher.json` со ссылками на файлы релиза:
его достаточно скопировать в корень API (а новости дописать вручную).

---

## Зеркала Mojang и Forge

Файлы игры по умолчанию скачиваются с официальных серверов. Если они недоступны вашим игрокам,
при сборке можно указать зеркала (структура путей должна совпадать с оригиналом):

| Опция CMake | Оригинал |
| --- | --- |
| `GDZ_VERSION_MANIFEST_URL` | `https://piston-meta.mojang.com/mc/game/version_manifest_v2.json` |
| `GDZ_JAVA_RUNTIME_URL` | `https://launchermeta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json` |
| `GDZ_LIBRARIES_URL` | `https://libraries.minecraft.net/` |
| `GDZ_FORGE_MAVEN_URL` | `https://maven.minecraftforge.net/` |
| `GDZ_RESOURCES_URL` | `https://resources.download.minecraft.net/` |

---

## Где лаунчер хранит данные

| Что | Windows | Linux |
| --- | --- | --- |
| Настройки | `%APPDATA%\GDZLauncher\settings.json` | `~/.config/GDZLauncher/settings.json` |
| Данные (по умолчанию) | `%APPDATA%\GDZLauncher\` | `~/.local/share/GDZLauncher/` |

Внутри папки данных:

* `game/` — папка игры: `mods/`, `config/`, `saves/`, `logs/launcher_output.log` (вывод игры);
* `libraries/`, `versions/`, `assets/` — файлы Minecraft и Forge;
* `runtime/jre-legacy/` — Java 8 от Mojang;
* `cache/client.json` — последняя полученная сборка (для запуска без сети);
* `launcher.log` — журнал последней подготовки к запуску (что скачано, команда запуска, ошибки).
