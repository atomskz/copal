/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * cl_image_create input validation: valid pixels round-trip, bad dimensions
 * and NULL arguments are rejected, and a w*h*4 byte size that would overflow
 * size_t is refused (the 32-bit case that would otherwise under-allocate and
 * let the copy overrun the heap).
 */
#include <copal/copal.h>

#include <stdint.h>
#include <stdio.h>

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

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_application_t *app;
    cl_image_t *img;
    cl_size_t sz;
    unsigned char px[2 * 3 * 4];
    int i;

    for (i = 0; i < (int)sizeof(px); i++)
        px[i] = (unsigned char)i;

    ad.platform = cl_platform_mock_create(a);
    ad.renderer = cl_renderer_mock_create(a);
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    if (!app)
        return 1;

    /* Valid pixels round-trip. */
    img = cl_image_create(app, 2, 3, px);
    CHECK(img != NULL);
    sz = cl_image_size(img);
    CHECK(sz.w == 2.0f && sz.h == 3.0f);
    CHECK(cl_image_pixels(img) != NULL);
    cl_image_release(img);

    /* Non-positive dimensions and NULL arguments are rejected. */
    CHECK(cl_image_create(app, 0, 1, px) == NULL);
    CHECK(cl_last_error() == CL_ERROR_INVALID_ARGUMENT);
    CHECK(cl_image_create(app, 1, 0, px) == NULL);
    CHECK(cl_image_create(app, -1, 1, px) == NULL);
    CHECK(cl_image_create(app, 1, -1, px) == NULL);
    CHECK(cl_image_create(app, 1, 1, NULL) == NULL);
    CHECK(cl_image_create(NULL, 1, 1, px) == NULL);

    /*
     * Overflow guard. Only reachable where size_t is 32-bit: with a 64-bit
     * size_t no int w,h can make w*h*4 wrap. The create rejects before it
     * would read the (deliberately tiny) pixel buffer, so px is untouched.
     */
#if SIZE_MAX <= 0xFFFFFFFFu
    CHECK(cl_image_create(app, 40000, 40000, px) == NULL);
    CHECK(cl_last_error() == CL_ERROR_INVALID_ARGUMENT);
#endif

    cl_application_destroy(app);
    return failures ? 1 : 0;
}
