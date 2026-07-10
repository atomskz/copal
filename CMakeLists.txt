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

# Core sources: foundation + widget/layout/event/theme/text + mock backends.
# The SDL2 platform and OpenGL renderer backends are added when their options
# are enabled (they require SDL2/OpenGL).
set(COPAL_SOURCES
    src/core/foundation/version.c
    src/core/foundation/error.c
    src/core/foundation/allocator.c
    src/core/foundation/utf8.c
    src/text/font.c
    src/text/stb_impl.c
    src/theme/theme.c
    src/render/paint_context.c
    src/render/mock/renderer_mock.c
    src/platform/mock/platform_mock.c
    src/widget/widget.c
    src/layout/vbox.c
    src/widgets/label.c
    src/widgets/button.c
    src/widgets/textbox.c
    src/app/application.c
    src/app/window.c)

add_library(copal ${COPAL_LIB_TYPE} ${COPAL_SOURCES})
add_library(copal::copal ALIAS copal)

target_compile_features(copal PUBLIC c_std_11)

target_include_directories(copal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)

# stb_truetype is third-party: build its implementation TU without our warnings.
if(NOT MSVC)
    set_source_files_properties(src/text/stb_impl.c PROPERTIES
        COMPILE_FLAGS "-w")
endif()

# stb_truetype uses the C math library.
if(NOT WIN32)
    target_link_libraries(copal PRIVATE m)
endif()

# Native backends: SDL2 platform + OpenGL renderer. Enable both together.
if(COPAL_ENABLE_SDL AND COPAL_ENABLE_OPENGL)
    find_package(SDL2 REQUIRED)
    find_package(OpenGL REQUIRED)
    target_sources(copal PRIVATE
        src/platform/sdl/platform_sdl.c
        src/render/gl/gl_loader.c
        src/render/gl/renderer_gl.c)
    target_compile_definitions(copal PRIVATE CL_ENABLE_SDL CL_ENABLE_OPENGL)
    target_link_libraries(copal PRIVATE SDL2::SDL2 OpenGL::GL)
elseif(COPAL_ENABLE_SDL OR COPAL_ENABLE_OPENGL)
    message(WARNING "COPAL_ENABLE_SDL and COPAL_ENABLE_OPENGL must be enabled "
                    "together; native backend disabled.")
endif()

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
