#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <vector>
#include "../shared/net.hpp"
#include "../shared/config.hpp"

#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#endif

// Доступно начиная с Windows 8.1: системные настройки прокси, включая автоконфигурацию.
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace {
	struct Internet {
		HINTERNET h = nullptr;
		Internet() = default;
		explicit Internet(HINTERNET v) : h(v) {}
		Internet(const Internet&) = delete;
		Internet& operator=(const Internet&) = delete;
		~Internet() { if (h) WinHttpCloseHandle(h); }
	};

	// Сессия на поток: WinHTTP переиспользует соединения внутри сессии.
	thread_local Internet tlsSession;

	std::wstring widen(const std::string& s) {
		if (s.empty()) return {};
		int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, 0);
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
		return w;
	}

	std::string lastError(const char* what) {
		DWORD e = GetLastError();
		return std::string(what) + " (код " + std::to_string(e) + ")";
	}

	HINTERNET session() {
		if (!tlsSession.h) {
			std::wstring ua = widen(std::string("GDZLauncher/") + GDZ_VERSION);
			tlsSession.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
			if (!tlsSession.h) {
				// Windows старше 8.1: настройки прокси WinHTTP по умолчанию.
				tlsSession.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
			}
			if (tlsSession.h) {
				// resolve, connect, send, receive (мс)
				WinHttpSetTimeouts(tlsSession.h, 10000, 10000, 30000, 60000);
				// Сжатие намеренно не включаем: Content-Length тогда относится к сжатому телу,
				// и проверка полноты загрузки ниже перестала бы работать.
			}
		}
		return tlsSession.h;
	}
}

void net::globalInit() {}

net::Result net::request(const std::string& url, const Sink& sink, const Progress& progress, const std::atomic<bool>* cancel) {
	Result r;
	HINTERNET s = session();
	if (!s) {
		r.error = lastError("Не удалось инициализировать WinHTTP");
		return r;
	}

	std::wstring wurl = widen(url);
	URL_COMPONENTS uc{};
	uc.dwStructSize = sizeof(uc);
	uc.dwSchemeLength = (DWORD)-1;
	uc.dwHostNameLength = (DWORD)-1;
	uc.dwUrlPathLength = (DWORD)-1;
	uc.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
		r.error = "Некорректный адрес: " + url;
		return r;
	}
	if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) {
		r.error = "Поддерживаются только http и https: " + url;
		return r;
	}
	std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
	std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
	if (uc.lpszExtraInfo) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
	if (path.empty()) path = L"/";

	Internet con(WinHttpConnect(s, host.c_str(), uc.nPort, 0));
	if (!con.h) { r.error = lastError("Не удалось подключиться") + ": " + url; return r; }

	DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
	Internet req(WinHttpOpenRequest(con.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
	if (!req.h) { r.error = lastError("Не удалось создать запрос") + ": " + url; return r; }

	if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	    !WinHttpReceiveResponse(req.h, nullptr)) {
		r.error = lastError("Сетевая ошибка") + ": " + url;
		return r;
	}

	DWORD status = 0, size = sizeof(status);
	WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
	r.status = (long)status;
	if (status < 200 || status >= 300) {
		r.error = "HTTP " + std::to_string(status) + ": " + url;
		return r;
	}

	std::uint64_t total = 0;
	wchar_t lenBuf[32];
	DWORD lenSize = sizeof(lenBuf);
	if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lenSize, WINHTTP_NO_HEADER_INDEX)) {
		total = _wcstoui64(lenBuf, nullptr, 10);
	}

	std::vector<char> buf(1 << 16);
	std::uint64_t done = 0;
	for (;;) {
		if (cancel && cancel->load()) { r.cancelled = true; r.error = "Отменено"; return r; }
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(req.h, &avail)) { r.error = lastError("Обрыв соединения") + ": " + url; return r; }
		if (avail == 0) break;
		while (avail > 0) {
			DWORD chunk = avail < buf.size() ? avail : (DWORD)buf.size();
			DWORD read = 0;
			if (!WinHttpReadData(req.h, buf.data(), chunk, &read)) { r.error = lastError("Обрыв соединения") + ": " + url; return r; }
			if (read == 0) break;
			if (!sink(buf.data(), read)) { r.error = "Загрузка прервана"; return r; }
			done += read;
			avail -= read;
			if (progress) progress(done, total);
		}
	}

	if (total > 0 && done != total) {
		r.error = "Файл загружен не полностью: " + url;
		return r;
	}
	r.ok = true;
	return r;
}
