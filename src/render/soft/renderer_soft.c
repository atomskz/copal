/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "render/soft/renderer_soft.h"
#include "text/font_internal.h"
#include "core/foundation/foundation_internal.h"

#include "stb_truetype.h"

#include <math.h>
#include <string.h>

#define SOFT_MAX_GLYPHS 512
#define SOFT_GLYPH_HASH 1024 /* 2x slots, power of two */
#define SOFT_CLIP_STACK 16

typedef struct soft_glyph {
    cl_font_t *font;
    uint32_t cp;
    unsigned char *cov; /* w*h 8-bit coverage, owned, or NULL for blank glyphs */
    int w, h;
    int xoff, yoff;
    float advance;
} soft_glyph_t;

typedef struct soft_renderer {
    cl_renderer_t base;
    const cl_allocator_t *a;
    cl_platform_t *platform;
    /* Current frame target; valid only between begin_frame and end_frame. */
    unsigned char *px;
    int w, h, pitch;
    int rsh, gsh, bsh, ash; /* channel shifts derived from the surface masks */
    bool has_alpha;
    float scale; /* logical -> physical pixel scale */
    int clip_depth;
    cl_rect_t clip_stack[SOFT_CLIP_STACK]; /* physical px, already intersected */
    soft_glyph_t glyphs[SOFT_MAX_GLYPHS];
    int glyph_count;
    uint16_t glyph_hash[SOFT_GLYPH_HASH]; /* index + 1 into glyphs; 0 = empty */
} soft_renderer_t;

/* ---- small math helpers -------------------------------------------------- */

static int mask_shift(uint32_t m)
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

static float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

static float smoothstep01(float e0, float e1, float x)
{
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);

    return t * t * (3.0f - 2.0f * t);
}

/* Signed distance to a rounded box centred at the origin (half-size b, radius
 * r); ported from the GL renderer's SDF fragment shader. */
static float sd_round_box(float px, float py, float bx, float by, float r)
{
    float qx = fabsf(px) - bx + r;
    float qy = fabsf(py) - by + r;
    float mx = qx > 0.0f ? qx : 0.0f;
    float my = qy > 0.0f ? qy : 0.0f;
    float outer = sqrtf(mx * mx + my * my);
    float inner = qx > qy ? qx : qy;

    if (inner > 0.0f)
        inner = 0.0f;
    return inner + outer - r;
}

/* ---- clipping ------------------------------------------------------------ */

static cl_rect_t rect_intersect(cl_rect_t a, cl_rect_t b)
{
    float x0 = a.x > b.x ? a.x : b.x;
    float y0 = a.y > b.y ? a.y : b.y;
    float x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    float y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    cl_rect_t out = { x0, y0, x1 - x0, y1 - y0 };

    if (out.w < 0.0f)
        out.w = 0.0f;
    if (out.h < 0.0f)
        out.h = 0.0f;
    return out;
}

static cl_rect_t clip_cur(const soft_renderer_t *r)
{
    int top = r->clip_depth < SOFT_CLIP_STACK ? r->clip_depth : SOFT_CLIP_STACK;

    if (top == 0)
        return (cl_rect_t){ 0.0f, 0.0f, (float)r->w, (float)r->h };
    return r->clip_stack[top - 1];
}

/* Integer clip bounds, clamped to the framebuffer. */
static void clip_ibounds(const soft_renderer_t *r, int *x0, int *y0, int *x1,
                         int *y1)
{
    cl_rect_t c = clip_cur(r);

    *x0 = (int)floorf(c.x);
    *y0 = (int)floorf(c.y);
    *x1 = (int)ceilf(c.x + c.w);
    *y1 = (int)ceilf(c.y + c.h);
    if (*x0 < 0)
        *x0 = 0;
    if (*y0 < 0)
        *y0 = 0;
    if (*x1 > r->w)
        *x1 = r->w;
    if (*y1 > r->h)
        *y1 = r->h;
}

/* ---- pixel write --------------------------------------------------------- */

/* Source-over blend of `color` at coverage `cov` into physical pixel (ix, iy),
 * which the caller guarantees is inside the framebuffer and the clip. */
