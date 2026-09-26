#include "../shared/net.hpp"
#include "../shared/config.hpp"
#include <curl/curl.h>
#include <mutex>

namespace {
	struct Handle {
		CURL* curl = nullptr;
		~Handle() { if (curl) curl_easy_cleanup(curl); }
	};

	// Отдельный handle на поток: curl переиспользует соединения (keep-alive, TLS-сессии) между запросами.
	thread_local Handle tlsHandle;

	CURL* handle() {
		if (!tlsHandle.curl) tlsHandle.curl = curl_easy_init();
		else curl_easy_reset(tlsHandle.curl);
		return tlsHandle.curl;
	}

	struct Context {
		const net::Sink* sink;
		const net::Progress* progress;
		const std::atomic<bool>* cancel;
		bool sinkStopped = false;
	};

	size_t onWrite(char* ptr, size_t size, size_t nmemb, void* userdata) {
		auto* ctx = static_cast<Context*>(userdata);
		size_t len = size * nmemb;
		if (ctx->cancel && ctx->cancel->load()) return 0;
		if (!(*ctx->sink)(ptr, len)) { ctx->sinkStopped = true; return 0; }
		return len;
	}

	int onProgress(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
		auto* ctx = static_cast<Context*>(userdata);
		if (ctx->cancel && ctx->cancel->load()) return 1;
		if (ctx->progress && *ctx->progress) {
			(*ctx->progress)(static_cast<std::uint64_t>(dlnow), static_cast<std::uint64_t>(dltotal > 0 ? dltotal : 0));
		}
		return 0;
	}
}

void net::globalInit() {
	static std::once_flag once;
	std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

net::Result net::request(const std::string& url, const Sink& sink, const Progress& progress, const std::atomic<bool>* cancel) {
	globalInit();
	Result r;
	CURL* c = handle();
	if (!c) {
		r.error = "Не удалось инициализировать libcurl";
		return r;
	}

	Context ctx{ &sink, &progress, cancel };
	char errbuf[CURL_ERROR_SIZE] = { 0 };
	static const std::string userAgent = std::string("GDZLauncher/") + GDZ_VERSION;

	curl_easy_setopt(c, CURLOPT_URL, url.c_str());
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
	curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
	curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
	curl_easy_setopt(c, CURLOPT_USERAGENT, userAgent.c_str());
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	// Обрыв, если скорость ниже 1 КБ/с дольше 30 секунд (зависшее соединение).
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onWrite);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &ctx);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
	curl_easy_setopt(c, CURLOPT_XFERINFODATA, &ctx);

	CURLcode code = curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);

	if (code == CURLE_OK) {
		r.ok = true;
		return r;
	}
	if (cancel && cancel->load()) {
		r.cancelled = true;
		r.error = "Отменено";
		return r;
	}
	if (ctx.sinkStopped) {
		r.error = "Загрузка прервана";
		return r;
	}
	r.error = (errbuf[0] ? std::string(errbuf) : std::string(curl_easy_strerror(code))) + " (" + url + ")";
	return r;
}
