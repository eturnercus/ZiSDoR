#!/usr/bin/env python3
"""
Сквозной тест лаунчера (Linux).

Поднимает локальный сервер, который изображает Mojang, Forge, Java-рантайм и сервер проекта,
и прогоняет лаунчер по сценариям: первый запуск, синхронизация модов, запуск без сети,
отмена загрузки, ошибка ника, самообновление с перезапуском.

Лаунчер должен быть собран с -DGDZ_E2E_HARNESS=ON (адреса указывают на 127.0.0.1:8765).
Нужны: Java 8 (JDK, для javac и как «скачиваемая» JRE), xvfb-run, WebKitGTK.

    python3 tests/e2e/run_e2e.py --launcher build-e2e/build/launcher
"""
import argparse, hashlib, http.server, io, json, os, shutil, stat, subprocess, sys, threading, zipfile
from functools import partial

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 8765
BASE = "http://127.0.0.1:%d/" % PORT


def sha1(data):
    return hashlib.sha1(data).hexdigest()


def make_jar(entries):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in entries.items():
            z.writestr(name, data)
    return buf.getvalue()


def maven_path(name):
    parts = name.split(":")
    group, artifact, version = parts[0], parts[1], parts[2]
    file = artifact + "-" + version + ("-" + parts[3] if len(parts) > 3 else "")
    return group.replace(".", "/") + "/" + artifact + "/" + version + "/" + file + ".jar"


