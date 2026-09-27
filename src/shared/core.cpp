#include "core.hpp"
#include "config.hpp"
#include "debug.hpp"
#include "embedded.hpp"
#include "game.hpp"
#include "net.hpp"
#include "process.hpp"
#include "selfupdate.hpp"
#include "settings.hpp"
#include "util.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define GDZ_PLATFORM "windows"
#else
#define GDZ_PLATFORM "linux"
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

#ifdef GDZ_E2E_HARNESS
// Тестовые сценарии (tests/e2e/harness.cpp), только в сборке для сквозного теста.
namespace e2e { void install(webview::webview& w); }
#endif

namespace {
	std::atomic<bool> g_restart{ false };

	/// dump без исключений на некорректном UTF-8 (например, в системных сообщениях об ошибках).
	std::string dump(const json& j) {
		return j.dump(-1, ' ', false, json::error_handler_t::replace);
	}

	/// Первый аргумент вызова из JS: webview передаёт аргументы JSON-массивом.
	json firstArg(const std::string& req) {
		try {
			json args = json::parse(req);
			if (args.is_array() && !args.empty()) return args[0];
		} catch (...) {}
		return json();
	}

	/// Связь рабочих потоков с окном. После закрытия окна (detach) события молча отбрасываются,
	/// поэтому фоновые потоки могут пережить webview без обращения к уничтоженному объекту.
	class Bridge {
	public:
		explicit Bridge(webview::webview* w) : w_(w) {}

		void detach() {
			std::lock_guard<std::mutex> lock(mutex_);
			w_ = nullptr;
		}

		void emit(const json& event) {
			std::string js = "window.__gdz && window.__gdz(" + dump(event) + ")";
			std::lock_guard<std::mutex> lock(mutex_);
			if (!w_) return;
			webview::webview* w = w_;
			w->dispatch([w, js] { w->eval(js); });
		}

		void resolve(const std::string& id, const std::string& result) {
			std::lock_guard<std::mutex> lock(mutex_);
			if (w_) w_->resolve(id, 0, result);
		}

		void terminate() {
			std::lock_guard<std::mutex> lock(mutex_);
			if (!w_) return;
			webview::webview* w = w_;
			w->dispatch([w] { w->terminate(); });
		}

	private:
		std::mutex mutex_;
		webview::webview* w_;
	};

	/// Журнал лаунчера (data/launcher.log): подробности для разбора ошибок.
	class LogFile {
	public:
		void open(const fs::path& path) {
			std::lock_guard<std::mutex> lock(mutex_);
			std::error_code ec;
			fs::create_directories(path.parent_path(), ec);
			out_.open(path, std::ios::binary | std::ios::trunc);
		}
		void write(const std::string& line) {
			std::lock_guard<std::mutex> lock(mutex_);
			LOG_DEBUG(line);
			if (!out_) return;
			std::time_t t = std::time(nullptr);
			char stamp[32];
			std::strftime(stamp, sizeof(stamp), "%H:%M:%S", std::localtime(&t));
			out_ << "[" << stamp << "] " << line << "\n";
			out_.flush();
		}

	private:
		std::mutex mutex_;
		std::ofstream out_;
	};

	struct Runtime {
		std::shared_ptr<Bridge> bridge;
		std::mutex mutex;
		std::thread worker;
		std::atomic<bool> busy{ false };
		std::atomic<bool> cancel{ false };
		std::atomic<bool> gameRunning{ false };
		LogFile log;
		selfupdate::Info lastUpdate;
	};

	/// События подготовки -> интерфейс (этапы и прогресс) и журнал (подробности).
	class UiEvents : public game::Events {
	public:
		explicit UiEvents(std::shared_ptr<Runtime> rt) : rt_(std::move(rt)) {}
		void stage(const std::string& text) override {
			rt_->log.write("== " + text);
			rt_->bridge->emit({ { "type", "stage" }, { "text", text } });
		}
		void progress(std::uint64_t done, std::uint64_t total, std::uint64_t bytes) override {
			rt_->bridge->emit({ { "type", "progress" }, { "done", done }, { "total", total }, { "bytes", bytes } });
		}
		void log(const std::string& line) override { rt_->log.write(line); }

	private:
		std::shared_ptr<Runtime> rt_;
	};

