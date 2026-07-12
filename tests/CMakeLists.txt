get_filename_component(VSDB_QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
get_filename_component(VSDB_QT_PLUGIN_DIR "${Qt6_DIR}/../../../plugins" ABSOLUTE)
find_program(VSDB_WINDLOYQT_EXECUTABLE windeployqt HINTS "${VSDB_QT_BIN_DIR}" REQUIRED)

function(vsdb_add_test name source)
    qt_add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE vsdb_lib Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen"
        "QT_QPA_PLATFORM_PLUGIN_PATH=${VSDB_QT_PLUGIN_DIR}/platforms"
        "PATH=${VSDB_QT_BIN_DIR};C:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
    )
endfunction()

vsdb_add_test(vsdb_model_tests ModelTests.cpp)
vsdb_add_test(vsdb_main_window_tests MainWindowTests.cpp)
