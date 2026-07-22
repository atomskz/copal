/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/progressbar.h>
#include <copal/widget_impl.h>
#include <copal/application.h>

#include "widget/widget_internal.h"
#include "core/foundation/foundation_internal.h"
#include "theme/theme_internal.h"

#define PB_DEFAULT_W 160.0f
#define PB_HEIGHT 8.0f

typedef struct cl_progressbar {
    cl_widget_t base;
    float value; /* 0..1 */
} cl_progressbar_t;

static cl_size_t progressbar_measure(cl_widget_t *w, cl_constraints_t c);
static void progressbar_paint(cl_widget_t *w, cl_paint_context_t *ctx);

static const cl_widget_vtable_t progressbar_vtable = {
    .measure = progressbar_measure,
    .paint = progressbar_paint,
};

static const cl_widget_class_t cl_progressbar_class = {
    .name = "cl_progressbar",
    .base = NULL,
    .type_id = 0x70726f67u, /* 'prog' */
    .instance_size = sizeof(cl_progressbar_t),
    .vtable = &progressbar_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static float pb_clamp(float v)
{
    if (!(v > 0.0f)) /* v <= 0, or NaN */
        return 0.0f;
    return v > 1.0f ? 1.0f : v;
}

static cl_size_t progressbar_measure(cl_widget_t *w, cl_constraints_t c)
{
    (void)w;
    (void)c;
    return (cl_size_t){ PB_DEFAULT_W, PB_HEIGHT };
}

static void progressbar_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_progressbar_t *pb = CL_WIDGET_CAST(cl_progressbar, w);
    float radius = w->rect.h * 0.5f;
    cl_rect_t fill = w->rect;

    cl_paint_fill_round_rect(ctx, w->rect, radius,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    cl_paint_stroke_round_rect(ctx, w->rect, radius, 1.0f,
                               cl_paint_theme_color(ctx, CL_COLOR_BORDER));
    fill.w = w->rect.w * pb->value;
    if (fill.w >= 1.0f)
        cl_paint_fill_round_rect(ctx, fill, radius,
                                 cl_paint_theme_color(ctx, CL_COLOR_ACCENT));
}

cl_widget_t *cl_progressbar_create(cl_application_t *app,
                                   const cl_progressbar_desc_t *desc)
{
    cl_widget_t *w;
    cl_progressbar_t *pb;

    cl_progressbar_desc_t norm;
    if (!CL_DESC_NORM(desc, norm))
        return NULL;
    w = cl_widget_alloc(app, &cl_progressbar_class);
    if (!w)
        return NULL;
    pb = CL_WIDGET_CAST(cl_progressbar, w);
    if (desc)
        pb->value = pb_clamp(desc->value);
    return w;
}

void cl_progressbar_set_value(cl_widget_t *w, float value)
{
    cl_progressbar_t *pb = CL_WIDGET_CAST(cl_progressbar, w);

    if (!pb)
        return;
    value = pb_clamp(value);
    if (value != pb->value) {
        pb->value = value;
        cl_widget_invalidate(w);
    }
}

float cl_progressbar_value(cl_widget_t *w)
{
    cl_progressbar_t *pb = CL_WIDGET_CAST(cl_progressbar, w);

    return pb ? pb->value : 0.0f;
}
