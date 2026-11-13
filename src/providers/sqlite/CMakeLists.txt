# SQLite is compiled into its plugin (docs/ARCHITECTURE.md section 8).
find_package(unofficial-sqlite3 CONFIG REQUIRED)

quarry_add_provider(sqlite
    SOURCES
        sqlite.json
        sqlite_plugin.cpp
        sqlite_provider.h
        sqlite_provider.cpp
        sqlite_connection.h
        sqlite_connection.cpp
        sqlite_introspection.cpp
        sqlite_stream.h
        sqlite_stream.cpp
        sqlite_dialect.h
        sqlite_dialect.cpp
        sqlite_db_state.h
        sqlite_errors.h
        sqlite_errors.cpp
        sqlite_text.h
    LIBS unofficial::sqlite3::sqlite3)
