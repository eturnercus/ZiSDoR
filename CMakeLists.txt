cmake_minimum_required(VERSION 3.16)
# Версия лаунчера: из неё берутся номер в интерфейсе, VERSIONINFO exe и тег релиза в GitHub Actions.
project(GDZLauncher VERSION 0.001 LANGUAGES C CXX)

# Устанавливаем стандарт C++17 для всего проекта
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES OFF)
set(CMAKE_C_USE_RESPONSE_FILE_FOR_INCLUDES OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build/)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build/)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Скачивание архивов через FetchContent: время файлов берём текущее (без предупреждения CMP0135).
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# ---------------------------------------------------------------------------
# Конфигурация лаунчера при сборке (см. docs/API.md)
# ---------------------------------------------------------------------------
set(GDZ_API_URL "" CACHE STRING "Базовый адрес API проекта (launcher.json, client.json). Пусто: без сервера проекта")
set(GDZ_SERVER_ADDRESS "" CACHE STRING "Сервер, к которому игра подключается при запуске (host или host:port). Имеет приоритет над client.json")
set(GDZ_VERSION_MANIFEST_URL "" CACHE STRING "Зеркало списка версий Mojang (пусто: официальный)")
set(GDZ_JAVA_RUNTIME_URL "" CACHE STRING "Зеркало списка сред Java от Mojang (пусто: официальный)")
set(GDZ_LIBRARIES_URL "" CACHE STRING "Зеркало libraries.minecraft.net (пусто: официальный)")
set(GDZ_FORGE_MAVEN_URL "" CACHE STRING "Зеркало maven.minecraftforge.net (пусто: официальный)")
set(GDZ_RESOURCES_URL "" CACHE STRING "Зеркало resources.download.minecraft.net (пусто: официальный)")

# Сквозной тест (tests/e2e): встраивает тестовые сценарии и направляет все адреса на локальный сервер 127.0.0.1:8765.
# Только для проверки, не для раздачи игрокам.
option(GDZ_E2E_HARNESS "Сборка для сквозного теста tests/e2e/run_e2e.py" OFF)
if(GDZ_E2E_HARNESS)
    set(E2E_BASE "http://127.0.0.1:8765/")
    set(GDZ_API_URL "${E2E_BASE}api/")
    set(GDZ_SERVER_ADDRESS "")
    set(GDZ_VERSION_MANIFEST_URL "${E2E_BASE}mojang/version_manifest_v2.json")
    set(GDZ_JAVA_RUNTIME_URL "${E2E_BASE}java/all.json")
    set(GDZ_LIBRARIES_URL "${E2E_BASE}libraries/")
    set(GDZ_FORGE_MAVEN_URL "${E2E_BASE}forge/")
    set(GDZ_RESOURCES_URL "${E2E_BASE}resources/")
endif()

