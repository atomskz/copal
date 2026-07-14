/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/label.h>
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <string.h>

#include "widget/widget_internal.h"
#include "core/foundation/foundation_internal.h"

typedef struct cl_label {
    cl_widget_t base;
    char *text;
    cl_font_t *font;  /* NULL -> theme default */
    bool has_color;
    cl_color_t color;
} cl_label_t;

static cl_size_t label_measure(cl_widget_t *w, cl_constraints_t c);
static void label_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static void label_destroy(cl_widget_t *w);

static const cl_widget_vtable_t label_vtable = {
    .destroy = label_destroy,
    .measure = label_measure,
    .paint = label_paint,
};

static const cl_widget_class_t cl_label_class = {
    .name = "cl_label",
    .base = NULL,
    .type_id = 0x6c61626cu, /* 'labl' */
    .instance_size = sizeof(cl_label_t),
    .vtable = &label_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static cl_font_t *label_font(cl_label_t *self)
{
    if (self->font)
        return self->font;
    return cl_theme_font(cl_application_theme(self->base.app));
}

static cl_size_t label_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_label_t *self = CL_WIDGET_CAST(cl_label, w);
    cl_font_t *font = label_font(self);
    cl_size_t sz = { 0, 0 };

    (void)c;
    if (!font)
        return sz;
    if (self->text)
        sz = cl_text_measure(font, self->text, CL_UNBOUNDED);
    else
        sz.h = cl_font_metrics(font).line_height;
    return sz;
}

static void label_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_label_t *self = CL_WIDGET_CAST(cl_label, w);
    cl_font_t *font = label_font(self);
    cl_color_t color;
    cl_point_t pos;

    if (!font || !self->text)
        return;
    color = self->has_color ? self->color
                            : cl_paint_theme_color(ctx, CL_COLOR_TEXT);
    pos.x = w->rect.x;
    pos.y = w->rect.y;
    /* Clip to our rect: a label whose parent gives it less width than its
     * measured text (no wrapping/ellipsis in the MVP) is cut off at its box
     * rather than overflowing onto sibling widgets. */
    cl_paint_push_clip(ctx, w->rect);
    cl_paint_draw_text(ctx, font, self->text, pos, color);
    cl_paint_pop_clip(ctx);
}

static void label_destroy(cl_widget_t *w)
{
    cl_label_t *self = CL_WIDGET_CAST(cl_label, w);

    cl_free(cl_application_allocator(w->app), self->text);
}

cl_widget_t *cl_label_create(cl_application_t *app, const cl_label_desc_t *desc)
{
    cl_widget_t *w;
    cl_label_t *self;

    if (!CL_DESC_ABI_OK(desc, cl_label_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_label_class);
    if (!w)
        return NULL;
    self = CL_WIDGET_CAST(cl_label, w);
    if (desc) {
        self->text = cl_strdup(cl_application_allocator(app), desc->text);
        if (desc->style) {
            self->font = desc->style->font;
            self->color = desc->style->color;
            self->has_color = true;
        }
    }
    return w;
}

void cl_label_set_text(cl_widget_t *label, const char *utf8)
{
    cl_label_t *self = CL_WIDGET_CAST(cl_label, label);

    if (!self)
        return;
    cl_free(cl_application_allocator(label->app), self->text);
    self->text = cl_strdup(cl_application_allocator(label->app), utf8);
    cl_widget_invalidate_layout(label);
}
