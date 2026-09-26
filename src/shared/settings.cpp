#include "settings.hpp"
#include "debug.hpp"
#include "util.hpp"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace {
	// Защита от мусора: настройки небольшие, всё, что больше, считаем повреждённым.
	constexpr std::size_t kMaxSettingsSize = 64 * 1024;

	std::string trim(const std::string& s) {
		const char* ws = " \t\r\n";
		auto b = s.find_first_not_of(ws);
		if (b == std::string::npos) return {};
		auto e = s.find_last_not_of(ws);
		return s.substr(b, e - b + 1);
	}

	bool looksLikeObject(const std::string& s) {
		std::string t = trim(s);
		return t.size() >= 2 && t.front() == '{' && t.back() == '}';
	}

	// util::env читает переменные на Windows в UTF-16: в %APPDATA% может быть кириллица (имя пользователя),
	// а std::getenv вернул бы её в ANSI-кодировке, и путь сломался бы.
	std::string env(const char* name) {
		return util::env(name);
	}
}

fs::path settings::configDir() {
	fs::path base;
#ifdef _WIN32
	std::string appdata = env("APPDATA");
	if (!appdata.empty()) base = fs::u8path(appdata);
#else
	std::string xdg = env("XDG_CONFIG_HOME");
	std::string home = env("HOME");
	if (!xdg.empty()) base = fs::u8path(xdg);
	else if (!home.empty()) base = fs::u8path(home) / ".config";
#endif
	if (base.empty()) {
		std::error_code ec;
		base = fs::temp_directory_path(ec);
	}
	return base / "GDZLauncher";
}

fs::path settings::filePath() {
	return configDir() / "settings.json";
}

std::string settings::load() {
	std::ifstream in(filePath(), std::ios::binary);
	if (!in) return "{}";

	std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (data.size() > kMaxSettingsSize || !looksLikeObject(data)) {
		LOG_DEBUG("[Settings] Settings file is invalid, using defaults");
		return "{}";
	}
	return trim(data);
}

bool settings::save(const std::string& json) {
	if (json.size() > kMaxSettingsSize || !looksLikeObject(json)) {
		LOG_DEBUG("[Settings] Refused to save invalid settings");
		return false;
	}

	std::error_code ec;
	fs::create_directories(configDir(), ec);
	if (ec) {
		LOG_DEBUG("[Settings] Could not create config directory: " + ec.message());
		return false;
	}

	// Пишем во временный файл и переименовываем, чтобы не оставить обрезанный файл при сбое.
	fs::path target = filePath();
	fs::path tmp = target;
	tmp += ".tmp";

	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out << trim(json);
		out.flush();
		if (!out) return false;
	}

	fs::rename(tmp, target, ec);
	if (ec) {
		LOG_DEBUG("[Settings] Could not replace settings file: " + ec.message());
		fs::remove(tmp, ec);
		return false;
	}
	return true;
}
