#include <stdexcept>
#include <windows.h>
#include <commctrl.h>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <atomic>
#include <string>
#include <cstdio>
#include <shellapi.h>
#include <vector>
// Точка входа Windows: один GDZLauncher.exe. Сначала небольшое окно подготовки (WebView2, обновление),
// затем в том же процессе открывается интерфейс лаунчера (core::app).
#include "../shared/core.hpp"
#include "../shared/debug.hpp"
#include "../shared/net.hpp"
#include "../shared/selfupdate.hpp"
#include "../shared/util.hpp"
#include "bootstrap.hpp"
#include "resource.h"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#endif

// Состояние приложения
struct AppState {
    std::atomic<bool> isRunning{ true };
    std::atomic<bool> updateComplete{ false };
    std::atomic<bool> restartAfterUpdate{ false }; // exe заменён новой версией: перезапускаемся

    HWND hDlg = nullptr;
    HWND hProgressBar = nullptr;
    std::mutex statusMutex;                    // Защищает statusText: пишет рабочий поток, читает поток интерфейса.
    std::string statusText = "Initializing...";
};

AppState g_state;

// Прототип функциии.
void WorkerThread();
// Сообщение для обновления UI
#define WM_UPDATE_UI (WM_USER + 1)

// Обновляет статус и просит диалог перерисовать его.
static void setStatus(const std::string& text) {
    {
        std::lock_guard<std::mutex> lock(g_state.statusMutex);
        g_state.statusText = text;
    }
    if (g_state.hDlg && IsWindow(g_state.hDlg)) {
        PostMessage(g_state.hDlg, WM_UPDATE_UI, 0, 0);
    }
}

// Закрывает диалог из рабочего потока (если пользователь его уже не закрыл).
static void closeDialog() {
    if (g_state.hDlg && IsWindow(g_state.hDlg)) {
        PostMessage(g_state.hDlg, WM_CLOSE, 0, 0);
    }
}

// Показывает ошибку и завершает подготовку.
static void fail(const wchar_t* text) {
    MessageBoxW(g_state.hDlg, text, L"Ошибка", MB_OK | MB_ICONERROR);
    g_state.isRunning = false;
    closeDialog();
}

// Безопасная конвертация UTF-8 -> UTF-16 для MessageBoxW
static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    if (size <= 0) return L"Неизвестная ошибка.";
    std::wstring w(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], size);
    w.resize(size - 1); // без завершающего нуля, который добавляет конвертация
    return w;
}

// Запуск новой версии после самообновления. Путь к exe запомнен при старте (util::selfExePath),
// а старый файл уже переименован в *.old. Новый процесс дождётся завершения текущего (--wait-pid).
static bool relaunchSelf() {
    std::wstring exe = util::selfExePath().wstring();
    std::wstring cmd = L"\"" + exe + L"\" --wait-pid " + std::to_wstring(GetCurrentProcessId());
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        MessageBoxW(nullptr, L"Обновление установлено. Запустите лаунчер заново.", L"GDZLauncher", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// Функция-обработчик диалога
INT_PTR CALLBACK StartupDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            g_state.hDlg = hDlg;

            #ifdef ID_PROGRESS_BAR
            g_state.hProgressBar = GetDlgItem(hDlg, ID_PROGRESS_BAR);
            #endif

            // Стиль PBS_MARQUEE сам по себе анимацию не запускает: её нужно включить сообщением.
            if (g_state.hProgressBar) {
                SendMessage(g_state.hProgressBar, PBM_SETMARQUEE, TRUE, 30);
            }

            // Центрируем окно по экрану
            RECT rc;
            GetWindowRect(hDlg, &rc);
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            int screenH = GetSystemMetrics(SM_CYSCREEN);
            int x = (screenW - (rc.right - rc.left)) / 2;
            int y = (screenH - (rc.bottom - rc.top)) / 2;
            SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

            // Запускаем поток логики
            std::thread(WorkerThread).detach();

            return (INT_PTR)TRUE;
        }
        case WM_CLOSE:
        case WM_COMMAND: {
            if (message == WM_CLOSE || LOWORD(wParam) == IDCANCEL) {
                g_state.isRunning = false;
                EndDialog(hDlg, 0);
                return (INT_PTR)TRUE;
            }
            break;
        }
        case WM_UPDATE_UI: {
            std::string text;
            {
                std::lock_guard<std::mutex> lock(g_state.statusMutex);
                text = g_state.statusText;
            }
            std::wstring wstrTo = utf8ToWide(text);

            HWND hLabel = GetDlgItem(hDlg, ID_STATUS_LABEL);
            SetWindowTextW(hLabel, wstrTo.c_str());
            InvalidateRect(hLabel, NULL, TRUE); // Принудительная перерисовка, чтобы текст не обрезался
            return (INT_PTR)TRUE;
        }
    }
    return (INT_PTR)FALSE;
}

