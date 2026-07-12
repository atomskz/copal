# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)

project(copal
    VERSION 0.1.0
    DESCRIPTION "Lightweight cross-platform GUI library in C"
    HOMEPAGE_URL "https://atomskz.github.io/copal"
    LANGUAGES C)

# Detect whether we are the top-level project (for add_subdirectory use).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(COPAL_IS_TOP_LEVEL ON)
else()
    set(COPAL_IS_TOP_LEVEL OFF)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
# Examples, tests and install rules default to OFF when copal is embedded via
# add_subdirectory - the host project can still opt in explicitly.
option(COPAL_BUILD_SHARED      "Build copal as a shared library"    OFF)
option(COPAL_BUILD_EXAMPLES    "Build examples"                     ${COPAL_IS_TOP_LEVEL})
option(COPAL_BUILD_TESTS       "Build tests"                        ${COPAL_IS_TOP_LEVEL})
option(COPAL_ENABLE_SDL        "Enable SDL2 platform backend"       OFF) # Stage 6
option(COPAL_ENABLE_OPENGL     "Enable OpenGL renderer backend"     OFF) # Stage 6
option(COPAL_FETCH_SDL2        "Download and build SDL2 via FetchContent" OFF)
option(COPAL_ENABLE_SANITIZERS "Enable ASan/UBSan"                  OFF)
option(COPAL_ENABLE_COVERAGE   "Instrument for gcov/llvm-cov coverage" OFF)
option(COPAL_ENABLE_INSTALL    "Enable install rules"               ${COPAL_IS_TOP_LEVEL})

