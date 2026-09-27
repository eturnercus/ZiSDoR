#!/usr/bin/env python3
"""
Генератор файлов API лаунчера (формат описан в docs/API.md).

Сборка (моды и конфиги) -> client.json:

    python3 tools/make_manifest.py client --dir ./pack --publish ./site \\
        --name "Моя сборка" --server play.example.org:25565 \\
        --sync mods --ignore "mods/optifine*.jar" --once "config/**" --once options.txt

    Папка ./pack повторяет папку игры: pack/mods/*.jar, pack/config/... .
    В ./site появятся client.json и files/<путь> — их нужно выложить в корень API (GDZ_API_URL).

Лаунчер (самообновление и новости) -> launcher.json:

    python3 tools/make_manifest.py launcher --version 0.002 --publish ./site \\
        --windows-exe ./GDZLauncher.exe --linux-appimage ./GDZLauncher-x86_64.AppImage --news news.json

    --linux-bin добавляет обычный бинарник Linux (для тех, кто запускает не AppImage, а собранный launcher).

    Вместо --publish можно указать --base-url, если файлы уже лежат в другом месте
    (например, в релизе GitHub): тогда в launcher.json попадут абсолютные ссылки.
"""
import argparse, fnmatch, hashlib, json, os, re, shutil, sys, urllib.parse

# Имена в launcher.json, по которым лаунчер узнаёт свой файл (src/shared/selfupdate.cpp).
WINDOWS_EXE = "GDZLauncher.exe"
LINUX_APPIMAGE = "GDZLauncher-x86_64.AppImage"
LINUX_BINARY = "launcher"


def die(msg):
    print("Ошибка: " + msg, file=sys.stderr)
    sys.exit(1)


