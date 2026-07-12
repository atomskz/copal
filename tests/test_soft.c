/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Golden pixel tests for the software renderer, driven through a stub platform
 * that hands it a plain malloc'd 32-bit (ARGB8888) buffer. No SDL/GL: this
 * verifies the CPU rasterizer's output directly and deterministically.
 */
#include <copal/copal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/renderer.h"
#include "render/soft/renderer_soft.h"
#include "platform/platform.h"
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

#define W 64
#define H 48

typedef struct stub_platform {
    cl_platform_t base;
    uint32_t *px;
    uint32_t a_mask; /* 0 = opaque surface (no alpha channel) */
} stub_platform_t;

static bool stub_lock(cl_platform_t *p, cl_platform_window_t *win,
                      cl_pixmap_t *out)
{
    stub_platform_t *s = (stub_platform_t *)p;

    (void)win;
    out->pixels = s->px;
    out->w = W;
    out->h = H;
    out->pitch = W * 4;
    out->r_mask = 0x00FF0000u;
    out->g_mask = 0x0000FF00u;
    out->b_mask = 0x000000FFu;
    out->a_mask = s->a_mask;
    return true;
}

static void stub_unlock(cl_platform_t *p, cl_platform_window_t *win)
{
    (void)p;
    (void)win;
}

static const cl_platform_ops_t stub_ops = {
    .lock_framebuffer = stub_lock,
    .unlock_framebuffer = stub_unlock,
};

static uint32_t at(const uint32_t *buf, int x, int y)
{
    return buf[y * W + x];
}

static bool is_rgb(uint32_t p, cl_color_t c)
{
    return (int)((p >> 16) & 0xFFu) == c.r && (int)((p >> 8) & 0xFFu) == c.g &&
           (int)(p & 0xFFu) == c.b;
}

