# Shim port: provides nothing. Dependents find Qt through CMAKE_PREFIX_PATH=$ENV{QT_ROOT_DIR},
# injected by the *-quarry overlay triplets, so vcpkg never builds a second Qt.
if(NOT DEFINED ENV{QT_ROOT_DIR} OR NOT EXISTS "$ENV{QT_ROOT_DIR}/lib/cmake/Qt6")
    message(FATAL_ERROR "QT_ROOT_DIR must point to the aqt Qt install (set by cmake/QtLocate.cmake). Use a *-quarry triplet.")
endif()
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
