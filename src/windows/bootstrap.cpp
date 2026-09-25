#include <optional>
#include <windows.h>
#include <urlmon.h>
#include "WebView2.h"
#include "bootstrap.hpp"

#ifdef MSVC
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "WebView2Loader.lib")
#endif

/// \brief Функция для проверки наличия WebView2
bool bootstrap::isWebView2Installed() {
      // Динамически загружаем WebView2Loader.dll, чтобы полностью исключить
      // зависимость от WebView2Loader.lib на этапе линковки (решаем LNK2019)
      HMODULE hModule = LoadLibraryA("WebView2Loader.dll");
      if (!hModule) {
              return false;
      }

      // Определяем тип функции, которую мы будем искать в DLL
      typedef HRESULT (WINAPI *GetVersionFunc)(LPWSTR*);
      auto GetAvailableCoreWebView2BrowserVersionString = (GetVersionFunc)GetProcAddress(hModule, "GetAvailableCoreWebView2BrowserVersionString");

      if (!GetAvailableCoreWebView2BrowserVersionString) {
              FreeLibrary(hModule);
              return false;
      }

      LPWSTR versionInfo = nullptr;
      // Вызываем функцию из загруженной DLL
      HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(&versionInfo);

      if (SUCCEEDED(hr) && versionInfo != nullptr) {
              CoTaskMemFree(versionInfo);
              FreeLibrary(hModule);
              return true;
      }

      FreeLibrary(hModule);
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
