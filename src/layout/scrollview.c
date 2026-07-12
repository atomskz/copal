/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/scrollview.h>
#include <copal/widget_impl.h>
#include <copal/timer.h>

#include "widget/widget_internal.h"
#include "core/foundation/foundation_internal.h"

#define SB_SIZE 12.0f      /* scrollbar gutter thickness */
#define SB_MIN_THUMB 24.0f /* minimum thumb length */
#define SB_INSET 2.0f      /* thumb inset within the track */
#define WHEEL_STEP 40.0f   /* pixels scrolled per wheel notch */
#define SV_DEFAULT 160.0f  /* default viewport size when none is set */
#define SMOOTH_TICK_MS 16  /* ~60 Hz animation cadence */
#define SMOOTH_FACTOR 0.3f /* fraction of the remaining distance per tick */
#define SMOOTH_SNAP 1.0f   /* within this many px, snap to the target and stop */

typedef enum sv_drag {
    SV_DRAG_NONE,
    SV_DRAG_V, /* dragging the vertical thumb */
    SV_DRAG_H  /* dragging the horizontal thumb */
} sv_drag_t;

typedef struct cl_scrollview {
    cl_widget_t base;
    float scroll_x; /* current (possibly animating) offset */
    float scroll_y;
    float target_x; /* smooth-scroll destination (== scroll when settled) */
    float target_y;
    float content_w; /* laid-out content width (== view_w unless horizontal) */
    float content_h; /* last measured content height */
    float view_w;    /* content-area width (excludes the vertical gutter) */
    float view_h;    /* content-area height (excludes the horizontal gutter) */
    bool horizontal; /* allow horizontal overflow and scrolling */
    bool smooth;     /* animate wheel scrolling toward the target */
    sv_drag_t drag;  /* which thumb, if any, is being dragged */
    float drag_off;  /* pointer coord - thumb start, on the drag axis */
    cl_timer_t *anim; /* repeating animation timer, or NULL when settled */
} cl_scrollview_t;

static cl_size_t scrollview_measure(cl_widget_t *w, cl_constraints_t c);
static void scrollview_arrange(cl_widget_t *w, cl_rect_t rect);
static void scrollview_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static cl_rect_t scrollview_clip_rect(cl_widget_t *w);
static void scrollview_reveal(cl_widget_t *w, cl_rect_t target);
static bool scrollview_wheel(cl_widget_t *w, const cl_event_t *ev);
static bool scrollview_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool scrollview_mouse_move(cl_widget_t *w, const cl_event_t *ev);
static bool scrollview_mouse_up(cl_widget_t *w, const cl_event_t *ev);
static void scrollview_destroy(cl_widget_t *w);

static const cl_widget_vtable_t scrollview_vtable = {
    .destroy = scrollview_destroy,
    .measure = scrollview_measure,
    .arrange = scrollview_arrange,
    .paint = scrollview_paint,
    .clip_rect = scrollview_clip_rect,
    .reveal = scrollview_reveal,
    .mouse_wheel = scrollview_wheel,
    .mouse_down = scrollview_mouse_down,
    .mouse_move = scrollview_mouse_move,
    .mouse_up = scrollview_mouse_up,
};

static const cl_widget_class_t cl_scrollview_class = {
    .name = "cl_scrollview",
    .base = NULL,
    .type_id = 0x7363726cu, /* 'scrl' */
    .instance_size = sizeof(cl_scrollview_t),
    .vtable = &scrollview_vtable,
};

/* ---- geometry helpers --------------------------------------------------- */

static float sv_max_scroll_x(const cl_scrollview_t *sv)
{
    float m = sv->content_w - sv->view_w;

    return m > 0.0f ? m : 0.0f;
}

static float sv_max_scroll_y(const cl_scrollview_t *sv)
{
    float m = sv->content_h - sv->view_h;

    return m > 0.0f ? m : 0.0f;
}

static bool sv_scroll_x_on(const cl_scrollview_t *sv)
{
    return sv->content_w > sv->view_w;
}

static bool sv_scroll_y_on(const cl_scrollview_t *sv)
{
    return sv->content_h > sv->view_h;
}

static float sv_clampf(float v, float max)
{
    if (v < 0.0f)
        return 0.0f;
    return v > max ? max : v;
}

