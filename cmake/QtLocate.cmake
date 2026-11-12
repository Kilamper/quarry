# Locates the pinned Qt install so the version lives only in .qt-version.
#
# Resolution order:
#   1. QT_ROOT_DIR environment variable (set by CI / install-qt-action), or
#   2. ${QT_BASE_DIR}/<version>/${QT_ARCH_DIR} (set by CMakePresets.json).
# The resolved root is prepended to CMAKE_PREFIX_PATH; find_package(Qt6 ... EXACT)
# in the top-level CMakeLists then guarantees the right version was picked.

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../.qt-version" DBM_QT_VERSION LIMIT_COUNT 1)
string(STRIP "${DBM_QT_VERSION}" DBM_QT_VERSION)
if(NOT DBM_QT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR ".qt-version must contain a version like 6.12.0, got '${DBM_QT_VERSION}'")
endif()

if(DEFINED ENV{QT_ROOT_DIR})
    set(_dbm_qt_root "$ENV{QT_ROOT_DIR}")
elseif(QT_BASE_DIR AND QT_ARCH_DIR)
    set(_dbm_qt_root "${QT_BASE_DIR}/${DBM_QT_VERSION}/${QT_ARCH_DIR}")
endif()

if(_dbm_qt_root)
    if(NOT EXISTS "${_dbm_qt_root}/lib/cmake/Qt6")
        message(FATAL_ERROR
            "Qt ${DBM_QT_VERSION} not found at '${_dbm_qt_root}'. "
            "Run scripts/setup.ps1 (Windows) or scripts/setup.sh (macOS/Linux).")
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${_dbm_qt_root}")
    message(STATUS "Using Qt ${DBM_QT_VERSION} from ${_dbm_qt_root}")
endif()
