/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/font.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_truetype.h"
#include "app/app_internal.h"
#include "core/foundation/foundation_internal.h"
#include "text/font_internal.h"

#define ADV_CACHE_SIZE 512

struct cl_font {
    const cl_allocator_t *a;
    cl_application_t *app; /* weak: reaches the renderer for glyph eviction */
    unsigned char *data;
    stbtt_fontinfo info;
    float px;
    float scale;
    float ascent;
    float descent;
    float line_gap;
    /* Pixel advance per cached codepoint; -1 = uncached (see adv_slot). */
    float adv_cache[ADV_CACHE_SIZE];
};

/* Cache slot for a codepoint: Latin-1 (0..255) and Cyrillic U+0400..U+04FF
 * (256..511) - the ranges this project's UIs measure constantly. -1 = none. */
static int adv_slot(uint32_t cp)
{
    if (cp < 256)
        return (int)cp;
    if (cp >= 0x400 && cp < 0x500)
        return (int)(cp - 0x400 + 256);
    return -1;
}

static cl_font_t *font_from_data(cl_application_t *app,
                                 const cl_allocator_t *a, unsigned char *data,
                                 size_t len, float size_px)
{
    cl_font_t *f;
    int offset;
    int asc;
    int desc;
    int gap;
    int i;

    /* Smaller than an sfnt offset table cannot be a font, and stb_truetype
     * reads the first tags unconditionally. */
    if (len < 12) {
        cl_log(CL_LOG_WARN, "font: data too short to be a font");
        cl_set_last_error(CL_ERROR_FONT);
        cl_free(a, data);
        return NULL;
    }

    f = cl_alloc(a, sizeof(*f));
    if (!f) {
        cl_free(a, data);
        return NULL;
    }
    memset(f, 0, sizeof(*f));
    f->a = a;
    f->app = app;
    f->data = data;

    /* -1 means "not a font": passing it to stbtt_InitFont would read ~4 GB
     * past the buffer (fontstart wraps to 0xFFFFFFFF). */
    offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || !stbtt_InitFont(&f->info, data, offset)) {
        cl_log(CL_LOG_WARN, "font: data is not a supported font");
        cl_set_last_error(CL_ERROR_FONT);
        cl_free(a, data);
        cl_free(a, f);
        return NULL;
    }

    f->px = size_px;
    f->scale = stbtt_ScaleForPixelHeight(&f->info, size_px);
    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
    f->ascent = (float)asc * f->scale;
    f->descent = (float)(-desc) * f->scale;
    f->line_gap = (float)gap * f->scale;
    for (i = 0; i < ADV_CACHE_SIZE; i++)
        f->adv_cache[i] = -1.0f;
    return f;
}

/*
 * Pixel advance of a codepoint. stbtt_GetCodepointHMetrics does a cmap lookup +
 * hmtx read on every call, and text is measured repeatedly (measure() and
 * paint(), plus multiline wrap probes), so memoize the Latin-1 and Cyrillic
 * ranges (adv_slot).
 */
static float advance_px(cl_font_t *f, uint32_t cp)
{
    int slot = adv_slot(cp);
    int advance;
    int lsb;

    if (slot >= 0 && f->adv_cache[slot] >= 0.0f)
        return f->adv_cache[slot];
    stbtt_GetCodepointHMetrics(&f->info, (int)cp, &advance, &lsb);
    if (slot >= 0)
        f->adv_cache[slot] = (float)advance * f->scale;
    return (float)advance * f->scale;
}

cl_font_t *cl_font_load_memory(cl_application_t *app, const void *data,
                               size_t len, float size_px)
{
    const cl_allocator_t *a = cl_application_allocator(app);
    unsigned char *buf;

    if (!data || len == 0) {
        cl_set_last_error(CL_ERROR_INVALID_ARGUMENT);
        return NULL;
    }
    buf = cl_alloc(a, len);
    if (!buf)
        return NULL;
    memcpy(buf, data, len);
    return font_from_data(app, a, buf, len, size_px);
}

