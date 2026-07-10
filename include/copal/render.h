/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_RENDER_H
#define CL_RENDER_H

#include <copal/export.h>
#include <copal/types.h>
#include <copal/theme.h>
#include <copal/font.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Drawing surface handed to a widget's paint() method. Valid ONLY for the
 * duration of that call; must not be stored (ARCHITECTURE §3.3). Coordinates
 * are absolute logical pixels within the window.
 */
typedef struct cl_paint_context cl_paint_context_t;

CL_API void cl_paint_fill_rect(cl_paint_context_t *ctx, cl_rect_t r,
                               cl_color_t color);
CL_API void cl_paint_fill_round_rect(cl_paint_context_t *ctx, cl_rect_t r,
                                     float radius, cl_color_t color);
CL_API void cl_paint_stroke_round_rect(cl_paint_context_t *ctx, cl_rect_t r,
                                       float radius, float width,
                                       cl_color_t color);
CL_API void cl_paint_draw_text(cl_paint_context_t *ctx, cl_font_t *font,
                               const char *utf8, cl_point_t pos,
                               cl_color_t color);

/*
 * Clip region stack. cl_paint_push_clip() intersects r with the current clip
 * and restricts subsequent drawing to it; cl_paint_pop_clip() restores the
 * previous region. Pushes and pops must balance within a paint() call.
 */
CL_API void cl_paint_push_clip(cl_paint_context_t *ctx, cl_rect_t r);
CL_API void cl_paint_pop_clip(cl_paint_context_t *ctx);

CL_API cl_theme_t *cl_paint_theme(cl_paint_context_t *ctx);
CL_API cl_color_t cl_paint_theme_color(cl_paint_context_t *ctx,
                                       cl_color_role_t role);

#ifdef __cplusplus
}
#endif

#endif /* CL_RENDER_H */