/* Clamp both the current offset and the smooth-scroll target to [0, max]. */
static void sv_clamp(cl_scrollview_t *sv)
{
    float mx = sv_max_scroll_x(sv);
    float my = sv_max_scroll_y(sv);

    sv->scroll_x = sv_clampf(sv->scroll_x, mx);
    sv->scroll_y = sv_clampf(sv->scroll_y, my);
    sv->target_x = sv_clampf(sv->target_x, mx);
    sv->target_y = sv_clampf(sv->target_y, my);
}

/* Re-place the content child at the current scroll offset without remeasuring. */
static void sv_reposition(cl_scrollview_t *sv)
{
    cl_widget_t *content = sv->base.first_child;

    if (!content)
        return;
    cl_widget_do_arrange(content,
                         (cl_rect_t){ sv->base.rect.x - sv->scroll_x,
                                      sv->base.rect.y - sv->scroll_y,
                                      sv->content_w, sv->content_h });
}

/* ---- smooth scrolling --------------------------------------------------- */

static void sv_stop_anim(cl_scrollview_t *sv)
{
    if (sv->anim) {
        cl_timer_cancel(sv->anim);
        sv->anim = NULL;
    }
}

/* Commit an instant offset: stop any animation and settle target == current. */
static void sv_commit(cl_scrollview_t *sv)
{
    sv_stop_anim(sv);
    sv->target_x = sv->scroll_x;
    sv->target_y = sv->scroll_y;
    sv_clamp(sv);
    sv_reposition(sv);
    cl_widget_invalidate(&sv->base);
}

/* Ease the current offset toward the target each tick; stop once settled. */
static void sv_tick(cl_timer_t *t, void *user)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, user);
    float dx = sv->target_x - sv->scroll_x;
    float dy = sv->target_y - sv->scroll_y;
    float ax = dx < 0.0f ? -dx : dx;
    float ay = dy < 0.0f ? -dy : dy;

    if (ax < SMOOTH_SNAP && ay < SMOOTH_SNAP) {
        sv->scroll_x = sv->target_x;
        sv->scroll_y = sv->target_y;
        cl_timer_cancel(t);
        sv->anim = NULL;
    } else {
        sv->scroll_x += dx * SMOOTH_FACTOR;
        sv->scroll_y += dy * SMOOTH_FACTOR;
    }
    sv_reposition(sv);
    cl_widget_invalidate(&sv->base);
}

/* Start (or keep) the animation toward the current target. */
static void sv_anim_start(cl_scrollview_t *sv)
{
    if (!sv->anim)
        sv->anim = cl_timer_create(sv->base.app, SMOOTH_TICK_MS, true, sv_tick,
                                   &sv->base);
    if (!sv->anim) { /* no clock (e.g. custom backend): settle instantly */
        sv->scroll_x = sv->target_x;
        sv->scroll_y = sv->target_y;
        sv_reposition(sv);
        cl_widget_invalidate(&sv->base);
    }
}

static float sv_thumb_h(const cl_scrollview_t *sv)
{
    float h;

    if (sv->content_h <= 0.0f)
        return sv->view_h;
    h = sv->view_h * (sv->view_h / sv->content_h);
    if (h < SB_MIN_THUMB)
        h = SB_MIN_THUMB;
    if (h > sv->view_h)
        h = sv->view_h;
    return h;
}

static float sv_thumb_y(const cl_scrollview_t *sv)
{
    float max_scroll = sv_max_scroll_y(sv);
    float travel = sv->view_h - sv_thumb_h(sv);
    float t = max_scroll > 0.0f ? sv->scroll_y / max_scroll : 0.0f;

    return sv->base.rect.y + t * travel;
}

static float sv_thumb_w(const cl_scrollview_t *sv)
{
    float wdt;

    if (sv->content_w <= 0.0f)
        return sv->view_w;
    wdt = sv->view_w * (sv->view_w / sv->content_w);
    if (wdt < SB_MIN_THUMB)
        wdt = SB_MIN_THUMB;
    if (wdt > sv->view_w)
        wdt = sv->view_w;
    return wdt;
}

static float sv_thumb_x(const cl_scrollview_t *sv)
{
    float max_scroll = sv_max_scroll_x(sv);
    float travel = sv->view_w - sv_thumb_w(sv);
    float t = max_scroll > 0.0f ? sv->scroll_x / max_scroll : 0.0f;

    return sv->base.rect.x + t * travel;
}

/* ---- layout ------------------------------------------------------------- */

static cl_size_t scrollview_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_size_t out = w->pref_size;

    (void)c;
    if (out.w <= 0.0f)
        out.w = SV_DEFAULT;
    if (out.h <= 0.0f)
        out.h = SV_DEFAULT;
    return out;
}

