/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/render.h>

#include "render/paint_context.h"
#include "theme/theme_internal.h"

void cl_paint_fill_rect(cl_paint_context_t *ctx, cl_rect_t r, cl_color_t color)
{
    ctx->renderer->ops->fill_rect(ctx->renderer, r, color);
}

void cl_paint_fill_round_rect(cl_paint_context_t *ctx, cl_rect_t r, float radius,
                              cl_color_t color)
{
    ctx->renderer->ops->fill_round_rect(ctx->renderer, r, radius, color);
}

void cl_paint_stroke_round_rect(cl_paint_context_t *ctx, cl_rect_t r,
                                float radius, float width, cl_color_t color)
{
    ctx->renderer->ops->stroke_round_rect(ctx->renderer, r, radius, width,
                                          color);
}

void cl_paint_draw_text(cl_paint_context_t *ctx, cl_font_t *font,
                        const char *utf8, cl_point_t pos, cl_color_t color)
{
    ctx->renderer->ops->draw_text(ctx->renderer, font, utf8, pos, color);
}

void cl_paint_push_clip(cl_paint_context_t *ctx, cl_rect_t r)
{
    ctx->renderer->ops->push_clip(ctx->renderer, r);
}

void cl_paint_pop_clip(cl_paint_context_t *ctx)
{
    ctx->renderer->ops->pop_clip(ctx->renderer);
}

cl_theme_t *cl_paint_theme(cl_paint_context_t *ctx)
{
    return ctx->theme;
}

cl_color_t cl_paint_theme_color(cl_paint_context_t *ctx, cl_color_role_t role)
{
    return cl_theme_color(ctx->theme, role);
}
