/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/layout.h>
#include <copal/widget_impl.h>

#include "widget/widget_internal.h"

typedef struct cl_vbox {
    cl_widget_t base;
    float spacing;
    cl_insets_t padding;
    cl_align_t align_cross;
} cl_vbox_t;

static cl_size_t vbox_measure(cl_widget_t *w, cl_constraints_t c);
static void vbox_arrange(cl_widget_t *w, cl_rect_t rect);

static const cl_widget_vtable_t vbox_vtable = {
    .measure = vbox_measure,
    .arrange = vbox_arrange,
};

static const cl_widget_class_t cl_vbox_class = {
    .name = "cl_vbox",
    .base = NULL,
    .type_id = 0x76626f78u, /* 'vbox' */
    .instance_size = sizeof(cl_vbox_t),
    .vtable = &vbox_vtable,
};

static cl_size_t vbox_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_vbox_t *self = CL_WIDGET_CAST(cl_vbox, w);
    cl_widget_t *ch;
    cl_size_t out = { 0, 0 };
    int n = 0;

    for (ch = w->first_child; ch; ch = ch->next_sibling) {
        cl_size_t cs;
        float cw;

        if (!(ch->flags & CL_WF_VISIBLE))
            continue;
        cs = cl_widget_do_measure(ch, c);
        cw = cs.w + ch->margin.left + ch->margin.right;
        if (cw > out.w)
            out.w = cw;
        out.h += cs.h + ch->margin.top + ch->margin.bottom;
        n++;
    }
    if (n > 1)
        out.h += self->spacing * (float)(n - 1);
    out.w += self->padding.left + self->padding.right;
    out.h += self->padding.top + self->padding.bottom;
    return out;
}

static void vbox_arrange(cl_widget_t *w, cl_rect_t rect)
{
    cl_vbox_t *self = CL_WIDGET_CAST(cl_vbox, w);
    cl_widget_t *ch;
    float x0 = rect.x + self->padding.left;
    float content_w = rect.w - self->padding.left - self->padding.right;
    float y = rect.y + self->padding.top;
    float used_h = self->padding.top + self->padding.bottom;
    float total_flex = 0.0f;
    float leftover;
    int n = 0;
    bool first = true;

    /* First pass: natural main-axis extent and the sum of flex weights, to
     * split any leftover height between flexible children. */
    for (ch = w->first_child; ch; ch = ch->next_sibling) {
        if (!(ch->flags & CL_WF_VISIBLE))
            continue;
        used_h += ch->measured.h + ch->margin.top + ch->margin.bottom;
        if (ch->flex > 0.0f)
            total_flex += ch->flex;
        n++;
    }
    if (n > 1)
        used_h += self->spacing * (float)(n - 1);
    leftover = rect.h - used_h;
    if (leftover < 0.0f || total_flex <= 0.0f)
        leftover = 0.0f; /* grow only: never shrink below measured */

    for (ch = w->first_child; ch; ch = ch->next_sibling) {
        cl_size_t cs;
        cl_align_t cross;
        float avail_w;
        float cw;
        float cx;

        if (!(ch->flags & CL_WF_VISIBLE))
            continue;
        if (!first)
            y += self->spacing;
        first = false;

        cs = ch->measured;
        if (ch->flex > 0.0f && leftover > 0.0f)
            cs.h += leftover * (ch->flex / total_flex);
        avail_w = content_w - ch->margin.left - ch->margin.right;
        cw = cs.w;
        cross = ch->align_h != CL_ALIGN_AUTO ? ch->align_h : self->align_cross;
        switch (cross) {
            case CL_ALIGN_STRETCH:
                cw = avail_w;
                cx = x0 + ch->margin.left;
                break;

            case CL_ALIGN_CENTER:
                cx = x0 + ch->margin.left + (avail_w - cw) * 0.5f;
                break;

            case CL_ALIGN_END:
                cx = x0 + ch->margin.left + (avail_w - cw);
                break;

            default:
                cx = x0 + ch->margin.left;
                break;
        }

        y += ch->margin.top;
        cl_widget_do_arrange(ch, (cl_rect_t){ cx, y, cw, cs.h });
        y += cs.h + ch->margin.bottom;
    }
}

cl_widget_t *cl_vbox_create(cl_application_t *app, const cl_vbox_desc_t *desc)
{
    cl_widget_t *w = cl_widget_alloc(app, &cl_vbox_class);
    cl_vbox_t *self;

    if (!w)
        return NULL;
    self = CL_WIDGET_CAST(cl_vbox, w);
    if (desc) {
        self->spacing = desc->spacing;
        self->padding = desc->padding;
        self->align_cross = desc->align_cross;
    }
    return w;
}
