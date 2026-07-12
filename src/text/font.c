/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/font.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <stdio.h>
#include <string.h>

#include "stb_truetype.h"
#include "core/foundation/foundation_internal.h"
#include "text/font_internal.h"

struct cl_font {
    const cl_allocator_t *a;
    unsigned char *data;
    stbtt_fontinfo info;
    float px;
    float scale;
    float ascent;
    float descent;
    float line_gap;
    float adv_cache[256]; /* pixel advance per Latin-1 codepoint; -1 = uncached */
};

static cl_font_t *font_from_data(const cl_allocator_t *a, unsigned char *data,
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
    for (i = 0; i < 256; i++)
        f->adv_cache[i] = -1.0f;
    return f;
}

/*
 * Pixel advance of a codepoint. stbtt_GetCodepointHMetrics does a cmap lookup +
 * hmtx read on every call, and text is measured repeatedly (measure() and
 * paint(), plus multiline wrap probes), so memoize the common Latin-1 range.
 */
static float advance_px(cl_font_t *f, uint32_t cp)
{
    int advance;
    int lsb;

    if (cp < 256 && f->adv_cache[cp] >= 0.0f)
        return f->adv_cache[cp];
    stbtt_GetCodepointHMetrics(&f->info, (int)cp, &advance, &lsb);
    if (cp < 256)
        f->adv_cache[cp] = (float)advance * f->scale;
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
    return font_from_data(a, buf, len, size_px);
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
        cl_log(CL_LOG_WARN, "font: cannot open '%s'", path);
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
    return font_from_data(a, buf, (size_t)size, size_px);
}

void cl_font_release(cl_font_t *font)
{
    const cl_allocator_t *a;

    if (!font)
        return;
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
