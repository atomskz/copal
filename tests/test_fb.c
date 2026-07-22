/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Host render-to-pixmap tests for the linear-framebuffer platform backend
 * (copal/platform/fb.h). An application drives the built-in software renderer
 * into a malloc'd 32-bit buffer with no SDL/GL, so backend authors can
 * unit-test exact pixel mapping - the clear colour, a filled rect at known
 * coordinates, every common channel layout, and the misaligned-buffer path
 * that must drop frames (with a diagnostic) rather than corrupt memory.
 */
#include <copal/copal.h>
#include <copal/widget_impl.h>
#include <copal/platform/fb.h>

#include <stdio.h>
#include <stdlib.h>
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

/* ---- a widget that fills a fixed absolute rect with a known colour -------- */

static cl_color_t g_color;
static cl_rect_t g_rect;

static void solid_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    (void)w;
    cl_paint_fill_rect(ctx, g_rect, g_color);
}

static const cl_widget_vtable_t solid_vtable = {
    .paint = solid_paint,
};

static const cl_widget_class_t solid_class = {
    .name = "test_solid",
    .instance_size = sizeof(cl_widget_t),
    .vtable = &solid_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

/* ---- pixel readback (channels via the pixmap's own masks) ----------------- */

static int shift_of(uint32_t m)
{
    int s = 0;

    if (!m)
        return 0;
    while (!(m & 1u)) {
        m >>= 1;
        s++;
    }
    return s;
}

static uint32_t px_at(const cl_pixmap_t *fb, int x, int y)
{
    const uint32_t *row =
        (const uint32_t *)((const unsigned char *)fb->pixels +
                           (size_t)y * (size_t)fb->pitch);

    return row[x];
}

/* True when the pixel's R/G/B (decoded through the surface masks) match c. */
static bool px_is(const cl_pixmap_t *fb, int x, int y, cl_color_t c)
{
    uint32_t p = px_at(fb, x, y);

    return (int)((p & fb->r_mask) >> shift_of(fb->r_mask)) == c.r &&
           (int)((p & fb->g_mask) >> shift_of(fb->g_mask)) == c.g &&
           (int)((p & fb->b_mask) >> shift_of(fb->b_mask)) == c.b;
}

/* ---- log capture (used by the misaligned-buffer test) --------------------- */

static char cap_log[192];
static void cap_log_sink(cl_log_level_t level, const char *msg, void *user)
{
    (void)level;
    (void)user;
    snprintf(cap_log, sizeof(cap_log), "%s", msg);
}

/* Create an app whose software renderer targets *fb, plus a window sized to it,
 * and render one frame that fills g_rect with g_color over the theme clear. */
static cl_application_t *render_one(const cl_pixmap_t *fb, int w, int h,
                                    cl_color_t *bg_out)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS, .width = w,
                            .height = h };
    cl_application_t *app;
    cl_window_t *win;

    ad.platform = cl_platform_fb_create(cl_allocator_default(), fb);
    ad.render_backend = CL_RENDER_SOFTWARE;
    app = cl_application_create(&ad);
    if (!app)
        return NULL;
    if (bg_out)
        *bg_out = cl_theme_color(cl_application_theme(app), CL_COLOR_BACKGROUND);
    win = cl_window_create(app, &wd);
    CHECK(win != NULL);
    cl_window_set_content(win, cl_widget_alloc(app, &solid_class));
    cl_window_show(win);
    cl_application_step(app, false);
    return app;
}

/* ---- F1: clear colour + a filled rect at known coordinates ---------------- */

static void test_fill_and_clear(void)
{
    enum { W = 40, H = 30 };
    uint32_t *buf = malloc((size_t)W * H * 4);
    cl_pixmap_t fb;
    cl_application_t *app;
    cl_color_t bg;

    CHECK(buf != NULL);
    if (!buf)
        return;
    memset(&fb, 0, sizeof(fb));
    fb.pixels = buf;
    fb.w = W;
    fb.h = H;
    fb.pitch = W * 4;
    CHECK(cl_pixmap_set_format(&fb, CL_PIXEL_ARGB8888));

    g_color = (cl_color_t){ 200, 100, 50, 255 };
    g_rect = (cl_rect_t){ 10, 8, 20, 14 }; /* x 10..30, y 8..22 */

    app = render_one(&fb, W, H, &bg);
    CHECK(app != NULL);
    if (app) {
        CHECK(px_is(&fb, 20, 15, g_color));  /* inside the rect */
        CHECK(px_is(&fb, 11, 9, g_color));   /* just inside the corner */
        CHECK(px_is(&fb, 2, 2, bg));         /* outside: theme clear */
        CHECK(px_is(&fb, 38, 28, bg));       /* far corner: theme clear */
        CHECK(px_is(&fb, 20, 2, bg));        /* above the rect */
        cl_application_destroy(app);
    }
    free(buf);
}

/* ---- F2: every common channel layout round-trips through the masks -------- */

