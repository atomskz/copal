# SPDX-License-Identifier: GPL-3.0-or-later

# Minimal smoke-test example: links the library and prints its version.
# The full GUI example (hello_gui) is added with the vertical prototype (Stage 6).
add_executable(hello_version hello_version.c)
target_link_libraries(hello_version PRIVATE copal::copal)
copal_set_warnings(hello_version)
if(COPAL_ENABLE_SANITIZERS)
    copal_enable_sanitizers(hello_version)
endif()
