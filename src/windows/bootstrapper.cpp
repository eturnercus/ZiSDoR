#include <optional>
#include <string>
#include <system_error>
#include <windows.h>
#include <urlmon.h>
#include <filesystem>
#include "WebView2.h"

// Несмотря на то, что у нас официально поддерживается компиляция для виндовс только через MSVC, все равно проверяем, так как в будущем это возможно изменится.
#ifdef MSVC
#pragma comment(lib, "urlmon.lib")
#endif

namespace fs = std::filesystem;

/// \brief Функция для проверки наличия WebView2
/// Берет страшную длинную функцию с противными параметрами и оборачивает её в красивую обёртку.
bool isWebView2Installed() {
	LPWSTR versionInfo = nullptr;

	// ХИХОЗ СУКА)))
	HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &versionInfo);

	if (SUCCEEDED(hr) && versionInfo != nullptr) {
		CoTaskMemFree(versionInfo);
		return true;
	}
	
	return false;
}

/// \brief Функа для скачивания webview. При удаче вернет путь, в ином случае ничего.
std::optional<fs::path> downloadWebView2() {
	// Ссылка на скачивания взята с официального сайта майков и данный способ загрузки является рекомендованным.
	LPCWSTR url = L"https://go.microsoft.com/fwlink/p/?LinkId=2124703";

	fs::path downloadPath = fs::temp_directory_path();

	if (!fs::exists(downloadPath)) return {};
	
	downloadPath += "WebView2Setup.exe";
}