# Single-config generators build with no optimisation at all when
# CMAKE_BUILD_TYPE is empty; default to Release for a top-level build.
if(COPAL_IS_TOP_LEVEL AND NOT CMAKE_CONFIGURATION_TYPES
   AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
    message(STATUS "copal: CMAKE_BUILD_TYPE not set; defaulting to Release")
endif()

include(GNUInstallDirs)
include(cmake/CompilerWarnings.cmake)

# ---------------------------------------------------------------------------
# Version consistency
# ---------------------------------------------------------------------------
# version.h is the source of truth (the ABI handshake compiles it into user
# code); the CMake project version feeds SOVERSION and the package files, so
# the two must never drift apart.
file(STRINGS include/copal/version.h _copal_ver_lines
     REGEX "^#define COPAL_VERSION_(MAJOR|MINOR|PATCH) ")
string(REGEX MATCH "COPAL_VERSION_MAJOR ([0-9]+)" _ "${_copal_ver_lines}")
set(_copal_h_ver "${CMAKE_MATCH_1}")
string(REGEX MATCH "COPAL_VERSION_MINOR ([0-9]+)" _ "${_copal_ver_lines}")
string(APPEND _copal_h_ver ".${CMAKE_MATCH_1}")
string(REGEX MATCH "COPAL_VERSION_PATCH ([0-9]+)" _ "${_copal_ver_lines}")
string(APPEND _copal_h_ver ".${CMAKE_MATCH_1}")
if(NOT _copal_h_ver STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "Version mismatch: include/copal/version.h declares ${_copal_h_ver} "
        "but project(copal VERSION ...) declares ${PROJECT_VERSION}; "
        "update both together.")
endif()
unset(_copal_ver_lines)
unset(_copal_h_ver)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
if(COPAL_BUILD_SHARED)
    set(COPAL_LIB_TYPE SHARED)
else()
    set(COPAL_LIB_TYPE STATIC)
endif()

# Core sources: foundation + widget/layout/event/theme/text. The SDL2 platform
# and OpenGL renderer backends are added when their options are enabled (they
# require SDL2/OpenGL); the mock backends live in the separate copal_mocks
# test-support library below.
set(COPAL_SOURCES
    src/core/foundation/version.c
    src/core/foundation/error.c
    src/core/foundation/allocator.c
    src/core/foundation/utf8.c
    src/core/foundation/mutex.c
    src/text/font.c
    src/text/stb_impl.c
    src/theme/theme.c
    src/render/paint_context.c
    src/render/image.c
    src/render/soft/renderer_soft.c
    src/widget/widget.c
    src/layout/vbox.c
    src/layout/hbox.c
    src/layout/scrollview.c
    src/widgets/imageview.c
    src/widgets/label.c
    src/widgets/button.c
    src/widgets/checkbox.c
    src/widgets/radiobutton.c
    src/widgets/slider.c
    src/widgets/menu.c
    src/widgets/menubar.c
    src/widgets/combobox.c
    src/widgets/textbox.c
    src/widgets/tooltip.c
    src/app/application.c
    src/app/window.c
    src/app/timer.c)

add_library(copal ${COPAL_LIB_TYPE} ${COPAL_SOURCES})
add_library(copal::copal ALIAS copal)

target_compile_features(copal PUBLIC c_std_11)

target_include_directories(copal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)

# stb_truetype is third-party. GCC/Clang: build its implementation TU with
# warnings off. MSVC: stb_impl.c silences warnings with a pragma instead — a
# per-file /w would override the target's /W4 and trigger warning D9025.
if(NOT MSVC)
    set_source_files_properties(src/text/stb_impl.c PROPERTIES
        COMPILE_FLAGS "-w")
endif()

# stb_truetype uses the C math library.
if(NOT WIN32)
    target_link_libraries(copal PRIVATE m)
endif()

# Cross-thread task queue (cl_application_post) uses a mutex; PUBLIC so headless
# consumers that post from their own threads inherit the pthread flags.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(copal PUBLIC Threads::Threads)

# Native backend: SDL2 platform. The SDL platform (including the OpenGL-free
# software window) needs only SDL2; the OpenGL renderer is added on top when
# COPAL_ENABLE_OPENGL is also set. So COPAL_ENABLE_SDL alone yields a working
# software-rendering build with no GL/driver dependency.
if(COPAL_ENABLE_SDL)
    # SDL2 comes from either a system/vcpkg package (find_package) or, when
    # COPAL_FETCH_SDL2 is set, is downloaded and built as a subproject. The
    # latter needs no local SDL2 install and is the easy path on Windows.
    if(COPAL_FETCH_SDL2)
        include(FetchContent)
        # Pinned by commit, not by tag: tags are movable, so a tag pin is
        # neither reproducible nor tamper-evident. The hash below is what
        # release-2.30.9 pointed at when it was vendored.
        FetchContent_Declare(SDL2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG c98c4fbff6d8f3016a3ce6685bf8f43433c3efcc # release-2.30.9
            GIT_SHALLOW TRUE)
        set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL2)
    else()
        find_package(SDL2 REQUIRED)
    endif()
    target_sources(copal PRIVATE src/platform/sdl/platform_sdl.c)
    target_compile_definitions(copal PRIVATE CL_ENABLE_SDL)
    target_link_libraries(copal PRIVATE SDL2::SDL2)

    if(COPAL_ENABLE_OPENGL)
        find_package(OpenGL REQUIRED)
        target_sources(copal PRIVATE
            src/render/gl/gl_loader.c
            src/render/gl/renderer_gl.c)
        target_compile_definitions(copal PRIVATE CL_ENABLE_OPENGL)
        target_link_libraries(copal PRIVATE OpenGL::GL)
    endif()
elseif(COPAL_ENABLE_OPENGL)
    message(WARNING "COPAL_ENABLE_OPENGL requires COPAL_ENABLE_SDL; "
                    "OpenGL renderer disabled.")
endif()

if(COPAL_FETCH_SDL2 AND NOT COPAL_ENABLE_SDL)
    message(WARNING "COPAL_FETCH_SDL2 is ON but COPAL_ENABLE_SDL is OFF; "
                    "SDL2 will not be downloaded. Set COPAL_ENABLE_SDL=ON "
                    "to build the SDL2 backend.")
