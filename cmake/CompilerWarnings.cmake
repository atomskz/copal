# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiler warnings and sanitizers as opt-in helper functions (target-scoped;
# no global flags).

function(copal_set_warnings target)
    if(MSVC)
        # /wd4127: the do { } while (0) idiom (CHECK macros etc.) trips C4127
        # "conditional expression is constant" under /W4 — a known false alarm.
        target_compile_options(${target} PRIVATE /W4 /permissive- /wd4127)
        # Standard C str* functions are not "unsafe"; silence MSVC's C4996 nags
        # (matches how the library TU is already built).
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wstrict-prototypes
            -Wmissing-prototypes)
    endif()
endfunction()

function(copal_enable_sanitizers target)
    if(MSVC)
        # MSVC has no UBSan, and its /fsanitize=address conflicts with the
        # /RTC1 that CMake puts into the default Debug flags - enabling it
        # per target would break the build. Be loud (once) instead of
        # silently building without sanitizers.
        get_property(warned GLOBAL PROPERTY COPAL_MSVC_SANITIZERS_WARNED)
        if(NOT warned)
            set_property(GLOBAL PROPERTY COPAL_MSVC_SANITIZERS_WARNED ON)
            message(WARNING
                "COPAL_ENABLE_SANITIZERS has no effect with MSVC (no UBSan; "
                "/fsanitize=address is incompatible with CMake's default "
                "/RTC1 Debug flags). Use clang-cl or a GNU toolchain for "
                "sanitized builds.")
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined)
    endif()
endfunction()

# gcov/llvm-cov instrumentation (COPAL_ENABLE_COVERAGE). Debug-friendly flags:
# coverage numbers are meaningless on optimized builds.
function(copal_enable_coverage target)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE --coverage -O0 -g)
        # PUBLIC: everything linking an instrumented static library needs
        # libgcov too (tests, examples).
        target_link_options(${target} PUBLIC --coverage)
    else()
        message(WARNING "COPAL_ENABLE_COVERAGE is not supported with MSVC")
    endif()
endfunction()