def sha1_file(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def safe_relative(rel):
    """Те же правила, что у лаунчера (util::isSafeRelative): иначе он отклонит манифест целиком."""
    if not rel or rel.startswith("/") or "\\" in rel or ":" in rel or len(rel) > 1024:
        return False
    for part in rel.split("/"):
        if part in ("", ".", "..") or part.endswith(" ") or part.endswith("."):
            return False
    return True


def glob_match(pattern, path):
    """'*' и '?' не переходят через '/', '**' — переходит (как в лаунчере)."""
    regex = ""
    i = 0
    while i < len(pattern):
        if pattern.startswith("**", i):
            regex += ".*"
            i += 2
        elif pattern[i] == "*":
            regex += "[^/]*"
            i += 1
        elif pattern[i] == "?":
            regex += "[^/]"
            i += 1
        else:
            regex += re.escape(pattern[i])
            i += 1
    return re.fullmatch(regex, path) is not None


def url_path(rel):
    return "/".join(urllib.parse.quote(p) for p in rel.split("/"))


def copy_to(publish, rel, src):
    if not publish:
        return
    dst = os.path.join(publish, *rel.split("/"))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def write_json(path, data):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print("Записан " + path)


def cmd_client(a):
    if not os.path.isdir(a.dir):
        die("нет папки " + a.dir)
    for d in a.sync:
        if not safe_relative(d):
            die("недопустимая папка синхронизации: " + d)

    files = []
    for root, dirs, names in os.walk(a.dir):
        dirs.sort()
        for name in sorted(names):
            full = os.path.join(root, name)
            rel = os.path.relpath(full, a.dir).replace(os.sep, "/")
            if any(fnmatch.fnmatch(name, pat) for pat in a.exclude):
                continue
            if not safe_relative(rel):
                die("недопустимое имя файла (пробел или точка в конце, двоеточие, обратный слэш): " + rel)
            url_rel = "files/" + url_path(rel)
            entry = {"path": rel, "url": url_rel, "sha1": sha1_file(full), "size": os.path.getsize(full)}
            if any(glob_match(p, rel) for p in a.once):
                entry["mode"] = "once"
            files.append(entry)
            copy_to(a.publish, "files/" + rel, full)

    manifest = {"formatVersion": 1, "name": a.name, "minecraft": "1.7.10"}
    if a.server:
        manifest["server"] = a.server
    manifest["syncDirs"] = a.sync
    manifest["ignore"] = a.ignore
    manifest["jvmArgs"] = a.jvm_arg
    manifest["files"] = files

    out = a.out or (os.path.join(a.publish, "client.json") if a.publish else "client.json")
    write_json(out, manifest)
    print("Файлов: %d, всего %.1f МБ" % (len(files), sum(f["size"] for f in files) / 1048576))


def cmd_launcher(a):
    def entry(path, name, prefix):
        if not os.path.isfile(path):
            die("нет файла " + path)
        rel = prefix + name
        url = (a.base_url.rstrip("/") + "/" + name) if a.base_url else url_path(rel)
        copy_to(a.publish if not a.base_url else None, rel, path)
        return {"path": name, "url": url, "sha1": sha1_file(path), "size": os.path.getsize(path)}

    windows, linux = [], []
    if a.windows_exe:
        windows.append(entry(a.windows_exe, WINDOWS_EXE, "bin/windows/"))
    if a.linux_appimage:
        linux.append(entry(a.linux_appimage, LINUX_APPIMAGE, "bin/linux/"))
    if a.linux_bin:
        linux.append(entry(a.linux_bin, LINUX_BINARY, "bin/linux/"))

    news = []
    if a.news:
        with open(a.news, encoding="utf-8") as f:
            news = json.load(f)
        if not isinstance(news, list):
            die("файл новостей должен содержать JSON-массив")

    out = a.out or (os.path.join(a.publish, "launcher.json") if a.publish else "launcher.json")
    write_json(out, {"version": a.version, "news": news, "windows": windows, "linux": linux})


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("client", help="client.json из папки сборки")
    c.add_argument("--dir", required=True, help="папка сборки (как папка игры: mods/, config/ ...)")
    c.add_argument("--publish", help="куда сложить client.json и files/ для выкладки на сервер")
    c.add_argument("--out", help="путь к client.json (по умолчанию <publish>/client.json)")
    c.add_argument("--name", default="", help="название сборки")
    c.add_argument("--server", default="", help="host или host:port для автоподключения")
    c.add_argument("--sync", action="append", default=[], help="папка, где лишние файлы удаляются (можно несколько)")
    c.add_argument("--ignore", action="append", default=[], help="шаблон файлов, которые синхронизация не удаляет")
    c.add_argument("--once", action="append", default=[], help="шаблон файлов, которые скачиваются только если их нет")
    c.add_argument("--jvm-arg", action="append", default=[], help="дополнительный аргумент JVM")
    c.add_argument("--exclude", action="append", default=[".DS_Store", "Thumbs.db", "desktop.ini"], help="имена файлов, которые не публикуются")
    c.set_defaults(func=cmd_client)

    l = sub.add_parser("launcher", help="launcher.json для самообновления и новостей")
    l.add_argument("--version", required=True, help="версия, как в project(GDZLauncher VERSION ...)")
    l.add_argument("--windows-exe", help="GDZLauncher.exe для Windows")
    l.add_argument("--linux-appimage", help="GDZLauncher-x86_64.AppImage для Linux")
    l.add_argument("--linux-bin", help="обычный бинарник launcher для Linux (необязательно)")
    l.add_argument("--news", help="JSON-массив новостей: [{\"title\", \"date\", \"text\"}]")
    l.add_argument("--publish", help="куда сложить launcher.json и bin/ для выкладки на сервер")
    l.add_argument("--base-url", help="абсолютный адрес, где уже лежат файлы (например, релиз GitHub)")
    l.add_argument("--out", help="путь к launcher.json (по умолчанию <publish>/launcher.json)")
    l.set_defaults(func=cmd_launcher)

    a = ap.parse_args()
    a.func(a)


if __name__ == "__main__":
    main()
