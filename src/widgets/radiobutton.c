/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/radiobutton.h>
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <string.h>

#include "widget/widget_internal.h"
#include "theme/theme_internal.h"

#define RB_DOT 16.0f   /* indicator diameter (logical px) */
#define RB_GAP 6.0f    /* gap between indicator and label */
#define RB_INSET 4.0f  /* inner-dot inset within the indicator */

typedef struct cl_radiobutton {
    cl_widget_t base;
    char *text;
    bool selected;
    int group;
    cl_toggled_fn on_select;
    void *user;
} cl_radiobutton_t;

static cl_size_t radio_measure(cl_widget_t *w, cl_constraints_t c);
static void radio_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool radio_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool radio_key_down(cl_widget_t *w, const cl_event_t *ev);
static void radio_focus_changed(cl_widget_t *w);
static void radio_destroy(cl_widget_t *w);

static const cl_widget_vtable_t radio_vtable = {
    .destroy = radio_destroy,
    .measure = radio_measure,
    .paint = radio_paint,
    .mouse_down = radio_mouse_down,
    .key_down = radio_key_down,
    .focus_gained = radio_focus_changed,
    .focus_lost = radio_focus_changed,
};

static const cl_widget_class_t cl_radiobutton_class = {
    .name = "cl_radiobutton",
    .base = NULL,
    .type_id = 0x7261646fu, /* 'rado' */
    .instance_size = sizeof(cl_radiobutton_t),
    .vtable = &radio_vtable,
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

/* Deselect every same-group radio in the tree except keep. */
static void deselect_group(cl_widget_t *w, cl_radiobutton_t *keep)
{
    cl_radiobutton_t *r = CL_WIDGET_CAST(cl_radiobutton, w);
    cl_widget_t *c;

    if (r && r != keep && r->group == keep->group && r->selected) {
        r->selected = false;
        cl_widget_invalidate(w);
    }
    for (c = w->first_child; c; c = c->next_sibling)
        deselect_group(c, keep);
}

static void select_radio(cl_radiobutton_t *rb, bool notify)
{
    if (rb->selected)
        return; /* radios do not toggle off on re-click */
    rb->selected = true;
    if (rb->group > 0) {
        cl_widget_t *root = &rb->base;

        while (root->parent)
            root = root->parent;
        deselect_group(root, rb); /* ungrouped (group <= 0) excludes nobody */
    }
    cl_widget_invalidate(&rb->base);
    if (notify && rb->on_select)
        rb->on_select(&rb->base, true, rb->user);
}

static cl_size_t radio_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_radiobutton_t *self = CL_WIDGET_CAST(cl_radiobutton, w);
    cl_font_t *font = cl_theme_font(cl_application_theme(w->app));
    float lh = font ? cl_font_metrics(font).line_height : RB_DOT;
    cl_size_t out;

    (void)c;
    out.w = RB_DOT;
    out.h = lh > RB_DOT ? lh : RB_DOT;
    if (self->text && self->text[0] && font)
        out.w += RB_GAP + cl_text_measure(font, self->text, CL_UNBOUNDED).w;
    return out;
}

static void radio_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_radiobutton_t *self = CL_WIDGET_CAST(cl_radiobutton, w);
    cl_font_t *font = cl_theme_font(cl_paint_theme(ctx));
    bool focused = cl_widget_has_focus(w);
    float dot_y = w->rect.y + (w->rect.h - RB_DOT) * 0.5f;
    cl_rect_t dot = { w->rect.x, dot_y, RB_DOT, RB_DOT };

    /* fully-rounded rect == circle */
    cl_paint_fill_round_rect(ctx, dot, RB_DOT * 0.5f,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    cl_paint_stroke_round_rect(
        ctx, dot, RB_DOT * 0.5f, focused ? 2.0f : 1.0f,
        cl_paint_theme_color(ctx, focused ? CL_COLOR_FOCUS_RING
                                          : CL_COLOR_BORDER));

    if (self->selected) {
        cl_rect_t inner = { dot.x + RB_INSET, dot.y + RB_INSET,
                            RB_DOT - 2.0f * RB_INSET, RB_DOT - 2.0f * RB_INSET };

        cl_paint_fill_round_rect(ctx, inner, inner.w * 0.5f,
                                 cl_paint_theme_color(ctx, CL_COLOR_ACCENT));
    }

    if (self->text && self->text[0] && font) {
        cl_size_t ts = cl_text_measure(font, self->text, CL_UNBOUNDED);
        cl_point_t p;

        p.x = dot.x + RB_DOT + RB_GAP;
        p.y = w->rect.y + (w->rect.h - ts.h) * 0.5f;
        cl_paint_draw_text(ctx, font, self->text, p,
                           cl_paint_theme_color(ctx, CL_COLOR_TEXT));
    }
}

static bool radio_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    if (ev->data.mouse.button != CL_MOUSE_LEFT)
        return false;
    select_radio(CL_WIDGET_CAST(cl_radiobutton, w), true);
    return true;
}

static bool radio_key_down(cl_widget_t *w, const cl_event_t *ev)
{
    if (ev->data.key.key == CL_KEY_SPACE) {
        select_radio(CL_WIDGET_CAST(cl_radiobutton, w), true);
        return true;
    }
    return false;
}

static void radio_focus_changed(cl_widget_t *w)
{
    cl_widget_invalidate(w);
}

static void radio_destroy(cl_widget_t *w)
{
    cl_radiobutton_t *self = CL_WIDGET_CAST(cl_radiobutton, w);

    cl_free(cl_application_allocator(w->app), self->text);
}

cl_widget_t *cl_radiobutton_create(cl_application_t *app,
                                   const cl_radiobutton_desc_t *desc)
{
    cl_widget_t *w;
    cl_radiobutton_t *self;

    if (!CL_DESC_ABI_OK(desc, cl_radiobutton_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_radiobutton_class);
    if (!w)
        return NULL;
    self = CL_WIDGET_CAST(cl_radiobutton, w);
    w->flags |= CL_WF_FOCUSABLE;
    if (desc) {
        self->group = desc->group;
        self->selected = desc->selected;
        self->text = dup_str(cl_application_allocator(app), desc->text);
    }
    return w;
}

void cl_radiobutton_set_selected(cl_widget_t *rb_w, bool selected)
{
    cl_radiobutton_t *self = CL_WIDGET_CAST(cl_radiobutton, rb_w);

    if (!self)
        return;
    if (selected) {
        select_radio(self, false);
    } else if (self->selected) {
        self->selected = false;
        cl_widget_invalidate(rb_w);
    }
}

bool cl_radiobutton_is_selected(cl_widget_t *rb_w)
{
    cl_radiobutton_t *self = CL_WIDGET_CAST(cl_radiobutton, rb_w);

    return self ? self->selected : false;
}

void cl_radiobutton_set_on_select(cl_widget_t *rb_w, cl_toggled_fn fn,
                                  void *user)
{
    cl_radiobutton_t *self = CL_WIDGET_CAST(cl_radiobutton, rb_w);

    if (!self)
        return;
    self->on_select = fn;
    self->user = user;
}
