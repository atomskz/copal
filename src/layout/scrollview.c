/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/scrollview.h>
#include <copal/widget_impl.h>

#include "widget/widget_internal.h"
#include "core/foundation/foundation_internal.h"

#define SB_SIZE 12.0f     /* scrollbar gutter thickness */
#define SB_MIN_THUMB 24.0f /* minimum thumb length */
#define SB_INSET 2.0f      /* thumb inset within the track */
#define WHEEL_STEP 40.0f   /* pixels scrolled per wheel notch */
#define SV_DEFAULT 160.0f  /* default viewport size when none is set */

typedef struct cl_scrollview {
    cl_widget_t base;
    float scroll_y;
    float content_h; /* last measured content height */
    float view_w;    /* content-area width (excludes the scrollbar gutter) */
    float view_h;    /* content-area height */
    bool dragging;   /* dragging the vertical thumb */
    float drag_dy;   /* pointer.y - thumb.y captured at drag start */
} cl_scrollview_t;

static cl_size_t scrollview_measure(cl_widget_t *w, cl_constraints_t c);
static void scrollview_arrange(cl_widget_t *w, cl_rect_t rect);
static void scrollview_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool scrollview_wheel(cl_widget_t *w, const cl_event_t *ev);
static bool scrollview_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool scrollview_mouse_move(cl_widget_t *w, const cl_event_t *ev);
static bool scrollview_mouse_up(cl_widget_t *w, const cl_event_t *ev);

static const cl_widget_vtable_t scrollview_vtable = {
    .measure = scrollview_measure,
    .arrange = scrollview_arrange,
    .paint = scrollview_paint,
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

static float sv_max_scroll(const cl_scrollview_t *sv)
{
    float m = sv->content_h - sv->view_h;

    return m > 0.0f ? m : 0.0f;
}

static bool sv_scrollable(const cl_scrollview_t *sv)
{
    return sv->content_h > sv->view_h;
}

static void sv_clamp(cl_scrollview_t *sv)
{
    float m = sv_max_scroll(sv);

    if (sv->scroll_y < 0.0f)
        sv->scroll_y = 0.0f;
    if (sv->scroll_y > m)
        sv->scroll_y = m;
}

/* Re-place the content child at the current scroll offset without remeasuring. */
static void sv_reposition(cl_scrollview_t *sv)
{
    cl_widget_t *content = sv->base.first_child;

    if (!content)
        return;
    cl_widget_do_arrange(content,
                         (cl_rect_t){ sv->base.rect.x,
                                      sv->base.rect.y - sv->scroll_y,
                                      sv->view_w, sv->content_h });
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
    float max_scroll = sv_max_scroll(sv);
    float travel = sv->view_h - sv_thumb_h(sv);
    float t = max_scroll > 0.0f ? sv->scroll_y / max_scroll : 0.0f;

    return sv->base.rect.y + t * travel;
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
    float vw = rect.w;
    bool need_v;

    if (!content) {
        sv->view_w = rect.w;
        sv->view_h = rect.h;
        sv->content_h = 0.0f;
        return;
    }

    cc.min = (cl_size_t){ 0.0f, 0.0f };
    cc.max = (cl_size_t){ vw, CL_UNBOUNDED };
    cs = cl_widget_do_measure(content, cc);

    need_v = cs.h > rect.h;
    if (need_v) {
        vw -= SB_SIZE;
        if (vw < 0.0f)
            vw = 0.0f;
        cc.max.w = vw;
        cs = cl_widget_do_measure(content, cc); /* reflow at reduced width */
    }

    sv->view_w = vw;
    sv->view_h = rect.h;
    sv->content_h = cs.h;
    sv_clamp(sv);
    cl_widget_do_arrange(content,
                         (cl_rect_t){ rect.x, rect.y - sv->scroll_y, vw, cs.h });
}

/* ---- paint -------------------------------------------------------------- */

static void scrollview_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, w);
    cl_rect_t r = w->rect;

    cl_paint_fill_rect(ctx, r, cl_paint_theme_color(ctx, CL_COLOR_SURFACE));

    if (sv_scrollable(sv)) {
        float th = sv_thumb_h(sv);
        float ty = sv_thumb_y(sv);
        cl_rect_t track = { r.x + r.w - SB_SIZE, r.y, SB_SIZE, r.h };
        cl_rect_t thumb = { track.x + SB_INSET, ty, SB_SIZE - 2.0f * SB_INSET,
                            th };

        cl_paint_fill_rect(ctx, track,
                           cl_paint_theme_color(ctx, CL_COLOR_SURFACE_ACTIVE));
        cl_paint_fill_round_rect(ctx, thumb, (SB_SIZE - 2.0f * SB_INSET) * 0.5f,
                                 cl_paint_theme_color(ctx, CL_COLOR_BORDER));
    }
    /* content children are painted next by cl_widget_do_paint, clipped to r
     * via CL_WF_CLIP set at construction. */
}

