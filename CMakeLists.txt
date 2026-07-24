cmake_minimum_required(VERSION 3.21)

project(VsDB VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Svg Sql)
set(CMAKE_AUTOMOC ON)

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
    src/database/ConnectionStore.cpp
    src/database/ConnectionStore.h
    src/database/DatabaseTypes.h
    src/database/postgres/PostgresQueryWorker.cpp
    src/database/postgres/PostgresQueryWorker.h
    src/database/postgres/PostgresSession.cpp
    src/database/postgres/PostgresSession.h
    src/database/redis/RedisConnectionStore.cpp
    src/database/redis/RedisConnectionStore.h
    src/database/redis/RedisProtocol.cpp
    src/database/redis/RedisProtocol.h
    src/database/redis/RedisSession.cpp
    src/database/redis/RedisSession.h
    src/ui/IconProvider.cpp
    src/ui/IconProvider.h
    src/ui/PostgresConnectionDialog.cpp
    src/ui/PostgresConnectionDialog.h
    src/ui/QueryPage.cpp
    src/ui/QueryPage.h
    src/ui/RedisConnectionDialog.cpp
    src/ui/RedisConnectionDialog.h
    src/ui/RedisKeyPage.cpp
    src/ui/RedisKeyPage.h
)

target_include_directories(VsDB PRIVATE src)

configure_file(resources/theme.qss theme.qss COPYONLY)

target_link_libraries(VsDB PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Svg Qt6::Sql)
if(WIN32)
    target_link_libraries(VsDB PRIVATE advapi32)
endif()
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

    add_executable(VsDBPostgresTests
        tests/PostgresSessionTest.cpp
        src/database/DatabaseTypes.h
        src/database/postgres/PostgresQueryWorker.cpp
        src/database/postgres/PostgresQueryWorker.h
        src/database/postgres/PostgresSession.cpp
        src/database/postgres/PostgresSession.h
    )
    target_include_directories(VsDBPostgresTests PRIVATE src)
    target_link_libraries(VsDBPostgresTests PRIVATE Qt6::Core Qt6::Sql)
    add_test(NAME VsDB.PostgresSession COMMAND VsDBPostgresTests)

    add_executable(VsDBConnectionStoreTests
        tests/ConnectionStoreTest.cpp
        src/database/ConnectionStore.cpp
        src/database/ConnectionStore.h
        src/database/DatabaseTypes.h
    )
    target_include_directories(VsDBConnectionStoreTests PRIVATE src)
    target_link_libraries(VsDBConnectionStoreTests PRIVATE Qt6::Core)
    if(WIN32)
        target_link_libraries(VsDBConnectionStoreTests PRIVATE advapi32)
    endif()
    add_test(NAME VsDB.ConnectionStore COMMAND VsDBConnectionStoreTests)

    add_executable(VsDBRedisTests
        tests/RedisProtocolTest.cpp
        src/database/redis/RedisProtocol.cpp
        src/database/redis/RedisProtocol.h
        src/database/redis/RedisSession.cpp
        src/database/redis/RedisSession.h
    )
    target_include_directories(VsDBRedisTests PRIVATE src)
    target_link_libraries(VsDBRedisTests PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME VsDB.Redis COMMAND VsDBRedisTests)

    add_executable(VsDBRedisConnectionStoreTests
        tests/RedisConnectionStoreTest.cpp
        src/database/ConnectionStore.cpp
        src/database/ConnectionStore.h
        src/database/DatabaseTypes.h
        src/database/redis/RedisConnectionStore.cpp
        src/database/redis/RedisConnectionStore.h
        src/database/redis/RedisProtocol.cpp
        src/database/redis/RedisProtocol.h
        src/database/redis/RedisSession.cpp
        src/database/redis/RedisSession.h
    )
    target_include_directories(VsDBRedisConnectionStoreTests PRIVATE src)
    target_link_libraries(VsDBRedisConnectionStoreTests PRIVATE Qt6::Core Qt6::Network)
    if(WIN32)
        target_link_libraries(VsDBRedisConnectionStoreTests PRIVATE advapi32)
    endif()
    add_test(NAME VsDB.RedisConnectionStore COMMAND VsDBRedisConnectionStoreTests)
endif()
