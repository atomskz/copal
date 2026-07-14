/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Robustness sweep for the two entry points that ingest caller-supplied bulk
 * data: cl_font_load_memory and cl_image_create. Deterministic (fixed-seed
 * LCG, no rand()) so it is reproducible, and meant to run under ASan/UBSan
 * (the sanitized CI build) where any over-read or overflow aborts.
 *
 * Scope: this exercises the guards the library actually enforces - the font
 * header-length and not-a-font-tag checks, and image dimension/size validation.
 * It deliberately does NOT feed a *valid* sfnt tag with truncated tables:
 * stb_truetype does not bounds-check a truncated real font, which the library
 * documents as an accepted limitation ("fonts from a trusted source"), so that
 * path is out of scope here rather than an expected pass.
 */
#include <copal/copal.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

static uint32_t lcg = 0x9e3779b9u;

static uint32_t rnd(void)
{
    lcg = lcg * 1664525u + 1013904223u;
    return lcg;
}

static void quiet_log(cl_log_level_t l, const char *m, void *u)
{
    (void)l;
    (void)m;
    (void)u; /* the font rejections log WARN; swallow it */
}

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_application_t *app;
    unsigned char buf[512];
    unsigned char px[16 * 16 * 4];
    size_t len;
    int it;

    cl_set_log_callback(quiet_log, NULL);
    ad.platform = cl_platform_mock_create(a);
    ad.renderer = cl_renderer_mock_create(a);
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    if (!app)
        return 1;

    /* --- Font loading: NULL / empty are rejected without touching bytes. --- */
    CHECK(cl_font_load_memory(app, NULL, 100, 16.0f) == NULL);
    CHECK(cl_font_load_memory(app, buf, 0, 16.0f) == NULL);

    /* Sub-header lengths (< 12) are rejected by the length guard. */
    for (len = 1; len < 12; len++) {
        size_t i;

        for (i = 0; i < len; i++)
            buf[i] = (unsigned char)rnd();
        CHECK(cl_font_load_memory(app, buf, len, 16.0f) == NULL);
    }

    /*
     * Random 12..512-byte buffers with a deliberately invalid sfnt tag: these
     * are rejected as "not a font" before stb_truetype parses any table, so no
     * matter the garbage that follows, load returns NULL and never over-reads.
     */
    for (it = 0; it < 3000; it++) {
        size_t i;

        len = 12 + (size_t)(rnd() % (unsigned)(sizeof(buf) - 12));
        for (i = 0; i < len; i++)
            buf[i] = (unsigned char)rnd();
        buf[0] = 0xDE; /* not 0x00010000 / OTTO / true / ttcf / typ1 */
        buf[1] = 0xAD;
        buf[2] = 0xBE;
        buf[3] = 0xEF;
        CHECK(cl_font_load_memory(app, buf, len, 16.0f) == NULL);
    }

    /* --- Image creation: bad dimensions never crash or over-read px. --- */
    memset(px, 0xA5, sizeof(px));
    CHECK(cl_image_create(app, 4, 4, NULL) == NULL);
    CHECK(cl_image_create(NULL, 4, 4, px) == NULL);
    for (it = 0; it < 2000; it++) {
        /* -4..16: positives stay within the 16x16 source buffer. */
        int w = (int)(rnd() % 21u) - 4;
        int h = (int)(rnd() % 21u) - 4;
        cl_image_t *img = cl_image_create(app, w, h, px);

        if (w > 0 && h > 0) {
            CHECK(img != NULL);
            cl_image_release(img);
        } else {
            CHECK(img == NULL);
        }
    }

    cl_application_destroy(app);
    return failures ? 1 : 0;
}
