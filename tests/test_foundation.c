/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/copal.h>

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void test_version(void)
{
    CHECK(cl_version_runtime() == COPAL_VERSION);
    CHECK(strcmp(cl_version_string(), "0.1.0") == 0);
    CHECK(COPAL_VERSION_ENCODE(1, 2, 3) == 0x010203u);
}

static void test_error(void)
{
    CHECK(strcmp(cl_result_string(CL_OK), "OK") == 0);
    CHECK(cl_result_string(CL_ERROR_OUT_OF_MEMORY) != NULL);
    CHECK(cl_result_string(CL_ERROR_ABI_MISMATCH) != NULL);
    CHECK(cl_last_error() == CL_OK);
}

static void test_allocator(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    void *p;

    CHECK(a != NULL);

    p = cl_alloc(NULL, 64); /* NULL selects default allocator */
    CHECK(p != NULL);
    p = cl_realloc(NULL, p, 128);
    CHECK(p != NULL);
    cl_free(NULL, p);
}

static void test_types(void)
{
    cl_color_t c = cl_rgba(1, 2, 3, 4);

    CHECK(c.r == 1 && c.g == 2 && c.b == 3 && c.a == 4);
    CHECK(sizeof(cl_color_t) == 4);
}

int main(void)
{
    test_version();
    test_error();
    test_allocator();
    test_types();

    if (failures == 0)
        printf("all foundation tests passed\n");

    return failures == 0 ? 0 : 1;
}
