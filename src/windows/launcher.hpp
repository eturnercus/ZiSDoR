#pragma once

#ifdef LAUNCHER_EXPORTS
#define LAUNCHER_API __declspec(dllexport)
#else
#define LAUNCHER_API __declspec(dllimport)
#endif

extern "C" LAUNCHER_API int RunApp();
