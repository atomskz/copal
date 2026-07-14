/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * GL golden test: render the same primitives with the OpenGL and the software
 * renderer and compare read-back pixels. The software rasterizer is already
 * pixel-verified (test_soft.c), so it is the reference. Interior/solid pixels
 * must match within a small tolerance; anti-aliased edges are not compared.
 *
 * Needs a usable GL 3.3 context. Where none can be created (a headless CI box
 * without a GL-capable display) the test skips itself (exit 77).
 */
#include <copal/copal.h>

#include <SDL.h>
#include <GL/gl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"
#include "render/renderer.h"
#include "render/soft/renderer_soft.h"
#include "render/gl/renderer_gl.h"
#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"

#define W 64
#define H 48

static int failures;

/* The GL renderer resolves entry points through the platform's gl_get_proc. */
static void *gl_getproc(cl_platform_t *p, const char *name)
{
    (void)p;
    return SDL_GL_GetProcAddress(name);
}
static const cl_platform_ops_t gl_ops = { .gl_get_proc = gl_getproc };
/* File scope: the GL renderer keeps this pointer for later gl_get_proc calls. */
static cl_platform_t gl_plat = { &gl_ops };

/* The software renderer draws into a plain 32-bit (ARGB8888) buffer. */
typedef struct {
    cl_platform_t base;
    uint32_t *px;
} soft_stub_t;

static bool soft_lock(cl_platform_t *p, cl_platform_window_t *w, cl_pixmap_t *o)
{
    soft_stub_t *s = (soft_stub_t *)p;

    (void)w;
    o->pixels = s->px;
    o->w = W;
    o->h = H;
    o->pitch = W * 4;
    o->r_mask = 0x00FF0000u;
    o->g_mask = 0x0000FF00u;
    o->b_mask = 0x000000FFu;
    o->a_mask = 0xFF000000u;
    return true;
}
static void soft_unlock(cl_platform_t *p, cl_platform_window_t *w)
{
    (void)p;
    (void)w;
}
static const cl_platform_ops_t soft_ops = { .lock_framebuffer = soft_lock,
                                            .unlock_framebuffer = soft_unlock };

static unsigned char glpx[W * H * 4];
static uint32_t swbuf[W * H];
static cl_renderer_t *g_gl, *g_sw;
static const cl_color_t BG = { 10, 20, 30, 255 };
static const cl_color_t FILL = { 200, 100, 50, 255 };

/* GL read-back is bottom-up; convert to top-down (x, y). */
static void gl_rgb(int x, int y, int *r, int *g, int *b)
{
    const unsigned char *p = glpx + ((size_t)(H - 1 - y) * W + (size_t)x) * 4;

    *r = p[0];
    *g = p[1];
    *b = p[2];
}
static void sw_rgb(int x, int y, int *r, int *g, int *b)
{
    uint32_t p = swbuf[(size_t)y * W + (size_t)x];

    *r = (int)((p >> 16) & 0xFFu);
    *g = (int)((p >> 8) & 0xFFu);
    *b = (int)(p & 0xFFu);
}

static int diff(int a, int b)
{
    return a > b ? a - b : b - a;
}

/* Compare GL vs software at (x, y) within `tol` per channel. */
static void cmp(int x, int y, int tol, const char *what)
{
    int gr, gg, gb, sr, sg, sb;

    gl_rgb(x, y, &gr, &gg, &gb);
    sw_rgb(x, y, &sr, &sg, &sb);
    if (diff(gr, sr) > tol || diff(gg, sg) > tol || diff(gb, sb) > tol) {
        fprintf(stderr, "FAIL %s @(%d,%d): GL %d,%d,%d vs SW %d,%d,%d\n", what, x,
                y, gr, gg, gb, sr, sg, sb);
        failures++;
    }
}

typedef void (*prim_fn)(cl_renderer_t *r, void *user);

static void render_both(prim_fn p, void *user)
{
    g_gl->ops->begin_frame(g_gl, (cl_size_t){ W, H }, 1.0f, BG);
    p(g_gl, user);
    g_gl->ops->end_frame(g_gl);
    glFinish();
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, glpx);

    g_sw->ops->begin_frame(g_sw, (cl_size_t){ W, H }, 1.0f, BG);
    p(g_sw, user);
    g_sw->ops->end_frame(g_sw);
}

static void p_fill(cl_renderer_t *r, void *u)
{
    (void)u;
    r->ops->fill_rect(r, (cl_rect_t){ 10, 10, 24, 20 }, FILL);
}
static void p_round(cl_renderer_t *r, void *u)
{
    (void)u;
    r->ops->fill_round_rect(r, (cl_rect_t){ 8, 8, 34, 28 }, 8.0f, FILL);
}
static void p_xform(cl_renderer_t *r, void *u)
{
    (void)u;
    r->ops->push_transform(r, (cl_point_t){ 8, 4 }, 2.0f);
    r->ops->fill_rect(r, (cl_rect_t){ 2, 2, 8, 8 }, FILL); /* -> 12,8..28,24 */
    r->ops->pop_transform(r);
}
static void p_opacity(cl_renderer_t *r, void *u)
{
    (void)u;
    r->ops->push_opacity(r, 0.5f);
    r->ops->fill_rect(r, (cl_rect_t){ 0, 0, W, H },
                      (cl_color_t){ 255, 255, 255, 255 });
    r->ops->pop_opacity(r);
}
static void p_image(cl_renderer_t *r, void *u)
{
    r->ops->draw_image(r, (cl_image_t *)u, (cl_rect_t){ 16, 16, 20, 16 });
}
static void p_text(cl_renderer_t *r, void *u)
{
    r->ops->draw_text(r, (cl_font_t *)u, "8", (cl_point_t){ 8, 6 },
                      (cl_color_t){ 255, 255, 255, 255 });
}

