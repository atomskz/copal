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

#define SDL_MAIN_HANDLED /* this test owns main(); avoids linking SDL2main */
#include <SDL.h>
#include <SDL_opengl.h> /* pulls in the platform GL header in the right order
                         * (on Windows <GL/gl.h> needs <windows.h> first) */

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
    /* A deliberately fractional pen position (8.5, 6.5): centred labels rarely
     * land on whole pixels, and a fractional origin is exactly what tripped the
     * software glyph blit. Both renderers snap the origin, so they must still
     * agree; an un-snapped blit would shift by a pixel and diverge. */
    r->ops->draw_text(r, (cl_font_t *)u, "8", (cl_point_t){ 8.5f, 6.5f },
                      (cl_color_t){ 255, 255, 255, 255 });
}
/* A 1px rounded border - the primitive every widget frame is drawn with, and
 * the one the GL renderer used to drop the outer sides of. */
static void p_stroke(cl_renderer_t *r, void *u)
{
    (void)u;
    r->ops->stroke_round_rect(r, (cl_rect_t){ 6, 6, 44, 32 }, 6.0f, 1.0f, FILL);
}

/* Whole-frame comparison: count pixels whose GL and software values differ by
 * more than `tol` on any channel. A missing stroke side or smeared glyph shows
 * up as a big contiguous block of such pixels; genuine AA differences at the
 * rasterizers' edges stay well under the caller's budget. */
static int frame_diff(int tol)
{
    int x, y, n = 0;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int gr, gg, gb, sr, sg, sb;

            gl_rgb(x, y, &gr, &gg, &gb);
            sw_rgb(x, y, &sr, &sg, &sb);
            if (diff(gr, sr) > tol || diff(gg, sg) > tol || diff(gb, sb) > tol)
                n++;
        }
    return n;
}

static void cmp_frame(int tol, int budget, const char *what)
{
    int n = frame_diff(tol);

    if (getenv("COPAL_GOLDEN_VERBOSE"))
        fprintf(stderr, "  frame %-14s diff>%d = %3d px (budget %d)\n", what,
                tol, n, budget);
    if (n > budget) {
        fprintf(stderr, "FAIL %s: %d px differ by >%d per channel (budget %d)\n",
                what, n, tol, budget);
        failures++;
    }
}

/* Largest rise of the red channel above the background along a short scan (the
 * stroke colour is far redder than the bg, so a present border lifts it; a
 * missing side leaves it at the background). */
static int scan_max(int use_gl, int x0, int y0, int dx, int dy, int steps)
{
    int i, best = 0;

    for (i = 0; i < steps; i++) {
        int r, g, b, x = x0 + dx * i, y = y0 + dy * i;

        if (x < 0 || y < 0 || x >= W || y >= H)
            continue;
        if (use_gl)
            gl_rgb(x, y, &r, &g, &b);
        else
            sw_rgb(x, y, &r, &g, &b);
        if (r - (int)BG.r > best)
            best = r - (int)BG.r;
    }
    return best;
}

/* Both renderers must draw the OUTER band of a border side - the pixels just
 * beyond the rect edge, scanned AWAY from the shape so the inner half of the
 * stroke (which rasterizes even with the bug) can't mask a missing outer edge.
 * That outer band is exactly what the un-inflated GL geometry used to drop. */
