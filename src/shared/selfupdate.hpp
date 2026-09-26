#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/// \brief Обновление самого лаунчера и новости проекта (файл launcher.json на сервере API).
namespace selfupdate {
	struct FileEntry {
		std::string path; // имя файла в папке лаунчера ("launcher", "launcher.dll", "Updater.exe")
		std::string url;
		std::string sha1;
		std::uint64_t size = 0;
	};

	struct NewsItem {
		std::string title, date, text, url;
	};

	struct Info {
		bool configured = false;      // задан ли адрес API при сборке
		bool ok = false;              // launcher.json получен и разобран
		std::string error;
		std::string latestVersion;
		bool updateAvailable = false; // версия отличается и хотя бы один файл не совпадает с локальным
		std::vector<FileEntry> files; // файлы, которые нужно заменить
		std::vector<NewsItem> news;
	};

	/// Папка, где лежат исполняемые файлы лаунчера.
	std::filesystem::path installDir();

	/// Загружает launcher.json и сравнивает файлы текущей платформы с установленными.
	Info check(const std::atomic<bool>* cancel = nullptr);

	/// JSON для интерфейса.
	std::string toJson(const Info& info);

	/// Скачивает и устанавливает файлы из info.files в installDir().
	/// Запущенный исполняемый файл заменяется через переименование (на Windows его нельзя перезаписать,
	/// но можно переименовать). Возвращает false и описание в err при ошибке.
	bool applyFiles(const Info& info, std::string& err);

	/// Удаляет остатки прошлых обновлений своих файлов (*.old, *.new).
	void cleanup();
}
