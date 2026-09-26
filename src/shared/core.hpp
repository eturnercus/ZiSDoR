#pragma once

#include <webview/webview.h>

namespace core {
	int app();

	/// true, если интерфейс завершился ради перезапуска после самообновления (Linux: main() выполняет execv).
	bool restartRequested();
}
