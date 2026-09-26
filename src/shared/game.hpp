#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

/// \brief Установка и запуск Minecraft 1.7.10 + Forge и синхронизация файлов сборки с сервером проекта.
namespace game {
	/// Ошибка с текстом, который можно показать пользователю.
	struct Error : std::runtime_error {
		using std::runtime_error::runtime_error;
	};

	/// Отменено пользователем.
	struct Cancelled : std::runtime_error {
		Cancelled() : std::runtime_error("Отменено") {}
	};

	struct Options {
		std::string nick;
		int ramMb = 4096;
		std::string javaPath; // пусто: Java 8 от Mojang скачивается автоматически
		std::string dataDir;  // пусто: каталог по умолчанию
	};

	/// Получатель событий подготовки. Методы вызываются из рабочих потоков.
	struct Events {
		virtual ~Events() = default;
		/// Этап подготовки ("Загрузка библиотек").
		virtual void stage(const std::string& text) = 0;
		/// Прогресс этапа: files — обработано/всего файлов, bytes — скачано байт.
		virtual void progress(std::uint64_t filesDone, std::uint64_t filesTotal, std::uint64_t bytes) = 0;
		/// Строка для журнала лаунчера.
		virtual void log(const std::string& line) = 0;
	};

	struct Prepared {
		std::vector<std::string> args;      // полная команда запуска (args[0] — java)
		std::filesystem::path gameDir;      // рабочий каталог игры
		std::filesystem::path logFile;      // куда писать вывод игры
	};

	/// Каталог данных лаунчера для этих настроек.
	std::filesystem::path dataDir(const Options& opt);

	/// Каталог игры (моды, конфиги, сохранения).
	std::filesystem::path gameDir(const Options& opt);

	/// "Minecraft 1.7.10 · Forge 10.13.4.1614"
	std::string packTitle();

	/// Проверяет и докачивает все файлы, синхронизирует сборку и собирает команду запуска.
	/// Бросает game::Error (сообщение для пользователя) или game::Cancelled.
	Prepared prepare(const Options& opt, Events& events, const std::atomic<bool>& cancel);
}
