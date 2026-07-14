/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Headless micro-benchmarks for copal, driven through the mock and software
 * backends so they run anywhere (no window, no GPU). Each case auto-scales its
 * iteration count to ~0.1 s of wall clock and reports microseconds per op.
 *
 * Groups: pixel cost (software renderer + a stub framebuffer), widget/layout/
 * input cost (a window on the mock renderer, exercising the real paint, layout
 * and event walk), and text measurement (font advance path). The text cases
 * need a system font: set COPAL_FONT=/path/to/font.ttf, otherwise they skip.
 */
#include <copal/copal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/app_internal.h" /* cl_window_render/mark_dirty/resize, app->platform */
#include "platform/platform.h"
#include "platform/mock/platform_mock.h"
#include "render/renderer.h"
#include "render/mock/renderer_mock.h"
#include "render/soft/renderer_soft.h"

/* ---- timer --------------------------------------------------------------- */
#if defined(_WIN32)
#include <windows.h>
static double now_sec(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
#else
#include <time.h>
static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

typedef void (*bench_fn)(void *ctx);

/* Run `fn` until at least 0.1 s has elapsed; print and return us/op. */
static double run(const char *name, bench_fn fn, void *ctx)
{
    long iters = 1;
    double secs = 0.0, us;

    fn(ctx); /* warm up */
    for (;;) {
        double t0 = now_sec();
        long i;

        for (i = 0; i < iters; i++)
            fn(ctx);
        secs = now_sec() - t0;
        if (secs >= 0.1 || iters >= (1L << 30))
            break;
        iters *= 2;
    }
    us = secs / (double)iters * 1e6;
    printf("  %-34s %11.3f us/op  %12.0f ops/s\n", name, us,
           (double)iters / secs);
    return us;
}

/* ---- window/tree helpers (mock backend) ---------------------------------- */

static cl_application_t *make_window_app(cl_window_t **win_out, int w, int h)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS, .width = w, .height = h };
    cl_application_t *app;

    ad.platform = cl_platform_mock_create(a);
    ad.renderer = cl_renderer_mock_create(a);
    app = cl_application_create(&ad);
    *win_out = cl_window_create(app, &wd);
    cl_window_show(*win_out);
    return app;
}

/* A vbox of `n` fixed-size panels - a flat, realistic paint/layout workload. */
static cl_widget_t *panel_column(cl_application_t *app, int n)
{
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS, .spacing = 1.0f };
    cl_panel_desc_t pd = { CL_PANEL_DESC_INIT_FIELDS };
    cl_spacer_desc_t sd = { CL_SPACER_DESC_INIT_FIELDS, .width = 100.0f,
                            .height = 8.0f };
    cl_widget_t *box = cl_vbox_create(app, &vd);
    int i;

    for (i = 0; i < n; i++) {
        cl_widget_t *p = cl_panel_create(app, &pd);

        cl_widget_add_child(p, cl_spacer_create(app, &sd));
        cl_widget_add_child(box, p);
    }
    return box;
}

struct win_ctx {
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *root;
    cl_widget_t *leaf; /* one small widget, for the damage case */
    int toggle;
};

static void body_full_paint(void *c)
{
    struct win_ctx *w = c;

    cl_window_mark_dirty(w->win);
    cl_window_render(w->win);
}

static void body_relayout(void *c)
{
    struct win_ctx *w = c;

    cl_widget_invalidate_layout(w->root);
    cl_window_render(w->win);
}

static void body_damage_one(void *c)
{
    struct win_ctx *w = c;

    cl_widget_invalidate(w->leaf);
    cl_window_render(w->win);
}

static void body_resize(void *c)
{
    struct win_ctx *w = c;

    w->toggle ^= 1;
    cl_window_resize(w->win, (cl_size_t){ 400.0f, w->toggle ? 3000.0f : 2999.0f });
    cl_window_render(w->win);
}

static void body_mouse_move(void *c)
{
    struct win_ctx *w = c;
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.kind = CL_PEV_MOUSE_MOVE;
    ev.pos.x = 50.0f;
    ev.pos.y = (float)(w->toggle++ % 2000);
    cl_platform_mock_push(w->app->platform, ev);
    cl_application_step(w->app, false);
}

/* ---- benches: widget / layout / input ------------------------------------ */

static void bench_many_widgets(void)
{
    cl_window_t *win;
    cl_application_t *app = make_window_app(&win, 400, 6000);
    struct win_ctx c = { app, win, NULL, NULL, 0 };

    c.root = panel_column(app, 300);
    cl_window_set_content(win, c.root);
    cl_window_render(win);
    run("many_widgets (300, paint)", body_full_paint, &c);
    cl_application_destroy(app);
}

