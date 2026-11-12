# Provider plugin API: header-only contract between core and providers/* (ARCHITECTURE.md section 4).
add_library(quarry_provider_api INTERFACE)
target_include_directories(quarry_provider_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/api/include)
target_link_libraries(quarry_provider_api INTERFACE Qt6::Core)

# Core: no UI dependencies, enforced below.
qt_add_library(quarry_core STATIC
    api/include/quarry/provider_api.h
    include/quarry/provider_registry.h
    src/provider_registry.cpp
)
target_include_directories(quarry_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(quarry_core PUBLIC quarry_provider_api Qt6::Core)
quarry_set_warnings(quarry_core)
quarry_assert_no_ui_deps(quarry_core)

# Milestone 1 dependencies: resolved now so the vcpkg/Qt-shim wiring is proven from H0 on.
find_package(Qt6Keychain CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(PostgreSQL REQUIRED)
