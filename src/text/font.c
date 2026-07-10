/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/font.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <stdio.h>
#include <string.h>

#include "stb_truetype.h"
#include "core/foundation/foundation_internal.h"

struct cl_font {
    const cl_allocator_t *a;
    unsigned char *data;
    stbtt_fontinfo info;
    float px;
    float scale;
    float ascent;
    float descent;
    float line_gap;
};

static cl_font_t *font_from_data(const cl_allocator_t *a, unsigned char *data,
                                 float size_px)
{
    cl_font_t *f;
    int asc;
    int desc;
    int gap;

    f = cl_alloc(a, sizeof(*f));
    if (!f) {
        cl_free(a, data);
        return NULL;
    }
    memset(f, 0, sizeof(*f));
    f->a = a;
    f->data = data;

    if (!stbtt_InitFont(&f->info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
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
    return f;
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
    return font_from_data(a, buf, size_px);
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
    return font_from_data(a, buf, size_px);
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

cl_size_t cl_text_measure(cl_font_t *font, const char *utf8, float max_width)
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

    while ((n = cl_utf8_next(utf8 + i, &cp)) != 0) {
        int advance;
        int lsb;

        i += n;
        stbtt_GetCodepointHMetrics(&font->info, (int)cp, &advance, &lsb);
        size.w += (float)advance * font->scale;
    }
    return size;
}
