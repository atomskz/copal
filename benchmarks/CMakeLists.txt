# SPDX-License-Identifier: GPL-3.0-or-later

# copal::mocks carries copal::copal and the internal src/ include path (the
# benchmarks drive the real render/layout walk through white-box entry points).
add_executable(copal_bench bench.c)
target_link_libraries(copal_bench PRIVATE copal::mocks)
copal_set_warnings(copal_bench)
if(COPAL_ENABLE_SANITIZERS)
    copal_enable_sanitizers(copal_bench)
endif()