static void scrollview_arrange(cl_widget_t *w, cl_rect_t rect)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, w);
    cl_widget_t *content = w->first_child;
    cl_constraints_t cc;
    cl_size_t cs;
    float avail_w = rect.w;
    float avail_h = rect.h;
    bool need_v;
    bool need_h;

    if (!content) {
        sv->view_w = rect.w;
        sv->view_h = rect.h;
        sv->content_w = rect.w;
        sv->content_h = 0.0f;
        return;
    }

    /*
     * Measure the content. Without horizontal scrolling the width is bounded to
     * the viewport so wrapping content reflows to fit; with it, the width is
     * unbounded so the content keeps its natural width and overflows sideways.
     */
    cc.min = (cl_size_t){ 0.0f, 0.0f };
    cc.max = (cl_size_t){ sv->horizontal ? CL_UNBOUNDED : avail_w,
                          CL_UNBOUNDED };
    cs = cl_widget_do_measure(content, cc);

    /*
     * Decide which scrollbars are needed. Each bar steals space along its cross
     * axis, which can in turn make the other bar necessary; re-check once.
     */
    need_h = sv->horizontal && cs.w > avail_w;
    need_v = cs.h > avail_h;
    if (need_h)
        avail_h -= SB_SIZE;
    if (need_v)
        avail_w -= SB_SIZE;
    if (!need_h && sv->horizontal && cs.w > avail_w) {
        need_h = true;
        avail_h -= SB_SIZE;
    }
    if (!need_v && cs.h > avail_h) {
        need_v = true;
        avail_w -= SB_SIZE;
    }
    if (avail_w < 0.0f)
        avail_w = 0.0f;
    if (avail_h < 0.0f)
        avail_h = 0.0f;

    if (sv->horizontal) {
        /* Content keeps its natural width, but never narrower than the
         * viewport, so a narrow child can still stretch to fill. */
        sv->content_w = cs.w > avail_w ? cs.w : avail_w;
    } else {
        /* Reflow to the (possibly reduced) viewport width so wrapping content
         * fits; the content width then equals the viewport. */
        if (cc.max.w != avail_w) {
            cc.max.w = avail_w;
            cs = cl_widget_do_measure(content, cc);
        }
        sv->content_w = avail_w;
    }

    sv->view_w = avail_w;
    sv->view_h = avail_h;
    sv->content_h = cs.h;
    sv_clamp(sv);
    cl_widget_do_arrange(content,
                         (cl_rect_t){ rect.x - sv->scroll_x,
                                      rect.y - sv->scroll_y,
                                      sv->content_w, sv->content_h });
}

/* ---- paint -------------------------------------------------------------- */

static void sv_paint_bar(cl_paint_context_t *ctx, cl_rect_t track, cl_rect_t thumb)
{
    cl_paint_fill_rect(ctx, track,
                       cl_paint_theme_color(ctx, CL_COLOR_SURFACE_ACTIVE));
    cl_paint_fill_round_rect(ctx, thumb, (SB_SIZE - 2.0f * SB_INSET) * 0.5f,
                             cl_paint_theme_color(ctx, CL_COLOR_BORDER));
}

static void scrollview_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, w);
    cl_rect_t r = w->rect;

    cl_paint_fill_rect(ctx, r, cl_paint_theme_color(ctx, CL_COLOR_SURFACE));

    if (sv_scroll_y_on(sv)) {
        float th = sv_thumb_h(sv);
        float ty = sv_thumb_y(sv);
        cl_rect_t track = { r.x + r.w - SB_SIZE, r.y, SB_SIZE, sv->view_h };
        cl_rect_t thumb = { track.x + SB_INSET, ty, SB_SIZE - 2.0f * SB_INSET,
                            th };

        sv_paint_bar(ctx, track, thumb);
    }
    if (sv_scroll_x_on(sv)) {
        float tw = sv_thumb_w(sv);
        float tx = sv_thumb_x(sv);
        cl_rect_t track = { r.x, r.y + r.h - SB_SIZE, sv->view_w, SB_SIZE };
        cl_rect_t thumb = { tx, track.y + SB_INSET, tw,
                            SB_SIZE - 2.0f * SB_INSET };

        sv_paint_bar(ctx, track, thumb);
    }
    /* content children are painted next by cl_widget_do_paint, clipped to the
     * viewport via CL_WF_CLIP + scrollview_clip_rect(). */
}

