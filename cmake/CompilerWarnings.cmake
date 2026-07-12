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
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined)
    endif()
endfunction()
