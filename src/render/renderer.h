/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_RENDERER_INTERNAL_H
#define CL_RENDERER_INTERNAL_H

/* Renderer abstraction (ARCHITECTURE §3.3). */
#include <copal/types.h>
#include <copal/font.h>

typedef struct cl_renderer cl_renderer_t;

typedef struct cl_renderer_ops {
    void (*begin_frame)(cl_renderer_t *r, cl_size_t size, float scale);
    void (*end_frame)(cl_renderer_t *r);
    void (*fill_rect)(cl_renderer_t *r, cl_rect_t rect, cl_color_t color);
    void (*fill_round_rect)(cl_renderer_t *r, cl_rect_t rect, float radius,
                            cl_color_t color);
    void (*stroke_round_rect)(cl_renderer_t *r, cl_rect_t rect, float radius,
                              float width, cl_color_t color);
    void (*draw_text)(cl_renderer_t *r, cl_font_t *font, const char *utf8,
                      cl_point_t pos, cl_color_t color);
    /*
     * Clip stack. push_clip intersects rect with the current clip and makes it
     * the active scissor; pop_clip restores the previous one. Calls nest and
     * must be balanced. Coordinates are absolute logical pixels.
     */
    void (*push_clip)(cl_renderer_t *r, cl_rect_t rect);
    void (*pop_clip)(cl_renderer_t *r);
    void (*destroy)(cl_renderer_t *r);
} cl_renderer_ops_t;

/* Concrete backends embed this as their first member. */
struct cl_renderer {
    const cl_renderer_ops_t *ops;
};

#endif /* CL_RENDERER_INTERNAL_H */
