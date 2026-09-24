cmake_minimum_required(VERSION 3.16)
project(GDZLauncher LANGUAGES CXX)

set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES OFF)
set(CMAKE_C_USE_RESPONSE_FILE_FOR_INCLUDES OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build/)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build/)

# Оптимизация размера бинарников
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    set(OPTIMIZE_FLAGS "/O1 /GL")
    set(LINK_OPTIMIZE_FLAGS "/LTCG")
else()
    set(OPTIMIZE_FLAGS "-Os")
endif()


include(FetchContent)
FetchContent_Declare(
	webview
	GIT_REPOSITORY https://github.com/webview/webview
	GIT_TAG 0.12.0
)

# Добавляем путь к EventToken.h ГЛОБАЛЬНО до того, как webview будет сконфигурирована.
if (WIN32)
    include_directories(SYSTEM "${CMAKE_BINARY_DIR}/_deps/webview-src/compatibility/mingw/include")
endif()

FetchContent_MakeAvailable(webview)


if (WIN32)
    if (MSVC)
        # Настройки для MSVC (Visual Studio)
        set(WIN_ABI_FLAGS "")
        set(S_CRT_STATIC "/MT") # Статическая линковка CRT
        # Для MSVC используем стандартные флаги оптимизации, чтобы не было предупреждений D9002
        set(MSVC_OPT "/O1")
    else()
        # Настройки для MinGW
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")
        set(WIN_ABI_FLAGS "-D_GLIBCXX_USE_CXX11_ABI=0")
        set(S_CRT_STATIC "")
        set(MSVC_OPT ${OPTIMIZE_FLAGS})
    endif()

    add_executable(Updater WIN32)
    target_compile_options(Updater PRIVATE ${MSVC_OPT} ${WIN_ABI_FLAGS})
    if(MSVC)
        target_compile_options(Updater PRIVATE ${S_CRT_STATIC})
    endif()
    target_sources(Updater PRIVATE
       src/windows/updater.cpp
       src/windows/updater_res.rc
    )
    # ИСПРАВЛЕНИЕ: Добавляем папку с launcher.h в пути поиска
    target_include_directories(Updater PRIVATE src/windows)

    add_library(launcher SHARED)
    target_compile_definitions(launcher PRIVATE LAUNCHER_EXPORTS)
    target_compile_options(launcher PRIVATE ${MSVC_OPT} ${WIN_ABI_FLAGS})
    if(MSVC)
        target_compile_options(launcher PRIVATE ${S_CRT_STATIC})
    endif()
    target_link_options(launcher PRIVATE ${LINK_OPTIMIZE_FLAGS})
    target_sources(launcher PRIVATE
        src/windows/dll_main.cpp
        src/shared/core.cpp
    )

    if (MSVC)
        target_link_libraries(launcher PRIVATE webview::core)
    else()
        target_link_libraries(launcher PRIVATE
            webview::core
            -Wl,-Bstatic
            stdc++
            gcc
            winpthread
            -Wl,-Bdynamic
        )
    endif()

    if (NOT MSVC)
        target_include_directories(launcher SYSTEM PRIVATE
            "${CMAKE_BINARY_DIR}/_deps/webview-src/compatibility/mingw/include"
        )
    endif()
else()
	add_executable(launcher)
	target_compile_options(launcher PRIVATE ${OPTIMIZE_FLAGS})
	target_link_options(launcher PRIVATE ${LINK_OPTIMIZE_FLAGS})
	target_sources(launcher PRIVATE
		src/linux/main.cpp
		src/shared/core.cpp
	)
	target_link_libraries(launcher PRIVATE webview::core)
endif()
