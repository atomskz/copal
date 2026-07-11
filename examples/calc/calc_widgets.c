/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "calc_widgets.h"

#include <copal/widget_impl.h>

#include <string.h>

/* --- shared helpers ------------------------------------------------------- */

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

static bool rect_has(cl_rect_t r, cl_point_t p)
{
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

/* --- calc_key ------------------------------------------------------------- */

typedef struct cl_calc_key {
    cl_widget_t base;
    char *label;
    int code;
    calc_role_t role;
    calc_key_fn fn;
    void *user;
    bool pressed;
} cl_calc_key_t;

static cl_size_t calc_key_measure(cl_widget_t *w, cl_constraints_t c);
static void calc_key_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool calc_key_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool calc_key_mouse_up(cl_widget_t *w, const cl_event_t *ev);
static void calc_key_destroy(cl_widget_t *w);

static const cl_widget_vtable_t calc_key_vtable = {
    .destroy = calc_key_destroy,
    .measure = calc_key_measure,
    .paint = calc_key_paint,
    .mouse_down = calc_key_mouse_down,
    .mouse_up = calc_key_mouse_up,
};

static const cl_widget_class_t cl_calc_key_class = {
    .name = "cl_calc_key",
    .base = NULL,
    .type_id = 0,
    .instance_size = sizeof(cl_calc_key_t),
    .vtable = &calc_key_vtable,
};

static cl_size_t calc_key_measure(cl_widget_t *w, cl_constraints_t c)
{
    (void)w;
    (void)c;
    return (cl_size_t){ (float)CALC_KEY_W, (float)CALC_KEY_H };
}

static void calc_key_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_calc_key_t *self = CL_WIDGET_CAST(cl_calc_key, w);
    cl_font_t *font = cl_theme_font(cl_paint_theme(ctx));
    cl_color_t bg, fg;
    const float radius = 8.0f;

    if (self->pressed) {
        bg = cl_paint_theme_color(ctx, CL_COLOR_SURFACE_ACTIVE);
        fg = cl_paint_theme_color(ctx, CL_COLOR_TEXT);
    } else if (self->role == CALC_ROLE_OP) {
        bg = cl_paint_theme_color(ctx, CL_COLOR_ACCENT);
        fg = cl_paint_theme_color(ctx, CL_COLOR_SURFACE);
    } else if (self->role == CALC_ROLE_FUNC) {
        bg = cl_paint_theme_color(ctx, CL_COLOR_SURFACE_HOVER);
        fg = cl_paint_theme_color(ctx, CL_COLOR_TEXT);
    } else {
        bg = cl_paint_theme_color(ctx, CL_COLOR_SURFACE);
        fg = cl_paint_theme_color(ctx, CL_COLOR_TEXT);
    }

    cl_paint_fill_round_rect(ctx, w->rect, radius, bg);
    cl_paint_stroke_round_rect(ctx, w->rect, radius, 1.0f,
                               cl_paint_theme_color(ctx, CL_COLOR_BORDER));
    if (font && self->label) {
        cl_size_t ts = cl_text_measure(font, self->label, CL_UNBOUNDED);
        cl_point_t pos = { w->rect.x + (w->rect.w - ts.w) * 0.5f,
                           w->rect.y + (w->rect.h - ts.h) * 0.5f };

        cl_paint_draw_text(ctx, font, self->label, pos, fg);
    }
}

static bool calc_key_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_calc_key_t *self = CL_WIDGET_CAST(cl_calc_key, w);

    (void)ev;
    self->pressed = true;
    cl_widget_invalidate(w);
    return true;
}

static bool calc_key_mouse_up(cl_widget_t *w, const cl_event_t *ev)
{
    cl_calc_key_t *self = CL_WIDGET_CAST(cl_calc_key, w);
    bool was_pressed = self->pressed;

    self->pressed = false;
    cl_widget_invalidate(w);
    if (was_pressed && rect_has(w->rect, ev->data.mouse.pos) && self->fn)
        self->fn(w, self->code, self->user);
    return true;
}

