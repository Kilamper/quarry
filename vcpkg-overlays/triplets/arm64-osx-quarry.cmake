# Overlay triplet: same as the built-in arm64-osx, plus the Qt shim wiring (docs/ARCHITECTURE.md section 9).
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 14.4)

# Ports that link Qt build against the aqt install instead of a vcpkg-built Qt.
set(VCPKG_ENV_PASSTHROUGH QT_ROOT_DIR)
if(PORT MATCHES "^(qtkeychain-qt6|syntax-highlighting)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)  # Qt is shared (LGPL); keeps Qt resources/plugins sane
    set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_PREFIX_PATH=$ENV{QT_ROOT_DIR}")
endif()
