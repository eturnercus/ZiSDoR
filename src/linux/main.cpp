#include "../shared/core.hpp"
#include "../shared/selfupdate.hpp"
#include "../shared/util.hpp"
#include <string>
#include <unistd.h>

int main(int argc, char** argv) {
	(void)argc;
	util::selfExePath(); // запомнить путь до возможного переименования файла при обновлении
	int rc = core::app();
	// После самообновления файл уже заменён: запускаем новую версию на месте текущего процесса
	// (для AppImage — сам файл .AppImage, а не бинарник внутри смонтированного образа).
	if (core::restartRequested()) {
		std::string exe = selfupdate::restartPath().string();
		execv(exe.c_str(), argv);
		return 1;
	}
	return rc;
}
