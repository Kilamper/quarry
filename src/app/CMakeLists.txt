qt_add_executable(quarry main.cpp)

qt_add_qml_module(quarry
    URI Quarry
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_link_libraries(quarry PRIVATE quarry_core Qt6::Quick)
quarry_set_warnings(quarry)
set_target_properties(quarry PROPERTIES WIN32_EXECUTABLE ON)

# Used by the SQL editor in milestone 1.5; resolved now to prove the Qt shim wiring.
find_package(KF6SyntaxHighlighting CONFIG REQUIRED)

add_test(NAME quarry-smoke COMMAND quarry --smoke)
set_tests_properties(quarry-smoke PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen"
)
