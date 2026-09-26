// Тестовые привязки для сквозного теста (tests/e2e/run_e2e.py).
// Собирается только с -DGDZ_E2E_HARNESS=ON и никогда не попадает в обычные сборки.
#include <webview/webview.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace embedded {
	extern const unsigned char e2e_harness_js[];
}

namespace e2e {
	namespace {
		using json = nlohmann::json;

		std::string env(const char* name) {
			const char* v = std::getenv(name);
			return v ? v : "";
		}

		std::string arg(const std::string& req) {
			json a = json::parse(req, nullptr, false);
			if (a.is_array() && !a.empty() && a[0].is_string()) return a[0].get<std::string>();
			return {};
		}
	}

	void install(webview::webview& w) {
		// Сценарий из окружения. После самообновления (есть файл-флаг) сценарий "update" становится "after-update".
		w.bind("e2e_env", [](const std::string&) -> std::string {
			std::string scenario = env("E2E_SCENARIO");
			std::string flag = env("E2E_FLAG_FILE");
			std::error_code ec;
			if (scenario == "update" && !flag.empty() && std::filesystem::exists(flag, ec)) scenario = "after-update";
			return json(scenario).dump();
		});
		w.bind("e2e_mark", [](const std::string&) -> std::string {
			std::string flag = env("E2E_FLAG_FILE");
			if (!flag.empty()) std::ofstream(flag) << "1";
			return "true";
		});
		w.bind("e2e_print", [](const std::string& req) -> std::string {
			std::cout << "E2E: " << arg(req) << std::endl;
			return "true";
		});
		w.bind("e2e_report", [&w](const std::string& req) -> std::string {
			std::cout << "E2E: " << arg(req) << std::endl;
			w.terminate();
			return "true";
		});
		w.init(reinterpret_cast<const char*>(embedded::e2e_harness_js));
	}
}