static cl_rect_t scrollview_clip_rect(cl_widget_t *w)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, w);

    return (cl_rect_t){ w->rect.x, w->rect.y, sv->view_w, sv->view_h };
}

/*
 * Minimal 1-D offset to bring the segment [t0, t0+len] inside [v0, v0+view].
 * When the target is larger than the viewport, only its start edge is pinned
 * (never the far edge), so repeated reveals converge to one stable position
 * instead of flip-flopping between start- and end-alignment.
 */
static float sv_reveal_axis(float t0, float len, float v0, float view)
{
    if (len >= view)
        return t0 > v0 ? t0 - v0 : 0.0f; /* oversized: pin the start edge */
    if (t0 < v0)
        return t0 - v0; /* off the near side: scroll back */
    if (t0 + len > v0 + view)
        return (t0 + len) - (v0 + view); /* off the far side: scroll forward */
    return 0.0f;                         /* already fully visible */
}

/* Scroll by the minimal amount so the absolute rect `target` fits the viewport. */
static void sv_reveal_rect(cl_scrollview_t *sv, cl_rect_t target)
{
    cl_rect_t r = sv->base.rect;
    float dx = sv_reveal_axis(target.x, target.w, r.x, sv->view_w);
    float dy = sv_reveal_axis(target.y, target.h, r.y, sv->view_h);

    if (dx == 0.0f && dy == 0.0f)
        return;
    sv->scroll_x += dx;
    sv->scroll_y += dy;
    sv_commit(sv); /* reveal is instant, even in smooth mode */
}

static void scrollview_reveal(cl_widget_t *w, cl_rect_t target)
{
    sv_reveal_rect(CL_WIDGET_CAST(cl_scrollview, w), target);
}

/* ---- input -------------------------------------------------------------- */

/* True if an ancestor is a scrollview that can consume a vertical wheel. */
static bool sv_has_vertical_ancestor(const cl_widget_t *w)
{
    cl_widget_t *p;

    for (p = w->parent; p; p = p->parent) {
        cl_scrollview_t *anc = CL_WIDGET_CAST(cl_scrollview, p);

        if (anc && sv_scroll_y_on(anc))
            return true;
    }
    return false;
}

static bool scrollview_wheel(cl_widget_t *w, const cl_event_t *ev)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, w);
    float before_x = sv->target_x;
    float before_y = sv->target_y;

    if (sv_scroll_y_on(sv))
        sv->target_y -= ev->data.wheel.dy * WHEEL_STEP;
    if (sv_scroll_x_on(sv)) {
        float dx = ev->data.wheel.dx;

        /*
         * A horizontal-only view scrolls sideways on the vertical wheel too,
         * but only when it stands alone: if an outer scrollview can scroll
         * vertically, leave the vertical delta for it so the wheel is not
         * trapped by a nested horizontal strip.
         */
        if (dx == 0.0f && !sv_scroll_y_on(sv) && !sv_has_vertical_ancestor(w))
            dx = ev->data.wheel.dy;
        sv->target_x -= dx * WHEEL_STEP;
    }
    sv_clamp(sv);
    if (sv->target_x == before_x && sv->target_y == before_y)
        return false; /* nothing moved: bubble to an outer container */
    if (sv->smooth) {
        sv_anim_start(sv); /* ease toward the new target */
    } else {
        sv->scroll_x = sv->target_x;
        sv->scroll_y = sv->target_y;
        sv_reposition(sv);
        cl_widget_invalidate(w);
    }
    return true;
}

static bool scrollview_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, w);
    cl_rect_t r = w->rect;
    float x = ev->data.mouse.pos.x;
    float y = ev->data.mouse.pos.y;

    /* Vertical thumb / track: the right gutter, above the horizontal one so
     * the empty bottom-right corner (both bars visible) stays inert. */
    if (sv_scroll_y_on(sv) && x >= r.x + r.w - SB_SIZE &&
        y < r.y + sv->view_h) {
        float th = sv_thumb_h(sv);
        float ty = sv_thumb_y(sv);

        if (y >= ty && y < ty + th) {
            sv->drag = SV_DRAG_V;
            sv->drag_off = y - ty;
        } else { /* page towards the click */
            sv->scroll_y += (y < ty ? -sv->view_h : sv->view_h);
            sv_commit(sv);
        }
        return true;
    }
    /* Horizontal thumb / track: the bottom gutter, left of the vertical one. */
    if (sv_scroll_x_on(sv) && y >= r.y + r.h - SB_SIZE &&
        x < r.x + sv->view_w) {
        float tw = sv_thumb_w(sv);
        float tx = sv_thumb_x(sv);

        if (x >= tx && x < tx + tw) {
            sv->drag = SV_DRAG_H;
            sv->drag_off = x - tx;
        } else { /* page towards the click */
            sv->scroll_x += (x < tx ? -sv->view_w : sv->view_w);
            sv_commit(sv);
        }
        return true;
    }
    return false; /* click was in the content area: let a child handle it */
}

