/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/button.h>
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <string.h>

#include "widget/widget_internal.h"
#include "theme/theme_internal.h"

typedef struct cl_button {
    cl_widget_t base;
    char *text;
    cl_action_fn on_click;
    void *user;
    bool pressed;
} cl_button_t;

static cl_size_t button_measure(cl_widget_t *w, cl_constraints_t c);
static void button_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool button_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool button_mouse_up(cl_widget_t *w, const cl_event_t *ev);
static bool button_key_down(cl_widget_t *w, const cl_event_t *ev);
static void button_destroy(cl_widget_t *w);

static const cl_widget_vtable_t button_vtable = {
    .destroy = button_destroy,
    .measure = button_measure,
    .paint = button_paint,
    .mouse_down = button_mouse_down,
    .mouse_up = button_mouse_up,
    .key_down = button_key_down,
};

static const cl_widget_class_t cl_button_class = {
    .name = "cl_button",
    .base = NULL,
    .type_id = 0x6274746eu, /* 'bttn' */
    .instance_size = sizeof(cl_button_t),
    .vtable = &button_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
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

static cl_size_t button_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_button_t *self = CL_WIDGET_CAST(cl_button, w);
    cl_theme_t *theme = cl_application_theme(w->app);
    cl_font_t *font = cl_theme_font(theme);
    cl_size_t pad = cl_theme_button_padding(theme);
    cl_size_t ts = { 0, 0 };

    (void)c;
    if (font) {
        if (self->text)
            ts = cl_text_measure(font, self->text, CL_UNBOUNDED);
        else
            ts.h = cl_font_metrics(font).line_height;
    }
    return (cl_size_t){ ts.w + 2.0f * pad.w, ts.h + 2.0f * pad.h };
}

static void button_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_button_t *self = CL_WIDGET_CAST(cl_button, w);
    cl_theme_t *theme = cl_paint_theme(ctx);
    cl_font_t *font = cl_theme_font(theme);
    float radius = cl_theme_radius(theme);
    cl_color_t bg = self->pressed
                        ? cl_paint_theme_color(ctx, CL_COLOR_SURFACE_ACTIVE)
                        : cl_paint_theme_color(ctx, CL_COLOR_SURFACE);

    cl_paint_fill_round_rect(ctx, w->rect, radius, bg);
    cl_paint_stroke_round_rect(ctx, w->rect, radius, 1.0f,
                               cl_paint_theme_color(ctx, CL_COLOR_BORDER));

    if (font && self->text) {
        cl_size_t ts = cl_text_measure(font, self->text, CL_UNBOUNDED);
        cl_point_t pos;

        pos.x = w->rect.x + (w->rect.w - ts.w) * 0.5f;
        pos.y = w->rect.y + (w->rect.h - ts.h) * 0.5f;
        cl_paint_draw_text(ctx, font, self->text, pos,
                           cl_paint_theme_color(ctx, CL_COLOR_TEXT));
    }
}

static bool button_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_button_t *self = CL_WIDGET_CAST(cl_button, w);

    if (ev->data.mouse.button != CL_MOUSE_LEFT)
        return false; /* only the primary button presses; let others bubble */
    self->pressed = true;
    cl_widget_invalidate(w);
    return true;
}

static bool button_mouse_up(cl_widget_t *w, const cl_event_t *ev)
{
    cl_button_t *self = CL_WIDGET_CAST(cl_button, w);
    bool was_pressed = self->pressed;

    if (ev->data.mouse.button != CL_MOUSE_LEFT)
        return false;
    self->pressed = false;
    cl_widget_invalidate(w);
    if (was_pressed && cl_rect_contains(w->rect, ev->data.mouse.pos) &&
        self->on_click)
        self->on_click(w, self->user); /* last: may destroy the button */
    return true;
}

static bool button_key_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_button_t *self = CL_WIDGET_CAST(cl_button, w);

    /* The button sits in the Tab chain (CL_WF_FOCUSABLE), so the keyboard
     * must be able to press it too. */
    if (ev->data.key.key != CL_KEY_SPACE && ev->data.key.key != CL_KEY_ENTER)
        return false;
    cl_widget_invalidate(w);
    if (self->on_click)
        self->on_click(w, self->user); /* last: may destroy the button */
    return true;
}

static void button_destroy(cl_widget_t *w)
{
    cl_button_t *self = CL_WIDGET_CAST(cl_button, w);

    cl_free(cl_application_allocator(w->app), self->text);
}

cl_widget_t *cl_button_create(cl_application_t *app, const cl_button_desc_t *desc)
{
    cl_widget_t *w;
    cl_button_t *self;

    if (!CL_DESC_ABI_OK(desc, cl_button_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_button_class);
    if (!w)
        return NULL;
    self = CL_WIDGET_CAST(cl_button, w);
    w->flags |= CL_WF_FOCUSABLE;
    if (desc)
        self->text = dup_str(cl_application_allocator(app), desc->text);
    return w;
}

void cl_button_set_text(cl_widget_t *button, const char *utf8)
{
    cl_button_t *self = CL_WIDGET_CAST(cl_button, button);

    if (!self)
        return;
    cl_free(cl_application_allocator(button->app), self->text);
    self->text = dup_str(cl_application_allocator(button->app), utf8);
    cl_widget_invalidate_layout(button);
}

void cl_button_set_on_click(cl_widget_t *button, cl_action_fn fn, void *user)
{
    cl_button_t *self = CL_WIDGET_CAST(cl_button, button);

    if (!self)
        return;
    self->on_click = fn;
    self->user = user;
}
