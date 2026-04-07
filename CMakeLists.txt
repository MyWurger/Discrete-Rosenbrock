#
# ============================================================================
# ФАЙЛ CMAKELists.txt - ГЛАВНЫЙ СЦЕНАРИЙ СБОРКИ ПРОЕКТА LAB_2
# ============================================================================
# Назначение файла:
# 1) Задать общие параметры сборки проекта Lab_2.
# 2) Подключить локальную библиотеку muparser.
# 3) Найти обязательные и дополнительные Qt-модули для GUI и визуализации.
# 4) Собрать общие библиотеки проекта и итоговый исполняемый файл Lab_2.
# ============================================================================

# Требуем минимальную версию CMake, достаточную для используемых команд и Qt6.
cmake_minimum_required(VERSION 3.15)

# Объявляем проект:
# - имя проекта: Lab_2;
# - версия: 0.1.0;
# - язык сборки: только C++.
project(Lab_2 VERSION 0.1.0 LANGUAGES CXX)

# ============================================================================
# ОБЩИЕ НАСТРОЙКИ C++
# ============================================================================

# Используем стандарт C++17 во всех целях проекта.
set(CMAKE_CXX_STANDARD 17)
# Требуем именно этот стандарт, без автоматического отката на более старый.
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Запрещаем compiler-specific расширения вроде gnu++17; оставляем чистый c++17.
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# ПОДКЛЮЧЕНИЕ ЛОКАЛЬНОЙ КОПИИ MUPARSER
# ============================================================================

