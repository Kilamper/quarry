# Locates the pinned Qt install so the version lives only in .qt-version.
# Included BEFORE project(): the vcpkg toolchain installs the manifest during project(),
# and the Qt shim ports/triplets need QT_ROOT_DIR in the environment (ARCHITECTURE.md section 9).
#
# Resolution order:
#   1. QT_ROOT_DIR environment variable (set by CI / install-qt-action), or
#   2. ${QT_BASE_DIR}/<version>/${QT_ARCH_DIR} (set by CMakePresets.json).
# find_package(Qt6 ... EXACT) in the top-level CMakeLists guarantees the right version.

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../.qt-version" QUARRY_QT_VERSION LIMIT_COUNT 1)
string(STRIP "${QUARRY_QT_VERSION}" QUARRY_QT_VERSION)
if(NOT QUARRY_QT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR ".qt-version must contain a version like 6.12.0, got '${QUARRY_QT_VERSION}'")
endif()

if(DEFINED ENV{QT_ROOT_DIR})
    set(_quarry_qt_root "$ENV{QT_ROOT_DIR}")
elseif(QT_BASE_DIR AND QT_ARCH_DIR)
    set(_quarry_qt_root "${QT_BASE_DIR}/${QUARRY_QT_VERSION}/${QT_ARCH_DIR}")
endif()

if(_quarry_qt_root)
    file(TO_CMAKE_PATH "${_quarry_qt_root}" _quarry_qt_root)
    if(NOT EXISTS "${_quarry_qt_root}/lib/cmake/Qt6")
        message(FATAL_ERROR
            "Qt ${QUARRY_QT_VERSION} not found at '${_quarry_qt_root}'. "
            "Run scripts/setup.ps1 (Windows) or scripts/setup.sh (macOS/Linux).")
    endif()
    set(ENV{QT_ROOT_DIR} "${_quarry_qt_root}")
    list(PREPEND CMAKE_PREFIX_PATH "${_quarry_qt_root}")
    message(STATUS "Using Qt ${QUARRY_QT_VERSION} from ${_quarry_qt_root}")
endif()
