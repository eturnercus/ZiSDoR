#include <stdexcept>
#include <windows.h>
#include <thread>
#include <atomic>
#include <string>
#include "launcher.hpp"
#include "../shared/debug.hpp"
#include "bootstrap.hpp"
#include "resource.h"

// Состояние приложения
struct AppState {
    std::atomic<bool> isRunning{ true };
    std::atomic<bool> updateComplete{ false };
    HWND hDlg = nullptr;
    HWND hProgressBar = nullptr;
    std::string statusText = "Initializing...";
};

AppState g_state;

// Прототип функциии.
void WorkerThread();
// Сообщение для обновления UI
#define WM_UPDATE_UI (WM_USER + 1)

// Функция-обработчик диалога
INT_PTR CALLBACK UpdaterDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            g_state.hDlg = hDlg;
            // В .rc CONTROL "LOADING_BAR" имеет ID -1, но обычно в resource.h определены константы.
            // Если ID_PROGRESS_BAR не определен, придется искать по индексу или заменить в .rc.
            #ifdef ID_PROGRESS_BAR
            g_state.hProgressBar = GetDlgItem(hDlg, ID_PROGRESS_BAR);
            #endif

            // Запускаем поток логики
            std::thread(WorkerThread).detach();

            return (INT_PTR)TRUE;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == IDCANCEL) {
                g_state.isRunning = false;
                EndDialog(hDlg, 0);
                return (INT_PTR)TRUE;
            }
            break;
        }
        case WM_UPDATE_UI: {
            // Обновляем статус в диалоге
            #ifdef ID_STATUS_LABEL
            SetWindowTextW(GetDlgItem(hDlg, ID_STATUS_LABEL), (LPCWSTR)g_state.statusText.c_str());
            #endif
            return (INT_PTR)TRUE;
        }
    }
    return (INT_PTR)FALSE;
}

