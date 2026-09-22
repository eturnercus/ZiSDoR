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
    set(LINK_OPTIMIZE_FLAGS "-s")
endif()


include(FetchContent)
FetchContent_Declare(
	webview
	GIT_REPOSITORY https://github.com/webview/webview
	GIT_TAG 0.12.0
)
FetchContent_MakeAvailable(webview)


if (WIN32)
    add_executable(Updater WIN32)
    target_compile_options(Updater PRIVATE ${OPTIMIZE_FLAGS})
    target_sources(Updater PRIVATE
       src/windows/updater.cpp
       src/windows/updater_res.rc
    )


    add_library(launcher SHARED)
    target_compile_definitions(launcher PRIVATE LAUNCHER_EXPORTS)
    target_compile_options(launcher PRIVATE ${OPTIMIZE_FLAGS})
    target_link_options(launcher PRIVATE ${LINK_OPTIMIZE_FLAGS})
    target_sources(launcher PRIVATE
        src/windows/dll_main.cpp
        src/shared/core.cpp
    )

    target_link_libraries(launcher PRIVATE webview::core)

    # Исправление для YCM: добавляем путь к EventToken.h для MinGW
    # Используем переменную, которую предоставляет webview, или путь относительно бинарника. (По хорошему не надо аплоадить в апстрим.)
    target_include_directories(launcher SYSTEM PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/webview-src/compatibility/mingw/include"
    )
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
