#include <windows.h>
#include "launcher.hpp"
#include "../shared/debug.hpp"
#include "bootstrap.hpp"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    LOG_DEBUG("[Updater] Starting...");

    LOG_DEBUG("[Updater] Checking if webview2 is installed...");
    if (!bootstrap::isWebView2Installed()) {
        LOG_DEBUG("[Updater] Not installed.");
	MessageBoxW(NULL, L"Лаунчер требует Webview2 для работы. Установить?", L"Webview2", 
	    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
	return 1;
    } else LOG_DEBUG("[Updater] Installed."); 

    LOG_DEBUG("[Updater] Loading launcher.dll...");
    HMODULE hDll = LoadLibraryA("launcher.dll");
    if (!hDll) {
        LOG_DEBUG("[Updater] Error: Could not load launcher.dll");
	// В финальной версии мы должны будем скачивать ДЛЛ, если не найдём.
        MessageBoxW(NULL, L"launcher.dll не найден.", L"Ошибка", MB_ICONERROR);
        return 1;
    }

    char buf[64];
    sprintf_s(buf, "[Updater] DLL loaded successfully: %p", (void*)hDll);
    LOG_DEBUG(buf);

    LOG_DEBUG("[Updater] Searching for RunApp function...");
    typedef int (*RunAppFunc)();
    RunAppFunc RunApp = (RunAppFunc)GetProcAddress(hDll, "RunApp");

    if (!RunApp) {
        LOG_DEBUG("[Updater] Error: Could not find RunApp in launcher.dll");
        MessageBoxW(NULL, L"launcher.dll повреждён.", L"Ошибка", MB_ICONERROR);
        FreeLibrary(hDll);
        return 1;
    }

    sprintf_s(buf, "[Updater] Found RunApp at: %p", (void*)RunApp);
    LOG_DEBUG(buf);

    LOG_DEBUG("[Updater] Calling RunApp()...");
    int result = RunApp();

    sprintf_s(buf, "[Updater] RunApp returned: %d", result);
    LOG_DEBUG(buf);

    FreeLibrary(hDll);
    return result;
}
