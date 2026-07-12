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
 * Data that is not a font is rejected (NULL, CL_ERROR_FONT), but the parser
 * (stb_truetype) does not bounds-check a *truncated* real font against the
 * buffer length, so fonts must come from a trusted source.
 *
 * Return: a font handle, or NULL on error (see cl_last_error()).
 */
CL_API cl_font_t *cl_font_load_file(cl_application_t *app, const char *path,
                                    float size_px);

/** cl_font_load_memory() - load a font from an in-memory copy of the file. */
CL_API cl_font_t *cl_font_load_memory(cl_application_t *app, const void *data,
                                      size_t len, float size_px);

/**
 * cl_font_load_system() - load a default UI font from the host system.
 *
 * Honours COPAL_FONT=/path/to/font.ttf, then probes well-known system font
 * paths (Segoe UI/Arial on Windows, DejaVu/Liberation/Noto on Linux, Arial
 * on macOS). Meant to take the place of hand-rolled font path lists in
 * applications that just need readable text.
 *
 * Return: a font handle, or NULL when nothing usable was found (WARN is
 * logged; see cl_set_log_callback).
 */
CL_API cl_font_t *cl_font_load_system(cl_application_t *app, float size_px);

/**
 * cl_font_release() - release a font.
 *
 * Evicts the renderer's cached glyphs for this font, so releasing and
 * loading fonts at run time is safe. Release fonts BEFORE destroying the
 * application that loaded them (the font borrows its allocator).
 */
CL_API void cl_font_release(cl_font_t *font);

/** cl_font_metrics() - vertical metrics of a font at its pixel size. */
CL_API cl_font_metrics_t cl_font_metrics(cl_font_t *font);

/**
 * cl_text_measure() - measure a UTF-8 string (no rasterization).
 * @font:      the font.
 * @utf8:      NUL-terminated UTF-8 text.
 * @max_width: RESERVED for future wrapping and currently ignored: every
 *             value measures a single line (pass CL_UNBOUNDED). Multiline
 *             wrapping lives in the textbox widget for now.
 *
 * Return: the size in logical pixels.
 */
CL_API cl_size_t cl_text_measure(cl_font_t *font, const char *utf8,
                                 float max_width);

/**
 * cl_text_measure_bytes() - measure up to @len bytes of UTF-8 (the buffer
 * need not be NUL-terminated). Useful for caret positioning in editable
 * text.
 *
 * An embedded NUL byte ends the measurement early, mirroring rendering:
 * draw_text() consumes a NUL-terminated string, so bytes past a NUL never
 * reach the screen and are not counted here either.
 */
CL_API cl_size_t cl_text_measure_bytes(cl_font_t *font, const char *utf8,
                                       size_t len, float max_width);

#ifdef __cplusplus
}
#endif

#endif /* CL_FONT_H */
