#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

/// \brief Запуск игры отдельным процессом (реализации: src/linux/process_posix.cpp, src/windows/process_win.cpp).
namespace proc {
	class Child {
	public:
		virtual ~Child() = default;
		/// Блокирует поток до завершения процесса. Возвращает код выхода (128+сигнал для POSIX-сигналов).
		virtual int wait() = 0;
		virtual long pid() const = 0;
	};

	/// Запускает args[0] с аргументами args[1..]. Рабочий каталог cwd, stdout и stderr пишутся в logFile.
	/// Процесс не завершается вместе с лаунчером. При ошибке возвращает nullptr и описание в err.
	std::unique_ptr<Child> spawn(const std::vector<std::string>& args, const std::filesystem::path& cwd,
	                             const std::filesystem::path& logFile, std::string& err);
}
