#pragma once

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <iostream>
#endif

namespace debug {
    // Функции вместо макроса с локальным буфером: прежний макрос объявлял внутри собственный `buf`,
    // и вызов LOG_DEBUG(buf) с локальным буфером вызывающего кода печатал неинициализированную память.
    inline void log(const char* msg) {
#ifdef _WIN32
        OutputDebugStringA(msg);
        OutputDebugStringA("\n");
#else
        std::cout << msg << std::endl;
#endif
    }

    inline void log(const std::string& msg) {
        log(msg.c_str());
    }
}

// Макрос для универсального логирования (принимает const char* и std::string)
#define LOG_DEBUG(msg) ::debug::log(msg)
