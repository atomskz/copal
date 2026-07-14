# SPDX-License-Identifier: GPL-3.0-or-later

# copal_add_test(<name> [SKIP77]) - one standard test target: test_<name>.c
# linked against the library with the internal src/ include path (white-box
# tests use the mock backends and internal headers). SKIP77 marks suites that
# skip themselves (exit 77) when no system font is available.
function(copal_add_test name)
    cmake_parse_arguments(ARG "SKIP77" "" "" ${ARGN})
    add_executable(test_${name} test_${name}.c)
    # copal::mocks carries copal::copal and the internal src/ include path.
    target_link_libraries(test_${name} PRIVATE copal::mocks)
    # On Windows the library may pull in a shared SDL2: put its DLL beside
    # the test binary (same recipe as the examples).
    if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
        add_custom_command(TARGET test_${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E
                    $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:test_${name}>>,copy_if_different,true>
                    $<TARGET_RUNTIME_DLLS:test_${name}> $<TARGET_FILE_DIR:test_${name}>
            COMMAND_EXPAND_LISTS)
    endif()
    copal_set_warnings(test_${name})
    if(COPAL_ENABLE_SANITIZERS)
        copal_enable_sanitizers(test_${name})
    endif()
    if(COPAL_ENABLE_COVERAGE)
        copal_enable_coverage(test_${name})
    endif()
    add_test(NAME ${name} COMMAND test_${name})
    # A hung test (event loop regression) must not block ctest forever.
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
    if(ARG_SKIP77)
        set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endfunction()

copal_add_test(foundation)
copal_add_test(gui SKIP77)
copal_add_test(textbox)
copal_add_test(scrollview SKIP77)
copal_add_test(widgets)
copal_add_test(image)
copal_add_test(theme)
copal_add_test(popup)
copal_add_test(combobox)
copal_add_test(timer)
copal_add_test(animation)
copal_add_test(tooltip SKIP77)
copal_add_test(post)
copal_add_test(multiline SKIP77)
copal_add_test(ime)
copal_add_test(soft)
copal_add_test(damage)
copal_add_test(oom)
copal_add_test(layout)
copal_add_test(lifecycle)