static int gl_sanity(void)
{
    int r, g, b;

    g_gl->ops->begin_frame(g_gl, (cl_size_t){ W, H }, 1.0f, BG);
    g_gl->ops->end_frame(g_gl);
    glFinish();
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, glpx);
    gl_rgb(1, 1, &r, &g, &b);
    return diff(r, BG.r) <= 6 && diff(g, BG.g) <= 6 && diff(b, BG.b) <= 6;
}

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    SDL_Window *win;
    SDL_GLContext ctx;
    soft_stub_t sstub;
    cl_application_t *res_app; /* owns image/font resources */
    cl_image_t *img;
    cl_font_t *font;
    unsigned char rgba[8 * 8 * 4];
    int i;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("skip: SDL_Init: %s\n", SDL_GetError());
        return 77;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    win = SDL_CreateWindow("gl_golden", 0, 0, W, H,
                           SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) {
        printf("skip: window: %s\n", SDL_GetError());
        SDL_Quit();
        return 77;
    }
    ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        printf("skip: GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 77;
    }

    g_gl = cl_renderer_gl_create(a, &gl_plat);
    if (!g_gl) {
        printf("skip: GL renderer create failed\n");
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 77;
    }

    memset(&sstub, 0, sizeof sstub);
    sstub.base.ops = &soft_ops;
    sstub.px = swbuf;
    g_sw = cl_renderer_soft_create(a, &sstub.base);

    if (!g_sw || !gl_sanity()) {
        printf("skip: GL not rendering in this environment\n");
        return 77;
    }

    /* resources: a solid-red image and (optionally) a system font */
    for (i = 0; i < 8 * 8; i++) {
        rgba[i * 4 + 0] = 220;
        rgba[i * 4 + 1] = 40;
        rgba[i * 4 + 2] = 40;
        rgba[i * 4 + 3] = 255;
    }
    {
        cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;

        ad.platform = cl_platform_mock_create(a);
        ad.renderer = cl_renderer_mock_create(a);
        res_app = cl_application_create(&ad);
    }
    img = cl_image_create(res_app, 8, 8, rgba);
    font = cl_font_load_system(res_app, 16.0f);

    /* fill_rect: solid interior matches; a bg point stays bg. */
    render_both(p_fill, NULL);
    cmp(20, 15, 3, "fill interior");
    cmp(2, 2, 3, "fill bg");

    /* round rect: centre filled. */
    render_both(p_round, NULL);
    cmp(24, 20, 3, "round centre");

    /* transform (translate+scale): rect maps to 12,8..28,24. */
    render_both(p_xform, NULL);
    cmp(18, 14, 3, "xform interior");
    cmp(2, 2, 3, "xform bg");

    /* group opacity: white at 0.5 over the bg blends to the same value. */
    render_both(p_opacity, NULL);
    cmp(32, 24, 8, "opacity blend");

    /* image: solid-red interior matches. */
    if (img) {
        render_both(p_image, img);
        cmp(24, 22, 3, "image interior");
        cmp(2, 2, 3, "image bg");
    }

    /* text: no exact match (AA differs), but both must mark some coverage in
     * the glyph box while leaving a far corner at bg. */
    if (font) {
        int gr, gg, gb, sr, sg, sb, gl_changed = 0, sw_changed = 0, x, y;

        render_both(p_text, font);
        for (y = 0; y < 20; y++) {
            for (x = 0; x < 20; x++) {
                gl_rgb(x, y, &gr, &gg, &gb);
                sw_rgb(x, y, &sr, &sg, &sb);
                if (diff(gr, BG.r) > 20 || diff(gg, BG.g) > 20)
                    gl_changed++;
                if (diff(sr, BG.r) > 20 || diff(sg, BG.g) > 20)
                    sw_changed++;
            }
        }
        if (gl_changed == 0 || sw_changed == 0) {
            fprintf(stderr, "FAIL text: GL changed=%d SW changed=%d\n",
                    gl_changed, sw_changed);
            failures++;
        }
    }

    if (img)
        cl_image_release(img);
    if (font)
        cl_font_release(font);
    cl_application_destroy(res_app);
    g_sw->ops->destroy(g_sw);
    g_gl->ops->destroy(g_gl);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();

    if (failures == 0)
        printf("GL golden: all primitives match the software reference\n");
    return failures ? 1 : 0;
}