static void blend_px(soft_renderer_t *r, int ix, int iy, cl_color_t color,
                     float cov)
{
    float a = (float)color.a / 255.0f * cov;
    uint32_t opaque = r->has_alpha ? (uint32_t)0xFFu << r->ash : 0u;
    uint32_t *p;
    uint32_t d;
    int dr, dg, db;

    if (a <= 0.0f)
        return;
    p = (uint32_t *)(r->px + (size_t)iy * r->pitch + (size_t)ix * 4);
    if (a >= 1.0f) {
        *p = ((uint32_t)color.r << r->rsh) | ((uint32_t)color.g << r->gsh) |
             ((uint32_t)color.b << r->bsh) | opaque;
        return;
    }
    d = *p;
    dr = (int)((d >> r->rsh) & 0xFFu);
    dg = (int)((d >> r->gsh) & 0xFFu);
    db = (int)((d >> r->bsh) & 0xFFu);
    *p = ((uint32_t)(int)((float)color.r * a + (float)dr * (1.0f - a) + 0.5f)
          << r->rsh) |
         ((uint32_t)(int)((float)color.g * a + (float)dg * (1.0f - a) + 0.5f)
          << r->gsh) |
         ((uint32_t)(int)((float)color.b * a + (float)db * (1.0f - a) + 0.5f)
          << r->bsh) |
         opaque;
}

/* ---- frame --------------------------------------------------------------- */

static void soft_begin_frame(cl_renderer_t *rr, cl_size_t size, float scale,
                             cl_color_t clear)
{
    soft_renderer_t *r = (soft_renderer_t *)rr;
    cl_pixmap_t pm;
    uint32_t packed;
    int x, y;

    (void)size;
    r->px = NULL;
    r->clip_depth = 0;
    if (!r->platform->ops->lock_framebuffer ||
        !r->platform->ops->lock_framebuffer(r->platform, &pm))
        return;
    r->px = pm.pixels;
    r->w = pm.w;
    r->h = pm.h;
    r->pitch = pm.pitch;
    /* We access the buffer as uint32_t; a misaligned base/pitch would be UB.
     * Fail closed (like the lock failure) rather than risk it. */
    if (((uintptr_t)r->px & 3u) || (r->pitch & 3)) {
        if (r->platform->ops->unlock_framebuffer)
            r->platform->ops->unlock_framebuffer(r->platform);
        r->px = NULL;
        return;
    }
    r->rsh = mask_shift(pm.r_mask);
    r->gsh = mask_shift(pm.g_mask);
    r->bsh = mask_shift(pm.b_mask);
    r->ash = mask_shift(pm.a_mask);
    r->has_alpha = pm.a_mask != 0;
    r->scale = scale > 0.0f ? scale : 1.0f;

    packed = ((uint32_t)clear.r << r->rsh) | ((uint32_t)clear.g << r->gsh) |
             ((uint32_t)clear.b << r->bsh) |
             (r->has_alpha ? (uint32_t)0xFFu << r->ash : 0u);
    for (y = 0; y < r->h; y++) {
        uint32_t *row = (uint32_t *)(r->px + (size_t)y * r->pitch);

        for (x = 0; x < r->w; x++)
            row[x] = packed;
    }
}

static void soft_end_frame(cl_renderer_t *rr)
{
    soft_renderer_t *r = (soft_renderer_t *)rr;

    if (r->px && r->platform->ops->unlock_framebuffer)
        r->platform->ops->unlock_framebuffer(r->platform);
    r->px = NULL;
}

/* ---- primitives ---------------------------------------------------------- */

static void soft_fill_rect(cl_renderer_t *rr, cl_rect_t rect, cl_color_t color)
{
    soft_renderer_t *r = (soft_renderer_t *)rr;
    int cx0, cy0, cx1, cy1;
    int x0, y0, x1, y1, x, y;

    if (!r->px)
        return;
    clip_ibounds(r, &cx0, &cy0, &cx1, &cy1);
    x0 = (int)floorf(rect.x * r->scale);
    y0 = (int)floorf(rect.y * r->scale);
    x1 = (int)ceilf((rect.x + rect.w) * r->scale);
    y1 = (int)ceilf((rect.y + rect.h) * r->scale);
    if (x0 < cx0)
        x0 = cx0;
    if (y0 < cy0)
        y0 = cy0;
    if (x1 > cx1)
        x1 = cx1;
    if (y1 > cy1)
        y1 = cy1;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            blend_px(r, x, y, color, 1.0f);
}

