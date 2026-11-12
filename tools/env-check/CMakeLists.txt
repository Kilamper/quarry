qt_add_executable(env-check main.cpp)

qt_add_qml_module(env-check
    URI EnvCheck
    VERSION 1.0
    QML_FILES Main.qml
)

target_link_libraries(env-check PRIVATE Qt6::Quick)

# Smoke test: starts the window offscreen, fails on any QML warning, quits after 500 ms.
add_test(NAME env-check-smoke COMMAND env-check --smoke)
set_tests_properties(env-check-smoke PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen"
)