static void bench_deep_layout(void)
{
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS, .padding = { 1, 1, 1, 1 } };
    cl_window_t *win;
    cl_application_t *app = make_window_app(&win, 400, 400);
    cl_widget_t *root = cl_vbox_create(app, &vd);
    cl_widget_t *cur = root;
    struct win_ctx c = { app, win, root, NULL, 0 };
    int i;

    for (i = 0; i < 60; i++) { /* nested, well under the depth cap */
        cl_widget_t *n = cl_vbox_create(app, &vd);

        cl_widget_add_child(cur, n);
        cur = n;
    }
    cl_window_set_content(win, root);
    cl_window_render(win);
    run("deep_layout (60 nested, relayout)", body_relayout, &c);
    cl_application_destroy(app);
}

static void bench_resize(void)
{
    cl_window_t *win;
    cl_application_t *app = make_window_app(&win, 400, 3000);
    struct win_ctx c = { app, win, NULL, NULL, 0 };

    c.root = panel_column(app, 150);
    cl_window_set_content(win, c.root);
    cl_window_render(win);
    run("resize + relayout (150)", body_resize, &c);
    cl_application_destroy(app);
}

static void bench_input(void)
{
    cl_window_t *win;
    cl_application_t *app = make_window_app(&win, 400, 6000);
    struct win_ctx c = { app, win, NULL, NULL, 0 };

    c.root = panel_column(app, 300);
    cl_window_set_content(win, c.root);
    cl_window_render(win);
    run("input mouse-move (300 widgets)", body_mouse_move, &c);
    cl_application_destroy(app);
}

static void bench_damage_vs_full(void)
{
    cl_window_t *win;
    cl_application_t *app = make_window_app(&win, 400, 6000);
    struct win_ctx c = { app, win, NULL, NULL, 0 };
    cl_widget_t *ch;
    double full, dmg;
    int i;

    c.root = panel_column(app, 300);
    cl_window_set_content(win, c.root);
    cl_window_render(win);
    ch = c.root->first_child; /* pick a panel near the middle */
    for (i = 0; i < 150 && ch && ch->next_sibling; i++)
        ch = ch->next_sibling;
    c.leaf = ch;
    full = run("damage: full redraw (300)", body_full_paint, &c);
    dmg = run("damage: one widget (300)", body_damage_one, &c);
    printf("  -> damage speedup vs full: %.2fx\n", full / dmg);
    cl_application_destroy(app);
}

/* ---- pixel benches (software renderer + stub framebuffer) ---------------- */

typedef struct stub_platform {
    cl_platform_t base;
    uint32_t *px;
    int w, h;
} stub_platform_t;

static bool stub_lock(cl_platform_t *p, cl_platform_window_t *win,
                      cl_pixmap_t *out)
{
    stub_platform_t *s = (stub_platform_t *)p;

    (void)win;
    out->pixels = s->px;
    out->w = s->w;
    out->h = s->h;
    out->pitch = s->w * 4;
    out->r_mask = 0x00FF0000u;
    out->g_mask = 0x0000FF00u;
    out->b_mask = 0x000000FFu;
    out->a_mask = 0xFF000000u;
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

struct pix_ctx {
    cl_renderer_t *r;
    int w, h;
};

static void body_empty_frame(void *c)
{
    struct pix_ctx *p = c;
    cl_color_t bg = { 20, 24, 28, 255 };

    p->r->ops->begin_frame(p->r, (cl_size_t){ (float)p->w, (float)p->h }, 1.0f,
                           bg);
    p->r->ops->end_frame(p->r);
}

static void body_fullscreen(void *c)
{
    struct pix_ctx *p = c;
    cl_color_t bg = { 20, 24, 28, 255 };
    cl_color_t fg = { 200, 120, 60, 200 }; /* alpha -> per-pixel blend */

    p->r->ops->begin_frame(p->r, (cl_size_t){ (float)p->w, (float)p->h }, 1.0f,
                           bg);
    p->r->ops->fill_rect(p->r, (cl_rect_t){ 0, 0, (float)p->w, (float)p->h },
                         fg);
    p->r->ops->end_frame(p->r);
}

static void bench_pixels(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    int w = 800, h = 600;
    uint32_t *buf = malloc((size_t)w * (size_t)h * 4);
    stub_platform_t stub;
    struct pix_ctx c;

    if (!buf)
        return;
    memset(&stub, 0, sizeof stub);
    stub.base.ops = &stub_ops;
    stub.px = buf;
    stub.w = w;
    stub.h = h;
    c.r = cl_renderer_soft_create(a, &stub.base);
    c.w = w;
    c.h = h;
    if (!c.r) {
        free(buf);
        return;
    }
    run("empty_frame (800x600 clear)", body_empty_frame, &c);
    run("fullscreen_frame (800x600 blend)", body_fullscreen, &c);
    c.r->ops->destroy(c.r);
    free(buf);
}

/* ---- text benches (need a font) ------------------------------------------ */

/* Latin + Cyrillic (both cached today) + CJK + symbols (uncached today). */
static const char mixed_text[] =
    "The quick brown fox 1234567890 "
    "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 \xd0\xbc\xd0\xb8\xd1\x80 "
    "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c "
    "\xe2\x86\x92\xe2\x98\x85\xe2\x9c\x93\xe2\x88\x91 "
    "The quick brown fox jumps over the lazy dog.";

struct text_ctx {
    cl_font_t *font;
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *tb;
    int step;
};

static void body_measure(void *c)
{
    struct text_ctx *t = c;

    (void)cl_text_measure(t->font, mixed_text, CL_UNBOUNDED);
}

static void body_text_edit(void *c)
{
    struct text_ctx *t = c;
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof ev);
    if ((t->step++ & 15) == 15) {
        ev.kind = CL_PEV_KEY_DOWN;
        ev.key = CL_KEY_BACKSPACE;
    } else {
        ev.kind = CL_PEV_TEXT_INPUT;
        ev.text[0] = (char)('a' + (t->step & 7));
    }
    cl_platform_mock_push(t->app->platform, ev);
    cl_application_step(t->app, false);
}