static void calc_key_destroy(cl_widget_t *w)
{
    cl_calc_key_t *self = CL_WIDGET_CAST(cl_calc_key, w);

    cl_free(cl_application_allocator(w->app), self->label);
}

cl_widget_t *calc_key_create(cl_application_t *app, const char *label, int code,
                             calc_role_t role)
{
    cl_widget_t *w = cl_widget_alloc(app, &cl_calc_key_class);
    cl_calc_key_t *self;

    if (!w)
        return NULL;
    self = CL_WIDGET_CAST(cl_calc_key, w);
    self->label = dup_str(cl_application_allocator(app), label);
    self->code = code;
    self->role = role;
    return w;
}

void calc_key_set_handler(cl_widget_t *key, calc_key_fn fn, void *user)
{
    cl_calc_key_t *self = CL_WIDGET_CAST(cl_calc_key, key);

    if (!self)
        return;
    self->fn = fn;
    self->user = user;
}

/* --- calc_display --------------------------------------------------------- */

typedef struct cl_calc_display {
    cl_widget_t base;
    char *text;
} cl_calc_display_t;

static cl_size_t calc_display_measure(cl_widget_t *w, cl_constraints_t c);
static void calc_display_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static void calc_display_destroy(cl_widget_t *w);

static const cl_widget_vtable_t calc_display_vtable = {
    .destroy = calc_display_destroy,
    .measure = calc_display_measure,
    .paint = calc_display_paint,
};

static const cl_widget_class_t cl_calc_display_class = {
    .name = "cl_calc_display",
    .base = NULL,
    .type_id = 0,
    .instance_size = sizeof(cl_calc_display_t),
    .vtable = &calc_display_vtable,
};

static cl_size_t calc_display_measure(cl_widget_t *w, cl_constraints_t c)
{
    (void)w;
    (void)c;
    /* Width 0: the parent vbox (STRETCH cross-align) fills us to its width. */
    return (cl_size_t){ 0.0f, (float)CALC_DISPLAY_H };
}

static void calc_display_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_calc_display_t *self = CL_WIDGET_CAST(cl_calc_display, w);
    cl_font_t *font = cl_theme_font(cl_paint_theme(ctx));
    cl_color_t bg = cl_paint_theme_color(ctx, CL_COLOR_SURFACE_RAISED);
    const float radius = 10.0f;

    cl_paint_fill_round_rect(ctx, w->rect, radius, bg);
    cl_paint_stroke_round_rect(ctx, w->rect, radius, 1.0f,
                               cl_paint_theme_color(ctx, CL_COLOR_BORDER));
    if (font && self->text) {
        const float padx = 14.0f;
        cl_rect_t inner = { w->rect.x + padx, w->rect.y,
                            w->rect.w - 2.0f * padx, w->rect.h };
        cl_size_t ts = cl_text_measure(font, self->text, CL_UNBOUNDED);
        cl_point_t pos = { inner.x + inner.w - ts.w,
                           w->rect.y + (w->rect.h - ts.h) * 0.5f };

        /* Right-align, clipping the left so long results show their tail. */
        cl_paint_push_clip(ctx, inner);
        cl_paint_draw_text(ctx, font, self->text, pos,
                           cl_paint_theme_color(ctx, CL_COLOR_TEXT));
        cl_paint_pop_clip(ctx);
    }
}

static void calc_display_destroy(cl_widget_t *w)
{
    cl_calc_display_t *self = CL_WIDGET_CAST(cl_calc_display, w);

    cl_free(cl_application_allocator(w->app), self->text);
}

cl_widget_t *calc_display_create(cl_application_t *app)
{
    return cl_widget_alloc(app, &cl_calc_display_class);
}

void calc_display_set_text(cl_widget_t *display, const char *utf8)
{
    cl_calc_display_t *self = CL_WIDGET_CAST(cl_calc_display, display);
    const cl_allocator_t *a;

    if (!self)
        return;
    a = cl_application_allocator(display->app);
    cl_free(a, self->text);
    self->text = dup_str(a, utf8);
    cl_widget_invalidate(display);
}
