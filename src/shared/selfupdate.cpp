#include "selfupdate.hpp"
#include "config.hpp"
#include "net.hpp"
#include "sha1.hpp"
#include "util.hpp"
#include <nlohmann/json.hpp>
#include <system_error>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
	const char* platformKey() {
#ifdef _WIN32
		return "windows";
#else
		return "linux";
#endif
	}

	bool isPlainName(const std::string& name) {
		return util::isSafeRelative(name) && name.find('/') == std::string::npos;
	}

	/// Куда устанавливается файл из launcher.json. На Linux запись "launcher" — это сам запущенный бинарник,
	/// даже если пользователь переименовал его.
	fs::path localPath(const std::string& name) {
#ifndef _WIN32
		if (name == "launcher") return util::selfExePath();
#endif
		return util::selfExePath().parent_path() / fs::u8path(name);
	}

	std::string str(const json& j, const char* key) {
		auto it = j.find(key);
		return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
	}
}

fs::path selfupdate::installDir() {
	return util::selfExePath().parent_path();
}

selfupdate::Info selfupdate::check(const std::atomic<bool>* cancel) {
	Info info;
	const std::string api = GDZ_API_URL;
	info.configured = !api.empty();
	if (!info.configured) return info;

	std::string text;
	net::Result r = net::fetch(util::joinUrl(api, "launcher.json"), text, cancel, 4u * 1024u * 1024u);
	if (!r.ok) {
		info.error = r.error;
		return info;
	}

	try {
		json j = json::parse(text);
		info.latestVersion = str(j, "version");

		for (const auto& n : j.value("news", json::array())) {
			if (!n.is_object()) continue;
			NewsItem item{ str(n, "title"), str(n, "date"), str(n, "text"), str(n, "url") };
			if (item.title.empty() && item.text.empty()) continue;
			info.news.push_back(item);
			if (info.news.size() >= 10) break;
		}

		for (const auto& f : j.value(platformKey(), json::array())) {
			FileEntry e{ str(f, "path"), str(f, "url"), util::lower(str(f, "sha1")), f.value("size", std::uint64_t{ 0 }) };
			if (!isPlainName(e.path) || !util::isSha1(e.sha1) || e.url.empty()) {
				info.error = "Некорректная запись в launcher.json: " + e.path;
				return info;
			}
			e.url = util::joinUrl(api, e.url);
			auto local = sha1::ofFile(localPath(e.path));
			if (!local || *local != e.sha1) info.files.push_back(e);
		}
		info.ok = true;
		// Номер версии защищает локальные сборки разработчика от замены файлами с сервера.
		info.updateAvailable = !info.latestVersion.empty() && info.latestVersion != GDZ_VERSION && !info.files.empty();
	} catch (const std::exception& e) {
		info.error = std::string("Ошибка в launcher.json: ") + e.what();
	}
	return info;
}

std::string selfupdate::toJson(const Info& info) {
	json j;
	j["configured"] = info.configured;
	j["ok"] = info.ok;
	j["error"] = info.error;
	j["current"] = GDZ_VERSION;
	j["latest"] = info.latestVersion;
	j["updateAvailable"] = info.updateAvailable;
	j["news"] = json::array();
	for (const auto& n : info.news) {
		j["news"].push_back({ { "title", n.title }, { "date", n.date }, { "text", n.text }, { "url", n.url } });
	}
	return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

bool selfupdate::applyFiles(const Info& info, std::string& err) {
	const fs::path dir = installDir();
	std::error_code ec;

	// 1. Скачиваем всё в *.new и проверяем. До замены ни один установленный файл не трогаем.
	for (const auto& f : info.files) {
		fs::path fresh = localPath(f.path);
		fresh += ".new";
		net::Result r = net::download(f.url, fresh);
		if (!r.ok) {
			err = "Не удалось скачать обновление: " + r.error +
			      "\nПроверьте, что папка " + dir.u8string() + " доступна для записи.";
			return false;
		}
		auto h = sha1::ofFile(fresh);
		if (!h || *h != f.sha1) {
			fs::remove(fresh, ec);
			err = "Контрольная сумма обновления не совпадает: " + f.path;
			return false;
		}
#ifndef _WIN32
		fs::permissions(fresh, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
		                       fs::perms::others_read | fs::perms::others_exec, fs::perm_options::replace, ec);
#endif
	}

	// 2. Меняем файлы: текущий -> *.old, *.new -> текущий. При ошибке возвращаем старый файл.
	for (const auto& f : info.files) {
		fs::path target = localPath(f.path);
		fs::path fresh = target;
		fresh += ".new";
		fs::path old = target;
		old += ".old";

		fs::remove(old, ec);
		bool hadTarget = fs::exists(target, ec);
		if (hadTarget) {
			fs::rename(target, old, ec);
			if (ec) {
				err = "Нет доступа к " + target.u8string() + ": " + ec.message();
				return false;
			}
		}
		fs::rename(fresh, target, ec);
		if (ec) {
			err = "Не удалось установить " + target.u8string() + ": " + ec.message();
			if (hadTarget) fs::rename(old, target, ec);
			return false;
		}
	}
	return true;
}

void selfupdate::cleanup() {
	// Удаляем только остатки наших собственных файлов: лаунчер может лежать в общей папке
	// (например, «Загрузки»), где чужие *.old трогать нельзя.
	std::error_code ec;
	const fs::path dir = installDir();
	std::vector<std::string> names = { util::selfExePath().filename().u8string() };
#ifdef _WIN32
	names.push_back("launcher.dll");
	names.push_back("Updater.exe");
#endif
	for (const auto& name : names) {
		for (const char* suffix : { ".old", ".new", ".new.part" }) {
			fs::remove(dir / fs::u8path(name + suffix), ec); // занятый файл просто останется до следующего раза
		}
	}
}