static void bench_text(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_application_t *app;
    cl_font_t *font;
    struct text_ctx c;

    ad.platform = cl_platform_mock_create(a);
    ad.renderer = cl_renderer_mock_create(a);
    app = cl_application_create(&ad);
    font = cl_font_load_system(app, 16.0f);
    if (!font) {
        printf("  text_*                             SKIPPED (set COPAL_FONT)\n");
        cl_application_destroy(app);
        return;
    }
    cl_theme_set_font(cl_application_theme(app), font);

    c.font = font;
    run("text_measure_mixed (Latin/Cyr/CJK/sym)", body_measure, &c);

    /* text edit: type into a single-line textbox through the event loop */
    {
        cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS, .width = 400,
                                .height = 100 };
        cl_textbox_desc_t td = { CL_TEXTBOX_DESC_INIT_FIELDS };

        c.app = app;
        c.win = cl_window_create(app, &wd);
        c.tb = cl_textbox_create(app, &td);
        c.step = 0;
        cl_window_set_content(c.win, c.tb);
        cl_widget_focus(c.tb);
        cl_window_show(c.win);
        cl_window_render(c.win);
        run("text_edit (type/backspace)", body_text_edit, &c);
    }
    cl_font_release(font);
    cl_application_destroy(app);
}

/* ---- scroll -------------------------------------------------------------- */

struct scroll_ctx {
    cl_application_t *app;
    cl_window_t *win;
    int dir;
};

static void body_scroll(void *c)
{
    struct scroll_ctx *s = c;
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.kind = CL_PEV_MOUSE_WHEEL;
    ev.pos.x = 50.0f;
    ev.pos.y = 50.0f;
    ev.wheel_y = (s->dir ^= 1) ? -1.0f : 1.0f;
    cl_platform_mock_push(s->app->platform, ev);
    cl_application_step(s->app, false);
}

static void bench_scroll(void)
{
    cl_window_t *win;
    cl_application_t *app = make_window_app(&win, 200, 200);
    cl_scrollview_desc_t svd = { CL_SCROLLVIEW_DESC_INIT_FIELDS };
    cl_widget_t *sv = cl_scrollview_create(app, &svd);
    struct scroll_ctx c = { app, win, 0 };

    cl_scrollview_set_content(sv, panel_column(app, 300));
    cl_window_set_content(win, sv);
    cl_window_show(win);
    cl_window_render(win);
    run("scroll (wheel, 300-tall content)", body_scroll, &c);
    cl_application_destroy(app);
}

/* churn body needs the app; defined after the struct is known */
static void body_churn(void *c)
{
    cl_application_t *app = *(cl_application_t **)c;
    cl_panel_desc_t pd = { CL_PANEL_DESC_INIT_FIELDS };
    int i;

    for (i = 0; i < 100; i++)
        cl_widget_destroy(cl_panel_create(app, &pd));
}

int main(void)
{
    printf("copal benchmarks (%s)\n", cl_version_string());
    printf("pixel (software renderer):\n");
    bench_pixels();
    printf("widget / layout / input (mock renderer):\n");
    bench_many_widgets();
    bench_deep_layout();
    bench_resize();
    bench_input();
    bench_scroll();
    bench_damage_vs_full();
    {
        const cl_allocator_t *a = cl_allocator_default();
        cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
        cl_application_t *app;

        ad.platform = cl_platform_mock_create(a);
        ad.renderer = cl_renderer_mock_create(a);
        app = cl_application_create(&ad);
        run("resource_churn (100 create+destroy)", body_churn, &app);
        cl_application_destroy(app);
    }
    printf("text:\n");
    bench_text();
    return 0;
}
