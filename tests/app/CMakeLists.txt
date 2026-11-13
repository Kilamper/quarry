quarry_add_test(tst_theme_contrast SOURCES tst_theme_contrast.cpp LIBS quarry_ui)
quarry_add_test(tst_theme_manager SOURCES tst_theme_manager.cpp LIBS quarry_ui)

# Qt Quick Test: loads tst_controls.qml (and src/app/qml/Main.qml) against the static modules.
quarry_add_test(tst_controls SOURCES tst_controls_main.cpp LIBS quarry_ui quarry_uiplugin Qt6::QuickTest)
target_compile_definitions(tst_controls PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(WIN32)
    # Qt's offscreen platform finds no fonts on Windows, which collapses all text metrics to zero.
    set_property(TEST tst_controls APPEND PROPERTY ENVIRONMENT_MODIFICATION "QT_QPA_FONTDIR=set:$ENV{WINDIR}/Fonts")
endif()