	game::Options optionsFrom(const json& s) {
		game::Options o;
		if (!s.is_object()) return o;
		o.nick = s.value("nick", "");
		double ramGb = 4;
		if (s.contains("ram") && s["ram"].is_number()) ramGb = s["ram"].get<double>();
		o.ramMb = static_cast<int>(ramGb * 1024);
		o.javaPath = s.value("java", "");
		o.dataDir = s.value("gameDir", "");
		return o;
	}

	void runPrepare(std::shared_ptr<Runtime> rt, json settingsJson) {
		UiEvents events(rt);
		game::Options opt = optionsFrom(settingsJson);
		bool closeOnLaunch = settingsJson.is_object() && settingsJson.value("closeOnLaunch", false);
		rt->log.open(game::dataDir(opt) / "launcher.log");
		rt->log.write(std::string("GDZLauncher ") + GDZ_VERSION + " (" + GDZ_PLATFORM + "), " + game::packTitle());

		try {
			game::Prepared p = game::prepare(opt, events, rt->cancel);

			std::string command;
			for (const auto& a : p.args) command += (command.empty() ? "" : " ") + a;
			rt->log.write("Команда запуска: " + command);
			events.stage("Запуск игры");

			std::string err;
			auto child = proc::spawn(p.args, p.gameDir, p.logFile, err);
			if (!child) throw game::Error(err);

			rt->gameRunning = true;
			rt->log.write("Игра запущена, PID " + std::to_string(child->pid()));
			rt->bridge->emit({ { "type", "launched" }, { "pid", child->pid() } });

			std::string logPath = p.logFile.u8string();
			std::shared_ptr<proc::Child> shared(std::move(child));
			std::thread([rt, shared, logPath] {
				int code = shared->wait();
				rt->gameRunning = false;
				rt->log.write("Игра завершилась с кодом " + std::to_string(code));
				rt->bridge->emit({ { "type", "exited" }, { "code", code }, { "log", logPath } });
			}).detach();

			if (closeOnLaunch) rt->bridge->terminate();
		} catch (const game::Cancelled&) {
			rt->log.write("Подготовка отменена");
			rt->bridge->emit({ { "type", "cancelled" } });
		} catch (const std::exception& e) {
			rt->log.write(std::string("Ошибка: ") + e.what());
			rt->bridge->emit({ { "type", "error" }, { "message", e.what() } });
		}
		rt->busy = false;
	}

	/// Асинхронная привязка: обработчик выполняется в отдельном потоке, чтобы сеть не блокировала окно.
	template <typename Fn>
	void bindAsync(webview::webview& w, const std::string& name, std::shared_ptr<Runtime> rt, Fn fn) {
		w.bind(name, [rt, fn](const std::string& id, const std::string& req, void*) {
			std::thread([rt, fn, id, req] {
				std::string result;
				try {
					result = fn(req);
				} catch (const std::exception& e) {
					result = dump({ { "ok", false }, { "error", e.what() } });
				}
				rt->bridge->resolve(id, result);
			}).detach();
		}, nullptr);
	}

}

bool core::restartRequested() {
	return g_restart.load();
}

int core::app() {
		LOG_DEBUG("loaded launcher");
		net::globalInit();
		selfupdate::cleanup();

		auto rt = std::make_shared<Runtime>();
		try {
				webview::webview w(false, nullptr);
				rt->bridge = std::make_shared<Bridge>(&w);
				w.set_title("GDZLauncher");
				w.set_size(980, 640, WEBVIEW_HINT_MIN);
				w.set_size(1120, 720, WEBVIEW_HINT_NONE);
#ifdef _WIN32
				// Иконка окна и панели задач из ресурсов exe (IDI_APP_ICON = 1).
				if (auto hwnd = w.window(); hwnd.ok()) {
						HINSTANCE inst = GetModuleHandleW(nullptr);
						HICON iconBig = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
						HICON iconSmall = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
						if (iconBig) SendMessageW(static_cast<HWND>(hwnd.value()), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconBig));
						if (iconSmall) SendMessageW(static_cast<HWND>(hwnd.value()), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall));
				}