/* Fill (border <= 0) or stroke a rounded rect via per-pixel SDF coverage. */
static void soft_round(soft_renderer_t *r, cl_rect_t rect, float radius,
                       float border, cl_color_t color)
{
    int cx0, cy0, cx1, cy1;
    int x0, y0, x1, y1, ix, iy;
    float ccx = rect.x + rect.w * 0.5f; /* logical centre */
    float ccy = rect.y + rect.h * 0.5f;
    float hbx = rect.w * 0.5f;
    float hby = rect.h * 0.5f;
    const float aa = 1.0f;

    if (!r->px)
        return;
    clip_ibounds(r, &cx0, &cy0, &cx1, &cy1);
    x0 = (int)floorf(rect.x * r->scale);
    y0 = (int)floorf(rect.y * r->scale);
    x1 = (int)ceilf((rect.x + rect.w) * r->scale);
    y1 = (int)ceilf((rect.y + rect.h) * r->scale);
    if (x0 < cx0)
        x0 = cx0;
    if (y0 < cy0)
        y0 = cy0;
    if (x1 > cx1)
        x1 = cx1;
    if (y1 > cy1)
        y1 = cy1;
    for (iy = y0; iy < y1; iy++) {
        float ly = ((float)iy + 0.5f) / r->scale;

        for (ix = x0; ix < x1; ix++) {
            float lx = ((float)ix + 0.5f) / r->scale;
            float d = sd_round_box(lx - ccx, ly - ccy, hbx, hby, radius);
            float cov;

            if (border <= 0.0f) {
                cov = 1.0f - smoothstep01(0.0f, aa, d);
            } else {
                float outer = 1.0f - smoothstep01(0.0f, aa, d);
                float inner = 1.0f - smoothstep01(0.0f, aa, d + border);

                cov = clampf(outer - inner, 0.0f, 1.0f);
            }
            if (cov > 0.0f)
                blend_px(r, ix, iy, color, cov);
        }
    }
}

static void soft_fill_round_rect(cl_renderer_t *rr, cl_rect_t rect, float radius,
                                 cl_color_t color)
{
    soft_round((soft_renderer_t *)rr, rect, radius, 0.0f, color);
}

static void soft_stroke_round_rect(cl_renderer_t *rr, cl_rect_t rect,
                                   float radius, float width, cl_color_t color)
{
    soft_round((soft_renderer_t *)rr, rect, radius, width, color);
}

/* ---- text ---------------------------------------------------------------- */

static unsigned glyph_hash_of(const cl_font_t *font, uint32_t cp)
{
    uintptr_t f = (uintptr_t)font;
    uint32_t h = (uint32_t)(f ^ (f >> 9));

    h = h * 2654435761u ^ cp * 2246822519u;
    return h & (SOFT_GLYPH_HASH - 1u);
}

/* Free every cached coverage bitmap and start over. Called when the cache
 * fills up: the visible glyph set re-rasterizes within a frame, which beats
 * silently never drawing new glyphs again. */
static void soft_cache_reset(soft_renderer_t *r)
{
    int i;

    for (i = 0; i < r->glyph_count; i++)
        cl_free(r->a, r->glyphs[i].cov);
    r->glyph_count = 0;
    memset(r->glyph_hash, 0, sizeof(r->glyph_hash));
}

static soft_glyph_t *soft_get_glyph(soft_renderer_t *r, cl_font_t *font,
                                    uint32_t cp)
{
    const stbtt_fontinfo *info = cl_font_info(font);
    float scale = cl_font_pixel_scale(font);
    unsigned char *bmp;
    soft_glyph_t *g;
    unsigned slot;
    int w = 0, h = 0, xoff = 0, yoff = 0, adv = 0, lsb = 0;

    for (slot = glyph_hash_of(font, cp); r->glyph_hash[slot];
         slot = (slot + 1) & (SOFT_GLYPH_HASH - 1u)) {
        g = &r->glyphs[r->glyph_hash[slot] - 1];
        if (g->font == font && g->cp == cp)
            return g;
    }
    if (r->glyph_count >= SOFT_MAX_GLYPHS) {
        soft_cache_reset(r);
        slot = glyph_hash_of(font, cp); /* table is empty: first probe wins */
    }

    bmp = stbtt_GetCodepointBitmap(info, 0, scale, (int)cp, &w, &h, &xoff,
                                   &yoff);
    stbtt_GetCodepointHMetrics(info, (int)cp, &adv, &lsb);
    g = &r->glyphs[r->glyph_count++];
    r->glyph_hash[slot] = (uint16_t)r->glyph_count; /* index + 1 */
    memset(g, 0, sizeof(*g));
    g->font = font;
    g->cp = cp;
    g->advance = (float)adv * scale;
    g->xoff = xoff;
    g->yoff = yoff;
    if (bmp && w > 0 && h > 0) {
        g->cov = cl_alloc(r->a, (size_t)w * (size_t)h);
        if (g->cov) {
            memcpy(g->cov, bmp, (size_t)w * (size_t)h);
            g->w = w;
            g->h = h;
        }
    }
    if (bmp)
        stbtt_FreeBitmap(bmp, NULL);
    return g;
}

