#include "core.hpp"
#include <iostream>
int core::app() {
		LOG_DEBUG("loaded launcher");
		try {
				webview::webview w(false, nullptr);
				w.set_title("Basic Example");
				w.set_size(480, 320, WEBVIEW_HINT_NONE);
				w.set_html("Thanks for using webview!");
				w.run();
		} catch (const webview::exception &e) {
				LOG_DEBUG(e.what());
				return 1;
		}

		return 0;
}

