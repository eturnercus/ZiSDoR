#include "net.hpp"
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

net::Result net::fetch(const std::string& url, std::string& out, const std::atomic<bool>* cancel, std::size_t maxBytes) {
	out.clear();
	bool tooBig = false;
	Result r = request(url, [&](const char* data, std::size_t len) {
		if (out.size() + len > maxBytes) { tooBig = true; return false; }
		out.append(data, len);
		return true;
	}, {}, cancel);
	if (tooBig) {
		r.ok = false;
		r.error = "Ответ слишком большой: " + url;
	}
	return r;
}

net::Result net::download(const std::string& url, const fs::path& dest, const Progress& progress, const std::atomic<bool>* cancel) {
	std::error_code ec;
	if (dest.has_parent_path()) fs::create_directories(dest.parent_path(), ec);

	fs::path part = dest;
	part += ".part";

	Result r;
	{
		std::ofstream out(part, std::ios::binary | std::ios::trunc);
		if (!out) {
			r.error = "Не удалось создать файл " + part.u8string();
			return r;
		}
		bool writeFailed = false;
		r = request(url, [&](const char* data, std::size_t len) {
			out.write(data, static_cast<std::streamsize>(len));
			if (!out) { writeFailed = true; return false; }
			return true;
		}, progress, cancel);
		out.close();
		if (writeFailed || (r.ok && !out)) {
			r.ok = false;
			r.error = "Ошибка записи на диск: " + part.u8string();
		}
	}

	if (!r.ok) {
		fs::remove(part, ec);
		return r;
	}

	fs::rename(part, dest, ec);
	if (ec) {
		// На Windows rename не заменяет существующий файл, если тот открыт; пробуем удалить и повторить.
		fs::remove(dest, ec);
		fs::rename(part, dest, ec);
	}
	if (ec) {
		fs::remove(part, ec);
		r.ok = false;
		r.error = "Не удалось сохранить " + dest.u8string() + ": " + ec.message();
	}
	return r;
}
