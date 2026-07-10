/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/label.h>
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <string.h>

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
};

static char *dup_str(const cl_allocator_t *a, const char *s)
{
    size_t n;
    char *p;

    if (!s)
        return NULL;
    n = strlen(s) + 1;
    p = cl_alloc(a, n);
    if (p)
        memcpy(p, s, n);
    return p;
}

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
    cl_paint_draw_text(ctx, font, self->text, pos, color);
}

static void label_destroy(cl_widget_t *w)
{
    cl_label_t *self = CL_WIDGET_CAST(cl_label, w);

    cl_free(cl_application_allocator(w->app), self->text);
}

cl_widget_t *cl_label_create(cl_application_t *app, const cl_label_desc_t *desc)
{
    cl_widget_t *w = cl_widget_alloc(app, &cl_label_class);
    cl_label_t *self;

    if (!w)
        return NULL;
    self = CL_WIDGET_CAST(cl_label, w);
    if (desc) {
        self->text = dup_str(cl_application_allocator(app), desc->text);
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
    self->text = dup_str(cl_application_allocator(label->app), utf8);
    cl_widget_invalidate_layout(label);
}
