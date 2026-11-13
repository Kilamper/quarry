# Shared provider conformance tests (docs/ARCHITECTURE.md section 4). Every provider test
# derives from quarry::conformance::ProviderConformanceTest and supplies a Fixture.
qt_add_library(quarry_provider_conformance STATIC
    provider_conformance.h
    provider_conformance.cpp)
target_include_directories(quarry_provider_conformance PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quarry_provider_conformance PUBLIC quarry_provider_api Qt6::Test)
quarry_set_warnings(quarry_provider_conformance)
