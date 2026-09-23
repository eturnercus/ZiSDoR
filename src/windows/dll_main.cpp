#include "launcher.hpp"
#include "../shared/core.hpp"

extern "C" LAUNCHER_API inline int RunApp() {
    return core::app();
}

