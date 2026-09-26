#include "../shared/core.hpp"
#include "../shared/util.hpp"
#include <string>
#include <unistd.h>

int main(int argc, char** argv) {
	(void)argc;
	int rc = core::app();
	// После самообновления бинарник уже заменён: запускаем новую версию на месте текущего процесса.
	if (core::restartRequested()) {
		std::string exe = util::selfExePath().string();
		execv(exe.c_str(), argv);
		return 1;
	}
	return rc;
}