# Проект Lab_2 опирается только на локальную копию muparser внутри собственного
# каталога libs. Это исключает зависимость от соседних лабораторных работ.
if (EXISTS "${CMAKE_SOURCE_DIR}/libs/muparser/CMakeLists.txt")
    # Запоминаем путь до исходников muparser, если локальная копия найдена.
    set(MUPARSER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/libs/muparser")
else ()
    # Прерываем конфигурацию, если локальной копии библиотеки нет.
    message(FATAL_ERROR "muparser not found. Expected: ${CMAKE_SOURCE_DIR}/libs/muparser")
endif ()

# Отключаем у muparser все лишнее для нашей лабораторной:
# - примеры;
# - OpenMP;
# - тесты.
# CACHE BOOL ... FORCE нужен, чтобы переопределить эти опции даже если они уже
# были сохранены в CMakeCache.txt на предыдущем запуске.
set(ENABLE_SAMPLES OFF CACHE BOOL "Disable muparser samples" FORCE)
set(ENABLE_OPENMP OFF CACHE BOOL "Disable muparser OpenMP" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable muparser tests" FORCE)

# Подключаем muparser как подпроект.
# Второй путь указывает отдельный build-каталог для muparser.
# EXCLUDE_FROM_ALL означает, что цели muparser не будут считаться "главными"
# целями верхнего проекта без явной зависимости.
add_subdirectory("${MUPARSER_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/muparser-build" EXCLUDE_FROM_ALL)

# ============================================================================
# ПОИСК QT И ДОПОЛНИТЕЛЬНЫХ МОДУЛЕЙ ВИЗУАЛИЗАЦИИ
# ============================================================================

# Дополнительные зависимости для визуализации ищем стандартными средствами CMake.
# При необходимости пользователь может явно передать:
# - CMAKE_PREFIX_PATH
# - Qt6_DIR
# - Qt6Charts_DIR
# - Qt6DataVisualization_DIR

# На macOS дополнительно пробуем автоматически определить пути до Qt-модулей,
# если проект установлен через Homebrew и пользователь не задал их вручную.
if (APPLE)
    # Ищем саму утилиту brew в PATH.
    find_program(BREW_EXECUTABLE brew)
    if (BREW_EXECUTABLE)
        # Если Qt6_DIR не задан, пробуем получить корень qtbase через brew.
        if (NOT Qt6_DIR)
            execute_process(
                # Выполняем `brew --prefix qtbase`, чтобы получить путь установки qtbase.
                COMMAND "${BREW_EXECUTABLE}" --prefix qtbase
                # Сохраняем stdout в переменную CMake.
                OUTPUT_VARIABLE QTBASE_PREFIX
                # Убираем завершающий перевод строки из вывода команды.
                OUTPUT_STRIP_TRAILING_WHITESPACE
                # Ошибки не печатаем, чтобы не засорять конфигурацию.
                ERROR_QUIET
            )
            if (QTBASE_PREFIX)
                # Указываем CMake каталог с Qt6Config.cmake.
                set(Qt6_DIR "${QTBASE_PREFIX}/lib/cmake/Qt6")
            endif ()
        endif ()

        # Аналогично ищем Qt Charts, если путь к модулю не задан пользователем.
        if (NOT Qt6Charts_DIR)
            execute_process(
                COMMAND "${BREW_EXECUTABLE}" --prefix qtcharts
                OUTPUT_VARIABLE QTCHARTS_PREFIX
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if (QTCHARTS_PREFIX)
                set(Qt6Charts_DIR "${QTCHARTS_PREFIX}/lib/cmake/Qt6Charts")
            endif ()
        endif ()

        # Аналогично ищем Qt Data Visualization, если путь к модулю не задан явно.
        if (NOT Qt6DataVisualization_DIR)
            execute_process(
                COMMAND "${BREW_EXECUTABLE}" --prefix qtdatavis3d
                OUTPUT_VARIABLE QTDATAVIS3D_PREFIX
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if (QTDATAVIS3D_PREFIX)
                set(Qt6DataVisualization_DIR "${QTDATAVIS3D_PREFIX}/lib/cmake/Qt6DataVisualization")
            endif ()
        endif ()
    endif ()
endif ()

# Widgets - обязательный модуль, без него GUI не соберется.
find_package(Qt6 COMPONENTS Widgets REQUIRED)
# Charts - дополнительный модуль; ищем тихо, чтобы проект собирался и без него.
find_package(Qt6 COMPONENTS Charts QUIET)
# DataVisualization - тоже дополнительный модуль; также ищем без фатальной ошибки.
find_package(Qt6 COMPONENTS DataVisualization QUIET)

# ============================================================================
# ПОДСКАЗКА ДЛЯ ПОИСКА QT PLATFORM PLUGINS В RUNTIME
# ============================================================================

# Переменная нужна приложению во время запуска, чтобы при необходимости найти
# каталог platforms и plugin cocoa на macOS.
set(LAB2_QT_PLATFORMS_HINT "")

# Если Qt6_DIR известен, восстанавливаем из него корневой каталог установки Qt.
if (Qt6_DIR)
    # ABSOLUTE гарантирует, что дальше работаем с полным путем, а не относительным.
    get_filename_component(_qt6_dir_abs "${Qt6_DIR}" ABSOLUTE)
    # Из .../lib/cmake/Qt6 поднимаемся к .../lib/cmake.
    get_filename_component(_qt6_cmake_dir "${_qt6_dir_abs}" DIRECTORY)
    # Поднимаемся еще на уровень к .../lib.
    get_filename_component(_qt6_lib_dir "${_qt6_cmake_dir}" DIRECTORY)
    # Поднимаемся еще на уровень к префиксу установки Qt.
    get_filename_component(_qt6_prefix_dir "${_qt6_lib_dir}" DIRECTORY)
    # Формируем ожидаемый путь до папки platforms внутри установки Qt.
    set(LAB2_QT_PLATFORMS_HINT "${_qt6_prefix_dir}/share/qt/plugins/platforms")
endif ()

# ============================================================================
# ОБЩАЯ БИБЛИОТЕКА С КОНСТАНТАМИ
# ============================================================================

# Выделяем общие константы в отдельную небольшую библиотеку, чтобы переиспользовать
# их из парсера, ядра и GUI.
add_library(
    lab2_common
    src/Constants.cpp
)

# PUBLIC означает, что include нужен как самой библиотеке, так и тем целям,
# которые будут с ней линковаться.
target_include_directories(lab2_common PUBLIC include)

# ============================================================================
# БИБЛИОТЕКА ПАРСЕРА ФУНКЦИЙ
# ============================================================================

# Отдельная библиотека для парсинга пользовательского выражения через muparser.
add_library(
    lab2_parser
    src/MuParserObjectiveParser.cpp
)

target_include_directories(lab2_parser PUBLIC include)
# Парсер зависит:
# - от общих констант;
# - от muparser как внешней библиотеки.
target_link_libraries(lab2_parser PUBLIC lab2_common muparser::muparser)

# ============================================================================
# ОСНОВНАЯ БИБЛИОТЕКА LAB_2
# ============================================================================

# В основную библиотеку выносим:
# - алгоритм метода;
# - предопределенные функции;
# - главное окно GUI;
# - 2D и 3D визуализаторы.
add_library(
    lab2_core
    src/RosenbrockDiscreteMethod.cpp
    src/PredefinedFunctions.cpp
    src/MainWindow.cpp
    src/Matplot2DViewer.cpp
    src/Matplot3DViewer.cpp
)

target_include_directories(lab2_core PUBLIC include)

# lab2_core использует:
# - общие константы;
# - парсер;
# - обязательный Qt Widgets.
target_link_libraries(lab2_core PUBLIC lab2_common lab2_parser Qt6::Widgets)

# Передаем в код compile definitions:
# - подсказку к пути plugin-ов Qt;
# - корневой путь проекта, например для экспорта PNG в папку images.
target_compile_definitions(
    lab2_core
    PUBLIC
        LAB2_QT_PLATFORMS_HINT="${LAB2_QT_PLATFORMS_HINT}"
        LAB2_PROJECT_ROOT_PATH="${CMAKE_SOURCE_DIR}"
)

# ============================================================================
# УСЛОВНОЕ ПОДКЛЮЧЕНИЕ QT CHARTS
# ============================================================================

# Если модуль Charts найден, подключаем его и говорим коду, что он доступен.
if (Qt6Widgets_FOUND AND Qt6Charts_FOUND)
    target_link_libraries(lab2_core PUBLIC Qt6::Widgets Qt6::Charts)
    target_compile_definitions(lab2_core PUBLIC LAB2_HAS_QTCHARTS=1)
else ()
    # Иначе явно выставляем 0, чтобы код мог собрать fallback-ветки через #if.
    target_compile_definitions(lab2_core PUBLIC LAB2_HAS_QTCHARTS=0)
endif ()

# ============================================================================
# УСЛОВНОЕ ПОДКЛЮЧЕНИЕ QT DATA VISUALIZATION
# ============================================================================

# Если модуль DataVisualization найден, подключаем его и включаем 3D-визуализацию.
if (Qt6Widgets_FOUND AND Qt6DataVisualization_FOUND)
    target_link_libraries(lab2_core PUBLIC Qt6::Widgets Qt6::DataVisualization)
    target_compile_definitions(lab2_core PUBLIC LAB2_HAS_QTDATAVIS3D=1)
else ()
    # Иначе код собирается без этой функциональности, но с явным флагом 0.
    target_compile_definitions(lab2_core PUBLIC LAB2_HAS_QTDATAVIS3D=0)
endif ()

# ============================================================================
# ИТОГОВЫЙ ИСПОЛНЯЕМЫЙ ФАЙЛ
# ============================================================================

# Создаем конечное Qt-приложение.
add_executable(Lab_2 main.cpp)

# Линкуем приложение с основной библиотекой проекта.
target_link_libraries(Lab_2 PRIVATE lab2_core)
