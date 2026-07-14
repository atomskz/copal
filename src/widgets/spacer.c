/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/spacer.h>
#include <copal/widget_impl.h>

#include "widget/widget_internal.h"

typedef struct cl_spacer {
    cl_widget_t base;
    cl_size_t size;
} cl_spacer_t;

static cl_size_t spacer_measure(cl_widget_t *w, cl_constraints_t c);

static const cl_widget_vtable_t spacer_vtable = {
    .measure = spacer_measure,
};

static const cl_widget_class_t cl_spacer_class = {
    .name = "cl_spacer",
    .base = NULL,
    .type_id = 0x73706372u, /* 'spcr' */
    .instance_size = sizeof(cl_spacer_t),
    .vtable = &spacer_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static cl_size_t spacer_measure(cl_widget_t *w, cl_constraints_t c)
{
    (void)c;
    return CL_WIDGET_CAST_UNCHECKED(cl_spacer, w)->size;
}

cl_widget_t *cl_spacer_create(cl_application_t *app,
                              const cl_spacer_desc_t *desc)
{
    cl_widget_t *w;
    cl_spacer_t *s;

    cl_spacer_desc_t norm;
    if (!CL_DESC_NORM(desc, norm))
        return NULL;
    w = cl_widget_alloc(app, &cl_spacer_class);
    if (!w)
        return NULL;
    s = CL_WIDGET_CAST(cl_spacer, w);
    if (desc) {
        s->size.w = desc->width > 0.0f ? desc->width : 0.0f;
        s->size.h = desc->height > 0.0f ? desc->height : 0.0f;
        if (desc->flex > 0.0f)
            w->flex = desc->flex;
    }
    return w;
}