/* ---- input -------------------------------------------------------------- */

static bool scrollview_wheel(cl_widget_t *w, const cl_event_t *ev)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, w);
    float before;

    if (!sv_scrollable(sv))
        return false; /* let an outer scroll container handle it */
    before = sv->scroll_y;
    sv->scroll_y -= ev->data.wheel.dy * WHEEL_STEP;
    sv_clamp(sv);
    if (sv->scroll_y == before)
        return false; /* already at the edge: bubble to an outer container */
    sv_reposition(sv);
    cl_widget_invalidate(w);
    return true;
}

static bool scrollview_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, w);
    float y = ev->data.mouse.pos.y;
    float th;
    float ty;

    if (!sv_scrollable(sv))
        return false;
    th = sv_thumb_h(sv);
    ty = sv_thumb_y(sv);

    if (y >= ty && y < ty + th) {
        sv->dragging = true;
        sv->drag_dy = y - ty;
    } else {
        /* page towards the click */
        sv->scroll_y += (y < ty ? -sv->view_h : sv->view_h);
        sv_clamp(sv);
        sv_reposition(sv);
        cl_widget_invalidate(w);
    }
    return true;
}

static bool scrollview_mouse_move(cl_widget_t *w, const cl_event_t *ev)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, w);
    float travel;
    float ty;

    if (!sv->dragging)
        return false;
    travel = sv->view_h - sv_thumb_h(sv);
    if (travel <= 0.0f)
        return true; /* degenerate track (viewport <= min thumb): nothing to drag */
    ty = ev->data.mouse.pos.y - sv->drag_dy - sv->base.rect.y;
    sv->scroll_y = (ty / travel) * sv_max_scroll(sv);
    sv_clamp(sv);
    sv_reposition(sv);
    cl_widget_invalidate(w);
    return true;
}

static bool scrollview_mouse_up(cl_widget_t *w, const cl_event_t *ev)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, w);

    (void)ev;
    if (!sv->dragging)
        return false;
    sv->dragging = false;
    return true;
}

/* ---- public ------------------------------------------------------------- */

cl_widget_t *cl_scrollview_create(cl_application_t *app,
                                  const cl_scrollview_desc_t *desc)
{
    cl_widget_t *w;

    if (desc && (desc->struct_size != sizeof(cl_scrollview_desc_t) ||
                 desc->abi_version != CL_VERSION)) {
        cl_set_last_error(CL_ERROR_ABI_MISMATCH);
        return NULL;
    }
    w = cl_widget_alloc(app, &cl_scrollview_class);
    if (!w)
        return NULL;
    w->flags |= CL_WF_CLIP;
    return w;
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
    sv->scroll_y = 0.0f;
    sv->dragging = false;
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
    sv_clamp(sv);
    sv_reposition(sv);
    cl_widget_invalidate(sv_w);
}

float cl_scrollview_scroll_y(cl_widget_t *sv_w)
{
    cl_scrollview_t *sv = CL_WIDGET_CAST(cl_scrollview, sv_w);

    return sv ? sv->scroll_y : 0.0f;
}