// ПОТОК ЛОГИКИ (Worker Thread)
void WorkerThread() {
    LOG_DEBUG("[Worker] Started");

    // 1. Проверка WebView2
    g_state.statusText = "Проверка зависимостей...";
    PostMessage(g_state.hDlg, WM_UPDATE_UI, 0, 0);

    if (!bootstrap::isWebView2Installed()) {
        LOG_DEBUG("[Worker] WebView2 not installed");
        // Показываем ошибку через MessageBox, так как диалог загрузки не предназначен для ввода
        int sel = MessageBoxW(g_state.hDlg, L"Лаунчер требует WebView2 для работы. Установить? Необходимо подключение к интернету.", L"WebView2", MB_YESNO | MB_ICONQUESTION);
	
	if (sel != IDYES) {
		LOG_DEBUG("[Worker] User rejected WebView2 installation. Exiting now.");
		g_state.isRunning = false;
		PostMessage(g_state.hDlg, WM_CLOSE, 0, 0);
		return;
    	}
	
	LOG_DEBUG("[Worker] Downloading WebView2.....");
	g_state.statusText = "Скачивание WebView2...";
	PostMessage(g_state.hDlg, WM_UPDATE_UI, 0, 0);

	// Скачивание установщика
	fs::path webView2Installer;
	try {
		auto filePath = bootstrap::downloadWebView2();
		if (!filePath.has_value()) {
			LOG_DEBUG("[Worker] Unexpected error occured during download.");
			MessageBoxW(g_state.hDlg, L"Произошла неизвестная ошибка при скачивании WebView2.", L"Ошибка", MB_OK | MB_ICONERROR);
			g_state.isRunning = false;
			PostMessage(g_state.hDlg, WM_CLOSE, 0,0);
			return;
		}
		webView2Installer = filePath.value();
	}
	catch (std::runtime_error& e) {
		std::string error_message = e.what();
		LOG_DEBUG(("[Worker] Error occured during download: "+error_message).c_str());
		MessageBoxW(g_state.hDlg, (LPCWSTR)e.what(), L"Ошибка при загрузке WebView2.", MB_OK | MB_ICONERROR);
		g_state.isRunning = false;
		PostMessage(g_state.hDlg, WM_CLOSE, 0, 0);
		return;
	}

	LOG_DEBUG("[Worker] Installing WebView2.....");
	g_state.statusText = "Установка WebView2...";
	PostMessage(g_state.hDlg, WM_UPDATE_UI, 0, 0);
	
	try {
		bool result = bootstrap::installWebView2(webView2Installer);
		if (!result) {
			LOG_DEBUG("[Worker] Installer returned non-zero exit code. Exiting now.");
			MessageBoxW(g_state.hDlg, L"Установщик завершился некорректно.", L"Ошибка", MB_OK | MB_ICONERROR);
			g_state.isRunning = false;
			PostMessage(g_state.hDlg, WM_CLOSE, 0, 0);
			return;
		}
		LOG_DEBUG("[Worker] WebView2 install succeeded.");
		MessageBoxW(g_state.hDlg, L"Установка WebView2 завершена.", L"WebView2", MB_OK | MB_ICONINFORMATION);
	}
	catch (std::runtime_error& e) {
		std::string error_message = e.what();
		LOG_DEBUG(("[Worker] Error occured during install: "+error_message).c_str());
		MessageBoxW(g_state.hDlg, (LPCWSTR)e.what(), L"Ошибка при установке WebView2.", MB_OK | MB_ICONERROR);
		g_state.isRunning = false;
		PostMessage(g_state.hDlg, WM_CLOSE, 0, 0);
		return;
	} 
    }

    // 2. Загрузка DLL
    g_state.statusText = "Запуск лаунчера...";
    PostMessage(g_state.hDlg, WM_UPDATE_UI, 0, 0);

    HMODULE hDll = LoadLibraryA("launcher.dll");
    if (!hDll) {
        LOG_DEBUG("[Worker] Could not load launcher.dll");
        MessageBoxW(g_state.hDlg, L"launcher.dll не найден.", L"Ошибка", MB_OK | MB_ICONERROR);
        g_state.isRunning = false;
        PostMessage(g_state.hDlg, WM_CLOSE, 0, 0);
        return;
    }

    // 3. Поиск функции RunApp
    typedef int (*RunAppFunc)();
    RunAppFunc RunApp = (RunAppFunc)GetProcAddress(hDll, "RunApp");

    if (!RunApp) {
        LOG_DEBUG("[Worker] RunApp function not found in DLL");
        MessageBoxW(g_state.hDlg, L"launcher.dll повреждён.", L"Ошибка", MB_OK | MB_ICONERROR);
        FreeLibrary(hDll);
        g_state.isRunning = false;
        PostMessage(g_state.hDlg, WM_CLOSE, 0, 0);
        return;
    }
    
    // Освобождаем библиотеку.
    FreeLibrary(hDll);

    // 4. Завершение
    g_state.statusText = "Готово!";
    PostMessage(g_state.hDlg, WM_UPDATE_UI, 0, 0);

    // Небольшая пауза, чтобы пользователь увидел статус "Ready"
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    g_state.updateComplete = true;
    PostMessage(g_state.hDlg, WM_CLOSE, 0, 0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    LOG_DEBUG("[Updater] Entry point");

    // Запускаем диалог. DialogBoxParamA блокирует поток до закрытия окна.
    // Используем MAKEINTRESOURCE, так как LAUNCHER_DLG определен как числовой ID в resource.h
    DialogBoxParamA(hInstance, MAKEINTRESOURCE(LAUNCHER_DLG), NULL, (DLGPROC)UpdaterDlgProc, NULL);

    // Если диалог закрылся и подготовка была успешной
    if (g_state.updateComplete) {
        LOG_DEBUG("[Updater] Launching main app...");

        HMODULE hDll = LoadLibraryA("launcher.dll");
        if (hDll) {
            typedef int (*RunAppFunc)();
            RunAppFunc RunApp = (RunAppFunc)GetProcAddress(hDll, "RunApp");
            if (RunApp) {
                int result = RunApp();
                char buf[64];
                sprintf_s(buf, "RunApp returned: %d", result);
                LOG_DEBUG(buf);
                return result;
            }
            FreeLibrary(hDll);
        }
    }

    return 0;
}
