#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>
#include "../shared/process.hpp"

namespace {
	std::wstring widen(const std::string& s) {
		if (s.empty()) return {};
		int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, 0);
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
		return w;
	}

	// Экранирование аргумента по правилам CommandLineToArgvW / CRT.
	void appendQuoted(std::wstring& cmd, const std::wstring& arg) {
		if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
			cmd += arg;
			return;
		}
		cmd.push_back(L'"');
		for (auto it = arg.begin();; ++it) {
			unsigned backslashes = 0;
			while (it != arg.end() && *it == L'\\') { ++it; ++backslashes; }
			if (it == arg.end()) {
				cmd.append(backslashes * 2, L'\\');
				break;
			}
			if (*it == L'"') {
				cmd.append(backslashes * 2 + 1, L'\\');
				cmd.push_back(L'"');
			} else {
				cmd.append(backslashes, L'\\');
				cmd.push_back(*it);
			}
		}
		cmd.push_back(L'"');
	}

	std::string errorText(const char* what) {
		DWORD e = GetLastError();
		return std::string(what) + " (код " + std::to_string(e) + ")";
	}

	class WinChild : public proc::Child {
	public:
		WinChild(HANDLE process, DWORD pid) : process_(process), pid_(pid) {}
		~WinChild() override { if (process_) CloseHandle(process_); }
		int wait() override {
			WaitForSingleObject(process_, INFINITE);
			DWORD code = 0;
			if (!GetExitCodeProcess(process_, &code)) return -1;
			return (int)code;
		}
		long pid() const override { return (long)pid_; }

	private:
		HANDLE process_;
		DWORD pid_;
	};
}

std::unique_ptr<proc::Child> proc::spawn(const std::vector<std::string>& args, const std::filesystem::path& cwd,
                                         const std::filesystem::path& logFile, std::string& err) {
	if (args.empty()) { err = "Пустая команда запуска"; return nullptr; }

	std::wstring app = widen(args[0]);
	std::wstring cmd;
	for (size_t i = 0; i < args.size(); ++i) {
		if (i) cmd.push_back(L' ');
		appendQuoted(cmd, widen(args[i]));
	}
	if (cmd.size() >= 32767) { err = "Слишком длинная командная строка запуска"; return nullptr; }

	SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
	HANDLE log = CreateFileW(logFile.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
	                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (log == INVALID_HANDLE_VALUE) { err = errorText("Не удалось создать журнал игры"); return nullptr; }
	HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

	// Наследуем только журнал и NUL, а не все наследуемые дескрипторы лаунчера.
	HANDLE inherit[2] = { log, nul };
	DWORD inheritCount = (nul != INVALID_HANDLE_VALUE) ? 2 : 1;
	SIZE_T attrSize = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
	std::vector<char> attrBuf(attrSize);
	auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
	bool attrsOk = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
	               UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, inheritCount * sizeof(HANDLE), nullptr, nullptr);

	STARTUPINFOEXW si{};
	si.StartupInfo.cb = sizeof(si);
	si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	si.StartupInfo.hStdInput = (nul != INVALID_HANDLE_VALUE) ? nul : nullptr;
	si.StartupInfo.hStdOutput = log;
	si.StartupInfo.hStdError = log;
	si.lpAttributeList = attrsOk ? attrs : nullptr;

	PROCESS_INFORMATION pi{};
	std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
	cmdBuf.push_back(L'\0');
	std::wstring dir = cwd.wstring();

	DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | (attrsOk ? EXTENDED_STARTUPINFO_PRESENT : 0);
	BOOL ok = CreateProcessW(app.c_str(), cmdBuf.data(), nullptr, nullptr, TRUE, flags, nullptr, dir.c_str(),
	                         reinterpret_cast<STARTUPINFOW*>(&si), &pi);
	if (!ok) err = errorText("Не удалось запустить Java");

	if (attrsOk) DeleteProcThreadAttributeList(attrs);
	CloseHandle(log);
	if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
	if (!ok) return nullptr;

	CloseHandle(pi.hThread);
	return std::unique_ptr<proc::Child>(new WinChild(pi.hProcess, pi.dwProcessId));
}
