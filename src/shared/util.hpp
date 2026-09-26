#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace util {
	/// "windows" или "linux": ключ ОС в правилах библиотек Mojang.
	const char* osName();

	/// Каталог данных по умолчанию: %APPDATA%\GDZLauncher или $XDG_DATA_HOME/GDZLauncher (~/.local/share/GDZLauncher).
	std::filesystem::path defaultDataDir();

	/// Путь к исполняемому файлу текущего процесса.
	std::filesystem::path selfExePath();

	/// Безопасно соединяет root и относительный путь из манифеста (разделитель '/').
	/// Отклоняет абсолютные пути, "..", пустые компоненты, двоеточия и обратные слэши:
	/// файл с сервера не должен попасть за пределы root.
	std::optional<std::filesystem::path> safeJoin(const std::filesystem::path& root, const std::string& rel);

	/// Проверка относительного пути по тем же правилам, что и safeJoin.
	bool isSafeRelative(const std::string& rel);

	/// Сопоставление с шаблоном: '*' — любая последовательность символов (кроме '/'), '?' — один символ.
	/// "**" — любая последовательность, включая '/'.
	bool globMatch(const std::string& pattern, const std::string& text);

	/// "group:artifact:version[:classifier][@ext]" -> "group/path/artifact/version/artifact-version[-classifier].ext"
	std::string mavenPath(const std::string& name);

	/// Абсолютный адрес: rel, если он уже начинается с http(s)://, иначе base + rel.
	std::string joinUrl(const std::string& base, const std::string& rel);

	/// Строчные буквы (только ASCII).
	std::string lower(std::string s);

	/// Похоже на SHA-1 в hex (40 символов 0-9a-f).
	bool isSha1(const std::string& s);

	/// Детерминированный UUID офлайн-профиля по нику (формат 8-4-4-4-12).
	std::string offlineUuid(const std::string& nick);

	/// Путь для передачи в Java. На Windows Java 8 читает командную строку в ANSI-кодировке,
	/// поэтому путь с символами вне ASCII заменяется коротким именем 8.3 (если оно доступно).
	std::string javaPath(const std::filesystem::path& p);

	/// Открывает файл или папку в системном приложении (проводник, xdg-open).
	bool openPath(const std::filesystem::path& p);

	/// Значение переменной окружения или пустая строка.
	std::string env(const char* name);
}
