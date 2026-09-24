#include <windows.h>
#include <iostream>
#include "launcher.hpp"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Принудительно создаем консоль для вывода std::cout в WinMain
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    std::cout << "[Updater] Starting..." << std::endl;

    std::cout << "[Updater] Loading launcher.dll..." << std::endl;
    HMODULE hDll = LoadLibraryA("launcher.dll");
    if (!hDll) {
        std::cout << "[Updater] Error: Could not load launcher.dll" << std::endl;
        MessageBoxA(NULL, "Could not load launcher.dll", "Error", MB_ICONERROR);
        return 1;
    }
    std::cout << "[Updater] DLL loaded successfully: " << hDll << std::endl;

    std::cout << "[Updater] Searching for RunApp function..." << std::endl;
    typedef int (*RunAppFunc)();
    RunAppFunc RunApp = (RunAppFunc)GetProcAddress(hDll, "RunApp");

    if (!RunApp) {
        std::cout << "[Updater] Error: Could not find RunApp in launcher.dll" << std::endl;
        MessageBoxA(NULL, "Could not find RunApp in launcher.dll", "Error", MB_ICONERROR);
        FreeLibrary(hDll);
        return 1;
    }
    std::cout << "[Updater] Found RunApp at: " << (void*)RunApp << std::endl;

    std::cout << "[Updater] Calling RunApp()..." << std::endl;
    int result = RunApp();
    std::cout << "[Updater] RunApp returned: " << result << std::endl;

    FreeLibrary(hDll);
    return result;
}
