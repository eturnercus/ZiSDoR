#pragma once

#include <cstddef>

/// \brief Файлы, встроенные в бинарник на этапе конфигурации CMake (cmake/embed_file.cmake).
/// Каждый массив заканчивается нулевым байтом и может использоваться как C-строка.
namespace embedded {
	/// Интерфейс: src/ui/index.html
	extern const unsigned char index_html[];
	extern const std::size_t index_html_size;

	/// Профиль Forge: src/profiles/forge-1.7.10.json
	extern const unsigned char forge_profile[];
	extern const std::size_t forge_profile_size;
}
