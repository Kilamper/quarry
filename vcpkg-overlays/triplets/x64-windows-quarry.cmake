# Overlay triplet: same as the built-in x64-windows, plus the Qt shim wiring (docs/ARCHITECTURE.md section 9).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# Ports that link Qt build against the aqt install instead of a vcpkg-built Qt.
set(VCPKG_ENV_PASSTHROUGH QT_ROOT_DIR)
# SQLite is compiled into its plugin and into core (ARCHITECTURE.md section 8): static, like on Linux/macOS.
if(PORT STREQUAL "sqlite3")
    set(VCPKG_LIBRARY_LINKAGE static)
endif()

if(PORT MATCHES "^(qtkeychain-qt6|syntax-highlighting)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)  # Qt is shared (LGPL); keeps Qt resources/plugins sane
    set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_PREFIX_PATH=$ENV{QT_ROOT_DIR}")
    # Build-time tools (katehighlightingindexer) link Qt: without its DLLs on PATH they die
    # with 0xC0000135 (STATUS_DLL_NOT_FOUND). The triplet runs in the port's build process.
    set(ENV{PATH} "$ENV{QT_ROOT_DIR}/bin;$ENV{PATH}")
endif()