cl_font_t *cl_font_load_file(cl_application_t *app, const char *path,
                             float size_px)
{
    const cl_allocator_t *a = cl_application_allocator(app);
    unsigned char *buf;
    long size;
    size_t got;
    FILE *fp;

    if (!path) {
        cl_set_last_error(CL_ERROR_INVALID_ARGUMENT);
        return NULL;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        /* INFO, not WARN: probing a candidate list is a legitimate pattern
         * (the caller still sees NULL + CL_ERROR_FONT). */
        cl_log(CL_LOG_INFO, "font: cannot open '%s'", path);
        cl_set_last_error(CL_ERROR_FONT);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) <= 0) {
        fclose(fp);
        cl_set_last_error(CL_ERROR_FONT);
        return NULL;
    }
    rewind(fp);

    buf = cl_alloc(a, (size_t)size);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (got != (size_t)size) {
        cl_free(a, buf);
        cl_set_last_error(CL_ERROR_FONT);
        return NULL;
    }
    return font_from_data(app, a, buf, (size_t)size, size_px);
}

cl_font_t *cl_font_load_system(cl_application_t *app, float size_px)
{
    static const char *const candidates[] = {
#if defined(_WIN32)
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
#endif
    };
    const char *env = getenv("COPAL_FONT");
    cl_font_t *font;
    size_t i;

    if (env && env[0]) {
        font = cl_font_load_file(app, env, size_px);
        if (font)
            return font;
        cl_log(CL_LOG_WARN, "font: COPAL_FONT='%s' could not be loaded", env);
    }
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        font = cl_font_load_file(app, candidates[i], size_px);
        if (font)
            return font;
    }
    cl_log(CL_LOG_WARN, "font: no usable system font found "
                        "(set COPAL_FONT=/path/to/font.ttf)");
    cl_set_last_error(CL_ERROR_FONT);
    return NULL;
}

void cl_font_release(cl_font_t *font)
{
    const cl_allocator_t *a;

    if (!font)
        return;
    /* The renderers key glyph caches by the raw font pointer; a later font
     * can reuse this address, so their entries must go now. */
    if (font->app && font->app->renderer &&
        font->app->renderer->ops->evict_font)
        font->app->renderer->ops->evict_font(font->app->renderer, font);
    a = font->a;
    cl_free(a, font->data);
    cl_free(a, font);
}

cl_font_metrics_t cl_font_metrics(cl_font_t *font)
{
    cl_font_metrics_t m = { 0 };

    if (font) {
        m.ascent = font->ascent;
        m.descent = font->descent;
        m.line_height = font->ascent + font->descent + font->line_gap;
    }
    return m;
}

cl_size_t cl_text_measure_bytes(cl_font_t *font, const char *utf8, size_t len,
                                float max_width)
{
    cl_size_t size = { 0, 0 };
    size_t i = 0;
    size_t n;
    uint32_t cp;

    (void)max_width; /* MVP: single line, no wrapping */
    if (!font)
        return size;
    size.h = font->ascent + font->descent + font->line_gap;
    if (!utf8)
        return size;

    while (i < len && (n = cl_utf8_next_n(utf8 + i, len - i, &cp)) != 0) {
        i += n;
        size.w += advance_px(font, cp);
    }
    return size;
}

cl_size_t cl_text_measure(cl_font_t *font, const char *utf8, float max_width)
{
    size_t len = utf8 ? strlen(utf8) : 0;

    return cl_text_measure_bytes(font, utf8, len, max_width);
}

const stbtt_fontinfo *cl_font_info(cl_font_t *f)
{
    return &f->info;
}

float cl_font_pixel_scale(cl_font_t *f)
{
    return f->scale;
}

float cl_font_ascent_px(cl_font_t *f)
{
    return f->ascent;
}
