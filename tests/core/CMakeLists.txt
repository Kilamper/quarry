quarry_add_test(tst_provider_registry SOURCES tst_provider_registry.cpp LIBS quarry_core)
target_compile_definitions(tst_provider_registry PRIVATE
    QUARRY_TEST_PLUGIN_DIR="${QUARRY_TEST_PLUGIN_OUTPUT_DIR}")
add_dependencies(tst_provider_registry quarry_provider_dummy quarry_provider_dummy_future)

quarry_add_test(tst_result SOURCES tst_result.cpp LIBS quarry_provider_api)
quarry_add_test(tst_column_buffer SOURCES tst_column_buffer.cpp LIBS quarry_provider_api)
quarry_add_test(tst_session SOURCES tst_session.cpp LIBS quarry_core)
quarry_add_test(tst_connection_store SOURCES tst_connection_store.cpp LIBS quarry_core unofficial::sqlite3::sqlite3)
quarry_add_test(tst_credential_store SOURCES tst_credential_store.cpp LIBS quarry_core)
quarry_add_test(tst_connection_tester SOURCES tst_connection_tester.cpp LIBS quarry_core)
target_compile_definitions(tst_connection_tester PRIVATE
    QUARRY_PLUGIN_DIR="${QUARRY_PLUGIN_OUTPUT_DIR}"
    QUARRY_SLOW_PLUGIN_DIR="${QUARRY_SLOW_PLUGIN_OUTPUT_DIR}")
add_dependencies(tst_connection_tester quarry_provider_sqlite quarry_provider_slow)
quarry_add_test(tst_schema_children SOURCES tst_schema_children.cpp LIBS quarry_core unofficial::sqlite3::sqlite3)
target_compile_definitions(tst_schema_children PRIVATE QUARRY_PLUGIN_DIR="${QUARRY_PLUGIN_OUTPUT_DIR}")
add_dependencies(tst_schema_children quarry_provider_sqlite)
