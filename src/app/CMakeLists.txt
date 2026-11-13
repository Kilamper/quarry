# quarry_ui: static library with the C++ UI types (ThemeManager = QML singleton `Theme`) and the
# QML module Quarry.Controls (accessible controls, Icons singleton, icon font). Tests link it.
# Linking the quarry_uiplugin target registers the module in a static link.
# Warning flags on our own sources only: MSVC flags C4702 inside Qt headers as instantiated by the
# generated qmlcache code, which must not fail the build.
function(quarry_app_warnings)
    if(MSVC)
        set(flags /W4 /permissive- /utf-8 /Zc:__cplusplus $<$<BOOL:${QUARRY_WARNINGS_AS_ERRORS}>:/WX>)
    else()
        set(flags -Wall -Wextra -Wpedantic $<$<BOOL:${QUARRY_WARNINGS_AS_ERRORS}>:-Werror>)
    endif()
    set_property(SOURCE ${ARGN} APPEND PROPERTY COMPILE_OPTIONS ${flags})
endfunction()

set(QUARRY_CONTROLS_QML
    FocusRing Heading Icon IconButton QButton QCheckBox QComboBox QDialog QMenu QMenuItem
    QSplitView QTabBar QTabButton QTextField QToolTip)

# Flat resource layout: /qt/qml/Quarry/Controls/<Name>.qml and .../lucide-subset.ttf
set(controls_qml_files)
foreach(name IN LISTS QUARRY_CONTROLS_QML)
    set(file qml/Quarry/Controls/${name}.qml)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name}.qml)
    list(APPEND controls_qml_files ${file})
endforeach()
set_source_files_properties(qml/Quarry/Controls/Icons.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
    QT_RESOURCE_ALIAS Icons.qml)
set_source_files_properties(assets/icons/lucide-subset.ttf PROPERTIES
    QT_RESOURCE_ALIAS lucide-subset.ttf)

qt_add_library(quarry_ui STATIC)
qt_add_qml_module(quarry_ui
    URI Quarry.Controls
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Quarry/Controls
    SOURCES
        ui/command.cpp
        ui/command.h
        ui/command_registry.cpp
        ui/command_registry.h
        ui/connection_controller.cpp
        ui/connection_controller.h
        ui/connection_form_model.cpp
        ui/connection_form_model.h
        ui/connection_list_model.cpp
        ui/connection_list_model.h
        ui/language_manager.cpp
        ui/language_manager.h
        ui/layout_store.cpp
        ui/layout_store.h
        ui/theme_manager.cpp
        ui/theme_manager.h
        ui/theme_tokens.cpp
        ui/theme_tokens.h
    QML_FILES
        ${controls_qml_files}
        qml/Quarry/Controls/Icons.qml
    RESOURCES
        assets/icons/lucide-subset.ttf
    DEPENDENCIES QtQuick QtQuick.Templates
)
# ui/ too: the generated QML registration code includes the headers by bare file name.
target_include_directories(quarry_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(quarry_ui PUBLIC quarry_core Qt6::Gui Qt6::Qml Qt6::Quick)
quarry_app_warnings(
    ui/command.cpp ui/command_registry.cpp ui/connection_controller.cpp ui/connection_form_model.cpp
    ui/connection_list_model.cpp ui/language_manager.cpp ui/layout_store.cpp
    ui/theme_manager.cpp ui/theme_tokens.cpp)

qt_add_executable(quarry main.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # DT_RPATH (not RUNPATH) also applies to transitive deps: vcpkg's libqt6keychain.so needs
    # libQt6DBus from the aqt Qt dir, which its own RUNPATH does not list.
    target_link_options(quarry PRIVATE -Wl,--disable-new-dtags)
endif()

qt_add_qml_module(quarry
    URI Quarry
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/MainWindow.qml
        qml/CommandPalette.qml
        qml/SettingsDialog.qml
        qml/EmptyState.qml
        qml/Region.qml
        qml/ConnectionList.qml
        qml/ConnectionDialog.qml
        qml/ColorPicker.qml
        qml/DeleteConnectionDialog.qml
        qml/ProductionBadge.qml
        qml/FilePicker.qml
        qml/ErrorPanel.qml
    DEPENDENCIES QtQuick QtQuick.Controls.Basic QtQuick.Layouts QtQml Quarry.Controls
)

target_link_libraries(quarry PRIVATE quarry_core quarry_ui quarry_uiplugin Qt6::Quick Qt6::QuickControls2)
quarry_app_warnings(main.cpp)
set_target_properties(quarry PROPERTIES WIN32_EXECUTABLE ON)

# Spanish UI strings. The .qm files are generated into the build tree and embedded as
# :/i18n/quarry_es.qm (loaded by LanguageManager); only the .ts is versioned.
# After changing any visible string: cmake --build <dir> --target update_translations
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
qt_add_translations(quarry_ui
    SOURCE_TARGETS quarry quarry_ui
    TS_FILES i18n/quarry_es.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -locations none
    NO_GENERATE_PLURALS_TS_FILE)

# Used by the SQL editor in milestone 1.5; resolved now to prove the Qt shim wiring.
find_package(KF6SyntaxHighlighting CONFIG REQUIRED)

add_test(NAME quarry-smoke COMMAND quarry --smoke)
set_tests_properties(quarry-smoke PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen"
)
