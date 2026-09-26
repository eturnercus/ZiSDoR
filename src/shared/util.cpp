#include "util.hpp"
#include "config.hpp"
#include "sha1.hpp"
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <climits>
#endif

namespace fs = std::filesystem;

const char* util::osName() {
#ifdef _WIN32
	return "windows";
#elif defined(__APPLE__)
	return "osx";
#else
	return "linux";
#endif
}

std::string util::env(const char* name) {
#ifdef _WIN32
	// Переменные окружения на Windows читаем в UTF-16: в имени пользователя может быть кириллица.
	std::wstring wname(name, name + std::strlen(name));
	DWORD n = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
	if (n == 0) return {};
	std::wstring w(n, 0);
	n = GetEnvironmentVariableW(wname.c_str(), &w[0], n);
	w.resize(n);
	return fs::path(w).u8string();
#else
	const char* v = std::getenv(name);
	return v ? std::string(v) : std::string();
#endif
}

fs::path util::defaultDataDir() {
	fs::path base;
#ifdef _WIN32
	std::string appdata = env("APPDATA");
	if (!appdata.empty()) base = fs::u8path(appdata);
#else
	std::string xdg = env("XDG_DATA_HOME");
	std::string home = env("HOME");
	if (!xdg.empty()) base = fs::u8path(xdg);
	else if (!home.empty()) base = fs::u8path(home) / ".local" / "share";
#endif
	if (base.empty()) {
		std::error_code ec;
		base = fs::current_path(ec);
	}
	return base / GDZ_DATA_DIR_NAME;
}

namespace {
	fs::path readSelfExePath() {
#ifdef _WIN32
	std::wstring buf(MAX_PATH, 0);
	for (;;) {
		DWORD n = GetModuleFileNameW(nullptr, &buf[0], (DWORD)buf.size());
		if (n == 0) return {};
		if (n < buf.size()) { buf.resize(n); break; }
		buf.resize(buf.size() * 2);
	}
	return fs::path(buf);
#else
	std::error_code ec;
	fs::path p = fs::read_symlink("/proc/self/exe", ec);
	return ec ? fs::path() : p;
#endif
	}
}

fs::path util::selfExePath() {
	// Запоминаем путь при первом вызове (в начале работы): после самообновления файл переименовывается
	// в *.old, и /proc/self/exe начинает указывать на него, а перезапускать нужно новый файл.
	static const fs::path path = readSelfExePath();
	return path;
}

bool util::isSafeRelative(const std::string& rel) {
	if (rel.empty() || rel.size() > 1024) return false;
	if (rel.front() == '/') return false;
	if (rel.find('\\') != std::string::npos || rel.find(':') != std::string::npos) return false;
	if (rel.find('\0') != std::string::npos) return false;
	std::size_t start = 0;
	while (start <= rel.size()) {
		std::size_t end = rel.find('/', start);
		if (end == std::string::npos) end = rel.size();
		std::string part = rel.substr(start, end - start);
		if (part.empty() || part == "." || part == "..") return false;
		// Имена, которые Windows трактует особо (пробел или точка в конце).
		if (part.back() == ' ' || part.back() == '.') return false;
		start = end + 1;
	}
	return true;
}

std::optional<fs::path> util::safeJoin(const fs::path& root, const std::string& rel) {
	if (!isSafeRelative(rel)) return std::nullopt;
	// make_preferred: на Windows '/' из манифеста превращается в '\\' (единообразные пути в командной строке Java).
	return root / fs::u8path(rel).make_preferred();
}

bool util::globMatch(const std::string& pattern, const std::string& text) {
	// Итеративный алгоритм с откатом к последней звёздочке.
	std::size_t p = 0, t = 0, starP = std::string::npos, starT = 0;
	bool starAny = false;
	while (t < text.size()) {
		if (p < pattern.size() && pattern[p] == '*') {
			starAny = (p + 1 < pattern.size() && pattern[p + 1] == '*');
			p += starAny ? 2 : 1;
			starP = p;
			starT = t;
		} else if (p < pattern.size() && (pattern[p] == '?' ? text[t] != '/' : pattern[p] == text[t])) {
			++p; ++t;
		} else if (starP != std::string::npos && (starAny || text[starT] != '/')) {
			p = starP;
			t = ++starT;
		} else {
			return false;
		}
	}
	while (p < pattern.size() && pattern[p] == '*') ++p;
	return p == pattern.size();
}

std::string util::mavenPath(const std::string& name) {
	std::string spec = name, ext = "jar";
	auto at = spec.find('@');
	if (at != std::string::npos) { ext = spec.substr(at + 1); spec = spec.substr(0, at); }

	std::vector<std::string> parts;
	std::stringstream ss(spec);
	std::string item;
	while (std::getline(ss, item, ':')) parts.push_back(item);
	if (parts.size() < 3) return {};

	std::string group = parts[0];
	for (char& c : group) if (c == '.') c = '/';
	const std::string& artifact = parts[1];
	const std::string& version = parts[2];
	std::string file = artifact + "-" + version;
	if (parts.size() > 3 && !parts[3].empty()) file += "-" + parts[3];
	return group + "/" + artifact + "/" + version + "/" + file + "." + ext;
}

std::string util::joinUrl(const std::string& base, const std::string& rel) {
	if (rel.rfind("http://", 0) == 0 || rel.rfind("https://", 0) == 0) return rel;
	if (base.empty()) return rel;
	std::string b = base;
	if (b.back() != '/') b.push_back('/');
	std::string r = rel;
	while (!r.empty() && r.front() == '/') r.erase(r.begin());
	return b + r;
}

std::string util::lower(std::string s) {
	for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
	return s;
}

bool util::isSha1(const std::string& s) {
	if (s.size() != 40) return false;
	for (char c : s) {
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
	}
	return true;
}

std::string util::offlineUuid(const std::string& nick) {
	// Серверы в офлайн-режиме вычисляют UUID сами, клиенту нужен лишь стабильный идентификатор.
	std::string h = sha1::ofString("OfflinePlayer:" + nick);
	h[12] = '5';                                                // версия 5 (на основе SHA-1)
	h[16] = "89ab"[std::strtol(h.substr(16, 1).c_str(), nullptr, 16) & 3];  // вариант RFC 4122
	return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20, 12);
}

std::string util::javaPath(const fs::path& p) {
#ifdef _WIN32
	std::wstring w = p.wstring();
	bool ascii = true;
	for (wchar_t c : w) if (c > 127) { ascii = false; break; }
	if (!ascii) {
		DWORD n = GetShortPathNameW(w.c_str(), nullptr, 0);
		if (n > 0) {
			std::wstring s(n, 0);
			n = GetShortPathNameW(w.c_str(), &s[0], n);
			if (n > 0) {
				s.resize(n);
				return fs::path(s).u8string();
			}
		}
	}
	return p.u8string();
#else
	return p.string();
#endif
}

bool util::openPath(const fs::path& p) {
#ifdef _WIN32
	HINSTANCE r = ShellExecuteW(nullptr, L"open", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<INT_PTR>(r) > 32;
#else
	// Двойной fork, чтобы не оставлять зомби-процесс и не ждать завершения файлового менеджера.
	std::string path = p.string();
	pid_t pid = fork();
	if (pid < 0) return false;
	if (pid == 0) {
		if (fork() == 0) {
			setsid();
			execlp("xdg-open", "xdg-open", path.c_str(), static_cast<char*>(nullptr));
			_exit(127);
		}
		_exit(0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	return true;
#endif
}
