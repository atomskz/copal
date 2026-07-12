/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/panel.h>
#include <copal/widget_impl.h>
#include <copal/application.h>

#include "widget/widget_internal.h"
#include "core/foundation/foundation_internal.h"
#include "theme/theme_internal.h"

typedef struct cl_panel {
    cl_widget_t base;
    cl_insets_t padding;
    bool bordered;
} cl_panel_t;

static cl_size_t panel_measure(cl_widget_t *w, cl_constraints_t c);
static void panel_arrange(cl_widget_t *w, cl_rect_t rect);
static void panel_paint(cl_widget_t *w, cl_paint_context_t *ctx);

static const cl_widget_vtable_t panel_vtable = {
    .measure = panel_measure,
    .arrange = panel_arrange,
    .paint = panel_paint,
};

static const cl_widget_class_t cl_panel_class = {
    .name = "cl_panel",
    .base = NULL,
    .type_id = 0x70616e6cu, /* 'panl' */
    .instance_size = sizeof(cl_panel_t),
    .vtable = &panel_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static cl_size_t panel_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_panel_t *p = CL_WIDGET_CAST(cl_panel, w);
    float pad_w = p->padding.left + p->padding.right;
    float pad_h = p->padding.top + p->padding.bottom;
    cl_size_t out = { 0, 0 };
    cl_widget_t *ch;

    c.max.w -= pad_w;
    c.max.h -= pad_h;
    if (c.max.w < 0.0f)
        c.max.w = 0.0f;
    if (c.max.h < 0.0f)
        c.max.h = 0.0f;
    if (c.min.w > c.max.w)
        c.min.w = c.max.w;
    if (c.min.h > c.max.h)
        c.min.h = c.max.h;
    for (ch = w->first_child; ch; ch = ch->next_sibling) {
        cl_size_t cs;

        if (!(ch->flags & CL_WF_VISIBLE))
            continue;
        cs = cl_widget_do_measure(ch, c);
        if (cs.w > out.w)
            out.w = cs.w;
        if (cs.h > out.h)
            out.h = cs.h;
    }
    out.w += pad_w;
    out.h += pad_h;
    return out;
}

static void panel_arrange(cl_widget_t *w, cl_rect_t rect)
{
    cl_panel_t *p = CL_WIDGET_CAST(cl_panel, w);
    cl_rect_t inner = { rect.x + p->padding.left, rect.y + p->padding.top,
                        rect.w - p->padding.left - p->padding.right,
                        rect.h - p->padding.top - p->padding.bottom };
    cl_widget_t *ch;

    if (inner.w < 0.0f)
        inner.w = 0.0f;
    if (inner.h < 0.0f)
        inner.h = 0.0f;
    for (ch = w->first_child; ch; ch = ch->next_sibling) {
        if (ch->flags & CL_WF_VISIBLE)
            cl_widget_do_arrange(ch, inner); /* stack: children fill the box */
    }
}

static void panel_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_panel_t *p = CL_WIDGET_CAST(cl_panel, w);
    float radius = cl_theme_radius(cl_paint_theme(ctx));

    cl_paint_fill_round_rect(ctx, w->rect, radius,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    if (p->bordered)
        cl_paint_stroke_round_rect(
            ctx, w->rect, radius, 1.0f,
            cl_paint_theme_color(ctx, CL_COLOR_BORDER));
}

cl_widget_t *cl_panel_create(cl_application_t *app,
                             const cl_panel_desc_t *desc)
{
    cl_widget_t *w;
    cl_panel_t *p;

    if (!CL_DESC_ABI_OK(desc, cl_panel_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_panel_class);
    if (!w)
        return NULL;
    p = CL_WIDGET_CAST(cl_panel, w);
    if (desc) {
        p->padding = desc->padding;
        p->bordered = desc->bordered;
    }
    return w;
}
