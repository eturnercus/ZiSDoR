#include "launcher.hpp"
#include "../shared/core.hpp"

extern "C" LAUNCHER_API __stdcall int RunApp() {
    return core::app();
}

