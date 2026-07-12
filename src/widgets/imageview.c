/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/imageview.h>
#include <copal/widget_impl.h>
#include <copal/application.h>

#include "widget/widget_internal.h"
#include "core/foundation/foundation_internal.h"

typedef struct cl_imageview {
    cl_widget_t base;
    cl_image_t *image; /* borrowed */
} cl_imageview_t;

static cl_size_t imageview_measure(cl_widget_t *w, cl_constraints_t c);
static void imageview_paint(cl_widget_t *w, cl_paint_context_t *ctx);

static const cl_widget_vtable_t imageview_vtable = {
    .measure = imageview_measure,
    .paint = imageview_paint,
};

static const cl_widget_class_t cl_imageview_class = {
    .name = "cl_imageview",
    .base = NULL,
    .type_id = 0x696d6776u, /* 'imgv' */
    .instance_size = sizeof(cl_imageview_t),
    .vtable = &imageview_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static cl_size_t imageview_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_imageview_t *self = CL_WIDGET_CAST(cl_imageview, w);

    (void)c;
    return cl_image_size(self->image); /* {0,0} without an image */
}

static void imageview_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_imageview_t *self = CL_WIDGET_CAST(cl_imageview, w);

    if (self->image)
        cl_paint_draw_image(ctx, self->image, w->rect);
}

cl_widget_t *cl_imageview_create(cl_application_t *app,
                                 const cl_imageview_desc_t *desc)
{
    cl_widget_t *w;
    cl_imageview_t *self;

    if (!CL_DESC_ABI_OK(desc, cl_imageview_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_imageview_class);
    if (!w)
        return NULL;
    self = CL_WIDGET_CAST(cl_imageview, w);
    if (desc)
        self->image = desc->image;
    return w;
}

void cl_imageview_set_image(cl_widget_t *w, cl_image_t *image)
{
    cl_imageview_t *self = CL_WIDGET_CAST(cl_imageview, w);

    if (!self)
        return;
    self->image = image;
    cl_widget_invalidate_layout(w); /* the natural size may have changed */
}

cl_image_t *cl_imageview_image(cl_widget_t *w)
{
    cl_imageview_t *self = CL_WIDGET_CAST(cl_imageview, w);

    return self ? self->image : NULL;
}
