#include <optional>
#include <filesystem>
namespace fs = std::filesystem;
namespace bootstrap {
	bool isWebView2Installed();
	std::optional<fs::path> downloadWebView2();
	bool installWebView2(fs::path filePath);
}
