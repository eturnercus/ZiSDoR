# Кросс-сборка Windows-версии из Linux компилятором MinGW-w64 (пакет g++-mingw-w64-x86-64).
#
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-win
#
# Используется вариант компилятора с моделью потоков posix (std::thread, std::mutex).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_PREFIX x86_64-w64-mingw32)

find_program(MINGW_GCC NAMES ${MINGW_PREFIX}-gcc-posix ${MINGW_PREFIX}-gcc REQUIRED)
find_program(MINGW_GXX NAMES ${MINGW_PREFIX}-g++-posix ${MINGW_PREFIX}-g++ REQUIRED)
find_program(MINGW_WINDRES NAMES ${MINGW_PREFIX}-windres REQUIRED)

set(CMAKE_C_COMPILER ${MINGW_GCC})
set(CMAKE_CXX_COMPILER ${MINGW_GXX})
set(CMAKE_RC_COMPILER ${MINGW_WINDRES})

set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
