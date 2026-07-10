# SPDX-License-Identifier: GPL-3.0-or-later

# Minimal smoke-test example: links the library and prints its version.
add_executable(hello_version hello_version.c)
target_link_libraries(hello_version PRIVATE copal::copal)
copal_set_warnings(hello_version)
if(COPAL_ENABLE_SANITIZERS)
    copal_enable_sanitizers(hello_version)
endif()

# Native GUI example: requires the SDL2 + OpenGL backends.
if(COPAL_ENABLE_SDL AND COPAL_ENABLE_OPENGL)
    add_executable(hello_gui hello_gui.c)
    target_link_libraries(hello_gui PRIVATE copal::copal)
    copal_set_warnings(hello_gui)
    if(COPAL_ENABLE_SANITIZERS)
        copal_enable_sanitizers(hello_gui)
    endif()
endif()
