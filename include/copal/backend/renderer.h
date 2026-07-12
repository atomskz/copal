/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_BACKEND_RENDERER_H
#define CL_BACKEND_RENDERER_H

/*
 * Renderer backend SPI - for AUTHORS of renderer backends. Application code
 * never needs this header (it is not part of <copal/copal.h>).
 *
 * A backend allocates its own struct with cl_renderer_t as the FIRST member,
 * points `ops` at a static cl_renderer_ops_t whose struct_size/abi_version
 * are filled in, and hands the cl_renderer_t* to the application through
 * cl_application_desc_t.renderer. cl_application_create() rejects an ops
 * table built against different headers with CL_ERROR_ABI_MISMATCH.
 */
#include <stddef.h>
#include <stdint.h>

#include <copal/types.h>
#include <copal/font.h>
#include <copal/image.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_renderer cl_renderer_t;

typedef struct cl_renderer_ops {
    /*
     * ABI handshake: set to sizeof(cl_renderer_ops_t) and COPAL_VERSION of
     * the headers the backend was compiled against. Checked when the backend
     * is injected; a mismatch fails cl_application_create with
     * CL_ERROR_ABI_MISMATCH instead of calling through a reshaped table.
     */
    size_t struct_size;
    uint32_t abi_version;

    /* Begin a frame and clear it to `clear`. */
    void (*begin_frame)(cl_renderer_t *r, cl_size_t size, float scale,
                        cl_color_t clear);
    void (*end_frame)(cl_renderer_t *r);
    void (*fill_rect)(cl_renderer_t *r, cl_rect_t rect, cl_color_t color);
    void (*fill_round_rect)(cl_renderer_t *r, cl_rect_t rect, float radius,
                            cl_color_t color);
    void (*stroke_round_rect)(cl_renderer_t *r, cl_rect_t rect, float radius,
                              float width, cl_color_t color);
    void (*draw_text)(cl_renderer_t *r, cl_font_t *font, const char *utf8,
                      cl_point_t pos, cl_color_t color);
    /*
     * Blend the whole image into dst (logical px), scaling as needed;
     * straight-alpha source-over. Pixels come from cl_image_pixels().
     */
    void (*draw_image)(cl_renderer_t *r, cl_image_t *img, cl_rect_t dst);
    /*
     * Clip stack. push_clip intersects rect with the current clip and makes it
     * the active scissor; pop_clip restores the previous one. Calls nest and
     * must be balanced. Coordinates are absolute logical pixels.
     */
    void (*push_clip)(cl_renderer_t *r, cl_rect_t rect);
    void (*pop_clip)(cl_renderer_t *r);
    /*
     * Drop every cached glyph derived from `font` (called by
     * cl_font_release: the caches key by the raw pointer, and a later font
     * could reuse the address). May be coarse - resetting the whole cache is
     * fine. NULL slot = no glyph cache. Never called inside a frame.
     */
    void (*evict_font)(cl_renderer_t *r, cl_font_t *font);
    /* Same contract for cached image textures (cl_image_release). NULL slot
     * = no image cache. Never called inside a frame. */
    void (*evict_image)(cl_renderer_t *r, cl_image_t *img);
    void (*destroy)(cl_renderer_t *r);
} cl_renderer_ops_t;

/* Concrete backends embed this as their first member. */
struct cl_renderer {
    const cl_renderer_ops_t *ops;
};

#ifdef __cplusplus
}
#endif

#endif /* CL_BACKEND_RENDERER_H */
