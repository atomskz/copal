/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "widgets/tooltip_internal.h"

#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <string.h>

#include "widget/widget_internal.h"
#include "core/foundation/foundation_internal.h"
#include "theme/theme_internal.h"

#define TT_PAD_X 8.0f
#define TT_PAD_Y 5.0f

typedef struct cl_tooltip {
    cl_widget_t base;
    char *text;
} cl_tooltip_t;

static cl_size_t tooltip_measure(cl_widget_t *w, cl_constraints_t c);
static void tooltip_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static void tooltip_destroy(cl_widget_t *w);

static const cl_widget_vtable_t tooltip_vtable = {
    .destroy = tooltip_destroy,
    .measure = tooltip_measure,
    .paint = tooltip_paint,
};

static const cl_widget_class_t cl_tooltip_class = {
    .name = "cl_tooltip",
    .base = NULL,
    .type_id = 0x74746970u, /* 'ttip' */
    .instance_size = sizeof(cl_tooltip_t),
    .vtable = &tooltip_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static cl_size_t tooltip_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_tooltip_t *self = CL_WIDGET_CAST(cl_tooltip, w);
    cl_theme_t *theme = cl_application_theme(w->app);
    cl_font_t *font = cl_theme_font(theme);
    cl_size_t ts = { 0, 0 };

    (void)c;
    if (font) {
        if (self->text)
            ts = cl_text_measure(font, self->text, CL_UNBOUNDED);
        else
            ts.h = cl_font_metrics(font).line_height;
    }
    return (cl_size_t){ ts.w + 2.0f * TT_PAD_X, ts.h + 2.0f * TT_PAD_Y };
}

static void tooltip_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_tooltip_t *self = CL_WIDGET_CAST(cl_tooltip, w);
    cl_theme_t *theme = cl_paint_theme(ctx);
    cl_font_t *font = cl_theme_font(theme);
    float radius = cl_theme_radius(theme);

    cl_paint_fill_round_rect(ctx, w->rect, radius,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE_RAISED));
    cl_paint_stroke_round_rect(ctx, w->rect, radius, 1.0f,
                               cl_paint_theme_color(ctx, CL_COLOR_BORDER));
    if (font && self->text) {
        cl_point_t pos = { w->rect.x + TT_PAD_X, w->rect.y + TT_PAD_Y };

        cl_paint_draw_text(ctx, font, self->text, pos,
                           cl_paint_theme_color(ctx, CL_COLOR_TEXT));
    }
}

static void tooltip_destroy(cl_widget_t *w)
{
    cl_tooltip_t *self = CL_WIDGET_CAST(cl_tooltip, w);

    cl_free(cl_application_allocator(w->app), self->text);
}

cl_widget_t *cl_tooltip_create(cl_application_t *app, const char *text)
{
    cl_widget_t *w = cl_widget_alloc(app, &cl_tooltip_class);
    cl_tooltip_t *self;

    if (!w)
        return NULL;
    self = CL_WIDGET_CAST(cl_tooltip, w);
    self->text = cl_strdup(cl_application_allocator(app), text);
    return w;
}
