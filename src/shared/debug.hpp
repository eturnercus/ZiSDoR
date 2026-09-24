#pragma once

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#else
#include <iostream>
#endif

// Макрос для универсального логирования
#ifdef _WIN32
    #define LOG_DEBUG(msg) do { \
        char buf[1024]; \
        snprintf(buf, sizeof(buf), "%s", msg); \
        OutputDebugStringA(buf); \
        OutputDebugStringA("\n"); \
    } while(0)
#else
    #define LOG_DEBUG(msg) do { \
        std::cout << msg << std::endl; \
    } while(0)
#endif