# Версия и адреса попадают в код через сгенерированный build/generated/build_config.hpp (см. config.hpp).
function(gdz_configure TARGET)
    target_include_directories(${TARGET} PRIVATE ${CMAKE_BINARY_DIR}/generated)
    if(WIN32)
        target_compile_definitions(${TARGET} PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
    endif()
endfunction()

# Флаги оптимизации только для Release: /O1 несовместим с /RTC1 из Debug-конфигурации MSVC.
# /GL при компиляции и /LTCG при линковке должны идти парой.
if(MSVC)
    set(OPTIMIZE_FLAGS "$<$<CONFIG:Release>:/O1>" "$<$<CONFIG:Release>:/GL>")
    set(LINK_OPTIMIZE_FLAGS "$<$<CONFIG:Release>:/LTCG>")
else()
    set(OPTIMIZE_FLAGS "$<$<CONFIG:Release>:-Os>")
    set(LINK_OPTIMIZE_FLAGS "")
endif()

# Статическая C++ среда выполнения MSVC (ставится на все цели проекта, требует CMake 3.15+).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Зависимости
# ---------------------------------------------------------------------------
# Нам нужна только header-only цель webview::core: без сборки собственных библиотек webview,
# тестов и примеров (иначе по умолчанию собираются webview.dll / статическая библиотека).
set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_STATIC_LIBRARY OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
if(WIN32 AND NOT MSVC)
    # MinGW: заголовки совместимости webview (EventToken.h и др.), которых нет в MinGW-w64.
    set(WEBVIEW_USE_COMPAT_MINGW ON CACHE BOOL "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
	webview
	GIT_REPOSITORY https://github.com/webview/webview
	GIT_TAG 0.12.0
)

# JSON: разбор данных Mojang, Forge и сервера проекта.
FetchContent_Declare(
	json
	URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
	URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)

# miniz: распаковка нативных библиотек из jar-архивов.
FetchContent_Declare(
	miniz
	URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
	URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
)

FetchContent_MakeAvailable(webview json miniz)

add_library(gdz_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(gdz_miniz PUBLIC ${miniz_SOURCE_DIR})
# Только чтение из памяти: без stdio (пути с кириллицей открываем сами) и без zlib-совместимых макросов.
target_compile_definitions(gdz_miniz PUBLIC MINIZ_NO_STDIO MINIZ_NO_ZLIB_COMPATIBLE_NAMES MINIZ_NO_ARCHIVE_WRITING_APIS)
set_target_properties(gdz_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Встроенные файлы: интерфейс и профиль Forge
# ---------------------------------------------------------------------------
include(cmake/embed_file.cmake)
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})
# Числовые части версии для VERSIONINFO (0.001 -> 0,1,0): ведущие нули и пустые части убираем.
foreach(PART MAJOR MINOR PATCH)
    if("${PROJECT_VERSION_${PART}}" STREQUAL "")
        set(GDZ_VERSION_${PART} 0)
    else()
        math(EXPR GDZ_VERSION_${PART} "${PROJECT_VERSION_${PART}} + 0")
    endif()
endforeach()
configure_file(${CMAKE_SOURCE_DIR}/cmake/build_config.hpp.in ${GENERATED_DIR}/build_config.hpp @ONLY)
embed_file(${CMAKE_SOURCE_DIR}/src/ui/index.html ${GENERATED_DIR}/ui_data.cpp embedded index_html)
embed_file(${CMAKE_SOURCE_DIR}/src/profiles/forge-1.7.10.json ${GENERATED_DIR}/profile_data.cpp embedded forge_profile)

# Код самообновления и сети (общий для всех платформ).
set(COMMON_SOURCES
    src/shared/net_common.cpp
    src/shared/sha1.cpp
    src/shared/util.cpp
    src/shared/selfupdate.cpp
)

# Код лаунчера, общий для всех платформ.
set(LAUNCHER_SOURCES
    ${COMMON_SOURCES}
    src/shared/core.cpp
    src/shared/settings.cpp
    src/shared/game.cpp
    src/shared/archive.cpp
    ${GENERATED_DIR}/ui_data.cpp
    ${GENERATED_DIR}/profile_data.cpp
)

if (WIN32)
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    # Основной компилятор для Windows — MSVC (CI). MinGW-w64 поддерживается для кросс-сборки из Linux
    # (cmake/mingw-w64.cmake): статическая линковка, чтобы не таскать с собой libstdc++ и winpthread.
    if(NOT MSVC)
        add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
        add_link_options(-static -static-libgcc -static-libstdc++ "$<$<CONFIG:Release>:-s>")
    endif()

    # Регистрация класса RC нужна для .rc-файла; в MSVC он включается сам, здесь на всякий случай явно.
    enable_language(RC)

    # Один исполняемый файл: окно подготовки (WebView2, самообновление) и интерфейс лаунчера.
    add_executable(GDZLauncher WIN32)
    gdz_configure(GDZLauncher)
    target_compile_options(GDZLauncher PRIVATE ${OPTIMIZE_FLAGS})
    target_link_options(GDZLauncher PRIVATE ${LINK_OPTIMIZE_FLAGS})
    target_include_directories(GDZLauncher PRIVATE src/shared src/windows)
    if(NOT MSVC)
        # MSVC встраивает ui.manifest через компоновщик, для MinGW — ресурсом из resources.rc.
        target_compile_definitions(GDZLauncher PRIVATE GDZ_EMBED_MANIFEST)
    endif()
    target_sources(GDZLauncher PRIVATE
        src/windows/main.cpp
        src/windows/bootstrap.cpp
        src/windows/net_winhttp.cpp
        src/windows/process_win.cpp
        ${LAUNCHER_SOURCES}
        src/windows/resources.rc
        src/windows/ui.manifest
    )
    target_link_libraries(GDZLauncher PRIVATE webview::core nlohmann_json::nlohmann_json gdz_miniz
        comctl32 urlmon advapi32 user32 shell32 shlwapi winhttp)
else()
	find_package(CURL)
	if(NOT CURL_FOUND)
		message(FATAL_ERROR "Не найден libcurl. Установите пакет разработки: sudo apt install libcurl4-openssl-dev "
		                    "(Fedora: libcurl-devel, Arch: curl). Если это GitHub Actions — обновите .github/workflows/build.yml.")
	endif()

	add_executable(launcher)
	gdz_configure(launcher)
	target_compile_options(launcher PRIVATE ${OPTIMIZE_FLAGS})
	target_link_options(launcher PRIVATE ${LINK_OPTIMIZE_FLAGS})
	target_include_directories(launcher PRIVATE src/shared)
	target_sources(launcher PRIVATE
		src/linux/main.cpp
		src/linux/net_curl.cpp
		src/linux/process_posix.cpp
		${LAUNCHER_SOURCES}
	)
	target_link_libraries(launcher PRIVATE webview::core nlohmann_json::nlohmann_json gdz_miniz CURL::libcurl Threads::Threads)

	if(GDZ_E2E_HARNESS)
		embed_file(${CMAKE_SOURCE_DIR}/tests/e2e/harness.js ${GENERATED_DIR}/e2e_harness_data.cpp embedded e2e_harness_js)
		target_sources(launcher PRIVATE tests/e2e/harness.cpp ${GENERATED_DIR}/e2e_harness_data.cpp)
		target_compile_definitions(launcher PRIVATE GDZ_E2E_HARNESS)
	endif()
endif()
