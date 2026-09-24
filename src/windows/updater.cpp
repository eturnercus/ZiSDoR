#include <windows.h>
#include <iostream>
#include "launcher.hpp"
#include "../shared/debug.hpp"


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    LOG_DEBUG("[Updater] Starting...");

    LOG_DEBUG("[Updater] Loading launcher.dll...");
    HMODULE hDll = LoadLibraryA("launcher.dll");
    if (!hDll) {
        LOG_DEBUG("[Updater] Error: Could not load launcher.dll");
        MessageBoxA(NULL, "Could not load launcher.dll", "Error", MB_ICONERROR);
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
        MessageBoxA(NULL, "Could not find RunApp in launcher.dll", "Error", MB_ICONERROR);
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
