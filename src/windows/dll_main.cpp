#include "launcher.hpp"
#include "../shared/core.hpp"

extern "C" LAUNCHER_API int __stdcall RunApp() {
    return core::app();
}

