cmake_minimum_required(VERSION 3.21)

project(VsDB VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg)

qt_add_executable(VsDB
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/SqlEditor.cpp
    src/SqlEditor.h
    src/UiComponents.cpp
    src/UiComponents.h
    src/WorkbenchModels.cpp
    src/WorkbenchModels.h
)

configure_file(resources/theme.qss theme.qss COPYONLY)

target_link_libraries(VsDB PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)
add_custom_command(TARGET VsDB POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt6::Svg> $<TARGET_FILE_DIR:VsDB>
)
qt_add_resources(VsDB "vsdb_icons"
    PREFIX "/icons"
    BASE "resources/icons"
    FILES
        resources/icons/database-zap.svg
        resources/icons/cloud-cog.svg
        resources/icons/folder-open.svg
        resources/icons/plug.svg
        resources/icons/refresh-cw.svg
        resources/icons/unplug.svg
        resources/icons/file-code.svg
        resources/icons/file-plus.svg
        resources/icons/save.svg
        resources/icons/undo-2.svg
        resources/icons/history.svg
        resources/icons/database.svg
        resources/icons/table-2.svg
        resources/icons/boxes.svg
        resources/icons/eye.svg
        resources/icons/list-tree.svg
        resources/icons/square-function.svg
        resources/icons/list-ordered.svg
        resources/icons/key-round.svg
        resources/icons/layout-grid.svg
        resources/icons/rows-3.svg
        resources/icons/upload.svg
        resources/icons/copy.svg
        resources/icons/settings.svg
        resources/icons/x.svg
        resources/icons/chevron-right.svg
        resources/icons/chevron-down.svg
        resources/icons/postgresql.svg
        resources/icons/mysql.svg
        resources/icons/redis.svg
        resources/icons/LUCIDE_LICENSE
        resources/icons/SIMPLE_ICONS_LICENSE
)
target_compile_definitions(VsDB PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

if(MSVC)
    target_compile_options(VsDB PRIVATE /W4 /permissive-)
else()
    target_compile_options(VsDB PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(VsDBTests
        tests/WorkbenchModelsTest.cpp
        src/WorkbenchModels.cpp
        src/WorkbenchModels.h
    )
    target_include_directories(VsDBTests PRIVATE src)
    target_link_libraries(VsDBTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME VsDB.WorkbenchModels COMMAND VsDBTests)
    set_tests_properties(VsDB.WorkbenchModels PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
