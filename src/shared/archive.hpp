#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace archive {
	/// Распаковывает zip/jar в dest, пропуская записи, начинающиеся с любого из excludePrefixes (например "META-INF/").
	/// Записи с небезопасными путями ("..", абсолютные) пропускаются. Возвращает false и описание в err при ошибке.
	bool extract(const std::filesystem::path& zipFile, const std::filesystem::path& dest,
	             const std::vector<std::string>& excludePrefixes, std::string& err);

	/// Проверяет, что файл является читаемым zip-архивом (для библиотек без известной контрольной суммы).
	bool isValidZip(const std::filesystem::path& zipFile);
}
