/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/slider.h>
#include <copal/widget_impl.h>
#include <copal/application.h>

#include "widget/widget_internal.h"
#include "theme/theme_internal.h"

#define SL_WIDTH 160.0f  /* default preferred width */
#define SL_HEIGHT 20.0f  /* widget height */
#define SL_THUMB 8.0f    /* thumb radius */
#define SL_TRACK 4.0f    /* track thickness */

typedef struct cl_slider {
    cl_widget_t base;
    float min;
    float max;
    float value;
    float step;
    bool auto_step; /* step was auto-derived, so re-derive it on range change */
    bool dragging;
    cl_value_fn on_change;
    void *user;
} cl_slider_t;

static cl_size_t slider_measure(cl_widget_t *w, cl_constraints_t c);
static void slider_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool slider_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool slider_mouse_move(cl_widget_t *w, const cl_event_t *ev);
static bool slider_mouse_up(cl_widget_t *w, const cl_event_t *ev);
static bool slider_key_down(cl_widget_t *w, const cl_event_t *ev);
static void slider_focus_changed(cl_widget_t *w);

static const cl_widget_vtable_t slider_vtable = {
    .measure = slider_measure,
    .paint = slider_paint,
    .mouse_down = slider_mouse_down,
    .mouse_move = slider_mouse_move,
    .mouse_up = slider_mouse_up,
    .key_down = slider_key_down,
    .focus_gained = slider_focus_changed,
    .focus_lost = slider_focus_changed,
};

static const cl_widget_class_t cl_slider_class = {
    .name = "cl_slider",
    .base = NULL,
    .type_id = 0x736c6472u, /* 'sldr' */
    .instance_size = sizeof(cl_slider_t),
    .vtable = &slider_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void set_value(cl_slider_t *s, float v, bool notify)
{
    v = clampf(v, s->min, s->max);
    if (v == s->value)
        return;
    s->value = v;
    cl_widget_invalidate(&s->base);
    if (notify && s->on_change)
        s->on_change(&s->base, v, s->user);
}

/* Thumb radius, shrunk so the thumb never overflows a very narrow slider. */
static float thumb_r(const cl_slider_t *s)
{
    float half = s->base.rect.w * 0.5f;

    return SL_THUMB < half ? SL_THUMB : half;
}

static float track_left(const cl_slider_t *s)
{
    return s->base.rect.x + thumb_r(s);
}

static float track_width(const cl_slider_t *s)
{
    float tw = s->base.rect.w - 2.0f * thumb_r(s);

    return tw > 0.0f ? tw : 0.0f;
}

static float value_frac(const cl_slider_t *s)
{
    return s->max > s->min ? (s->value - s->min) / (s->max - s->min) : 0.0f;
}

static void set_from_x(cl_slider_t *s, float x)
{
    float tw = track_width(s);
    float frac;

    if (tw <= 0.0f)
        return;
    frac = clampf((x - track_left(s)) / tw, 0.0f, 1.0f);
    set_value(s, s->min + frac * (s->max - s->min), true);
}

static cl_size_t slider_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_size_t out = w->pref_size;

    (void)c;
    if (out.w <= 0.0f)
        out.w = SL_WIDTH;
    out.h = SL_HEIGHT;
    return out;
}

static void slider_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_slider_t *s = CL_WIDGET_CAST(cl_slider, w);
    bool focused = cl_widget_has_focus(w);
    float cy = w->rect.y + w->rect.h * 0.5f;
    float r = thumb_r(s);
    float tl = track_left(s);
    float thumb_cx = tl + value_frac(s) * track_width(s);
    cl_rect_t track = { w->rect.x, cy - SL_TRACK * 0.5f, w->rect.w, SL_TRACK };
    cl_rect_t fill = { tl, track.y, thumb_cx - tl, SL_TRACK };
    cl_rect_t thumb = { thumb_cx - r, cy - r, 2.0f * r, 2.0f * r };

    cl_paint_fill_round_rect(ctx, track, SL_TRACK * 0.5f,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE_ACTIVE));
    if (fill.w > 0.0f)
        cl_paint_fill_round_rect(ctx, fill, SL_TRACK * 0.5f,
                                 cl_paint_theme_color(ctx, CL_COLOR_ACCENT));
    cl_paint_fill_round_rect(ctx, thumb, r,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    cl_paint_stroke_round_rect(
        ctx, thumb, r, focused ? 2.0f : 1.0f,
        cl_paint_theme_color(ctx, focused ? CL_COLOR_FOCUS_RING
                                          : CL_COLOR_BORDER));
}

