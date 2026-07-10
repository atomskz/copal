# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(test_foundation test_foundation.c)
target_link_libraries(test_foundation PRIVATE copal::copal)
copal_set_warnings(test_foundation)
if(COPAL_ENABLE_SANITIZERS)
    copal_enable_sanitizers(test_foundation)
endif()
add_test(NAME foundation COMMAND test_foundation)

add_executable(test_gui test_gui.c)
target_link_libraries(test_gui PRIVATE copal::copal)
target_include_directories(test_gui PRIVATE ${CMAKE_SOURCE_DIR}/src)
copal_set_warnings(test_gui)
if(COPAL_ENABLE_SANITIZERS)
    copal_enable_sanitizers(test_gui)
endif()
add_test(NAME gui COMMAND test_gui)
set_tests_properties(gui PROPERTIES SKIP_RETURN_CODE 77)
