#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

/// \brief HTTP(S)-загрузки. Реализация: libcurl на Linux (src/linux/net_curl.cpp), WinHTTP на Windows
/// (src/windows/net_winhttp.cpp). Все функции потокобезопасны: у каждого потока своё соединение.
namespace net {
	/// Прогресс: скачано байт, всего байт (0, если сервер не сообщил размер).
	using Progress = std::function<void(std::uint64_t done, std::uint64_t total)>;

	/// Приёмник данных. Возвращает false, чтобы прервать загрузку.
	using Sink = std::function<bool(const char* data, std::size_t len)>;

	struct Result {
		bool ok = false;
		bool cancelled = false;
		long status = 0;       // HTTP-код, если ответ был получен
		std::string error;     // описание ошибки для журнала и пользователя

		/// Ошибку стоит повторить: сетевой сбой или ответ 5xx/429. 4xx (например 404) не повторяем.
		bool retryable() const { return !ok && !cancelled && (status == 0 || status >= 500 || status == 429); }
	};

	/// Платформенная часть: GET-запрос с потоковой передачей тела в sink.
	Result request(const std::string& url, const Sink& sink, const Progress& progress, const std::atomic<bool>* cancel);

	/// Загружает ответ в строку (не больше maxBytes).
	Result fetch(const std::string& url, std::string& out, const std::atomic<bool>* cancel = nullptr,
	             std::size_t maxBytes = 64u * 1024u * 1024u);

	/// Загружает файл: пишет во временный "<dest>.part" и переименовывает в dest только после успешной загрузки.
	Result download(const std::string& url, const std::filesystem::path& dest, const Progress& progress = {},
	                const std::atomic<bool>* cancel = nullptr);

	/// Однократная инициализация библиотеки (вызывается из главного потока до первых запросов).
	void globalInit();
}
