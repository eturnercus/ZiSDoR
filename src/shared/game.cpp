#include "game.hpp"
#include "archive.hpp"
#include "config.hpp"
#include "embedded.hpp"
#include "net.hpp"
#include "sha1.hpp"
#include "util.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <thread>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
	constexpr const char* kMcVersion = "1.7.10";
	constexpr int kWorkers = 8;

#if defined(_WIN32) && !defined(_WIN64)
	constexpr const char* kArch = "32";
#else
	constexpr const char* kArch = "64";
#endif

	// ------------------------------------------------------------------ вспомогательное

	struct FileSpec {
		fs::path dest;
		std::string url;
		std::string sha1;           // пусто: неизвестна
		std::uint64_t size = 0;     // 0: неизвестен
		bool companionSha1 = false; // запросить хеш из "<url>.sha1" (так публикует Maven)
		bool quick = false;         // проверять только размер (объекты ресурсов: имя файла и есть хеш)
		bool executable = false;
	};

	void checkCancel(const std::atomic<bool>& cancel) {
		if (cancel.load()) throw game::Cancelled();
	}

	void pause(int attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(700 * (attempt + 1)));
	}

	std::string readFile(const fs::path& p) {
		std::ifstream in(p, std::ios::binary);
		if (!in) return {};
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}

	void writeFileAtomic(const fs::path& p, const std::string& data) {
		std::error_code ec;
		fs::create_directories(p.parent_path(), ec);
		fs::path tmp = p;
		tmp += ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			out << data;
			if (!out) throw game::Error("Не удалось записать " + p.u8string());
		}
		fs::rename(tmp, p, ec);
		if (ec) throw game::Error("Не удалось записать " + p.u8string() + ": " + ec.message());
	}

	json parseJson(const std::string& text, const std::string& what) {
		try {
			return json::parse(text);
		} catch (const std::exception& e) {
			throw game::Error("Некорректные данные (" + what + "): " + e.what());
		}
	}

	void setExecutable(const fs::path& p) {
#ifndef _WIN32
		std::error_code ec;
		fs::permissions(p, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#else
		(void)p;
#endif
	}

	/// Адреса официального репозитория библиотек подменяются зеркалом, если оно задано при сборке.
	std::string mirror(const std::string& url) {
		static const std::string official = "https://libraries.minecraft.net/";
		if (url.rfind(official, 0) == 0 && std::string(GDZ_LIBRARIES_URL) != official) {
			return util::joinUrl(GDZ_LIBRARIES_URL, url.substr(official.size()));
		}
		return url;
	}

	/// Текст по сети с повторами и необязательной проверкой SHA-1. Сетевые ошибки -> game::Error.
	/// attempts = 1 там, где есть сохранённая копия: без сети не ждём повторов, а сразу работаем офлайн.
	std::string fetchText(const std::string& url, const std::atomic<bool>& cancel, const std::string& expectSha1 = {}, int attempts = 3) {
		std::string err;
		for (int attempt = 0; attempt < attempts; ++attempt) {
			checkCancel(cancel);
			std::string out;
			net::Result r = net::fetch(url, out, &cancel);
			if (r.cancelled) throw game::Cancelled();
			if (r.ok) {
				if (!expectSha1.empty() && sha1::ofString(out) != expectSha1) {
					err = "Контрольная сумма не совпала: " + url;
					continue;
				}
				return out;
			}
			err = r.error;
			if (!r.retryable() || attempt + 1 >= attempts) break;
			pause(attempt);
		}
		throw game::Error(err);
	}

	bool isValid(const FileSpec& f) {
		std::error_code ec;
		if (!fs::is_regular_file(f.dest, ec)) return false;
		std::uint64_t size = fs::file_size(f.dest, ec);
		if (ec) return false;
		if (f.size && size != f.size) return false;
		if (f.quick) return size > 0 || f.size == 0;
		if (!f.sha1.empty()) {
			auto h = sha1::ofFile(f.dest);
			return h && *h == f.sha1;
		}
		// Хеш неизвестен: файл был проверен при загрузке (запись атомарная), достаточно наличия.
		return size > 0;
	}

	void fetchFile(const FileSpec& f, const std::atomic<bool>& cancel, const std::function<void(std::uint64_t)>& addBytes) {
		std::string expected = f.sha1;
		if (expected.empty() && f.companionSha1) {
			std::string text;
			net::Result r = net::fetch(f.url + ".sha1", text, &cancel, 4096);
			if (r.cancelled) throw game::Cancelled();
			if (r.ok) {
				std::string h = util::lower(text.substr(0, std::min<std::size_t>(40, text.size())));
				if (util::isSha1(h)) expected = h;
			}
		}

		std::string err;
		for (int attempt = 0; attempt < 3; ++attempt) {
			checkCancel(cancel);
			std::uint64_t last = 0;
			net::Result r = net::download(f.url, f.dest, [&](std::uint64_t done, std::uint64_t) {
				if (done > last) { addBytes(done - last); last = done; }
			}, &cancel);
			if (r.cancelled) throw game::Cancelled();
			if (!r.ok) {
				err = r.error;
				if (!r.retryable()) break;
				pause(attempt);
				continue;
			}

			std::error_code ec;
			if (f.size && fs::file_size(f.dest, ec) != f.size) {
				err = "Размер файла не совпадает: " + f.url;
				fs::remove(f.dest, ec);
				continue;
			}
			if (!expected.empty()) {
				auto h = sha1::ofFile(f.dest);
				if (!h || *h != expected) {
					err = "Контрольная сумма не совпадает: " + f.url;
					fs::remove(f.dest, ec);
					continue;
				}
			} else if (f.dest.extension() == ".jar" && !archive::isValidZip(f.dest)) {
				err = "Повреждённый архив: " + f.url;
				fs::remove(f.dest, ec);
				continue;
			}
			if (f.executable) setExecutable(f.dest);
			return;
		}
		throw game::Error("Не удалось скачать файл.\n" + err);
	}

	/// Проверяет список файлов и параллельно докачивает недостающие или повреждённые.
	void syncFiles(const std::vector<FileSpec>& files, const std::string& title, game::Events& ev, const std::atomic<bool>& cancel) {
		ev.stage(title);
		if (files.empty()) { ev.progress(0, 0, 0); return; }

		std::atomic<std::size_t> next{ 0 }, done{ 0 };
		std::atomic<std::uint64_t> bytes{ 0 };
		std::atomic<bool> stop{ false };
		std::atomic<int> active{ 0 };
		std::mutex errMutex;
		std::exception_ptr firstError;

		auto worker = [&] {
			for (;;) {
				if (stop.load() || cancel.load()) return;
				std::size_t i = next.fetch_add(1);
				if (i >= files.size()) return;
				const FileSpec& f = files[i];
				try {
					if (!isValid(f)) {
						ev.log("Загрузка: " + f.url);
						fetchFile(f, cancel, [&](std::uint64_t n) { bytes += n; });
					} else if (f.executable) {
						setExecutable(f.dest);
					}
				} catch (...) {
					std::lock_guard<std::mutex> lock(errMutex);
					if (!firstError) firstError = std::current_exception();
					stop = true;
					return;
				}
				++done;
			}
		};

		int count = static_cast<int>(std::min<std::size_t>(kWorkers, files.size()));
		std::vector<std::thread> threads;
		active = count;
		for (int i = 0; i < count; ++i) {
			threads.emplace_back([&] { worker(); --active; });
		}
		while (active.load() > 0) {
			ev.progress(done.load(), files.size(), bytes.load());
			std::this_thread::sleep_for(std::chrono::milliseconds(120));
		}
		for (auto& t : threads) t.join();
		ev.progress(done.load(), files.size(), bytes.load());

		if (firstError) std::rethrow_exception(firstError);
		checkCancel(cancel);
	}

	// ------------------------------------------------------------------ профиль Forge

	struct Profile {
		std::string id, title, minecraft, mainClass, tweakClass;
		std::vector<std::string> jvmArgs;
		json libraries;
	};

	const Profile& forgeProfile() {
		static const Profile profile = [] {
			json j = json::parse(reinterpret_cast<const char*>(embedded::forge_profile));
			Profile p;
			p.id = j.at("id").get<std::string>();
			p.title = j.at("title").get<std::string>();
			p.minecraft = j.at("minecraft").get<std::string>();
			p.mainClass = j.at("mainClass").get<std::string>();
			p.tweakClass = j.value("tweakClass", "");
			p.jvmArgs = j.value("jvmArgs", std::vector<std::string>{});
			p.libraries = j.at("libraries");
			return p;
		}();
		return profile;
	}

	// ------------------------------------------------------------------ библиотеки

	struct Library {
		std::string key;                   // group:artifact (для замены ванильных библиотек версиями Forge)
		FileSpec file;
		bool native = false;
		std::vector<std::string> exclude;  // префиксы, не распаковываемые из нативного архива
	};

	std::string libraryKey(const std::string& name) {
		auto first = name.find(':');
		if (first == std::string::npos) return name;
		auto second = name.find(':', first + 1);
		return second == std::string::npos ? name : name.substr(0, second);
	}

	bool rulesAllow(const json& lib) {
		if (!lib.contains("rules")) return true;
		bool allowed = false;
		for (const auto& rule : lib["rules"]) {
			bool match = true;
			if (rule.contains("os")) {
				const auto& os = rule["os"];
				if (os.contains("name") && os["name"].get<std::string>() != util::osName()) match = false;
				if (os.contains("arch") && os["arch"].get<std::string>() == "x86" && std::string(kArch) == "64") match = false;
			}
			if (match) allowed = rule.value("action", "allow") == "allow";
		}
		return allowed;
	}

	FileSpec artifactSpec(const json& a, const fs::path& libDir) {
		std::string path = a.at("path").get<std::string>();
		auto dest = util::safeJoin(libDir, path);
		if (!dest) throw game::Error("Недопустимый путь библиотеки: " + path);
		FileSpec f;
		f.dest = *dest;
		f.url = mirror(a.at("url").get<std::string>());
		f.sha1 = util::lower(a.value("sha1", ""));
		f.size = a.value("size", std::uint64_t{ 0 });
		return f;
	}

	std::vector<Library> collectLibraries(const json& vanilla, const fs::path& libDir) {
		std::vector<Library> forgeLibs, result;
		std::set<std::string> forgeKeys;

		for (const auto& l : forgeProfile().libraries) {
			std::string name = l.at("name").get<std::string>();
			std::string path = util::mavenPath(name);
			auto dest = util::safeJoin(libDir, path);
			if (path.empty() || !dest) throw game::Error("Некорректная библиотека в профиле Forge: " + name);
			std::string base = l.value("repo", "mojang") == "forge" ? GDZ_FORGE_MAVEN_URL : GDZ_LIBRARIES_URL;
			Library lib;
			lib.key = libraryKey(name);
			lib.file.dest = *dest;
			lib.file.url = util::joinUrl(base, path);
			lib.file.sha1 = util::lower(l.value("sha1", ""));
			lib.file.companionSha1 = lib.file.sha1.empty();
			forgeKeys.insert(lib.key);
			forgeLibs.push_back(lib);
		}

		result = forgeLibs;
		for (const auto& l : vanilla.at("libraries")) {
			if (!rulesAllow(l)) continue;
			std::string name = l.at("name").get<std::string>();
			std::string key = libraryKey(name);
			json downloads = l.value("downloads", json::object());

			if (l.contains("natives")) {
				const auto& natives = l["natives"];
				if (natives.contains(util::osName())) {
					std::string cls = natives[util::osName()].get<std::string>();
					auto pos = cls.find("${arch}");
					if (pos != std::string::npos) cls.replace(pos, 7, kArch);
					if (downloads.contains("classifiers") && downloads["classifiers"].contains(cls)) {
						Library lib;
						lib.key = key + ":" + cls;
						lib.file = artifactSpec(downloads["classifiers"][cls], libDir);
						lib.native = true;
						lib.exclude = { "META-INF/" };
						if (l.contains("extract")) lib.exclude = l["extract"].value("exclude", lib.exclude);
						result.push_back(lib);
					}
				}
			}

			if (forgeKeys.count(key)) continue; // Forge приносит свою версию (guava 17, commons-lang3 3.3.2 и т.д.)

			if (downloads.contains("artifact")) {
				Library lib;
				lib.key = key;
				lib.file = artifactSpec(downloads["artifact"], libDir);
				result.push_back(lib);
			} else if (!l.contains("natives")) {
				std::string path = util::mavenPath(name);
				auto dest = util::safeJoin(libDir, path);
				if (path.empty() || !dest) continue;
				Library lib;
				lib.key = key;
				lib.file.dest = *dest;
				lib.file.url = util::joinUrl(l.value("url", std::string(GDZ_LIBRARIES_URL)), path);
				lib.file.companionSha1 = true;
				result.push_back(lib);
			}
		}
		return result;
	}

	// ------------------------------------------------------------------ Minecraft

	json loadVanilla(const fs::path& data, game::Events& ev, const std::atomic<bool>& cancel) {
		fs::path path = data / "versions" / kMcVersion / (std::string(kMcVersion) + ".json");
		std::string cached = readFile(path);
		if (!cached.empty()) {
			try {
				json v = json::parse(cached);
				if (v.contains("libraries") && v.contains("downloads")) return v;
			} catch (...) {}
		}

		ev.stage("Получение данных Minecraft " + std::string(kMcVersion));
		json manifest = parseJson(fetchText(GDZ_VERSION_MANIFEST_URL, cancel), "список версий Minecraft");
		for (const auto& v : manifest.at("versions")) {
			if (v.value("id", "") != kMcVersion) continue;
			std::string text = fetchText(v.at("url").get<std::string>(), cancel, util::lower(v.value("sha1", "")));
			json parsed = parseJson(text, "Minecraft " + std::string(kMcVersion));
			writeFileAtomic(path, text);
			return parsed;
		}
		throw game::Error("Версия " + std::string(kMcVersion) + " не найдена в списке версий Mojang");
	}

	struct Assets {
		fs::path dir;
		std::string id;
	};

	Assets ensureAssets(const json& vanilla, const fs::path& data, const fs::path& gameDir, game::Events& ev, const std::atomic<bool>& cancel) {
		const json& ai = vanilla.at("assetIndex");
		std::string id = ai.at("id").get<std::string>();
		std::string sha = util::lower(ai.value("sha1", ""));
		fs::path assetsDir = data / "assets";
		fs::path indexPath = assetsDir / "indexes" / (id + ".json");

		std::string text = readFile(indexPath);
		if (text.empty() || (!sha.empty() && sha1::ofString(text) != sha)) {
			ev.stage("Получение списка ресурсов");
			try {
				text = fetchText(mirror(ai.at("url").get<std::string>()), cancel, sha, text.empty() ? 3 : 1);
				writeFileAtomic(indexPath, text);
			} catch (const game::Error&) {
				if (text.empty()) throw;
				ev.log("Список ресурсов не обновлён, используется сохранённый");
			}
		}

		json index = parseJson(text, "список ресурсов");
		bool isVirtual = index.value("virtual", false);
		bool mapToResources = index.value("map_to_resources", false);

		std::vector<FileSpec> files;
		std::vector<std::pair<std::string, FileSpec>> named;
		for (const auto& item : index.at("objects").items()) {
			std::string hash = util::lower(item.value().at("hash").get<std::string>());
			if (!util::isSha1(hash)) continue;
			std::string rel = hash.substr(0, 2) + "/" + hash;
			FileSpec f;
			f.dest = assetsDir / "objects" / hash.substr(0, 2) / hash;
			f.url = util::joinUrl(GDZ_RESOURCES_URL, rel);
			f.sha1 = hash;
			f.size = item.value().value("size", std::uint64_t{ 0 });
			f.quick = true;
			files.push_back(f);
			named.emplace_back(item.key(), f);
		}
		syncFiles(files, "Загрузка ресурсов игры", ev, cancel);

		// Старые индексы требуют копий ресурсов под исходными именами.
		fs::path virtualDir = assetsDir / "virtual" / id;
		if (isVirtual || mapToResources) {
			std::error_code ec;
			for (const auto& [name, f] : named) {
				auto target = util::safeJoin(isVirtual ? virtualDir : gameDir / "resources", name);
				if (!target) continue;
				if (fs::exists(*target, ec) && fs::file_size(*target, ec) == f.size) continue;
				fs::create_directories(target->parent_path(), ec);
				fs::copy_file(f.dest, *target, fs::copy_options::overwrite_existing, ec);
			}
		}
		return { isVirtual ? virtualDir : assetsDir, id };
	}

	// ------------------------------------------------------------------ Java

	fs::path ensureJava(const game::Options& opt, const fs::path& data, game::Events& ev, const std::atomic<bool>& cancel) {
		if (!opt.javaPath.empty()) {
			fs::path custom = fs::u8path(opt.javaPath);
			std::error_code ec;
			if (!fs::is_regular_file(custom, ec)) throw game::Error("Java не найдена по указанному пути:\n" + opt.javaPath);
			return custom;
		}

#ifdef _WIN32
		const char* platform = (std::string(kArch) == "64") ? "windows-x64" : "windows-x86";
		const char* binName = "javaw.exe";
#else
		const char* platform = "linux";
		const char* binName = "java";
#endif
		fs::path root = data / "runtime" / "jre-legacy";
		fs::path bin = root / "bin" / binName;
		fs::path marker = root / ".gdz-manifest";
		std::error_code ec;

		ev.stage("Проверка Java 8");
		std::string allText;
		try {
			allText = fetchText(GDZ_JAVA_RUNTIME_URL, cancel, {}, fs::is_regular_file(bin, ec) ? 1 : 3);
		} catch (const game::Error& e) {
			if (fs::is_regular_file(bin, ec)) {
				ev.log(std::string("Сервер Java недоступен, используется установленная: ") + e.what());
				return bin;
			}
			throw game::Error(std::string("Не удалось получить Java 8.\n") + e.what());
		}

		json all = parseJson(allText, "список сред Java");
		if (!all.contains(platform) || !all[platform].contains("jre-legacy") || all[platform]["jre-legacy"].empty()) {
			throw game::Error("Mojang не предоставляет Java 8 для этой системы. Укажите путь к Java 8 в настройках.");
		}
		const json& manifestRef = all[platform]["jre-legacy"][0].at("manifest");
		std::string manifestUrl = manifestRef.at("url").get<std::string>();
		std::string manifestSha = util::lower(manifestRef.value("sha1", ""));

		if (readFile(marker) == manifestSha && fs::is_regular_file(bin, ec)) return bin;

		json manifest = parseJson(fetchText(manifestUrl, cancel, manifestSha), "состав Java 8");
		std::vector<FileSpec> files;
		std::vector<std::pair<fs::path, std::string>> links;
		for (const auto& item : manifest.at("files").items()) {
			auto dest = util::safeJoin(root, item.key());
			if (!dest) continue;
			const json& e = item.value();
			std::string type = e.value("type", "");
			if (type == "directory") {
				fs::create_directories(*dest, ec);
			} else if (type == "file") {
				const json& raw = e.at("downloads").at("raw");
				FileSpec f;
				f.dest = *dest;
				f.url = raw.at("url").get<std::string>();
				f.sha1 = util::lower(raw.value("sha1", ""));
				f.size = raw.value("size", std::uint64_t{ 0 });
				f.executable = e.value("executable", false);
				files.push_back(f);
			} else if (type == "link") {
				links.emplace_back(*dest, e.value("target", ""));
			}
		}
		syncFiles(files, "Загрузка Java 8", ev, cancel);
#ifndef _WIN32
		for (const auto& [path, target] : links) {
			if (target.empty() || target.front() == '/') continue;
			fs::remove(path, ec);
			fs::create_directories(path.parent_path(), ec);
			fs::create_symlink(target, path, ec);
		}
#endif
		if (!fs::is_regular_file(bin, ec)) throw game::Error("После установки Java 8 не найден файл " + bin.u8string());
		writeFileAtomic(marker, manifestSha);
		return bin;
	}

	// ------------------------------------------------------------------ сборка проекта

	struct PackFile {
		std::string path;
		FileSpec file;
		bool once = false;
	};

	struct Pack {
		bool present = false;
		std::string name;
		std::string server;
		std::vector<PackFile> files;
		std::vector<std::string> syncDirs, ignore, jvmArgs;
	};

	Pack loadPack(const fs::path& data, const fs::path& gameDir, game::Events& ev, const std::atomic<bool>& cancel) {
		Pack pack;
		const std::string api = GDZ_API_URL;
		if (api.empty()) return pack;

		fs::path cache = data / "cache" / "client.json";
		std::error_code ec;
		const bool haveCache = fs::is_regular_file(cache, ec);
		std::string text;
		ev.stage("Получение списка файлов сборки");
		try {
			text = fetchText(util::joinUrl(api, "client.json"), cancel, {}, haveCache ? 1 : 3);
			parseJson(text, "client.json");
			writeFileAtomic(cache, text);
		} catch (const game::Error& e) {
			text = readFile(cache);
			if (text.empty()) throw game::Error(std::string("Не удалось получить сборку с сервера проекта.\n") + e.what());
			ev.log(std::string("Сервер сборки недоступен, используется сохранённый список: ") + e.what());
		}

		json j = parseJson(text, "client.json");
		try {
			if (j.value("formatVersion", 0) != 1) throw game::Error("Неподдерживаемый формат client.json (ожидается formatVersion 1)");
			std::string mc = j.value("minecraft", std::string(kMcVersion));
			if (mc != kMcVersion) throw game::Error("Сборка рассчитана на Minecraft " + mc + ", а лаунчер запускает " + kMcVersion);

			pack.present = true;
			pack.name = j.value("name", "");
			pack.server = j.value("server", "");
			std::string base = util::joinUrl(api, j.value("baseUrl", ""));

			for (const auto& f : j.value("files", json::array())) {
				PackFile pf;
				pf.path = f.at("path").get<std::string>();
				auto dest = util::safeJoin(gameDir, pf.path);
				if (!dest) throw game::Error("Недопустимый путь в client.json: " + pf.path);
				pf.file.dest = *dest;
				pf.file.url = util::joinUrl(base, f.value("url", pf.path));
				pf.file.sha1 = util::lower(f.at("sha1").get<std::string>());
				if (!util::isSha1(pf.file.sha1)) throw game::Error("Некорректная контрольная сумма в client.json: " + pf.path);
				pf.file.size = f.value("size", std::uint64_t{ 0 });
				std::string mode = f.value("mode", "sync");
				if (mode != "sync" && mode != "once") throw game::Error("Неизвестный режим \"" + mode + "\" для " + pf.path);
				pf.once = (mode == "once");
				pack.files.push_back(pf);
			}
			for (const auto& d : j.value("syncDirs", std::vector<std::string>{})) {
				if (!util::isSafeRelative(d)) throw game::Error("Недопустимая папка синхронизации: " + d);
				pack.syncDirs.push_back(d);
			}
			pack.ignore = j.value("ignore", std::vector<std::string>{});
			pack.jvmArgs = j.value("jvmArgs", std::vector<std::string>{});
		} catch (const json::exception& e) {
			throw game::Error(std::string("Ошибка в client.json: ") + e.what());
		}
		return pack;
	}

	std::string normalizeCase(std::string s) {
#ifdef _WIN32
		return util::lower(s); // файловая система Windows не различает регистр
#else
		return s;
#endif
	}

	void syncPack(const Pack& pack, const fs::path& gameDir, game::Events& ev, const std::atomic<bool>& cancel) {
		std::error_code ec;
		std::vector<FileSpec> files;
		std::set<std::string> listed;
		for (const auto& pf : pack.files) {
			listed.insert(normalizeCase(pf.path));
			if (pf.once && fs::exists(pf.file.dest, ec)) continue; // пользователь мог изменить файл
			files.push_back(pf.file);
		}
		syncFiles(files, "Синхронизация сборки", ev, cancel);

		// Удаляем из папок синхронизации всё, чего нет в списке сервера (кроме исключений ignore).
		for (const auto& dir : pack.syncDirs) {
			fs::path root = gameDir / fs::u8path(dir);
			if (!fs::is_directory(root, ec)) continue;
			std::vector<fs::path> extra, dirs;
			for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
			     it != fs::recursive_directory_iterator(); it.increment(ec)) {
				if (ec) break;
				if (it->is_directory(ec) && !it->is_symlink(ec)) { dirs.push_back(it->path()); continue; }
				if (!it->is_regular_file(ec) && !it->is_symlink(ec)) continue;
				std::string rel = fs::relative(it->path(), gameDir, ec).generic_u8string();
				if (ec || rel.empty()) continue;
				if (listed.count(normalizeCase(rel))) continue;
				bool keep = false;
				for (const auto& pattern : pack.ignore) {
					if (util::globMatch(normalizeCase(pattern), normalizeCase(rel))) { keep = true; break; }
				}
				if (!keep) extra.push_back(it->path());
			}
			for (const auto& p : extra) {
				fs::remove(p, ec);
				ev.log("Удалён лишний файл: " + fs::relative(p, gameDir, ec).generic_u8string());
			}
			// Опустевшие подпапки убираем, начиная с самых глубоких (fs::remove не трогает непустые).
			std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });
			for (const auto& d : dirs) fs::remove(d, ec);
		}
	}

	// ------------------------------------------------------------------ запуск

	std::pair<std::string, std::string> splitServer(const std::string& address) {
		auto colon = address.rfind(':');
		if (colon == std::string::npos) return { address, "25565" };
		return { address.substr(0, colon), address.substr(colon + 1) };
	}

	fs::path prepareNatives(const std::vector<Library>& libs, const fs::path& data) {
		// Каждый запуск в отдельной папке: прошлый экземпляр игры может держать открытыми DLL.
		fs::path base = data / "natives";
		std::error_code ec;
		if (fs::is_directory(base, ec)) {
			for (const auto& entry : fs::directory_iterator(base, ec)) fs::remove_all(entry.path(), ec);
		}
		auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		fs::path dir = base / std::to_string(stamp);
		fs::create_directories(dir, ec);
		if (ec) throw game::Error("Не удалось создать папку " + dir.u8string());
		for (const auto& lib : libs) {
			if (!lib.native) continue;
			std::string err;
			if (!archive::extract(lib.file.dest, dir, lib.exclude, err)) throw game::Error(err);
		}
		return dir;
	}

	std::string substitute(const std::string& token, const std::map<std::string, std::string>& vars) {
		std::string out;
		std::size_t pos = 0;
		while (pos < token.size()) {
			auto start = token.find("${", pos);
			if (start == std::string::npos) { out += token.substr(pos); break; }
			auto end = token.find('}', start);
			if (end == std::string::npos) { out += token.substr(pos); break; }
			out += token.substr(pos, start - pos);
			auto it = vars.find(token.substr(start + 2, end - start - 2));
			out += (it != vars.end()) ? it->second : token.substr(start, end - start + 1);
			pos = end + 1;
		}
		return out;
	}
}

