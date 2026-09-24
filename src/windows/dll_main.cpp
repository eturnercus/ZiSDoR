#include "launcher.hpp"
#include "../shared/core.hpp"

extern "C" LAUNCHER_API int RunApp() {
    return core::app();
}

