/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/layout.h>
#include <copal/widget_impl.h>

#include "widget/widget_internal.h"

typedef struct cl_hbox {
    cl_widget_t base;
    float spacing;
    cl_insets_t padding;
    cl_align_t align_cross;
} cl_hbox_t;

static cl_size_t hbox_measure(cl_widget_t *w, cl_constraints_t c);
static void hbox_arrange(cl_widget_t *w, cl_rect_t rect);

static const cl_widget_vtable_t hbox_vtable = {
    .measure = hbox_measure,
    .arrange = hbox_arrange,
};

static const cl_widget_class_t cl_hbox_class = {
    .name = "cl_hbox",
    .base = NULL,
    .type_id = 0x68626f78u, /* 'hbox' */
    .instance_size = sizeof(cl_hbox_t),
    .vtable = &hbox_vtable,
};

static cl_size_t hbox_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_hbox_t *self = CL_WIDGET_CAST(cl_hbox, w);
    cl_widget_t *ch;
    cl_size_t out = { 0, 0 };
    int n = 0;

    for (ch = w->first_child; ch; ch = ch->next_sibling) {
        cl_size_t cs;
        float chh;

        if (!(ch->flags & CL_WF_VISIBLE))
            continue;
        cs = cl_widget_do_measure(ch, c);
        chh = cs.h + ch->margin.top + ch->margin.bottom;
        if (chh > out.h)
            out.h = chh;
        out.w += cs.w + ch->margin.left + ch->margin.right;
        n++;
    }
    if (n > 1)
        out.w += self->spacing * (float)(n - 1);
    out.w += self->padding.left + self->padding.right;
    out.h += self->padding.top + self->padding.bottom;
    return out;
}

static void hbox_arrange(cl_widget_t *w, cl_rect_t rect)
{
    cl_hbox_t *self = CL_WIDGET_CAST(cl_hbox, w);
    cl_widget_t *ch;
    float y0 = rect.y + self->padding.top;
    float content_h = rect.h - self->padding.top - self->padding.bottom;
    float x = rect.x + self->padding.left;
    float used_w = self->padding.left + self->padding.right;
    float total_flex = 0.0f;
    float leftover;
    int n = 0;
    bool first = true;

    /* First pass: natural main-axis extent and the sum of flex weights, to
     * split any leftover width between flexible children. */
    for (ch = w->first_child; ch; ch = ch->next_sibling) {
        if (!(ch->flags & CL_WF_VISIBLE))
            continue;
        used_w += ch->measured.w + ch->margin.left + ch->margin.right;
        if (ch->flex > 0.0f)
            total_flex += ch->flex;
        n++;
    }
    if (n > 1)
        used_w += self->spacing * (float)(n - 1);
    leftover = rect.w - used_w;
    if (leftover < 0.0f || total_flex <= 0.0f)
        leftover = 0.0f; /* grow only: never shrink below measured */

    for (ch = w->first_child; ch; ch = ch->next_sibling) {
        cl_size_t cs;
        cl_align_t cross;
        float avail_h;
        float chh;
        float cy;

        if (!(ch->flags & CL_WF_VISIBLE))
            continue;
        if (!first)
            x += self->spacing;
        first = false;

        cs = ch->measured;
        if (ch->flex > 0.0f && leftover > 0.0f)
            cs.w += leftover * (ch->flex / total_flex);
        avail_h = content_h - ch->margin.top - ch->margin.bottom;
        chh = cs.h;
        cross = ch->align_v != CL_ALIGN_AUTO ? ch->align_v : self->align_cross;
        switch (cross) {
            case CL_ALIGN_STRETCH:
                chh = avail_h;
                cy = y0 + ch->margin.top;
                break;

            case CL_ALIGN_CENTER:
                cy = y0 + ch->margin.top + (avail_h - chh) * 0.5f;
                break;

            case CL_ALIGN_END:
                cy = y0 + ch->margin.top + (avail_h - chh);
                break;

            default:
                cy = y0 + ch->margin.top;
                break;
        }

        x += ch->margin.left;
        cl_widget_do_arrange(ch, (cl_rect_t){ x, cy, cs.w, chh });
        x += cs.w + ch->margin.right;
    }
}

cl_widget_t *cl_hbox_create(cl_application_t *app, const cl_hbox_desc_t *desc)
{
    cl_widget_t *w;
    cl_hbox_t *self;

    if (!CL_DESC_ABI_OK(desc, cl_hbox_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_hbox_class);
    if (!w)
        return NULL;
    self = CL_WIDGET_CAST(cl_hbox, w);
    if (desc) {
        self->spacing = desc->spacing;
        self->padding = desc->padding;
        self->align_cross = desc->align_cross;
    }
    return w;
}
