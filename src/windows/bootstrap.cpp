#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <urlmon.h>
#include "WebView2.h"
#include "bootstrap.hpp"
#include <winreg.h>
#include "../shared/debug.hpp"

#ifdef MSVC
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "WebView2Loader.lib")
#endif

/// \brief Функция для проверки наличия WebView2
bool bootstrap::isWebView2Installed() {
    const wchar_t* guid = L"{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";

    struct RegistryCheck {
        HKEY root;
        const wchar_t* path;
        const char* label;
    };

    std::vector<RegistryCheck> checks = {
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\", "HKLM WOW6432Node"},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\", "HKLM Standard"},
        {HKEY_CURRENT_USER, L"Software\\Microsoft\\EdgeUpdate\\Clients\\", "HKCU"}
    };

    for (const auto& check : checks) {
        HKEY hKey;
        std::wstring fullPath = std::wstring(check.path) + guid;

        LOG_DEBUG((std::string("[WebView2Check] Trying: ") + check.label).c_str());

        if (RegOpenKeyExW(check.root, fullPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t version[256];
            DWORD size = sizeof(version);

            if (RegQueryValueExW(hKey, L"pv", NULL, NULL, (LPBYTE)version, &size) == ERROR_SUCCESS) {
                version[size / sizeof(wchar_t)] = L'\0';
                std::wstring vStr(version);

                // Логируем найденную версию
                char buf[256];
                sprintf_s(buf, "[WebView2Check] Found version: %ls", version);
                LOG_DEBUG(buf);

                if (vStr.length() > 0 && vStr != L"0.0.0.0") {
                    RegCloseKey(hKey);
                    return true;
                }
            } else {
                LOG_DEBUG((std::string("[WebView2Check] Key found, but 'pv' value missing in ") + check.label).c_str());
            }
            RegCloseKey(hKey);
        } else {
            LOG_DEBUG((std::string("[WebView2Check] Key not found in ") + check.label).c_str());
        }
    }

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
	catch (std::runtime_error& e) {
		throw e;
	}
	return std::nullopt;
}

/// \brief Функция установки Webview2
bool bootstrap::installWebView2(fs::path filePath) {
	std::wstring commandLine = L"\"" + filePath.wstring() + L"\" /silent /install";

	STARTUPINFOW si;
	PROCESS_INFORMATION pi;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	wchar_t cmdBuffer[MAX_PATH*2];
	wcscpy_s(cmdBuffer, commandLine.c_str());

	if (!CreateProcessW(NULL, cmdBuffer, NULL, NULL, false, 0, NULL, NULL, &si, &pi)) {
		throw std::runtime_error("Ошибка запуска установщика!");
	}

	WaitForSingleObject(pi.hProcess, INFINITE);

	DWORD exitCode = 0;
	GetExitCodeProcess(pi.hProcess, &exitCode);

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return (exitCode == 0);
}