// ---------------------------------------------------------------------- публичный интерфейс

fs::path game::dataDir(const Options& opt) {
	return opt.dataDir.empty() ? util::defaultDataDir() : fs::u8path(opt.dataDir);
}

fs::path game::gameDir(const Options& opt) {
	return dataDir(opt) / "game";
}

std::string game::packTitle() {
	return "Minecraft " + forgeProfile().minecraft + " · " + forgeProfile().title;
}

game::Prepared game::prepare(const Options& opt, Events& ev, const std::atomic<bool>& cancel) {
	static const std::regex nickRe("^[A-Za-z0-9_]{3,16}$");
	if (!std::regex_match(opt.nick, nickRe)) throw Error("Никнейм должен содержать от 3 до 16 символов: латиница, цифры и _");

	const fs::path data = dataDir(opt);
	const fs::path gdir = gameDir(opt);
	std::error_code ec;
	fs::create_directories(gdir / "logs", ec);
	if (ec) throw Error("Нет доступа к папке " + data.u8string() + ": " + ec.message());
	ev.log("Каталог данных: " + data.u8string());

	try {
		// 1. Сборка проекта (список модов) — первой: при ошибке сервера пользователь узнает сразу.
		Pack pack = loadPack(data, gdir, ev, cancel);

		// 2. Java 8.
		fs::path java = ensureJava(opt, data, ev, cancel);

		// 3. Minecraft и Forge.
		json vanilla = loadVanilla(data, ev, cancel);
		std::vector<Library> libs = collectLibraries(vanilla, data / "libraries");

		std::vector<FileSpec> libFiles;
		for (const auto& l : libs) libFiles.push_back(l.file);
		const json& client = vanilla.at("downloads").at("client");
		FileSpec clientJar;
		clientJar.dest = data / "versions" / kMcVersion / (std::string(kMcVersion) + ".jar");
		clientJar.url = client.at("url").get<std::string>();
		clientJar.sha1 = util::lower(client.value("sha1", ""));
		clientJar.size = client.value("size", std::uint64_t{ 0 });
		libFiles.push_back(clientJar);
		syncFiles(libFiles, "Загрузка Minecraft и Forge", ev, cancel);

		// 4. Ресурсы.
		Assets assets = ensureAssets(vanilla, data, gdir, ev, cancel);

		// 5. Моды и конфиги.
		if (pack.present) syncPack(pack, gdir, ev, cancel);

		// 6. Нативные библиотеки.
		ev.stage("Подготовка к запуску");
		fs::path natives = prepareNatives(libs, data);

		// 7. Команда запуска.
		const Profile& profile = forgeProfile();
		int ram = std::clamp(opt.ramMb, 512, 65536);
#ifdef _WIN32
		const char sep = ';';
#else
		const char sep = ':';
#endif
		std::string classpath;
		for (const auto& l : libs) {
			if (l.native) continue;
			if (!classpath.empty()) classpath += sep;
			classpath += util::javaPath(l.file.dest);
		}
		classpath += sep;
		classpath += util::javaPath(clientJar.dest);

		Prepared out;
		out.gameDir = gdir;
		out.logFile = gdir / "logs" / "launcher_output.log";
#ifdef _WIN32
		out.args.push_back(java.u8string());
#else
		out.args.push_back(java.string());
#endif
		out.args.push_back("-Xmx" + std::to_string(ram) + "M");
		out.args.push_back("-Xms" + std::to_string(std::min(ram, 512)) + "M");
#ifdef _WIN32
		// Параметр официального лаунчера: драйверы Intel включают для процесса с этим аргументом правильный профиль.
		out.args.push_back("-XX:HeapDumpPath=MojangTricksIntelDriversForPerformance_javaw.exe_minecraft.exe.heapdump");
#endif
		out.args.push_back("-Djava.library.path=" + util::javaPath(natives));
		for (const auto& a : profile.jvmArgs) out.args.push_back(a);
		for (const auto& a : pack.jvmArgs) out.args.push_back(a);
		out.args.push_back("-cp");
		out.args.push_back(classpath);
		out.args.push_back(profile.mainClass);

		std::string uuid = util::offlineUuid(opt.nick);
		uuid.erase(std::remove(uuid.begin(), uuid.end(), '-'), uuid.end());
		const std::map<std::string, std::string> vars = {
			{ "auth_player_name", opt.nick },
			{ "version_name", profile.id },
			{ "game_directory", util::javaPath(gdir) },
			{ "assets_root", util::javaPath(assets.dir) },
			{ "game_assets", util::javaPath(assets.dir) },
			{ "assets_index_name", assets.id },
			{ "auth_uuid", uuid },
			{ "auth_access_token", "0" },
			{ "auth_session", "0" },
			{ "user_properties", "{}" },
			{ "user_type", "legacy" },
		};
		std::string template_ = vanilla.value("minecraftArguments", "");
		std::size_t pos = 0;
		while (pos < template_.size()) {
			auto end = template_.find(' ', pos);
			if (end == std::string::npos) end = template_.size();
			std::string token = template_.substr(pos, end - pos);
			if (!token.empty()) out.args.push_back(substitute(token, vars));
			pos = end + 1;
		}
		if (!profile.tweakClass.empty()) {
			out.args.push_back("--tweakClass");
			out.args.push_back(profile.tweakClass);
		}

		std::string server = std::string(GDZ_SERVER_ADDRESS).empty() ? pack.server : std::string(GDZ_SERVER_ADDRESS);
		if (!server.empty()) {
			auto hostPort = splitServer(server);
			out.args.push_back("--server");
			out.args.push_back(hostPort.first);
			out.args.push_back("--port");
			out.args.push_back(hostPort.second);
		}
		return out;
	} catch (const json::exception& e) {
		throw Error(std::string("Некорректные данные версии: ") + e.what());
	}
}
