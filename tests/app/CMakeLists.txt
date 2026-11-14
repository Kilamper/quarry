quarry_add_test(tst_command_registry SOURCES tst_command_registry.cpp LIBS quarry_ui)
quarry_add_test(tst_connection_list_model SOURCES tst_connection_list_model.cpp LIBS quarry_ui)
quarry_add_test(tst_connection_controller SOURCES tst_connection_controller.cpp LIBS quarry_ui)
quarry_add_test(tst_editor_tabs_model SOURCES tst_editor_tabs_model.cpp LIBS quarry_ui)
quarry_add_test(tst_sql_document SOURCES tst_sql_document.cpp LIBS quarry_ui)
quarry_add_test(tst_schema_tree_model SOURCES tst_schema_tree_model.cpp
    LIBS quarry_ui unofficial::sqlite3::sqlite3)
foreach(t tst_connection_list_model tst_connection_controller tst_schema_tree_model tst_editor_tabs_model)
    # The real SQLite plugin, like tests/core/tst_connection_tester.cpp.
    target_compile_definitions(${t} PRIVATE QUARRY_PLUGIN_DIR="${QUARRY_PLUGIN_OUTPUT_DIR}")
    add_dependencies(${t} quarry_provider_sqlite)
endforeach()
quarry_add_test(tst_language_manager SOURCES tst_language_manager.cpp LIBS quarry_ui)
quarry_add_test(tst_theme_contrast SOURCES tst_theme_contrast.cpp LIBS quarry_ui)
quarry_add_test(tst_theme_manager SOURCES tst_theme_manager.cpp LIBS quarry_ui)

# Qt Quick Test: loads tst_controls.qml (and src/app/qml/Main.qml) against the static modules.
quarry_add_test(tst_controls SOURCES tst_controls_main.cpp LIBS quarry_ui quarry_uiplugin Qt6::QuickTest unofficial::sqlite3::sqlite3)
target_compile_definitions(tst_controls PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    QUARRY_PLUGIN_DIR="${QUARRY_PLUGIN_OUTPUT_DIR}")
add_dependencies(tst_controls quarry_provider_sqlite)
# Three QML test files, one of them (tst_connections) really saves and tests connections.
set_tests_properties(tst_controls PROPERTIES TIMEOUT 240)
if(WIN32)
    # Qt's offscreen platform finds no fonts on Windows, which collapses all text metrics to zero.
    set_property(TEST tst_controls APPEND PROPERTY ENVIRONMENT_MODIFICATION "QT_QPA_FONTDIR=set:$ENV{WINDIR}/Fonts")
endif()
