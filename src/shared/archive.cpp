#include "archive.hpp"
#include "util.hpp"
#include <fstream>
#include <iterator>
#include <system_error>
#include <miniz.h>

namespace fs = std::filesystem;

namespace {
	// Файл читается целиком через std::ifstream(fs::path): так корректно работают пути с кириллицей
	// на Windows (fopen внутри miniz принимает только ANSI-путь). Нативные библиотеки весят единицы мегабайт.
	bool readAll(const fs::path& p, std::string& out) {
		std::ifstream in(p, std::ios::binary);
		if (!in) return false;
		out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		return !in.bad();
	}

	struct Zip {
		mz_zip_archive z{};
		bool open = false;
		~Zip() { if (open) mz_zip_reader_end(&z); }
	};
}

bool archive::isValidZip(const fs::path& zipFile) {
	std::string data;
	if (!readAll(zipFile, data) || data.empty()) return false;
	Zip zip;
	zip.open = mz_zip_reader_init_mem(&zip.z, data.data(), data.size(), 0);
	return zip.open && mz_zip_reader_get_num_files(&zip.z) > 0;
}

bool archive::extract(const fs::path& zipFile, const fs::path& dest, const std::vector<std::string>& excludePrefixes, std::string& err) {
	std::string data;
	if (!readAll(zipFile, data)) { err = "Не удалось прочитать " + zipFile.u8string(); return false; }

	Zip zip;
	zip.open = mz_zip_reader_init_mem(&zip.z, data.data(), data.size(), 0);
	if (!zip.open) { err = "Повреждённый архив " + zipFile.u8string(); return false; }

	std::error_code ec;
	mz_uint count = mz_zip_reader_get_num_files(&zip.z);
	for (mz_uint i = 0; i < count; ++i) {
		mz_zip_archive_file_stat st;
		if (!mz_zip_reader_file_stat(&zip.z, i, &st)) continue;
		std::string name = st.m_filename;
		if (st.m_is_directory || name.empty() || name.back() == '/') continue;

		bool excluded = false;
		for (const auto& prefix : excludePrefixes) {
			if (name.rfind(prefix, 0) == 0) { excluded = true; break; }
		}
		if (excluded) continue;

		auto target = util::safeJoin(dest, name);
		if (!target) continue; // "zip slip": запись пытается выйти за пределы каталога

		size_t size = 0;
		void* buf = mz_zip_reader_extract_to_heap(&zip.z, i, &size, 0);
		if (!buf) { err = "Не удалось распаковать " + name + " из " + zipFile.u8string(); return false; }

		fs::create_directories(target->parent_path(), ec);
		std::ofstream out(*target, std::ios::binary | std::ios::trunc);
		out.write(static_cast<const char*>(buf), static_cast<std::streamsize>(size));
		mz_free(buf);
		if (!out) { err = "Не удалось записать " + target->u8string(); return false; }
	}
	return true;
}
