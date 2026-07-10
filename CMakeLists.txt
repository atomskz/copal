# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)

project(copal
    VERSION 0.1.0
    DESCRIPTION "Lightweight cross-platform GUI library in C"
    HOMEPAGE_URL "https://example.invalid/copal"
    LANGUAGES C)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(COPAL_BUILD_SHARED      "Build copal as a shared library"    OFF)
option(COPAL_BUILD_EXAMPLES    "Build examples"                     ON)
option(COPAL_BUILD_TESTS       "Build tests"                        ON)
option(COPAL_ENABLE_SDL        "Enable SDL2 platform backend"       OFF) # Stage 6
option(COPAL_ENABLE_OPENGL     "Enable OpenGL renderer backend"     OFF) # Stage 6
option(COPAL_ENABLE_SANITIZERS "Enable ASan/UBSan"                  OFF)
option(COPAL_ENABLE_INSTALL    "Enable install rules"               ON)

# Detect whether we are the top-level project (for add_subdirectory use).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(COPAL_IS_TOP_LEVEL ON)
else()
    set(COPAL_IS_TOP_LEVEL OFF)
endif()

include(GNUInstallDirs)
include(cmake/CompilerWarnings.cmake)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
if(COPAL_BUILD_SHARED)
    set(COPAL_LIB_TYPE SHARED)
else()
    set(COPAL_LIB_TYPE STATIC)
endif()

# MVP foundation sources. GUI modules (widget/layout/render/text/platform)
# are added with the vertical prototype in Stage 6.
set(COPAL_SOURCES
    src/core/foundation/version.c
    src/core/foundation/error.c
    src/core/foundation/allocator.c)

add_library(copal ${COPAL_LIB_TYPE} ${COPAL_SOURCES})
add_library(copal::copal ALIAS copal)

target_compile_features(copal PUBLIC c_std_11)

target_include_directories(copal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(copal PRIVATE COPAL_BUILDING)
if(COPAL_BUILD_SHARED)
    target_compile_definitions(copal PUBLIC COPAL_SHARED)
    set_target_properties(copal PROPERTIES
        C_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
endif()

set_target_properties(copal PROPERTIES
    OUTPUT_NAME copal
    EXPORT_NAME copal
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

copal_set_warnings(copal)
if(COPAL_ENABLE_SANITIZERS)
    copal_enable_sanitizers(copal)
endif()

# ---------------------------------------------------------------------------
# Examples / tests
# ---------------------------------------------------------------------------
if(COPAL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(COPAL_BUILD_TESTS AND COPAL_IS_TOP_LEVEL)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install + package config
# ---------------------------------------------------------------------------
if(COPAL_ENABLE_INSTALL)
    install(TARGETS copal
        EXPORT copalTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    install(DIRECTORY include/copal
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT copalTargets
        FILE copalTargets.cmake
        NAMESPACE copal::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/copal)

    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        cmake/copalConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/copalConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/copal)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/copalConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/copalConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/copalConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/copal)
endif()
