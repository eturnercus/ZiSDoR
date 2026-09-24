#include <windows.h>
#include <iostream>
#include "launcher.hpp"

void LogDebug(const char* msg) {
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    LogDebug("[Updater] Starting...");

    LogDebug("[Updater] Loading launcher.dll...");
    HMODULE hDll = LoadLibraryA("launcher.dll");
    if (!hDll) {
        LogDebug("[Updater] Error: Could not load launcher.dll");
        MessageBoxA(NULL, "Could not load launcher.dll", "Error", MB_ICONERROR);
        return 1;
    }

    char buf[64];
    sprintf_s(buf, "[Updater] DLL loaded successfully: %p", (void*)hDll);
    LogDebug(buf);

    LogDebug("[Updater] Searching for RunApp function...");
    typedef int (*RunAppFunc)();
    RunAppFunc RunApp = (RunAppFunc)GetProcAddress(hDll, "RunApp");

    if (!RunApp) {
        LogDebug("[Updater] Error: Could not find RunApp in launcher.dll");
        MessageBoxA(NULL, "Could not find RunApp in launcher.dll", "Error", MB_ICONERROR);
        FreeLibrary(hDll);
        return 1;
    }

    sprintf_s(buf, "[Updater] Found RunApp at: %p", (void*)RunApp);
    LogDebug(buf);

    LogDebug("[Updater] Calling RunApp()...");
    int result = RunApp();

    sprintf_s(buf, "[Updater] RunApp returned: %d", result);
    LogDebug(buf);

    FreeLibrary(hDll);
    return result;
}
