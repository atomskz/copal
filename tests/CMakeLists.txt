# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_foundation test_foundation.c)
target_link_libraries(test_foundation PRIVATE copal::copal)
copal_set_warnings(test_foundation)
if(COPAL_ENABLE_SANITIZERS)
    copal_enable_sanitizers(test_foundation)
endif()
add_test(NAME foundation COMMAND test_foundation)