endif()

target_compile_definitions(copal PRIVATE COPAL_BUILDING)
# _CRT_SECURE_NO_WARNINGS (MSVC C4996) is applied by copal_set_warnings() below.
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
if(COPAL_ENABLE_COVERAGE)
    copal_enable_coverage(copal)
endif()

# White-box tests reach internal (non-exported) symbols, so they require the
# static library: with COPAL_BUILD_SHARED they are disabled, and only the
# black-box example smoke tests remain in ctest.
set(COPAL_WHITEBOX_TESTS ${COPAL_BUILD_TESTS})
if(COPAL_BUILD_SHARED AND COPAL_BUILD_TESTS)
    message(STATUS "copal: COPAL_BUILD_SHARED=ON - the white-box test suite "
                   "needs the static library and is disabled; the example "
                   "smoke tests still run.")
    set(COPAL_WHITEBOX_TESTS OFF)
endif()

# ---------------------------------------------------------------------------
# Mock backends (test support)
# ---------------------------------------------------------------------------
# A headless platform/renderer pair for white-box tests. A separate static
# library so the mocks never end up in the installed artifact.
if(COPAL_WHITEBOX_TESTS)
    add_library(copal_mocks STATIC
        src/render/mock/renderer_mock.c
        src/platform/mock/platform_mock.c)
    add_library(copal::mocks ALIAS copal_mocks)
    target_link_libraries(copal_mocks PUBLIC copal::copal)
    target_include_directories(copal_mocks
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    copal_set_warnings(copal_mocks)
    if(COPAL_ENABLE_SANITIZERS)
        copal_enable_sanitizers(copal_mocks)
    endif()
    if(COPAL_ENABLE_COVERAGE)
        copal_enable_coverage(copal_mocks)
    endif()
endif()

# ---------------------------------------------------------------------------
# Examples / tests
# ---------------------------------------------------------------------------
# enable_testing() must precede the examples: they register headless smoke
# runs of themselves as tests.
if(COPAL_BUILD_TESTS AND COPAL_IS_TOP_LEVEL)
    enable_testing()
endif()

if(COPAL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(COPAL_WHITEBOX_TESTS AND COPAL_IS_TOP_LEVEL)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install + package config
# ---------------------------------------------------------------------------
# A FetchContent SDL2 is a target built in this tree, not an installable
# package, so it cannot be part of copal's export set. Disable install/export
# in that configuration (COPAL_FETCH_SDL2 is a build-it-locally convenience).
# Only when SDL is actually vendored: FETCH_SDL2 without ENABLE_SDL fetches
# nothing (warned about above) and must not cost the install rules too.
if(COPAL_ENABLE_INSTALL AND COPAL_FETCH_SDL2 AND COPAL_ENABLE_SDL)
    message(STATUS "copal: COPAL_FETCH_SDL2 is ON; disabling install/export "
                   "(the vendored SDL2 cannot be part of the package).")
    set(COPAL_ENABLE_INSTALL OFF)
endif()

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

    # pkg-config metadata for non-CMake consumers. The private entries matter
    # for static linking: copal is a static library by default, so consumers
    # must also pull its own dependencies.
    set(COPAL_PC_REQUIRES_PRIVATE "")
    if(COPAL_ENABLE_SDL)
        set(COPAL_PC_REQUIRES_PRIVATE "sdl2")
    endif()
    set(COPAL_PC_LIBS_PRIVATE "")
    if(NOT WIN32)
        set(COPAL_PC_LIBS_PRIVATE "-lm -pthread")
    endif()
    if(COPAL_ENABLE_SDL AND COPAL_ENABLE_OPENGL)
        string(STRIP "${COPAL_PC_LIBS_PRIVATE} -lGL" COPAL_PC_LIBS_PRIVATE)
    endif()
    configure_file(cmake/copal.pc.in ${CMAKE_CURRENT_BINARY_DIR}/copal.pc
        @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/copal.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()
