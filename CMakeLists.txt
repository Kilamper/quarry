cmake_minimum_required(VERSION 3.24)

project(dbmanager VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/QtLocate.cmake)
find_package(Qt6 ${DBM_QT_VERSION} EXACT REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES ${DBM_QT_VERSION})

enable_testing()

# The core/providers/app skeleton arrives in milestone 0; for now only the env check.
add_subdirectory(tools/env-check)
