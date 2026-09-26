#include "../shared/process.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
	class PosixChild : public proc::Child {
	public:
		explicit PosixChild(pid_t pid) : pid_(pid) {}
		int wait() override {
			int status = 0;
			for (;;) {
				pid_t r = waitpid(pid_, &status, 0);
				if (r == pid_) break;
				if (r < 0 && errno != EINTR) return -1;
			}
			if (WIFEXITED(status)) return WEXITSTATUS(status);
			if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
			return -1;
		}
		long pid() const override { return static_cast<long>(pid_); }

	private:
		pid_t pid_;
	};
}

std::unique_ptr<proc::Child> proc::spawn(const std::vector<std::string>& args, const std::filesystem::path& cwd,
                                         const std::filesystem::path& logFile, std::string& err) {
	if (args.empty()) { err = "Пустая команда запуска"; return nullptr; }

	// Всё, что нужно дочернему процессу, готовим до fork: после fork в многопоточной программе
	// допустимы только async-signal-safe вызовы.
	std::vector<char*> argv;
	argv.reserve(args.size() + 1);
	for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
	argv.push_back(nullptr);
	std::string dir = cwd.string();

	int logFd = open(logFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (logFd < 0) { err = "Не удалось создать журнал " + logFile.string() + ": " + std::strerror(errno); return nullptr; }
	int nullFd = open("/dev/null", O_RDONLY | O_CLOEXEC);

	// Канал для сообщения об ошибке exec: при успешном exec он закрывается (O_CLOEXEC) и родитель читает 0 байт.
	int pipeFd[2];
	if (pipe2(pipeFd, O_CLOEXEC) != 0) {
		err = std::string("pipe: ") + std::strerror(errno);
		close(logFd);
		if (nullFd >= 0) close(nullFd);
		return nullptr;
	}

	pid_t pid = fork();
	if (pid < 0) {
		err = std::string("fork: ") + std::strerror(errno);
		close(logFd); if (nullFd >= 0) close(nullFd); close(pipeFd[0]); close(pipeFd[1]);
		return nullptr;
	}

	if (pid == 0) {
		setsid(); // отдельная сессия: игра не завершится вместе с лаунчером
		if (chdir(dir.c_str()) != 0) { int e = errno; (void)!write(pipeFd[1], &e, sizeof(e)); _exit(127); }
		if (nullFd >= 0) dup2(nullFd, 0);
		dup2(logFd, 1);
		dup2(logFd, 2);
		// Закрываем унаследованные дескрипторы (сокеты, файлы GTK), кроме канала ошибок.
		for (int fd = 3; fd < 4096; ++fd) if (fd != pipeFd[1]) close(fd);
		execv(argv[0], argv.data());
		int e = errno;
		(void)!write(pipeFd[1], &e, sizeof(e));
		_exit(127);
	}

	close(pipeFd[1]);
	close(logFd);
	if (nullFd >= 0) close(nullFd);

	int childErr = 0;
	ssize_t n;
	do { n = read(pipeFd[0], &childErr, sizeof(childErr)); } while (n < 0 && errno == EINTR);
	close(pipeFd[0]);
	if (n > 0) {
		int status = 0;
		waitpid(pid, &status, 0);
		err = "Не удалось запустить " + args[0] + ": " + std::strerror(childErr);
		return nullptr;
	}
	return std::unique_ptr<proc::Child>(new PosixChild(pid));
}