// Проверка обновлений не дольше timeout: если сервер проекта не отвечает, лаунчер запускается без задержки.
// Проверка продолжается в фоне и просто завершится сама (её результат уже не нужен).
static selfupdate::Info checkWithTimeout(std::chrono::seconds timeout) {
    struct Shared {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        selfupdate::Info info;
    };
    auto shared = std::make_shared<Shared>();
    std::thread([shared] {
        selfupdate::Info info = selfupdate::check();
        std::lock_guard<std::mutex> lock(shared->m);
        shared->info = info;
        shared->done = true;
        shared->cv.notify_all();
    }).detach();

    std::unique_lock<std::mutex> lock(shared->m);
    if (!shared->cv.wait_for(lock, timeout, [&] { return shared->done; })) {
        selfupdate::Info timedOut;
        timedOut.configured = true;
        timedOut.error = "Сервер обновлений не ответил вовремя";
        return timedOut;
    }
    return shared->info;
}

// ПОТОК ЛОГИКИ (Worker Thread)
void WorkerThread() {
    LOG_DEBUG("[Worker] Started");

    // 1. Проверка WebView2
    setStatus("Проверка зависимостей...");

    if (!bootstrap::isWebView2Installed()) {
        LOG_DEBUG("[Worker] WebView2 not installed");
        // Показываем ошибку через MessageBox, так как диалог загрузки не предназначен для ввода
        int sel = MessageBoxW(g_state.hDlg, L"Лаунчер требует WebView2 для работы. Установить? Необходимо подключение к интернету.", L"WebView2", MB_YESNO | MB_ICONQUESTION);

        if (sel != IDYES || !g_state.isRunning) {
            LOG_DEBUG("[Worker] User rejected WebView2 installation. Exiting now.");
            g_state.isRunning = false;
            closeDialog();
            return;
        }

        LOG_DEBUG("[Worker] Downloading WebView2.....");
        setStatus("Скачивание WebView2...");

        // Скачивание установщика
        fs::path webView2Installer;
        try {
            auto filePath = bootstrap::downloadWebView2();
            if (!g_state.isRunning) return; // Пользователь закрыл окно во время скачивания.
            if (!filePath.has_value()) {
                LOG_DEBUG("[Worker] Unexpected error occured during download.");
                fail(L"Произошла неизвестная ошибка при скачивании WebView2.");
                return;
            }
            webView2Installer = filePath.value();
        }
        catch (std::runtime_error& e) {
            std::string error_message = e.what();
            LOG_DEBUG("[Worker] Error occured: " + error_message);
            fail(utf8ToWide(error_message).c_str());
            return;
        }

        LOG_DEBUG("[Worker] Installing WebView2.....");
        setStatus("Установка WebView2...");

        try {
            bool result = bootstrap::installWebView2(webView2Installer);
            if (!g_state.isRunning) return;
            if (!result) {
                LOG_DEBUG("[Worker] Installer returned non-zero exit code. Exiting now.");
                fail(L"Установщик завершился некорректно.");
                return;
            }
            LOG_DEBUG("[Worker] WebView2 install succeeded.");
            MessageBoxW(g_state.hDlg, L"Установка WebView2 завершена.", L"WebView2", MB_OK | MB_ICONINFORMATION);
        }
        catch (std::runtime_error& e) {
            std::string error_message = e.what();
            LOG_DEBUG("[Worker] Error occured: " + error_message);
            fail(utf8ToWide(error_message).c_str());
            return;
        }

        // Установщик мог вернуть 0, но рантайм всё равно не появился: проверяем ещё раз.
        if (!bootstrap::isWebView2Installed()) {
            LOG_DEBUG("[Worker] WebView2 still not detected after installation");
            fail(L"WebView2 не обнаружен после установки. Перезапустите лаунчер.");
            return;
        }
    }

    if (!g_state.isRunning) return;

    // 2. Обновление лаунчера (если при сборке задан адрес API).
    // Без сети или при ошибке сервера запускается уже установленная версия: играть можно и так.
    setStatus("Проверка обновлений...");
    selfupdate::Info info = checkWithTimeout(std::chrono::seconds(8));
    if (!g_state.isRunning) return;
    if (!info.configured) {
        LOG_DEBUG("[Worker] API is not configured, update check skipped");
    } else if (!info.ok) {
        LOG_DEBUG("[Worker] Update check failed: " + info.error);
    } else if (info.updateAvailable) {
        setStatus("Загрузка обновления " + info.latestVersion + "...");
        std::string err;
        if (selfupdate::applyFiles(info, err)) {
            LOG_DEBUG("[Worker] Updated to " + info.latestVersion);
            g_state.restartAfterUpdate = true;
            closeDialog();
            return;
        } else {
            LOG_DEBUG("[Worker] Update failed: " + err);
            std::wstring msg = L"Не удалось установить обновление:\n" + utf8ToWide(err) + L"\n\nБудет запущена текущая версия.";
            MessageBoxW(g_state.hDlg, msg.c_str(), L"Обновление", MB_OK | MB_ICONWARNING);
        }
    }
    if (!g_state.isRunning) return;

    // 3. Всё готово: окно подготовки закрывается, дальше WinMain открывает интерфейс.
    setStatus("Запуск лаунчера...");

    g_state.updateComplete = true;
    closeDialog();
}

