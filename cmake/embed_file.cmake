# embed_file(<входной файл> <выходной .cpp> <пространство имён> <имя массива>)
#
# Превращает файл в C++ массив байт (плюс завершающий нулевой байт), чтобы интерфейс и профили жили внутри бинарника.
# Массив, а не строковый литерал: у MSVC есть ограничение на длину литерала (~16 КБ).
# Перегенерация происходит при конфигурации; изменение входного файла запускает её автоматически.
function(embed_file INPUT OUTPUT NAMESPACE NAME)
    file(READ "${INPUT}" HEX_CONTENT HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BYTES "${HEX_CONTENT}")

    set(CONTENT "// Файл сгенерирован cmake/embed_file.cmake из ${INPUT}. Не редактировать.\n")
    string(APPEND CONTENT "#include <cstddef>\n\n")
    string(APPEND CONTENT "namespace ${NAMESPACE} {\n")
    # extern-объявления дают массиву внешнее связывание (у const на уровне пространства имён оно внутреннее).
    string(APPEND CONTENT "extern const unsigned char ${NAME}[];\n")
    string(APPEND CONTENT "extern const std::size_t ${NAME}_size;\n")
    string(APPEND CONTENT "const unsigned char ${NAME}[] = {${BYTES}0x00};\n")
    string(APPEND CONTENT "const std::size_t ${NAME}_size = sizeof(${NAME}) - 1;\n")
    string(APPEND CONTENT "}\n")

    # Не трогаем файл, если содержимое не изменилось, чтобы не вызывать лишнюю пересборку.
    set(OLD_CONTENT "")
    if(EXISTS "${OUTPUT}")
        file(READ "${OUTPUT}" OLD_CONTENT)
    endif()
    if(NOT "${OLD_CONTENT}" STREQUAL "${CONTENT}")
        file(WRITE "${OUTPUT}" "${CONTENT}")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${INPUT}")
endfunction()
