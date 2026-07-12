include(${PROJECT_SOURCE_DIR}/cmake/VsDBWarnings.cmake)

set(VSDB_SOURCES
    app/Application.cpp
    app/WorkspaceController.cpp
    core/MockWorkspaceData.cpp
    models/InspectorModel.cpp
    models/ResultTableModel.cpp
    models/SchemaTreeModel.cpp
    ui/CommandRegistry.cpp
    ui/ConnectionExplorer.cpp
    ui/InspectorPane.cpp
    ui/MainWindow.cpp
    ui/QueryEditorTab.cpp
    ui/QueryWorkspace.cpp
    ui/ResultsPane.cpp
)

set(VSDB_HEADERS
    app/Application.h
    app/WorkspaceController.h
    core/MockWorkspaceData.h
    core/WorkspaceData.h
    models/InspectorModel.h
    models/ResultTableModel.h
    models/SchemaTreeModel.h
    ui/CommandRegistry.h
    ui/ConnectionExplorer.h
    ui/InspectorPane.h
    ui/MainWindow.h
    ui/QueryEditorTab.h
    ui/QueryWorkspace.h
    ui/ResultsPane.h
)

qt_add_library(vsdb_lib STATIC ${VSDB_SOURCES} ${VSDB_HEADERS})
target_include_directories(vsdb_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vsdb_lib PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_definitions(vsdb_lib PRIVATE VSDB_VERSION="${PROJECT_VERSION}")
vsdb_enable_warnings(vsdb_lib)

qt_add_resources(vsdb_lib "vsdb_resources"
    PREFIX "/vsdb"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/styles/vsdb.qss"
        "${PROJECT_SOURCE_DIR}/resources/icons/add.svg"
        "${PROJECT_SOURCE_DIR}/resources/icons/clear.svg"
        "${PROJECT_SOURCE_DIR}/resources/icons/database.svg"
        "${PROJECT_SOURCE_DIR}/resources/icons/refresh.svg"
        "${PROJECT_SOURCE_DIR}/resources/icons/run.svg"
        "${PROJECT_SOURCE_DIR}/resources/icons/stop.svg"
)

qt_add_executable(VsDB main.cpp)
target_link_libraries(VsDB PRIVATE vsdb_lib)
vsdb_enable_warnings(VsDB)

install(TARGETS VsDB RUNTIME DESTINATION bin BUNDLE DESTINATION .)
