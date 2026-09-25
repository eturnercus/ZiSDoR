#include <optional>
#include <windows.h>
#include <urlmon.h>
#include "WebView2.h"
#include "bootstrap.hpp"
#include <winreg.h>

#ifdef MSVC
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "WebView2Loader.lib")
#endif

/// \brief Функция для проверки наличия WebView2
bool bootstrap::isWebView2Installed() {
    // GUID из официальной документации MS для WebView2 Runtime
    const wchar_t* guid = L"{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";

    // Формируем пути для 64-битной и 32-битной систем
    // Для 64-бит в HKLM смотрим в WOW6432Node
    const wchar_t* paths[] = {
        L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\" , // HKLM 64-bit
        L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\"               // HKLM 32-bit / HKCU
    };

    auto checkKey = [&](HKEY root, const wchar_t* basePath) -> bool {
        HKEY hKey;
        std::wstring fullPath = std::wstring(basePath) + guid;

        if (RegOpenKeyExW(root, fullPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t version[256];
            DWORD size = sizeof(version);

            // Ищем параметр "pv" (Product Version)
            if (RegQueryValueExW(hKey, L"pv", NULL, NULL, (LPBYTE)version, &size) == ERROR_SUCCESS) {
                version[size / sizeof(wchar_t)] = L'\0'; // Гарантируем null-termination

                // Проверяем, что версия не пустая и не "0.0.0.0"
                if (size > 0 && wcslen(version) > 0 && std::wstring(version) != L"0.0.0.0") {
                    RegCloseKey(hKey);
                    return true;
                }
            }
            RegCloseKey(hKey);
        }
        return false;
    };

    // 1. Проверяем HKLM (сначала WOW6432Node, потом обычный путь)
    for (const auto& path : paths) {
        if (checkKey(HKEY_LOCAL_MACHINE, path)) return true;
    }

    // 2. Проверяем HKCU (всегда обычный путь)
    if (checkKey(HKEY_CURRENT_USER, L"Softwarents\\")) return true;

    return false;
}

/// \brief Функа для скачивания WebView2. При удаче вернет путь, в ином случае ничего.
std::optional<fs::path> bootstrap::downloadWebView2() {
	try {
		// Ссылка на скачивания взята с официального сайта майков и данный способ загрузки является рекомендованным.
		const wchar_t* url = L"https://go.microsoft.com/fwlink/p/?LinkId=2124703";

		fs::path downloadPath = fs::temp_directory_path();

		if (!fs::exists(downloadPath)) return {};
		
		downloadPath += "WebView2Setup.exe";
		
		HRESULT hr = URLDownloadToFileW(NULL, url, downloadPath.c_str(), 0, NULL);

		if (SUCCEEDED(hr)) {
			return downloadPath;
		}
	}
	catch (...) {
		// Ловим всяческие ошибки фс.
	}
	return std::nullopt;
}
