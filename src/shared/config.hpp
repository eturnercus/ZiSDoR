#pragma once

// Конфигурация, задаваемая при сборке (см. опции GDZ_* в CMakeLists.txt).
// CMake записывает заданные значения в build_config.hpp; для остальных ниже действуют значения по умолчанию
// (официальные серверы Mojang и Forge).
#include "build_config.hpp"

#ifndef GDZ_VERSION
#define GDZ_VERSION "0.0.0"
#endif

// Базовый адрес API проекта: launcher.json (обновления, новости) и client.json (сборка).
// Пустая строка: лаунчер работает без сервера проекта (чистый Forge, без модов и обновлений).
#ifndef GDZ_API_URL
#define GDZ_API_URL ""
#endif

// Адрес сервера, к которому игра подключится сразу после запуска ("host" или "host:port").
// Если задан при сборке, имеет приоритет над полем "server" в client.json (лок сервера).
#ifndef GDZ_SERVER_ADDRESS
#define GDZ_SERVER_ADDRESS ""
#endif

#ifndef GDZ_VERSION_MANIFEST_URL
#define GDZ_VERSION_MANIFEST_URL "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
#endif

#ifndef GDZ_JAVA_RUNTIME_URL
#define GDZ_JAVA_RUNTIME_URL "https://launchermeta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json"
#endif

#ifndef GDZ_LIBRARIES_URL
#define GDZ_LIBRARIES_URL "https://libraries.minecraft.net/"
#endif

#ifndef GDZ_FORGE_MAVEN_URL
#define GDZ_FORGE_MAVEN_URL "https://maven.minecraftforge.net/"
#endif

#ifndef GDZ_RESOURCES_URL
#define GDZ_RESOURCES_URL "https://resources.download.minecraft.net/"
#endif

// Имя каталога данных: %APPDATA%\<имя> на Windows, $XDG_DATA_HOME/<имя> на Linux.
#ifndef GDZ_DATA_DIR_NAME
#define GDZ_DATA_DIR_NAME "GDZLauncher"
#endif