static void check_outer(const char *side, int x0, int y0, int dx, int dy,
                        int steps)
{
    int gl = scan_max(1, x0, y0, dx, dy, steps);
    int sw = scan_max(0, x0, y0, dx, dy, steps);

    if (getenv("COPAL_GOLDEN_VERBOSE"))
        fprintf(stderr, "  outer %-6s GL=%3d SW=%3d\n", side, gl, sw);
    if (gl < 40 || sw < 40) {
        fprintf(stderr,
                "FAIL stroke %s outer edge missing: GL=%d SW=%d (need >=40)\n",
                side, gl, sw);
        failures++;
    }
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

    SDL_SetMainReady(); /* we defined SDL_MAIN_HANDLED */
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

    /* fill_rect: solid interior matches; a bg point stays bg; and the whole
     * frame agrees (a plain fill is hard-edged in both). */
    render_both(p_fill, NULL);
    cmp(20, 15, 3, "fill interior");
    cmp(2, 2, 3, "fill bg");
    cmp_frame(24, 24, "fill");

    /* round rect: centre filled, and the frames now agree closely (both keep
     * fills pixel-tight; only a little corner AA can differ). */
    render_both(p_round, NULL);
    cmp(24, 20, 3, "round centre");
    cmp_frame(24, 40, "round");

    /*
     * Stroke: a 1px rounded border, the primitive every widget frame uses. The
     * GL renderer used to drop the outer sides of thin strokes (the geometry
     * ended exactly at the shape, so the outer edge never rasterized). Assert
     * each of the four sides is clearly present in BOTH renderers, then that
     * the whole frames agree. This is the check that was missing entirely -
     * the old suite never drew a stroke at all.
     */
    render_both(p_stroke, NULL);
    /* Rect is {6,6,44,32}: edges at x=6/50, y=6/38. Scan the outer band of each
     * side, moving away from the shape, so only the outer edge is sampled. */
    check_outer("top", 28, 5, 0, -1, 2);    /* rows 5,4 above the top edge */
    check_outer("bottom", 28, 38, 0, 1, 2); /* rows 38,39 below the bottom */
    check_outer("left", 5, 22, -1, 0, 2);   /* cols 5,4 left of the left edge */
    check_outer("right", 50, 22, 1, 0, 2);  /* cols 50,51 right of the right */
    cmp_frame(40, 64, "stroke");

    /* transform (translate+scale): rect maps to 12,8..28,24. */
    render_both(p_xform, NULL);
    cmp(18, 14, 3, "xform interior");
    cmp(2, 2, 3, "xform bg");
    cmp_frame(24, 40, "xform");

    /* group opacity: white at 0.5 over the bg blends to the same value. */
    render_both(p_opacity, NULL);
    cmp(32, 24, 8, "opacity blend");
    cmp_frame(10, 24, "opacity");

    /* image: solid-red interior matches. */
    if (img) {
        render_both(p_image, img);
        cmp(24, 22, 3, "image interior");
        cmp(2, 2, 3, "image bg");
        cmp_frame(24, 40, "image");
    }

    /*
     * Text: at scale 1 both renderers rasterize the same stb bitmap and now
     * snap the glyph origin to the pixel grid, so the frames should agree
     * closely. A fractional-origin blit (the old software path) smeared and
     * doubled glyph edges, pulling the two apart. Require real coverage in
     * both and a tight whole-frame agreement.
     */
    if (font) {
        int gr, gg, gb, sr, sg, sb, gl_ink = 0, sw_ink = 0, x, y;

        render_both(p_text, font);
        for (y = 0; y < 24; y++) {
            for (x = 0; x < 24; x++) {
                gl_rgb(x, y, &gr, &gg, &gb);
                sw_rgb(x, y, &sr, &sg, &sb);
                if (gr - (int)BG.r > 60)
                    gl_ink++;
                if (sr - (int)BG.r > 60)
                    sw_ink++;
            }
        }
        if (getenv("COPAL_GOLDEN_VERBOSE"))
            fprintf(stderr, "  text ink GL=%d SW=%d\n", gl_ink, sw_ink);
        if (gl_ink == 0 || sw_ink == 0) {
            fprintf(stderr, "FAIL text: GL ink=%d SW ink=%d\n", gl_ink, sw_ink);
            failures++;
        }
        cmp_frame(40, 40, "text");
    }

    /* Context-loss recovery: force a reset, render again, and confirm the
     * renderer tore down and rebuilt its GL objects and still draws correctly. */
    cl_renderer_gl_test_force_reset(g_gl);
    render_both(p_fill, NULL);
    cmp(20, 15, 3, "recovery interior");
    cmp(2, 2, 3, "recovery bg");

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