static void soft_draw_text(cl_renderer_t *rr, cl_font_t *font, const char *utf8,
                           cl_point_t pos, cl_color_t color)
{
    soft_renderer_t *r = (soft_renderer_t *)rr;
    int cx0, cy0, cx1, cy1;
    float penx, baseline;
    size_t i = 0, n;
    uint32_t cp;

    if (!r->px || !font || !utf8)
        return;
    clip_ibounds(r, &cx0, &cy0, &cx1, &cy1);
    baseline = pos.y + cl_font_ascent_px(font);
    penx = pos.x;
    while ((n = cl_utf8_next(utf8 + i, &cp)) != 0) {
        soft_glyph_t *g = soft_get_glyph(r, font, cp);

        i += n;
        if (!g)
            continue; /* unrasterizable: skip it, keep drawing the string */
        if (g->cov) {
            float gx = penx + (float)g->xoff; /* logical top-left of bitmap */
            float gy = baseline + (float)g->yoff;
            int px0 = (int)floorf(gx * r->scale);
            int py0 = (int)floorf(gy * r->scale);
            int px1 = (int)ceilf((gx + (float)g->w) * r->scale);
            int py1 = (int)ceilf((gy + (float)g->h) * r->scale);
            int ix, iy;

            if (px0 < cx0)
                px0 = cx0;
            if (py0 < cy0)
                py0 = cy0;
            if (px1 > cx1)
                px1 = cx1;
            if (py1 > cy1)
                py1 = cy1;
            /* Walk the glyph's physical footprint, sampling the (logical) glyph
             * bitmap per pixel, so text honours the device scale like the SDF
             * primitives do. At scale == 1 this is a straight 1:1 blit. */
            for (iy = py0; iy < py1; iy++) {
                int ty = (int)(((float)iy + 0.5f) / r->scale - gy);

                if (ty < 0 || ty >= g->h)
                    continue;
                for (ix = px0; ix < px1; ix++) {
                    int tx = (int)(((float)ix + 0.5f) / r->scale - gx);
                    float cov;

                    if (tx < 0 || tx >= g->w)
                        continue;
                    cov = (float)g->cov[ty * g->w + tx] / 255.0f;
                    if (cov > 0.0f)
                        blend_px(r, ix, iy, color, cov);
                }
            }
        }
        penx += g->advance;
    }
}

/* ---- clip ops / lifecycle ------------------------------------------------ */

static void soft_push_clip(cl_renderer_t *rr, cl_rect_t rect)
{
    soft_renderer_t *r = (soft_renderer_t *)rr;
    cl_rect_t phys = { rect.x * r->scale, rect.y * r->scale, rect.w * r->scale,
                       rect.h * r->scale };
    cl_rect_t merged = rect_intersect(clip_cur(r), phys);

    if (r->clip_depth < SOFT_CLIP_STACK)
        r->clip_stack[r->clip_depth] = merged;
    r->clip_depth++; /* always advance so a later pop stays balanced */
}

static void soft_pop_clip(cl_renderer_t *rr)
{
    soft_renderer_t *r = (soft_renderer_t *)rr;

    if (r->clip_depth > 0)
        r->clip_depth--;
}

/* Coarse but correct: entries of other fonts simply re-rasterize. */
static void soft_evict_font(cl_renderer_t *rr, cl_font_t *font)
{
    (void)font;
    soft_cache_reset((soft_renderer_t *)rr);
}

static void soft_destroy(cl_renderer_t *rr)
{
    soft_renderer_t *r = (soft_renderer_t *)rr;

    soft_cache_reset(r);
    cl_free(r->a, r);
}

static const cl_renderer_ops_t soft_ops = {
    .begin_frame = soft_begin_frame,
    .end_frame = soft_end_frame,
    .fill_rect = soft_fill_rect,
    .fill_round_rect = soft_fill_round_rect,
    .stroke_round_rect = soft_stroke_round_rect,
    .draw_text = soft_draw_text,
    .push_clip = soft_push_clip,
    .pop_clip = soft_pop_clip,
    .evict_font = soft_evict_font,
    .destroy = soft_destroy,
};

cl_renderer_t *cl_renderer_soft_create(const cl_allocator_t *a, cl_platform_t *p)
{
    soft_renderer_t *r = cl_alloc(a, sizeof(*r));

    if (!r)
        return NULL;
    memset(r, 0, sizeof(*r));
    r->base.ops = &soft_ops;
    r->a = a;
    r->platform = p;
    return &r->base;
}
