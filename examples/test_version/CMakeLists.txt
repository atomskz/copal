# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(test_version main.c)
target_link_libraries(test_version PRIVATE copal::copal)
copal_set_warnings(test_version)
if(COPAL_ENABLE_SANITIZERS)
    copal_enable_sanitizers(test_version)
endif()
