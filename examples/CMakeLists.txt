# SPDX-License-Identifier: GPL-3.0-or-later
#
# Examples are organised one directory per example; each builds to
# build/examples/<name>/<name>.

# test_version is a pure smoke test: it links the library and prints the
# version, needing no backends.
add_subdirectory(test_version)

# The GUI examples require the SDL2 + OpenGL backends.
if(COPAL_ENABLE_SDL AND COPAL_ENABLE_OPENGL)
    # Helpers shared by the GUI examples (font loading, headless run helper,
    # LSan suppressions), linked into each of them.
    add_library(copal_example_common STATIC common/example_util.c)
    target_link_libraries(copal_example_common PUBLIC copal::copal)
    target_include_directories(copal_example_common PUBLIC common)
    copal_set_warnings(copal_example_common)
    if(COPAL_ENABLE_SANITIZERS)
        copal_enable_sanitizers(copal_example_common)
    endif()

    # copal_add_gui_example(<name> <sources...>) - build a GUI example: link the
    # shared helpers, apply warnings/sanitizers, emit it flat under the examples
    # build dir, and on Windows drop runtime DLLs beside it so it can be run
    # directly.
    function(copal_add_gui_example name)
        add_executable(${name} ${ARGN})
        target_link_libraries(${name} PRIVATE copal_example_common)
        copal_set_warnings(${name})
        if(COPAL_ENABLE_SANITIZERS)
            copal_enable_sanitizers(${name})
        endif()
        if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E
                        $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${name}>>,copy_if_different,true>
                        $<TARGET_RUNTIME_DLLS:${name}> $<TARGET_FILE_DIR:${name}>
                COMMAND_EXPAND_LISTS)
        endif()
    endfunction()

    add_subdirectory(helloworld)
    add_subdirectory(calc)
endif()
