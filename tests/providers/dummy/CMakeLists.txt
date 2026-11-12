# Test-only plugins for ProviderRegistry (built into tests/plugins, never seen by the app).
quarry_add_provider(dummy TEST_ONLY SOURCES dummy_plugin.cpp dummy.json)
# Declares a future API major version: the registry must reject it without loading it.
quarry_add_provider(dummy_future TEST_ONLY SOURCES dummy_future_plugin.cpp dummy_future.json)
