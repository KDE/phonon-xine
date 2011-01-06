project(PhononXine)

cmake_minimum_required(VERSION 2.6.2 FATAL_ERROR)

# CMP0002: we have multiple targets with the same name for the unit tests
cmake_policy(SET CMP0002 OLD)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Phonon REQUIRED)
find_package(Xine REQUIRED)
macro_optional_find_package(XCB)

add_definitions(${QT_DEFINITIONS})
include_directories(${PHONON_INCLUDES} ${QT_INCLUDES})

set(PHONON_XINE_MAJOR_VERSION "4")
set(PHONON_XINE_MINOR_VERSION "4")
set(PHONON_XINE_PATCH_VERSION "4")
set(PHONON_XINE_VERSION "${PHONON_XINE_MAJOR_VERSION}.${PHONON_XINE_MINOR_VERSION}.${PHONON_XINE_PATCH_VERSION}")
add_definitions(-DPHONON_XINE_VERSION="${PHONON_XINE_VERSION}")

add_subdirectory(xine)

macro_display_feature_log()
