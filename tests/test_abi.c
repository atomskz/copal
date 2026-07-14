/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Desc/ops ABI evolution (ADR-005, Variant B). The handshake is tail-tolerant:
 * a desc is compatible when it stamps the same MAJOR version and is at least
 * the handshake header; the library normalises it into a full-size local so a
 * shorter (older) caller's missing fields default and a longer (newer) caller's
 * extra tail is ignored. Ops tables must carry the whole baseline (a missing op
 * cannot be called), so a shorter table is refused while a longer one is fine.
 * A different major is always refused. Run under ASan: the truncated-desc cases
 * use exact-size heap buffers so any read past the declared size aborts.
 */
#include <copal/copal.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"
#include "core/foundation/foundation_internal.h" /* CL_DESC_MIN_SIZE, header */

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

static cl_application_t *make_app(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;

    ad.platform = cl_platform_mock_create(a);
    ad.renderer = cl_renderer_mock_create(a);
    return cl_application_create(&ad);
}

int main(void)
{
    cl_application_t *app = make_app();
    uint32_t wrong_major = COPAL_VERSION_ENCODE(COPAL_VERSION_MAJOR + 1, 0, 0);

    CHECK(app != NULL);
    if (!app)
        return 1;

    /* --- Widget desc: older caller, struct truncated to just the handshake.
     * Exact-size heap buffer so ASan flags any over-read; must be accepted with
     * every omitted field defaulted. --- */
    {
        unsigned char *buf = malloc(CL_DESC_MIN_SIZE);
        cl_desc_header_t hdr = { .abi_version = COPAL_VERSION,
                                 .struct_size = CL_DESC_MIN_SIZE };
        cl_widget_t *b;

        memcpy(buf, &hdr, sizeof hdr);
        b = cl_button_create(app, (const cl_button_desc_t *)buf);
        CHECK(b != NULL); /* accepted: .text etc. default to NULL/0 */
        cl_widget_destroy(b);
        free(buf);
    }

    /* --- Widget desc: newer caller with extra tail bytes -> accepted, tail
     * ignored. --- */
    {
        struct {
            cl_button_desc_t d;
            unsigned char future[32];
        } big = { .d = { CL_BUTTON_DESC_INIT_FIELDS, .text = "hi" } };
        cl_widget_t *b;

        big.d.struct_size = sizeof(big);
        memset(big.future, 0xAB, sizeof big.future);
        b = cl_button_create(app, &big.d);
        CHECK(b != NULL);
        cl_widget_destroy(b);
    }

    /* --- Widget desc rejections: undersized (below the header) and wrong
     * major. --- */
    {
        cl_button_desc_t d = { CL_BUTTON_DESC_INIT_FIELDS };

        d.struct_size = 4; /* smaller than the {abi_version, struct_size} header */
        CHECK(cl_button_create(app, &d) == NULL);
        CHECK(cl_last_error() == CL_ERROR_ABI_MISMATCH);

        d.struct_size = sizeof(cl_button_desc_t);
        d.abi_version = wrong_major;
        CHECK(cl_button_create(app, &d) == NULL);
        CHECK(cl_last_error() == CL_ERROR_ABI_MISMATCH);
    }

    /* --- Window desc: truncated before width/height -> accepted, dimensions
     * default (observable through cl_window_size). --- */
    {
        cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                                .title = "t", .width = 800, .height = 600 };
        cl_window_t *win;
        cl_size_t sz;

        wd.struct_size = offsetof(cl_window_desc_t, width);
        win = cl_window_create(app, &wd);
        CHECK(win != NULL);
        sz = cl_window_size(win);
        CHECK(sz.w == 0.0f && sz.h == 0.0f); /* width/height were past the tail */
    }

    cl_application_destroy(app);

    /* --- Application desc: newer caller with extra tail -> accepted (with real
     * backends). --- */
    {
        const cl_allocator_t *a = cl_allocator_default();
        struct {
            cl_application_desc_t d;
            unsigned char future[32];
        } big = { .d = { CL_APPLICATION_DESC_INIT_FIELDS } };
        cl_application_t *app2;

        big.d.platform = cl_platform_mock_create(a);
        big.d.renderer = cl_renderer_mock_create(a);
        big.d.struct_size = sizeof(big);
        app2 = cl_application_create(&big.d);
        CHECK(app2 != NULL);
        cl_application_destroy(app2);
    }

    /* --- Ops table: a newer backend with extra tail -> accepted; the library
     * reads only the baseline it knows. Reuse the real mock platform's state
     * but point it at a longer copy of its own ops table. --- */
    {
        const cl_allocator_t *a = cl_allocator_default();
        cl_platform_t *mock = cl_platform_mock_create(a);
        struct {
            cl_platform_ops_t ops;
            unsigned char future[16];
        } big;
        cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
        cl_application_t *app3;

        memcpy(&big.ops, mock->ops, sizeof big.ops); /* the real ops... */
        memset(big.future, 0, sizeof big.future);
        big.ops.struct_size = sizeof(big);           /* ...claiming a larger table */
        mock->ops = &big.ops;                         /* real platform, longer ops */
        ad.platform = mock;
        ad.renderer = cl_renderer_mock_create(a);
        app3 = cl_application_create(&ad);
        CHECK(app3 != NULL); /* longer ops table accepted */
        cl_application_destroy(app3);
    }

    return failures ? 1 : 0;
}
