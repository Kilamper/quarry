# Test-only provider whose open() blocks until cancelled (connection-timeout tests). It lives in
# its own directory so the registry tests that scan tests/plugins do not see it.
set(QUARRY_SLOW_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/tests/plugins_slow" CACHE INTERNAL "")
quarry_add_provider(slow TEST_ONLY SOURCES slow_plugin.cpp slow.json)
set_target_properties(quarry_provider_slow PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${QUARRY_SLOW_PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${QUARRY_SLOW_PLUGIN_OUTPUT_DIR}")
