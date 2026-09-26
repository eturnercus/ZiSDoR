#pragma once

#include <filesystem>
#include <string>

/// \brief Хранение пользовательских настроек лаунчера (один JSON-файл в каталоге конфигурации).
namespace settings {
	/// Каталог конфигурации: %APPDATA%\GDZLauncher (Windows) или $XDG_CONFIG_HOME/GDZLauncher (~/.config/GDZLauncher).
	std::filesystem::path configDir();

	/// Путь к файлу настроек.
	std::filesystem::path filePath();

	/// Возвращает JSON-объект с сохранёнными настройками, либо "{}" если файла нет или он повреждён.
	std::string load();

	/// Сохраняет JSON-объект. Возвращает false, если данные не похожи на объект или запись не удалась.
	bool save(const std::string& json);
}