#endif

				// Сведения о лаунчере и сборке.
				w.bind("gdz_info", [](const std::string&) -> std::string {
						return dump({
								{ "version", GDZ_VERSION },
								{ "platform", GDZ_PLATFORM },
								{ "configPath", settings::filePath().u8string() },
								{ "dataDir", util::defaultDataDir().u8string() },
								{ "apiConfigured", !std::string(GDZ_API_URL).empty() },
								{ "server", GDZ_SERVER_ADDRESS },
								{ "pack", game::packTitle() },
						});
				});

				// Сохранённые настройки (JSON-объект или {}).
				w.bind("gdz_get_settings", [](const std::string&) -> std::string {
						return settings::load();
				});

				// Сохранение настроек. JS передаёт один аргумент: JSON-строку с объектом.
				w.bind("gdz_save_settings", [](const std::string& req) -> std::string {
						json arg = firstArg(req);
						return settings::save(arg.is_string() ? arg.get<std::string>() : std::string()) ? "true" : "false";
				});

				// Подготовка и запуск игры. Ход подготовки приходит событиями window.__gdz(...).
				w.bind("gdz_play", [rt](const std::string& req) -> std::string {
						if (rt->gameRunning) return dump({ { "ok", false }, { "code", "running" } });
						bool expected = false;
						if (!rt->busy.compare_exchange_strong(expected, true)) return dump({ { "ok", false }, { "code", "busy" } });

						json arg = firstArg(req);
						json settingsJson = json::object();
						if (arg.is_string()) {
								try { settingsJson = json::parse(arg.get<std::string>()); } catch (...) {}
						}
						std::lock_guard<std::mutex> lock(rt->mutex);
						if (rt->worker.joinable()) rt->worker.join(); // предыдущий запуск уже завершён (busy был false)
						rt->cancel = false;
						rt->worker = std::thread(runPrepare, rt, settingsJson);
						return dump({ { "ok", true } });
				});

				w.bind("gdz_cancel", [rt](const std::string&) -> std::string {
						rt->cancel = true;
						return "true";
				});

				// Открыть папку игры, журнал игры или папку данных.
				w.bind("gdz_open", [](const std::string& req) -> std::string {
						json arg = firstArg(req);
						std::string what = arg.is_string() ? arg.get<std::string>() : "";
						game::Options opt = optionsFrom(json::parse(settings::load(), nullptr, false));
						fs::path target;
						if (what == "game") target = game::gameDir(opt);
						else if (what == "log") target = game::gameDir(opt) / "logs" / "launcher_output.log";
						else if (what == "launcherLog") target = game::dataDir(opt) / "launcher.log";
						else target = game::dataDir(opt);
						std::error_code ec;
						if (!fs::exists(target, ec)) return "false";
						return util::openPath(target) ? "true" : "false";
				});

				// Проверка обновлений лаунчера и новости.
				bindAsync(w, "gdz_check_update", rt, [rt](const std::string&) {
						selfupdate::Info info = selfupdate::check(&rt->cancel);
						{
								std::lock_guard<std::mutex> lock(rt->mutex);
								rt->lastUpdate = info;
						}
						return selfupdate::toJson(info);
				});

				// Установка обновления и перезапуск.
				bindAsync(w, "gdz_apply_update", rt, [rt](const std::string&) -> std::string {
						if (rt->busy) return dump({ { "ok", false }, { "error", "Дождитесь окончания загрузки" } });
						std::string err;
						selfupdate::Info info;
						{
								std::lock_guard<std::mutex> lock(rt->mutex);
								info = rt->lastUpdate;
						}
						if (!info.updateAvailable) info = selfupdate::check();
						if (!info.updateAvailable) return dump({ { "ok", false }, { "error", "Обновление не найдено" } });
						if (!selfupdate::applyFiles(info, err)) return dump({ { "ok", false }, { "error", err } });
						// Файл уже заменён: после закрытия окна main()/WinMain запустят новую версию.
						g_restart = true;
						rt->bridge->terminate();
						return dump({ { "ok", true } });
				});

#ifdef GDZ_E2E_HARNESS
				e2e::install(w);
#endif
				w.set_html(reinterpret_cast<const char*>(embedded::index_html));
				w.run();

				// Окно закрыто: останавливаем подготовку и отвязываем фоновые потоки от окна.
				rt->cancel = true;
				{
						std::lock_guard<std::mutex> lock(rt->mutex);
						if (rt->worker.joinable()) rt->worker.join();
				}
				rt->bridge->detach();
		} catch (const webview::exception &e) {
				LOG_DEBUG(e.what());
				if (rt->bridge) rt->bridge->detach();
				return 1;
		}

		return 0;
}