static cl_font_t *load_any_font(cl_application_t *app)
{
    /* Honours COPAL_FONT (set it in CI), then probes the system fonts. */
    return cl_font_load_system(app, 16.0f);
}

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    uint32_t *buf = malloc((size_t)W * H * 4);
    stub_platform_t stub;
    cl_renderer_t *r;
    cl_color_t bg = { 10, 20, 30, 255 };
    cl_color_t fill = { 200, 100, 50, 255 };

    CHECK(buf != NULL);
    if (!buf)
        return 1;
    memset(&stub, 0, sizeof(stub));
    stub.base.ops = &stub_ops;
    stub.px = buf;
    stub.a_mask = 0xFF000000u;
    r = cl_renderer_soft_create(a, &stub.base);
    CHECK(r != NULL);
    if (!r)
        return 1;

    /* Clear + an opaque fill_rect. */
    r->ops->begin_frame(r, (cl_size_t){ W, H }, 1.0f, bg);
    r->ops->fill_rect(r, (cl_rect_t){ 10, 10, 20, 16 }, fill);
    r->ops->end_frame(r);
    CHECK(is_rgb(at(buf, 0, 0), bg));     /* cleared to bg */
    CHECK(is_rgb(at(buf, 20, 18), fill)); /* inside the fill */
    CHECK(is_rgb(at(buf, 5, 5), bg));     /* outside the fill stays bg */

    /* Clip bounds writes: a full-window fill limited to a small clip. */
    r->ops->begin_frame(r, (cl_size_t){ W, H }, 1.0f, bg);
    r->ops->push_clip(r, (cl_rect_t){ 0, 0, 8, 8 });
    r->ops->fill_rect(r, (cl_rect_t){ 0, 0, W, H }, fill);
    r->ops->pop_clip(r);
    r->ops->end_frame(r);
    CHECK(is_rgb(at(buf, 4, 4), fill));   /* inside clip: filled */
    CHECK(is_rgb(at(buf, 20, 20), bg));   /* outside clip: untouched */

    /* Rounded rect: centre filled, the rounded-away corner stays bg. */
    r->ops->begin_frame(r, (cl_size_t){ W, H }, 1.0f, bg);
    r->ops->fill_round_rect(r, (cl_rect_t){ 0, 0, 40, 40 }, 12.0f, fill);
    r->ops->end_frame(r);
    CHECK(is_rgb(at(buf, 20, 20), fill)); /* centre */
    CHECK(is_rgb(at(buf, 0, 0), bg));     /* corner rounded off -> bg */

    /* Source-over blending: a 50%-alpha white fill over the dark bg must land
     * halfway between them (+-1 for rounding). */
    {
        cl_color_t half = { 255, 255, 255, 128 };
        uint32_t p;
        int ch;

        r->ops->begin_frame(r, (cl_size_t){ W, H }, 1.0f, bg);
        r->ops->fill_rect(r, (cl_rect_t){ 0, 0, W, H }, half);
        r->ops->end_frame(r);
        p = at(buf, 30, 30);
        ch = (int)((p >> 16) & 0xFFu); /* red: 10 + (255-10)*128/255 ~ 133 */
        CHECK(ch >= 131 && ch <= 135);
        ch = (int)(p & 0xFFu);         /* blue: 30 + (255-30)*128/255 ~ 143 */
        CHECK(ch >= 141 && ch <= 145);
    }

    /* Device scale 2: logical coordinates land at doubled physical pixels. */
    r->ops->begin_frame(r, (cl_size_t){ W / 2, H / 2 }, 2.0f, bg);
    r->ops->fill_rect(r, (cl_rect_t){ 4, 4, 8, 6 }, fill);
    r->ops->end_frame(r);
    CHECK(is_rgb(at(buf, 9, 9), fill));   /* inside 8..24 x 8..20 */
    CHECK(is_rgb(at(buf, 23, 19), fill)); /* far physical corner */
    CHECK(is_rgb(at(buf, 7, 7), bg));     /* just outside */
    CHECK(is_rgb(at(buf, 24, 20), bg));

    /* Stroke: border pixels painted, the interior left untouched. */
    r->ops->begin_frame(r, (cl_size_t){ W, H }, 1.0f, bg);
    r->ops->stroke_round_rect(r, (cl_rect_t){ 8, 8, 30, 24 }, 0.0f, 2.0f,
                              fill);
    r->ops->end_frame(r);
    CHECK(is_rgb(at(buf, 8, 20), fill));  /* on the 2px left border */
    CHECK(is_rgb(at(buf, 23, 20), bg));   /* interior stays bg */
    CHECK(is_rgb(at(buf, 4, 20), bg));    /* outside stays bg */

    /* An opaque surface (a_mask == 0) must render the same colours. */
    stub.a_mask = 0;
    r->ops->begin_frame(r, (cl_size_t){ W, H }, 1.0f, bg);
    r->ops->fill_rect(r, (cl_rect_t){ 10, 10, 20, 16 }, fill);
    r->ops->end_frame(r);
    CHECK(is_rgb(at(buf, 20, 18), fill));
    CHECK(is_rgb(at(buf, 5, 5), bg));
    stub.a_mask = 0xFF000000u;

    /* Text (optional font): drawing must change some pixels vs the bg. */
    {
        cl_platform_t *mp = cl_platform_mock_create(a);
        cl_renderer_t *mr = cl_renderer_mock_create(a);
        cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
        cl_application_t *app;
        cl_font_t *font;

        ad.platform = mp;
        ad.renderer = mr;
        app = cl_application_create(&ad);
        CHECK(app != NULL);
        font = app ? load_any_font(app) : NULL;
        if (font) {
            int x, y, changed = 0;

            r->ops->begin_frame(r, (cl_size_t){ W, H }, 1.0f, bg);
            r->ops->draw_text(r, font, "8", (cl_point_t){ 6, 8 },
                              (cl_color_t){ 255, 255, 255, 255 });
            r->ops->end_frame(r);
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    if (!is_rgb(at(buf, x, y), bg))
                        changed++;
            CHECK(changed > 0); /* the glyph blitted some coverage */
            cl_font_release(font);
        }
        if (app)
            cl_application_destroy(app);
    }

    r->ops->destroy(r);
    free(buf);

    if (failures == 0)
        printf("all soft renderer tests passed\n");
    return failures == 0 ? 0 : 1;
}
