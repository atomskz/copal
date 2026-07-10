# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiler warnings and sanitizers as opt-in helper functions (target-scoped;
# no global flags).

function(copal_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
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