class Mock:
    """Содержимое тестового сервера."""

    def __init__(self, root, jre, launch_class):
        self.root, self.jre, self.launch_class = root, jre, launch_class

    def write(self, rel, data):
        path = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
        return {"url": BASE + rel, "sha1": sha1(data), "size": len(data)}

    def build(self):
        shutil.rmtree(self.root, ignore_errors=True)
        os.makedirs(self.root)
        self.minecraft()
        self.forge()
        self.java()
        self.project()
        self.launcher_json("0.0.0-test", None)

    def minecraft(self):
        # Настоящий JSON версии 1.7.10 от Mojang; файлы заменены заглушками с верными хешами.
        v = json.load(open(os.path.join(HERE, "mojang-1.7.10.json")))
        for lib in v["libraries"]:
            d = lib.get("downloads", {})
            items = ([("artifact", d["artifact"])] if "artifact" in d else []) + list(d.get("classifiers", {}).items())
            for cls, a in items:
                if cls.startswith("natives"):
                    entries = {"META-INF/MANIFEST.MF": "Manifest-Version: 1.0\n",
                               "lib" + lib["name"].split(":")[1] + "64.so": "native " + cls}
                else:
                    entries = {"README.txt": "fake " + lib["name"]}
                # URL остаётся официальным (libraries.minecraft.net): лаунчер обязан подменить его зеркалом.
                info = self.write("libraries/" + a["path"], make_jar(entries))
                a["sha1"], a["size"] = info["sha1"], info["size"]
        v["downloads"]["client"].update(self.write("mojang/client.jar", make_jar({"Main.class": "fake"})))

        objects = {}
        for name in ["minecraft/sounds/ambient/cave/cave1.ogg", "minecraft/lang/ru_RU.lang", "pack.mcmeta"]:
            data = ("asset " + name).encode()
            h = sha1(data)
            self.write("resources/" + h[:2] + "/" + h, data)
            objects[name] = {"hash": h, "size": len(data)}
        index = self.write("mojang/indexes/1.7.10.json", json.dumps({"objects": objects}).encode())
        v["assetIndex"].update({"url": index["url"], "sha1": index["sha1"], "size": index["size"]})

        version = self.write("mojang/1.7.10.json", json.dumps(v).encode())
        self.write("mojang/version_manifest_v2.json", json.dumps({"versions": [
            {"id": "1.12.2", "url": BASE + "missing.json", "sha1": "0" * 40},
            {"id": "1.7.10", "url": version["url"], "sha1": version["sha1"]}]}).encode())

    def forge(self):
        profile = json.load(open(os.path.join(HERE, "..", "..", "src", "profiles", "forge-1.7.10.json")))
        for lib in profile["libraries"]:
            path = maven_path(lib["name"])
            base = "forge/" if lib["repo"] == "forge" else "libraries/"
            if lib["name"].startswith("net.minecraft:launchwrapper"):
                data = make_jar({"net/minecraft/launchwrapper/Launch.class": self.launch_class})
            else:
                data = make_jar({"README.txt": "fake " + lib["name"]})
            self.write(base + path, data)
            # Как на настоящих Maven-репозиториях рядом лежит .sha1; для lzma его нет (запасной путь лаунчера).
            if not lib["name"].startswith("lzma:"):
                self.write(base + path + ".sha1", (sha1(data) + "  " + os.path.basename(path) + "\n").encode())

    def java(self):
        # Настоящая JRE 8, разложенная в формате манифеста Mojang (файлы, каталоги, ссылки).
        files = {}
        for dirpath, dirnames, filenames in os.walk(self.jre):
            rel_dir = os.path.relpath(dirpath, self.jre)
            for dn in list(dirnames):
                full = os.path.join(dirpath, dn)
                rel = os.path.normpath(os.path.join(rel_dir, dn)).replace(os.sep, "/")
                if os.path.islink(full):
                    dirnames.remove(dn)
                    if not os.readlink(full).startswith("/"):
                        files[rel] = {"type": "link", "target": os.readlink(full)}
                else:
                    files[rel] = {"type": "directory"}
            for fn in filenames:
                full = os.path.join(dirpath, fn)
                rel = os.path.normpath(os.path.join(rel_dir, fn)).replace(os.sep, "/")
                if os.path.islink(full) and not os.readlink(full).startswith("/"):
                    files[rel] = {"type": "link", "target": os.readlink(full)}
                    continue
                real = os.path.realpath(full)
                if not os.path.isfile(real):
                    continue
                data = open(real, "rb").read()
                files[rel] = {"type": "file", "executable": bool(os.stat(real).st_mode & stat.S_IXUSR),
                              "downloads": {"raw": self.write("java/files/" + rel, data)}}
        manifest = self.write("java/jre-legacy-linux.json", json.dumps({"files": files}).encode())
        self.write("java/all.json", json.dumps({"linux": {"jre-legacy": [{
            "manifest": {"url": manifest["url"], "sha1": manifest["sha1"], "size": manifest["size"]},
            "version": {"name": "8-test"}}]}}).encode())

    def project(self):
        files = []
        for path, data in {"mods/alpha.jar": make_jar({"mcmod.info": "alpha"}),
                           "mods/beta.jar": make_jar({"mcmod.info": "beta"}),
                           "mods/sub/gamma.jar": make_jar({"mcmod.info": "gamma"})}.items():
            info = self.write("api/files/" + path, data)
            files.append({"path": path, "url": "files/" + path, "sha1": info["sha1"], "size": info["size"]})
        info = self.write("api/files/config/test.cfg", b"value=1\n")
        files.append({"path": "config/test.cfg", "url": "files/config/test.cfg", "sha1": info["sha1"],
                      "size": info["size"], "mode": "once"})
        self.write("api/client.json", json.dumps({
            "formatVersion": 1, "name": "Тестовая сборка", "minecraft": "1.7.10",
            "server": "play.example.org:25570", "syncDirs": ["mods"], "ignore": ["mods/keep-*.jar"],
            "jvmArgs": ["-Dgdz.test=1"], "files": files}, ensure_ascii=False).encode())

    def launcher_json(self, version, binary):
        linux = []
        if binary:
            info = self.write("api/bin/launcher", open(binary, "rb").read())
            linux.append({"path": "launcher", "url": "bin/launcher", "sha1": info["sha1"], "size": info["size"]})
        self.write("api/launcher.json", json.dumps({
            "version": version, "news": [{"title": "Сервер открыт", "date": "26.09.2026", "text": "Тест"}],
            "linux": linux, "windows": []}, ensure_ascii=False).encode())


class Server:
    def __init__(self, root):
        self.root, self.httpd = root, None

    def start(self):
        class Quiet(http.server.SimpleHTTPRequestHandler):
            def log_message(self, *args):
                pass
        handler = partial(Quiet, directory=self.root)
        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), handler)
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def stop(self):
        if self.httpd:
            self.httpd.shutdown()
            self.httpd.server_close()
            self.httpd = None


