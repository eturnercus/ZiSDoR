#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

/// \brief SHA-1 для проверки целостности скачанных файлов (так их публикуют Mojang, Maven и manifest сборки).
namespace sha1 {
	class Hasher {
	public:
		Hasher();
		void update(const void* data, std::size_t len);
		/// Возвращает хеш в виде 40 шестнадцатеричных символов в нижнем регистре. После вызова объект не используется.
		std::string hexdigest();

	private:
		void block(const std::uint8_t* p);
		std::uint32_t h_[5];
		std::uint8_t buf_[64];
		std::size_t bufLen_ = 0;
		std::uint64_t total_ = 0;
	};

	std::string ofString(const std::string& data);

	/// Хеш файла или std::nullopt, если файл не читается.
	std::optional<std::string> ofFile(const std::filesystem::path& path);
}