static bool scrollview_mouse_move(cl_widget_t *w, const cl_event_t *ev)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, w);

    if (sv->drag == SV_DRAG_V) {
        float travel = sv->view_h - sv_thumb_h(sv);
        float ty;

        if (travel <= 0.0f)
            return true; /* degenerate track: nothing to drag */
        ty = ev->data.mouse.pos.y - sv->drag_off - sv->base.rect.y;
        sv->scroll_y = (ty / travel) * sv_max_scroll_y(sv);
        sv_commit(sv);
        return true;
    }
    if (sv->drag == SV_DRAG_H) {
        float travel = sv->view_w - sv_thumb_w(sv);
        float tx;

        if (travel <= 0.0f)
            return true;
        tx = ev->data.mouse.pos.x - sv->drag_off - sv->base.rect.x;
        sv->scroll_x = (tx / travel) * sv_max_scroll_x(sv);
        sv_commit(sv);
        return true;
    }
    return false;
}

static bool scrollview_mouse_up(cl_widget_t *w, const cl_event_t *ev)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, w);

    (void)ev;
    if (sv->drag == SV_DRAG_NONE)
        return false;
    sv->drag = SV_DRAG_NONE;
    return true;
}

/* ---- public ------------------------------------------------------------- */

cl_widget_t *cl_scrollview_create(cl_application_t *app,
                                  const cl_scrollview_desc_t *desc)
{
    cl_widget_t *w;
    cl_scrollview_t *sv;

    if (!CL_DESC_ABI_OK(desc, cl_scrollview_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_scrollview_class);
    if (!w)
        return NULL;
    sv = CL_WIDGET_CAST(cl_scrollview, w);
    if (desc) {
        sv->horizontal = desc->horizontal;
        sv->smooth = desc->smooth;
    }
    w->flags |= CL_WF_CLIP;
    return w;
}

static void scrollview_destroy(cl_widget_t *w)
{
    sv_stop_anim(CL_WIDGET_CAST(cl_scrollview, w));
}

void cl_scrollview_set_content(cl_widget_t *sv_w, cl_widget_t *content)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, sv_w);
    cl_widget_t *old;

    if (!sv)
        return;
    old = sv_w->first_child;
    if (old == content)
        return;
    if (old)
        cl_widget_destroy(old);
    sv_stop_anim(sv);
    sv->scroll_x = 0.0f;
    sv->scroll_y = 0.0f;
    sv->target_x = 0.0f;
    sv->target_y = 0.0f;
    sv->drag = SV_DRAG_NONE;
    if (content) {
        /*
         * The scrollview takes ownership. If the widget is still parented
         * elsewhere, cl_widget_add_child refuses it; destroy it rather than
         * leak, so the take-ownership contract holds even on misuse.
         */
        if (cl_widget_add_child(sv_w, content) != CL_OK)
            cl_widget_destroy(content);
    }
    cl_widget_invalidate_layout(sv_w);
}

cl_widget_t *cl_scrollview_content(cl_widget_t *sv_w)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, sv_w);

    return sv ? sv_w->first_child : NULL;
}

void cl_scrollview_scroll_to(cl_widget_t *sv_w, float y)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, sv_w);

    if (!sv)
        return;
    sv->scroll_y = y;
    sv_commit(sv);
}

void cl_scrollview_scroll_to_x(cl_widget_t *sv_w, float x)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, sv_w);

    if (!sv)
        return;
    sv->scroll_x = x;
    sv_commit(sv);
}

float cl_scrollview_scroll_y(cl_widget_t *sv_w)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, sv_w);

    return sv ? sv->scroll_y : 0.0f;
}

float cl_scrollview_scroll_x(cl_widget_t *sv_w)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, sv_w);

    return sv ? sv->scroll_x : 0.0f;
}

void cl_scrollview_scroll_to_widget(cl_widget_t *sv_w, cl_widget_t *descendant)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, sv_w);

    if (!sv || !descendant)
        return;
    sv_reveal_rect(sv, descendant->rect);
}