class Runner:
    def __init__(self, work):
        self.work = work
        self.home = os.path.join(work, "home")
        self.data = os.path.join(self.home, ".local", "share", "GDZLauncher")
        self.game = os.path.join(self.data, "game")
        self.flag = os.path.join(work, "updated.flag")
        self.failures = []

    def run(self, launcher, scenario):
        env = dict(os.environ, HOME=self.home, XDG_DATA_HOME="", XDG_CONFIG_HOME="", E2E_SCENARIO=scenario,
                   E2E_FLAG_FILE=self.flag, WEBKIT_DISABLE_COMPOSITING_MODE="1")
        os.makedirs(self.home, exist_ok=True)
        p = subprocess.run(["xvfb-run", "-a", launcher], env=env, capture_output=True, text=True, timeout=600)
        results = [json.loads(l[5:]) for l in p.stdout.splitlines() if l.startswith("E2E: ")]
        if not results:
            print(p.stdout[-3000:], p.stderr[-3000:])
        return results

    def check(self, name, cond, detail=""):
        print(("  ok   " if cond else "  FAIL ") + name + ("" if cond else "  " + str(detail)[:400]))
        if not cond:
            self.failures.append(name)

    def events(self, results):
        return [e["type"] for e in (results[-1].get("events", []) if results else [])]

    def report(self):
        path = os.path.join(self.game, "launch_report.txt")
        return open(path).read() if os.path.exists(path) else ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--launcher", required=True, help="лаунчер, собранный с -DGDZ_E2E_HARNESS=ON")
    ap.add_argument("--work", default="/tmp/gdz-e2e")
    ap.add_argument("--java-home", default=os.environ.get("JAVA8_HOME", "/usr/lib/jvm/java-8-openjdk-amd64"))
    args = ap.parse_args()

    work = os.path.abspath(args.work)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    classes = os.path.join(work, "classes")
    os.makedirs(classes)
    subprocess.run([os.path.join(args.java_home, "bin", "javac"), "-encoding", "UTF-8", "-source", "8", "-target", "8",
                    "-d", classes, os.path.join(HERE, "java", "net", "minecraft", "launchwrapper", "Launch.java")],
                   check=True, capture_output=True)
    launch_class = open(os.path.join(classes, "net", "minecraft", "launchwrapper", "Launch.class"), "rb").read()
    jre = os.path.join(args.java_home, "jre") if os.path.isdir(os.path.join(args.java_home, "jre")) else args.java_home

    mock = Mock(os.path.join(work, "www"), jre, launch_class)
    mock.build()
    server = Server(mock.root)
    server.start()
    r = Runner(work)

    try:
        print("[1] первый запуск")
        res = r.run(args.launcher, "play")
        rep = r.report()
        r.check("игра запущена и завершилась с кодом 0", r.events(res)[-2:] == ["launched", "exited"] and res[-1]["events"][-1].get("code") == 0, res)
        r.check("Java из скачанного рантайма", "java.home=" + os.path.join(r.data, "runtime", "jre-legacy") in rep, rep[:300])
        r.check("guava 17 от Forge вместо 15", "guava-17.0.jar" in rep and "guava-15.0.jar" not in rep)
        r.check("commons-lang3 3.3.2 вместо 3.1", "commons-lang3-3.3.2.jar" in rep and "commons-lang3-3.1.jar" not in rep)
        r.check("клиент последним в classpath", [l for l in rep.splitlines() if l.startswith("classpath=")][0].endswith("versions/1.7.10/1.7.10.jar"))
        r.check("нативные файлы распакованы без META-INF", "native-file=liblwjgl-platform64.so" in rep and "native-file=META-INF" not in rep)
        r.check("twitch-platform исключён правилами для Linux", "twitch-platform" not in rep)
        r.check("аргументы Forge и сервера", "arg=cpw.mods.fml.common.launcher.FMLTweaker" in rep and "arg=play.example.org" in rep and "arg=25570" in rep)
        r.check("JVM-аргументы сборки и профиля", "gdz.test=1" in rep and "fml.ignoreInvalidMinecraftCertificates=true" in rep)
        r.check("моды скачаны", all(os.path.isfile(os.path.join(r.game, p)) for p in ["mods/alpha.jar", "mods/beta.jar", "mods/sub/gamma.jar", "config/test.cfg"]))
        r.check("новости получены", res and res[-1].get("check", {}).get("news", [{}])[0].get("title") == "Сервер открыт", res and res[-1].get("check"))

        print("[2] синхронизация и восстановление")
        with open(os.path.join(r.game, "mods", "evil.jar"), "w") as f: f.write("x")
        os.makedirs(os.path.join(r.game, "mods", "deep"), exist_ok=True)
        with open(os.path.join(r.game, "mods", "deep", "extra.jar"), "w") as f: f.write("x")
        with open(os.path.join(r.game, "mods", "keep-minimap.jar"), "w") as f: f.write("x")
        with open(os.path.join(r.game, "mods", "alpha.jar"), "w") as f: f.write("broken")
        with open(os.path.join(r.game, "config", "test.cfg"), "w") as f: f.write("value=999\n")
        guava = os.path.join(r.data, "libraries", "com", "google", "guava", "guava", "17.0", "guava-17.0.jar")
        os.remove(guava)
        os.remove(os.path.join(r.game, "launch_report.txt"))
        res = r.run(args.launcher, "play")
        r.check("запуск после изменений", r.events(res)[-1:] == ["exited"], res)
        r.check("лишний мод удалён", not os.path.exists(os.path.join(r.game, "mods", "evil.jar")))
        r.check("пустая папка удалена", not os.path.exists(os.path.join(r.game, "mods", "deep")))
        r.check("файл из ignore сохранён", os.path.exists(os.path.join(r.game, "mods", "keep-minimap.jar")))
        r.check("испорченный мод восстановлен", open(os.path.join(r.game, "mods", "alpha.jar"), "rb").read() != b"broken")
        r.check("конфиг в режиме once не перезаписан", open(os.path.join(r.game, "config", "test.cfg")).read() == "value=999\n")
        r.check("удалённая библиотека перекачана", os.path.isfile(guava))

        print("[3] без сети")
        server.stop()
        os.remove(os.path.join(r.game, "launch_report.txt"))
        res = r.run(args.launcher, "play")
        r.check("запуск без сети из сохранённых файлов", r.events(res)[-1:] == ["exited"] and os.path.exists(os.path.join(r.game, "launch_report.txt")), res)
        r.check("проверка обновлений сообщает об отсутствии связи", res and res[-1].get("check", {}).get("ok") is False)
        server.start()

        print("[4] ошибки и отмена")
        res = r.run(args.launcher, "badnick")
        r.check("некорректный ник отклонён", r.events(res) == ["error"], res)
        shutil.rmtree(r.data)
        res = r.run(args.launcher, "cancel")
        r.check("загрузка отменена", r.events(res)[-1:] == ["cancelled"], res)
        res = r.run(args.launcher, "play")
        r.check("после отмены запуск докачивает файлы", r.events(res)[-1:] == ["exited"], res)
        parts = [os.path.join(d, f) for d, _, fs in os.walk(r.data) for f in fs if f.endswith(".part")]
        r.check("не осталось *.part", not parts, parts[:3])

        print("[5] самообновление")
        install = os.path.join(work, "install")
        os.makedirs(install)
        shutil.copy2(args.launcher, os.path.join(install, "launcher"))
        newbin = os.path.join(work, "newbin")
        shutil.copy2(args.launcher, newbin)
        with open(newbin, "ab") as f: f.write(b"GDZ-UPDATE-MARKER")
        mock.launcher_json("9.9.9", newbin)
        res = r.run(os.path.join(install, "launcher"), "update")
        scenarios = [x.get("scenario") for x in res]
        r.check("обновление найдено", res and res[0].get("check", {}).get("updateAvailable") is True, res)
        r.check("лаунчер перезапустился после обновления", "after-update" in scenarios, scenarios)
        installed = open(os.path.join(install, "launcher"), "rb").read()
        r.check("бинарник заменён новым", installed.endswith(b"GDZ-UPDATE-MARKER"))
        r.check("остатки обновления удалены", not os.path.exists(os.path.join(install, "launcher.old")), os.listdir(install))
        os.remove(r.flag)
        res = r.run(os.path.join(install, "launcher"), "update")
        r.check("повторное обновление не предлагается", res and res[0].get("check", {}).get("updateAvailable") is False, res)
    finally:
        server.stop()

    print()
    if r.failures:
        print("ПРОВАЛЕНО: %d проверок" % len(r.failures))
        sys.exit(1)
    print("ВСЕ ПРОВЕРКИ ПРОЙДЕНЫ")


if __name__ == "__main__":
    main()