static void test_formats(void)
{
    static const struct {
        cl_pixel_format_t fmt;
        const char *name;
    } cases[] = {
        { CL_PIXEL_ARGB8888, "ARGB8888" }, { CL_PIXEL_XRGB8888, "XRGB8888" },
        { CL_PIXEL_ABGR8888, "ABGR8888" }, { CL_PIXEL_XBGR8888, "XBGR8888" },
        { CL_PIXEL_RGBA8888, "RGBA8888" }, { CL_PIXEL_BGRA8888, "BGRA8888" },
    };
    enum { W = 8, H = 8 };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint32_t *buf = malloc((size_t)W * H * 4);
        cl_pixmap_t fb;
        cl_application_t *app;

        CHECK(buf != NULL);
        if (!buf)
            continue;
        memset(&fb, 0, sizeof(fb));
        fb.pixels = buf;
        fb.w = W;
        fb.h = H;
        fb.pitch = W * 4;
        CHECK(cl_pixmap_set_format(&fb, cases[i].fmt));

        /* An asymmetric colour (r != g != b) catches any channel swap. */
        g_color = (cl_color_t){ 200, 100, 50, 255 };
        g_rect = (cl_rect_t){ 0, 0, W, H };

        app = render_one(&fb, W, H, NULL);
        CHECK(app != NULL);
        if (app) {
            if (!px_is(&fb, 4, 4, g_color))
                fprintf(stderr, "FAIL format %s at (4,4): 0x%08lx\n",
                        cases[i].name,
                        (unsigned long)px_at(&fb, 4, 4));
            CHECK(px_is(&fb, 4, 4, g_color));
            CHECK(px_is(&fb, 0, 0, g_color));
            CHECK(px_is(&fb, W - 1, H - 1, g_color));
            cl_application_destroy(app);
        }
        free(buf);
    }
}

/* ---- F2: a misaligned framebuffer drops frames (with a diagnostic), no UB - */

static void test_misaligned(void)
{
    enum { W = 16, H = 12 };
    /* +8 headroom: the +1 offset makes the base un-aligned without running the
     * (dropped) frame past the allocation. */
    unsigned char *raw = malloc((size_t)W * H * 4 + 8);
    cl_pixmap_t fb;
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS, .width = W,
                            .height = H };
    cl_application_t *app;
    cl_window_t *win;

    CHECK(raw != NULL);
    if (!raw)
        return;
    memset(&fb, 0, sizeof(fb));
    fb.pixels = raw + 1; /* deliberately NOT 4-byte aligned */
    fb.w = W;
    fb.h = H;
    fb.pitch = W * 4;
    CHECK(cl_pixmap_set_format(&fb, CL_PIXEL_ARGB8888));

    ad.platform = cl_platform_fb_create(cl_allocator_default(), &fb);
    CHECK(ad.platform != NULL); /* create accepts it: pitch/size are valid */
    ad.render_backend = CL_RENDER_SOFTWARE;
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    win = app ? cl_window_create(app, &wd) : NULL;
    CHECK(win != NULL);

    g_color = (cl_color_t){ 10, 20, 30, 255 };
    g_rect = (cl_rect_t){ 0, 0, W, H };
    cap_log[0] = '\0';
    cl_set_log_callback(cap_log_sink, NULL);
    if (win) {
        cl_window_set_content(win, cl_widget_alloc(app, &solid_class));
        cl_window_show(win);
        cl_application_step(app, false); /* frame dropped: misaligned base */
    }
    /* The software renderer reported the drop exactly once (task D) rather than
     * dereferencing a misaligned uint32 pointer. */
    CHECK(strstr(cap_log, "align") != NULL);
    cl_set_log_callback(NULL, NULL);

    if (app)
        cl_application_destroy(app); /* no crash under ASan */
    free(raw);
}

/* ---- create-time validation of the framebuffer ---------------------------- */

static void test_bad_framebuffer(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    uint32_t px = 0;
    cl_pixmap_t fb;

    memset(&fb, 0, sizeof(fb));
    CHECK(cl_platform_fb_create(a, NULL) == NULL); /* NULL desc */

    fb.pixels = NULL;
    fb.w = 4;
    fb.h = 4;
    fb.pitch = 16;
    CHECK(cl_platform_fb_create(a, &fb) == NULL); /* NULL pixels */

    fb.pixels = &px;
    fb.w = 0;
    CHECK(cl_platform_fb_create(a, &fb) == NULL); /* w == 0 */

    fb.w = 4;
    fb.pitch = 8; /* < w * 4 */
    CHECK(cl_platform_fb_create(a, &fb) == NULL);
    CHECK(cl_last_error() == CL_ERROR_INVALID_ARGUMENT);

    CHECK(!cl_pixmap_set_format(&fb, (cl_pixel_format_t)999)); /* unknown */
}

int main(void)
{
    test_fill_and_clear();
    test_formats();
    test_misaligned();
    test_bad_framebuffer();

    if (failures == 0)
        printf("all framebuffer backend tests passed\n");
    return failures == 0 ? 0 : 1;
}
