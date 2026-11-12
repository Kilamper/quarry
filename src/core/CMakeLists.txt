# Provider plugin API: contract between core and providers/* (ARCHITECTURE.md section 4).
# STATIC because providers need ColumnBuffer's implementation; PIC because it is linked into
# MODULE plugins on Linux.
add_library(quarry_provider_api STATIC
    api/include/quarry/cancellation.h
    api/include/quarry/column_buffer.h
    api/include/quarry/object_path.h
    api/include/quarry/provider_api.h
    api/include/quarry/result.h
    api/include/quarry/result_batch.h
    api/include/quarry/value_type.h
    api/src/column_buffer.cpp
)
target_include_directories(quarry_provider_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/api/include)
target_link_libraries(quarry_provider_api PUBLIC Qt6::Core)
set_target_properties(quarry_provider_api PROPERTIES POSITION_INDEPENDENT_CODE ON)
quarry_set_warnings(quarry_provider_api)
quarry_assert_no_ui_deps(quarry_provider_api)

# Core: no UI dependencies, enforced below.
qt_add_library(quarry_core STATIC
    include/quarry/provider_registry.h
    include/quarry/session.h
    src/provider_registry.cpp
    src/session.cpp
    src/session_worker.h
    src/session_worker.cpp
)
target_include_directories(quarry_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(quarry_core PUBLIC quarry_provider_api Qt6::Core)
quarry_set_warnings(quarry_core)
quarry_assert_no_ui_deps(quarry_core)

# Milestone 1 dependencies: resolved now so the vcpkg/Qt-shim wiring is proven from H0 on.
find_package(Qt6Keychain CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(PostgreSQL REQUIRED)
