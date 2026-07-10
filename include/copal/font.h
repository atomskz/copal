/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_FONT_H
#define CL_FONT_H

#include <stddef.h>

#include <copal/export.h>
#include <copal/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;
typedef struct cl_font cl_font_t;

typedef struct cl_font_metrics {
    float ascent;      /* logical px above baseline (positive) */
    float descent;     /* logical px below baseline (positive) */
    float line_height; /* recommended line advance */
} cl_font_metrics_t;

/**
 * cl_font_load_file() - load a TrueType font at a given pixel size.
 * @app:     owning application (provides the allocator).
 * @path:    filesystem path to a .ttf/.otf file.
 * @size_px: nominal pixel size.
 *
 * Return: a font handle, or NULL on error (see cl_last_error()).
 */
CL_API cl_font_t *cl_font_load_file(cl_application_t *app, const char *path,
                                    float size_px);

/** cl_font_load_memory() - load a font from an in-memory copy of the file. */
CL_API cl_font_t *cl_font_load_memory(cl_application_t *app, const void *data,
                                      size_t len, float size_px);

/** cl_font_release() - release a font. */
CL_API void cl_font_release(cl_font_t *font);

/** cl_font_metrics() - vertical metrics of a font at its pixel size. */
CL_API cl_font_metrics_t cl_font_metrics(cl_font_t *font);

/**
 * cl_text_measure() - measure a UTF-8 string (no rasterization).
 * @font:      the font.
 * @utf8:      NUL-terminated UTF-8 text.
 * @max_width: CL_UNBOUNDED for a single line (MVP does not wrap).
 *
 * Return: the size in logical pixels.
 */
CL_API cl_size_t cl_text_measure(cl_font_t *font, const char *utf8,
                                 float max_width);

#ifdef __cplusplus
}
#endif

#endif /* CL_FONT_H */
