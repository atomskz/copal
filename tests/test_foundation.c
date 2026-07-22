/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/copal.h>

#include <stdio.h>
#include <string.h>

#include "core/foundation/foundation_internal.h"
#include "core/foundation/mutex_internal.h"

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
    CHECK(strcmp(cl_version_string(), "0.3.1") == 0);
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

    /* size 0 is normalised to 1: NULL always means out of memory */
    p = cl_alloc(NULL, 0);
    CHECK(p != NULL);
    p = cl_realloc(NULL, p, 0);
    CHECK(p != NULL);
    cl_free(NULL, p);
    cl_free(NULL, NULL); /* NULL pointer is a no-op */
}

static void test_mutex(void)
{
    cl_mutex_t *m = cl_mutex_create(NULL); /* NULL selects default allocator */

    CHECK(m != NULL);
    if (m) {
        cl_mutex_lock(m);
        cl_mutex_unlock(m);
        cl_mutex_destroy(m);
    }
    cl_mutex_destroy(NULL); /* NULL is ignored */
}

static void test_types(void)
{
    cl_color_t c = cl_rgba(1, 2, 3, 4);

    CHECK(c.r == 1 && c.g == 2 && c.b == 3 && c.a == 4);
    CHECK(sizeof(cl_color_t) == 4);
}

/* Table-driven decoder cases: the trickiest hand-written parser in the
 * foundation. Contract (foundation_internal.h): invalid input yields U+FFFD
 * and consumes ONE byte; NUL returns 0. */
static void test_utf8(void)
{
    static const struct {
        const char *bytes; /* NUL-terminated input */
        uint32_t cp;       /* expected codepoint */
        size_t consumed;   /* expected return */
    } cases[] = {
        /* valid 1..4-byte sequences */
        { "A", 0x41u, 1 },
        { "\x7F", 0x7Fu, 1 },
        { "\xC2\xA9", 0xA9u, 2 },             /* (c) */
        { "\xD0\x9F", 0x41Fu, 2 },            /* Cyrillic Pe */
        { "\xE2\x82\xAC", 0x20ACu, 3 },       /* euro sign */
        { "\xF0\x9F\x98\x80", 0x1F600u, 4 },  /* emoji */
        { "\xF4\x8F\xBF\xBF", 0x10FFFFu, 4 }, /* highest scalar */
        /* overlong encodings are invalid */
        { "\xC0\x80", 0xFFFDu, 1 },
        { "\xC1\xBF", 0xFFFDu, 1 },
        { "\xE0\x80\x80", 0xFFFDu, 1 },
        { "\xF0\x80\x80\x80", 0xFFFDu, 1 },
        /* surrogates and beyond U+10FFFF are invalid */
        { "\xED\xA0\x80", 0xFFFDu, 1 },       /* U+D800 */
        { "\xED\xBF\xBF", 0xFFFDu, 1 },       /* U+DFFF */
        { "\xF4\x90\x80\x80", 0xFFFDu, 1 },  /* U+110000 */
        /* truncated tails (NUL where a continuation belongs) */
        { "\xC2", 0xFFFDu, 1 },
        { "\xE2\x82", 0xFFFDu, 1 },
        { "\xF0\x9F\x98", 0xFFFDu, 1 },
        /* lone continuation and stray lead bytes */
        { "\x80", 0xFFFDu, 1 },
        { "\xBF", 0xFFFDu, 1 },
        { "\xFE", 0xFFFDu, 1 },
        { "\xFF", 0xFFFDu, 1 },
    };
    size_t i;
    uint32_t cp;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t n = cl_utf8_next(cases[i].bytes, &cp);

        if (n != cases[i].consumed || cp != cases[i].cp) {
            fprintf(stderr,
                    "FAIL utf8 case %zu: got cp=U+%04X n=%zu, "
                    "want cp=U+%04X n=%zu\n",
                    i, cp, n, cases[i].cp, cases[i].consumed);
            failures++;
        }
    }

    /* NUL terminates (consumes nothing). */
    CHECK(cl_utf8_next("", &cp) == 0);

    /* Bounded variant: never reads past avail. */
    CHECK(cl_utf8_next_n("A", 1, &cp) == 1 && cp == 0x41u);
    CHECK(cl_utf8_next_n("", 0, &cp) == 0);          /* nothing available */
    /* a 3-byte sequence cut off by avail is invalid: U+FFFD, one byte */
    CHECK(cl_utf8_next_n("\xE2\x82\xAC", 2, &cp) == 1 && cp == 0xFFFDu);
    CHECK(cl_utf8_next_n("\xE2\x82\xAC", 3, &cp) == 3 && cp == 0x20ACu);
    /* a NUL byte inside the window still terminates */
    CHECK(cl_utf8_next_n("\0A", 2, &cp) == 0);
}

int main(void)
{
    test_version();
    test_error();
    test_allocator();
    test_mutex();
    test_types();
    test_utf8();

    if (failures == 0)
        printf("all foundation tests passed\n");

    return failures == 0 ? 0 : 1;
}
