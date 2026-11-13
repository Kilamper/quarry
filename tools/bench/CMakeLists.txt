# bench_core: streams 1M rows x 10 columns through the SQLite plugin (docs/PERFORMANCE.md).
# Built with everything else but NOT registered with ctest (it takes seconds and its budget only
# applies to Release). Run it by hand, with Qt's bin directory on PATH on Windows:
#   build/windows-release/bench_core.exe [rows]      -> prints two JSON metric lines
find_package(unofficial-sqlite3 CONFIG REQUIRED)

qt_add_executable(bench_core bench_core.cpp)
# The fixture is generated with the library directly; the timed read goes through the plugin.
target_link_libraries(bench_core PRIVATE quarry_core Qt6::Core unofficial::sqlite3::sqlite3)
target_compile_definitions(bench_core PRIVATE
    QUARRY_PLUGIN_DIR="${QUARRY_PLUGIN_OUTPUT_DIR}")
add_dependencies(bench_core quarry_provider_sqlite)
quarry_set_warnings(bench_core)
