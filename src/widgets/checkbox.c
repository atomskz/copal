/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/checkbox.h>
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <string.h>

#include "widget/widget_internal.h"
#include "theme/theme_internal.h"

#define CB_BOX 16.0f    /* indicator side length (logical px) */
#define CB_GAP 6.0f     /* gap between indicator and label */
#define CB_RADIUS 3.0f  /* indicator corner radius */
#define CB_MARK "\xE2\x9C\x93" /* U+2713 CHECK MARK */

typedef struct cl_checkbox {
    cl_widget_t base;
    char *text;
    bool checked;
    cl_toggled_fn on_toggle;
    void *user;
} cl_checkbox_t;

static cl_size_t checkbox_measure(cl_widget_t *w, cl_constraints_t c);
static void checkbox_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool checkbox_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool checkbox_key_down(cl_widget_t *w, const cl_event_t *ev);
static void checkbox_focus_changed(cl_widget_t *w);
static void checkbox_destroy(cl_widget_t *w);

static const cl_widget_vtable_t checkbox_vtable = {
    .destroy = checkbox_destroy,
    .measure = checkbox_measure,
    .paint = checkbox_paint,
    .mouse_down = checkbox_mouse_down,
    .key_down = checkbox_key_down,
    .focus_gained = checkbox_focus_changed,
    .focus_lost = checkbox_focus_changed,
};

static const cl_widget_class_t cl_checkbox_class = {
    .name = "cl_checkbox",
    .base = NULL,
    .type_id = 0x63686b78u, /* 'chkx' */
    .instance_size = sizeof(cl_checkbox_t),
    .vtable = &checkbox_vtable,
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

static void toggle(cl_checkbox_t *cb)
{
    cb->checked = !cb->checked;
    cl_widget_invalidate(&cb->base);
    /* Last: the callback may destroy the checkbox. */
    if (cb->on_toggle)
        cb->on_toggle(&cb->base, cb->checked, cb->user);
}

static cl_size_t checkbox_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_checkbox_t *self = CL_WIDGET_CAST(cl_checkbox, w);
    cl_font_t *font = cl_theme_font(cl_application_theme(w->app));
    float lh = font ? cl_font_metrics(font).line_height : CB_BOX;
    cl_size_t out;

    (void)c;
    out.w = CB_BOX;
    out.h = lh > CB_BOX ? lh : CB_BOX;
    if (self->text && self->text[0] && font)
        out.w += CB_GAP + cl_text_measure(font, self->text, CL_UNBOUNDED).w;
    return out;
}

static void checkbox_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_checkbox_t *self = CL_WIDGET_CAST(cl_checkbox, w);
    cl_font_t *font = cl_theme_font(cl_paint_theme(ctx));
    bool focused = cl_widget_has_focus(w);
    float box_y = w->rect.y + (w->rect.h - CB_BOX) * 0.5f;
    cl_rect_t box = { w->rect.x, box_y, CB_BOX, CB_BOX };

    if (self->checked)
        cl_paint_fill_round_rect(ctx, box, CB_RADIUS,
                                 cl_paint_theme_color(ctx, CL_COLOR_ACCENT));
    else
        cl_paint_fill_round_rect(ctx, box, CB_RADIUS,
                                 cl_paint_theme_color(ctx, CL_COLOR_SURFACE));

    cl_paint_stroke_round_rect(
        ctx, box, CB_RADIUS, focused ? 2.0f : 1.0f,
        cl_paint_theme_color(ctx, focused ? CL_COLOR_FOCUS_RING
                                          : CL_COLOR_BORDER));

    /* check mark, drawn in the surface colour for contrast on the accent box */
    if (self->checked && font) {
        cl_size_t ms = cl_text_measure(font, CB_MARK, CL_UNBOUNDED);
        cl_point_t p;

        p.x = box.x + (CB_BOX - ms.w) * 0.5f;
        p.y = box.y + (CB_BOX - ms.h) * 0.5f;
        cl_paint_draw_text(ctx, font, CB_MARK, p,
                           cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    }

    if (self->text && self->text[0] && font) {
        cl_size_t ts = cl_text_measure(font, self->text, CL_UNBOUNDED);
        cl_point_t p;

        p.x = box.x + CB_BOX + CB_GAP;
        p.y = w->rect.y + (w->rect.h - ts.h) * 0.5f;
        cl_paint_draw_text(ctx, font, self->text, p,
                           cl_paint_theme_color(ctx, CL_COLOR_TEXT));
    }
}

static bool checkbox_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    if (ev->data.mouse.button != CL_MOUSE_LEFT)
        return false; /* only the primary button toggles; let others bubble */
    toggle(CL_WIDGET_CAST(cl_checkbox, w));
    return true;
}

static bool checkbox_key_down(cl_widget_t *w, const cl_event_t *ev)
{
    /* Space toggles; Enter is left to bubble (reserved for a default action). */
    if (ev->data.key.key == CL_KEY_SPACE) {
        toggle(CL_WIDGET_CAST(cl_checkbox, w));
        return true;
    }
    return false;
}

static void checkbox_focus_changed(cl_widget_t *w)
{
    cl_widget_invalidate(w);
}

static void checkbox_destroy(cl_widget_t *w)
{
    cl_checkbox_t *self = CL_WIDGET_CAST(cl_checkbox, w);

    cl_free(cl_application_allocator(w->app), self->text);
}

cl_widget_t *cl_checkbox_create(cl_application_t *app,
                                const cl_checkbox_desc_t *desc)
{
    cl_widget_t *w;
    cl_checkbox_t *self;

    if (!CL_DESC_ABI_OK(desc, cl_checkbox_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_checkbox_class);
    if (!w)
        return NULL;
    self = CL_WIDGET_CAST(cl_checkbox, w);
    w->flags |= CL_WF_FOCUSABLE;
    if (desc) {
        self->checked = desc->checked;
        self->text = dup_str(cl_application_allocator(app), desc->text);
    }
    return w;
}

void cl_checkbox_set_checked(cl_widget_t *cb_w, bool checked)
{
    cl_checkbox_t *self = CL_WIDGET_CAST(cl_checkbox, cb_w);

    if (!self || self->checked == checked)
        return;
    self->checked = checked;
    cl_widget_invalidate(cb_w);
}

bool cl_checkbox_is_checked(cl_widget_t *cb_w)
{
    cl_checkbox_t *self = CL_WIDGET_CAST(cl_checkbox, cb_w);

    return self ? self->checked : false;
}

void cl_checkbox_set_text(cl_widget_t *cb_w, const char *utf8)
{
    cl_checkbox_t *self = CL_WIDGET_CAST(cl_checkbox, cb_w);

    if (!self)
        return;
    cl_free(cl_application_allocator(cb_w->app), self->text);
    self->text = dup_str(cl_application_allocator(cb_w->app), utf8);
    cl_widget_invalidate_layout(cb_w);
}

void cl_checkbox_set_on_toggle(cl_widget_t *cb_w, cl_toggled_fn fn, void *user)
{
    cl_checkbox_t *self = CL_WIDGET_CAST(cl_checkbox, cb_w);

    if (!self)
        return;
    self->on_toggle = fn;
    self->user = user;
}
