quarry_add_test(tst_sqlite_provider
    SOURCES tst_sqlite_provider.cpp
    LIBS quarry_core quarry_provider_conformance)
target_compile_definitions(tst_sqlite_provider PRIVATE
    QUARRY_PLUGIN_DIR="${QUARRY_PLUGIN_OUTPUT_DIR}")
add_dependencies(tst_sqlite_provider quarry_provider_sqlite)
