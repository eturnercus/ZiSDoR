#include <windows.h>
#include "launcher.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    HMODULE hDll = LoadLibraryA("launcher.dll");
    if (!hDll) {
        MessageBoxA(NULL, "Could not load launcher.dll", "Error", MB_ICONERROR);
        return 1;
    }

    typedef int (*RunAppFunc)();
    RunAppFunc RunApp = (RunAppFunc)GetProcAddress(hDll, "RunApp");

    if (!RunApp) {
        MessageBoxA(NULL, "Could not find RunApp in launcher.dll", "Error", MB_ICONERROR);
        FreeLibrary(hDll);
        return 1;
    }

    int result = RunApp();
    FreeLibrary(hDll);
    return result;
}
