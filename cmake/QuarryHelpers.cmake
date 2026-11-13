# Shared build helpers. See CLAUDE.md for conventions.

option(QUARRY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (ON in CI)" OFF)

# Official provider plugins land here; the app looks in <exe dir>/plugins/quarry.
set(QUARRY_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins/quarry")
# Test-only plugins (never seen by the app).
set(QUARRY_TEST_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/tests/plugins")

function(quarry_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus
            $<$<BOOL:${QUARRY_WARNINGS_AS_ERRORS}>:/WX>)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic
            $<$<BOOL:${QUARRY_WARNINGS_AS_ERRORS}>:-Werror>)
    endif()
endfunction()

# Fails configuration if `target` links a UI module directly (core must stay UI-free).
function(quarry_assert_no_ui_deps target)
    foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(libs ${target} ${prop})
        if(NOT libs)
            continue()
        endif()
        foreach(lib IN LISTS libs)
            if(lib MATCHES "^Qt6?::(Gui|Qml|Quick|QuickControls2|Widgets)$")
                message(FATAL_ERROR "${target} must not link ${lib}: core has no UI dependencies (ARCHITECTURE.md section 2)")
            endif()
        endforeach()
    endforeach()
endfunction()

# quarry_add_provider(<id> SOURCES ... [LIBS ...] [TEST_ONLY])
# Builds a provider plugin MODULE named quarry_provider_<id>.
function(quarry_add_provider id)
    cmake_parse_arguments(ARG "TEST_ONLY" "" "SOURCES;LIBS" ${ARGN})
    set(target quarry_provider_${id})
    add_library(${target} MODULE ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE quarry_provider_api ${ARG_LIBS})
    quarry_set_warnings(${target})
    if(ARG_TEST_ONLY)
        set(out "${QUARRY_TEST_PLUGIN_OUTPUT_DIR}")
    else()
        set(out "${QUARRY_PLUGIN_OUTPUT_DIR}")
    endif()
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${out}"
        RUNTIME_OUTPUT_DIRECTORY "${out}"
        PREFIX "")
endfunction()

# quarry_add_test(<name> SOURCES ... [LIBS ...])
# Qt Test executable registered with CTest; runs offscreen with Qt on PATH.
function(quarry_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    quarry_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 60
        # LD_LIBRARY_PATH: vcpkg's shared Qt-based libs (QtKeychain needs Qt6DBus) carry a RUNPATH
        # without the aqt Qt dir; GUI-less test executables would not find libQt6DBus otherwise.
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen")
endfunction()