static bool slider_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_slider_t *s = CL_WIDGET_CAST(cl_slider, w);

    if (ev->data.mouse.button != CL_MOUSE_LEFT)
        return false;
    s->dragging = true;
    set_from_x(s, ev->data.mouse.pos.x);
    return true;
}

static bool slider_mouse_move(cl_widget_t *w, const cl_event_t *ev)
{
    cl_slider_t *s = CL_WIDGET_CAST(cl_slider, w);

    if (!s->dragging)
        return false;
    set_from_x(s, ev->data.mouse.pos.x);
    return true;
}

static bool slider_mouse_up(cl_widget_t *w, const cl_event_t *ev)
{
    cl_slider_t *s = CL_WIDGET_CAST(cl_slider, w);

    (void)ev;
    if (!s->dragging)
        return false;
    s->dragging = false;
    return true;
}

static bool slider_key_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_slider_t *s = CL_WIDGET_CAST(cl_slider, w);

    switch (ev->data.key.key) {
        case CL_KEY_LEFT:
        case CL_KEY_DOWN:
            set_value(s, s->value - s->step, true);
            return true;

        case CL_KEY_RIGHT:
        case CL_KEY_UP:
            set_value(s, s->value + s->step, true);
            return true;

        case CL_KEY_HOME:
            set_value(s, s->min, true);
            return true;

        case CL_KEY_END:
            set_value(s, s->max, true);
            return true;

        default:
            return false;
    }
}

static void slider_focus_changed(cl_widget_t *w)
{
    cl_widget_invalidate(w);
}

static void apply_range(cl_slider_t *s, float min, float max, float step)
{
    if (max <= min) {
        min = 0.0f;
        max = 1.0f;
    }
    s->min = min;
    s->max = max;
    s->auto_step = step <= 0.0f;
    s->step = s->auto_step ? (max - min) / 20.0f : step;
    s->value = clampf(s->value, min, max);
}

cl_widget_t *cl_slider_create(cl_application_t *app, const cl_slider_desc_t *desc)
{
    cl_widget_t *w;
    cl_slider_t *s;

    if (!CL_DESC_ABI_OK(desc, cl_slider_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_slider_class);
    if (!w)
        return NULL;
    s = CL_WIDGET_CAST(cl_slider, w);
    w->flags |= CL_WF_FOCUSABLE;
    if (desc) {
        s->value = desc->value;
        apply_range(s, desc->min, desc->max, desc->step);
    } else {
        apply_range(s, 0.0f, 1.0f, 0.0f);
    }
    return w;
}

void cl_slider_set_value(cl_widget_t *slider, float value)
{
    cl_slider_t *s = CL_WIDGET_CAST(cl_slider, slider);

    if (s)
        set_value(s, value, false);
}

float cl_slider_value(cl_widget_t *slider)
{
    cl_slider_t *s = CL_WIDGET_CAST(cl_slider, slider);

    return s ? s->value : 0.0f;
}

void cl_slider_set_range(cl_widget_t *slider, float min, float max)
{
    cl_slider_t *s = CL_WIDGET_CAST(cl_slider, slider);

    if (!s)
        return;
    /* Keep an explicit step; re-derive an auto step for the new range. */
    apply_range(s, min, max, s->auto_step ? 0.0f : s->step);
    cl_widget_invalidate(slider);
}

void cl_slider_set_on_change(cl_widget_t *slider, cl_value_fn fn, void *user)
{
    cl_slider_t *s = CL_WIDGET_CAST(cl_slider, slider);

    if (!s)
        return;
    s->on_change = fn;
    s->user = user;
}