// После самообновления новая версия запускается с "--wait-pid N": ждём, пока старый процесс завершится
// и освободит файл *.old, который затем удаляет selfupdate::cleanup().
static void waitForParent() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == L"--wait-pid") {
            DWORD pid = (DWORD)wcstoul(argv[i + 1], nullptr, 10);
            HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
            if (h) {
                WaitForSingleObject(h, 30000);
                CloseHandle(h);
            }
        }
    }
    LocalFree(argv);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    LOG_DEBUG("[GDZLauncher] Entry point");
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    util::selfExePath(); // запомнить путь к exe до возможного переименования при обновлении
    waitForParent();
    net::globalInit();
    selfupdate::cleanup();

    // Без регистрации класса msctls_progress32 диалог с индикатором не создаётся (DialogBoxParam вернёт -1).
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    // Запускаем диалог. DialogBoxParamA блокирует поток до закрытия окна.
    // Используем MAKEINTRESOURCEA (вызываем ANSI-версию), так как LAUNCHER_DLG определен как числовой ID в resource.h
    INT_PTR dialogResult = DialogBoxParamA(hInstance, MAKEINTRESOURCEA(LAUNCHER_DLG), NULL, (DLGPROC)StartupDlgProc, 0);
    if (dialogResult == -1) {
        LOG_DEBUG("[GDZLauncher] Could not create startup dialog");
        MessageBoxW(NULL, L"Не удалось создать окно загрузчика.", L"Ошибка", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Обновление установлено ещё до открытия интерфейса: запускаем новую версию.
    if (g_state.restartAfterUpdate) {
        relaunchSelf();
        return 0;
    }

    // Подготовка прошла успешно: открываем интерфейс в этом же процессе.
    if (g_state.updateComplete) {
        LOG_DEBUG("[GDZLauncher] Launching UI...");
        int result = core::app();
        if (core::restartRequested()) relaunchSelf(); // обновление из интерфейса («Обновить»)
        return result;
    }

    return 0;
}
