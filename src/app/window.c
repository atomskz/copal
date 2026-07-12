/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/window.h>
#include <copal/allocator.h>

#include <string.h>

#include "app/app_internal.h"
#include "widget/widget_internal.h"
#include "widgets/tooltip_internal.h"
#include "render/paint_context.h"
#include "core/foundation/foundation_internal.h"

static void tooltip_dismiss(cl_window_t *win); /* defined with the hover layer */

cl_window_t *cl_window_create(cl_application_t *app, const cl_window_desc_t *desc)
{
    cl_window_t *win;
    cl_result_t r;

    if (!app)
        return NULL;
    if (!desc || desc->struct_size != sizeof(cl_window_desc_t) ||
        desc->abi_version != CL_VERSION) {
        cl_set_last_error(CL_ERROR_ABI_MISMATCH);
        return NULL;
    }
    if (app->window) {
        cl_set_last_error(CL_ERROR_UNSUPPORTED); /* single window in MVP */
        return NULL;
    }

    r = app->platform->ops->create_window(app->platform, desc);
    if (r != CL_OK) {
        cl_set_last_error(r);
        return NULL;
    }

    win = cl_alloc(&app->alloc, sizeof(*win));
    if (!win)
        return NULL;
    memset(win, 0, sizeof(*win));
    win->app = app;
    win->size.w = (float)desc->width;
    win->size.h = (float)desc->height;
    win->scale = app->platform->ops->scale(app->platform);
    if (win->scale <= 0.0f)
        win->scale = 1.0f;
    win->dirty = true;
    win->layout_dirty = true;
    app->window = win;
    app->platform->ops->start_text_input(app->platform, true);
    return win;
}

void cl_window_destroy(cl_window_t *win)
{
    cl_application_t *app;

    if (!win)
        return;
    tooltip_dismiss(win); /* cancel the dwell timer and free the bubble first */
    if (win->overlay) {
        cl_widget_t *o = win->overlay;

        win->overlay = NULL; /* clear first: content destroy may re-enter here */
        win->overlay_owner = NULL;
        cl_widget_destroy(o);
    }
    if (win->content)
        cl_widget_destroy(win->content);
    app = win->app;
    if (app->window == win)
        app->window = NULL;
    cl_free(&app->alloc, win);
}

void cl_window_show(cl_window_t *win)
{
    win->dirty = true;
}

void cl_window_set_content(cl_window_t *win, cl_widget_t *root)
{
    if (win->content == root)
        return;
    /* The window owns its root: replacing destroys the previous subtree
     * (mirrors cl_scrollview_set_content). */
    if (win->content) {
        cl_widget_t *old = win->content;

        win->content = NULL; /* clear first: destroy may re-enter the window */
        cl_widget_destroy(old);
    }
    win->content = root;
    if (root)
        cl_widget_set_window(root, win);
    win->layout_dirty = true;
    win->dirty = true;
}

cl_widget_t *cl_window_content(cl_window_t *win)
{
    return win->content;
}

void cl_window_set_title(cl_window_t *win, const char *utf8)
{
    win->app->platform->ops->set_title(win->app->platform, utf8);
}

cl_size_t cl_window_size(cl_window_t *win)
{
    return win->size;
}

void cl_window_set_on_close(cl_window_t *win, cl_window_close_fn fn, void *user)
{
    win->on_close = fn;
    win->on_close_user = user;
}

void cl_window_mark_dirty(cl_window_t *win)
{
    win->dirty = true;
}

void cl_window_mark_layout_dirty(cl_window_t *win)
{
    win->layout_dirty = true;
    win->dirty = true;
}

void cl_window_resize(cl_window_t *win, cl_size_t size)
{
    win->size = size;
    win->layout_dirty = true;
    win->dirty = true;
}

/* ---- overlay popups ----------------------------------------------------- */

/* Measure the overlay and arrange it at its anchor, clamped on-screen. */
static void place_overlay(cl_window_t *win)
{
    cl_constraints_t c;
    cl_size_t sz;
    float x = win->overlay_anchor.x;
    float y = win->overlay_anchor.y;

    c.min = (cl_size_t){ 0.0f, 0.0f };
    c.max = (cl_size_t){ CL_UNBOUNDED, CL_UNBOUNDED };
    sz = cl_widget_do_measure(win->overlay, c);

    /* Keep the popup on-screen: shift left/up on overflow, clamp to origin. */
    if (x + sz.w > win->size.w)
        x = win->size.w - sz.w;
    if (y + sz.h > win->size.h)
        y = win->size.h - sz.h;
    if (x < 0.0f)
        x = 0.0f;
    if (y < 0.0f)
        y = 0.0f;
    cl_widget_do_arrange(win->overlay, (cl_rect_t){ x, y, sz.w, sz.h });
}

void cl_window_open_popup(cl_window_t *win, cl_widget_t *popup, cl_point_t at)
{
    if (!win || !popup)
        return;
    tooltip_dismiss(win); /* a popup supersedes any hover tooltip */
    if (win->overlay && win->overlay != popup)
        cl_widget_destroy(win->overlay); /* replace any existing popup */
    win->overlay = popup;
    win->overlay_owner = NULL;
    win->overlay_anchor = at;
    win->overlay_closing = false;
    cl_widget_set_window(popup, win);
    place_overlay(win);
    cl_window_mark_dirty(win);
}

void cl_window_set_overlay_owner(cl_window_t *win, cl_widget_t *owner)
{
    if (win)
        win->overlay_owner = owner;
}

void cl_window_owner_destroyed(cl_window_t *win, cl_widget_t *w)
{
    if (win && win->overlay && win->overlay_owner == w) {
        cl_widget_t *o = win->overlay;

        win->overlay = NULL;
        win->overlay_owner = NULL;
        win->overlay_closing = false;
        cl_widget_destroy(o); /* the popup references w, which is dying */
        cl_window_mark_dirty(win);
    }
}

void cl_window_close_popup(cl_window_t *win)
{
    if (win && win->overlay) {
        win->overlay_closing = true; /* reaped after event dispatch */
        cl_window_mark_dirty(win);
    }
}

cl_widget_t *cl_window_popup(cl_window_t *win)
{
    return win ? win->overlay : NULL;
}

void cl_window_reap_overlay(cl_window_t *win)
{
    if (win && win->overlay && win->overlay_closing) {
        cl_widget_t *o = win->overlay;

        win->overlay = NULL;
        win->overlay_owner = NULL;
        win->overlay_closing = false;
        cl_widget_destroy(o);
        cl_window_mark_dirty(win);
    }
}

/* ---- hover tooltips ----------------------------------------------------- */

#define TOOLTIP_DWELL_MS 500
#define TOOLTIP_GAP 18.0f /* vertical offset of the bubble from the cursor */

/* Nearest widget under `pos` (itself or an ancestor) that carries a tooltip and
 * is visible + enabled, or NULL. */
static cl_widget_t *tooltip_target_at(cl_window_t *win, cl_point_t pos)
{
    cl_widget_t *w;

    if (!win->content)
        return NULL;
    w = cl_widget_hit(win->content, pos);
    for (; w; w = w->parent) {
        const uint32_t vis = CL_WF_VISIBLE | CL_WF_ENABLED;

        if (w->tooltip && (w->flags & vis) == vis)
            return w;
    }
    return NULL;
}

/* True while the current tooltip target is still shown on screen: it and every
 * ancestor are visible and it is enabled. A target hidden/disabled/collapsed
 * without an intervening pointer move must not keep a bubble painted. */
static bool tooltip_target_live(cl_window_t *win)
{
    cl_widget_t *w = win->tooltip_target;

    if (!w || !(w->flags & CL_WF_ENABLED))
        return false;
    for (; w; w = w->parent) {
        if (!(w->flags & CL_WF_VISIBLE))
            return false;
    }
    return true;
}

/* Measure the bubble and place it near the anchor, clamped on-screen. */
static void tooltip_place(cl_window_t *win)
{
    cl_constraints_t c;
    cl_size_t sz;
    float x = win->tooltip_anchor.x;
    float y = win->tooltip_anchor.y + TOOLTIP_GAP;

    c.min = (cl_size_t){ 0.0f, 0.0f };
    c.max = (cl_size_t){ CL_UNBOUNDED, CL_UNBOUNDED };
    sz = cl_widget_do_measure(win->tooltip, c);
    if (x + sz.w > win->size.w)
        x = win->size.w - sz.w;
    if (y + sz.h > win->size.h)
        y = win->tooltip_anchor.y - sz.h; /* no room below: flip above */
    if (x < 0.0f)
        x = 0.0f;
    if (y < 0.0f)
        y = 0.0f;
    cl_widget_do_arrange(win->tooltip, (cl_rect_t){ x, y, sz.w, sz.h });
}

static void tooltip_cancel_timer(cl_window_t *win)
{
    if (win->tooltip_timer) {
        cl_timer_cancel(win->tooltip_timer);
        win->tooltip_timer = NULL;
    }
}

/* Hide the bubble, cancel any pending dwell, and forget the target. */
static void tooltip_dismiss(cl_window_t *win)
{
    tooltip_cancel_timer(win);
    if (win->tooltip) {
        cl_widget_destroy(win->tooltip);
        win->tooltip = NULL;
        cl_window_mark_dirty(win);
    }
    win->tooltip_target = NULL;
}

static void tooltip_on_dwell(cl_timer_t *timer, void *user)
{
    cl_window_t *win = user;
    cl_widget_t *tgt = win->tooltip_target;

    win->tooltip_timer = NULL; /* this one-shot has fired */
    cl_timer_cancel(timer);    /* free it (deferred while the pass runs) */
    if (!tgt || !tgt->tooltip)
        return;
    win->tooltip = cl_tooltip_create(win->app, tgt->tooltip);
    if (win->tooltip) {
        tooltip_place(win);
        cl_window_mark_dirty(win);
    }
}

/* Update the hover state on pointer motion (called only when not dragging). */
static void tooltip_hover(cl_window_t *win, cl_point_t pos)
{
    cl_widget_t *tgt = tooltip_target_at(win, pos);

    if (tgt != win->tooltip_target) {
        tooltip_dismiss(win); /* left the previous target */
        win->tooltip_target = tgt;
        if (tgt) {
            win->tooltip_anchor = pos;
            win->tooltip_timer = cl_timer_create(
                win->app, TOOLTIP_DWELL_MS, false, tooltip_on_dwell, win);
        }
    } else if (tgt && !win->tooltip) {
        win->tooltip_anchor = pos; /* not shown yet: keep near the cursor */
    }
}

void cl_window_tooltip_target_gone(cl_window_t *win, cl_widget_t *w)
{
    if (win && win->tooltip_target == w)
        tooltip_dismiss(win);
}

cl_widget_t *cl_window_tooltip(cl_window_t *win)
{
    return win ? win->tooltip : NULL;
}

void cl_window_render(cl_window_t *win)
{
    cl_application_t *app = win->app;
    struct cl_paint_context ctx;

    /* Drop a bubble whose target was hidden, disabled, or collapsed since the
     * last pointer move (the hover layer otherwise only reconciles on motion). */
    if (win->tooltip && !tooltip_target_live(win))
        tooltip_dismiss(win);

    if (win->layout_dirty) {
        if (win->content) {
            cl_constraints_t c;

            c.min = (cl_size_t){ 0, 0 };
            c.max = win->size;
            cl_widget_do_measure(win->content, c);
            cl_widget_do_arrange(
                win->content, (cl_rect_t){ 0, 0, win->size.w, win->size.h });
        }
        if (win->overlay && !win->overlay_closing)
            place_overlay(win); /* re-clamp the popup against the new size */
        if (win->tooltip)
            tooltip_place(win); /* re-clamp the bubble against the new size */
        win->layout_dirty = false;
    }

    /*
     * Reveal the focused widget against fresh geometry. This is what makes
     * scroll-to-focus work when focus was set before the first layout (the
     * immediate reveal in set_focus then ran against zero rects). It only fires
     * on a focus change, not on every relayout, so it never fights scrolling.
     */
    if (win->focus_reveal_pending) {
        if (win->focus)
            cl_widget_reveal(win->focus);
        win->focus_reveal_pending = false;
    }

    app->renderer->ops->begin_frame(app->renderer, win->size, win->scale,
                                    cl_theme_color(app->theme,
                                                   CL_COLOR_BACKGROUND));
    ctx.renderer = app->renderer;
    ctx.theme = app->theme;
    if (win->content)
        cl_widget_do_paint(win->content, &ctx);
    if (win->overlay && !win->overlay_closing)
        cl_widget_do_paint(win->overlay, &ctx); /* popups paint on top */
    if (win->tooltip)
        cl_widget_do_paint(win->tooltip, &ctx); /* tooltip paints on top */
    app->renderer->ops->end_frame(app->renderer);
    app->platform->ops->present(app->platform);
    win->dirty = false;
}

void cl_window_handle_mouse(cl_window_t *win, cl_platform_event_kind_t kind,
                            cl_point_t pos, cl_mouse_button_t button)
{
    cl_event_t ev;
    cl_widget_t *target;

    memset(&ev, 0, sizeof(ev));
    ev.mods = CL_MOD_NONE;
    ev.data.mouse.pos = pos;
    ev.data.mouse.button = button;

    /*
     * An open popup captures pointer input; a press outside dismisses it. A
     * popup whose close was requested this frame keeps capturing until it is
     * reaped (before the next render), so no queued event leaks to the content.
     */
    if (win->overlay) {
        cl_widget_t *ov = win->overlay;

        if (kind == CL_PEV_MOUSE_DOWN && !cl_rect_contains(ov->rect, pos)) {
            cl_window_close_popup(win);
            return;
        }
        ev.type = kind == CL_PEV_MOUSE_DOWN ? CL_EVENT_MOUSE_DOWN
                  : kind == CL_PEV_MOUSE_UP ? CL_EVENT_MOUSE_UP
                                            : CL_EVENT_MOUSE_MOVE;
        cl_widget_dispatch(ov, &ev);
        return;
    }

    if (!win->content)
        return;

    if (kind == CL_PEV_MOUSE_DOWN) {
        cl_widget_t *f;

        tooltip_dismiss(win); /* a click removes any tooltip */
        ev.type = CL_EVENT_MOUSE_DOWN;
        target = cl_widget_hit(win->content, pos);
        win->mouse_target = target;
        /*
         * Move focus to the nearest focusable ancestor of the click. Clicking
         * non-focusable chrome (a scrollbar, a button) leaves the current focus
         * untouched, so it does not interrupt an active editing session.
         */
        for (f = target; f && !(f->flags & CL_WF_FOCUSABLE); f = f->parent)
            ;
        if (f)
            cl_window_set_focus(win, f);
    } else if (kind == CL_PEV_MOUSE_UP) {
        ev.type = CL_EVENT_MOUSE_UP;
        target = win->mouse_target ? win->mouse_target
                                   : cl_widget_hit(win->content, pos);
        win->mouse_target = NULL;
    } else {
        ev.type = CL_EVENT_MOUSE_MOVE;
        target = win->mouse_target ? win->mouse_target
                                   : cl_widget_hit(win->content, pos);
        if (!win->mouse_target)
            tooltip_hover(win, pos); /* not dragging: update hover tooltip */
    }

    if (target)
        cl_widget_dispatch(target, &ev);
}

void cl_window_handle_wheel(cl_window_t *win, cl_point_t pos, float dx,
                            float dy)
{
    cl_event_t ev;
    cl_widget_t *target;

    memset(&ev, 0, sizeof(ev));
    ev.type = CL_EVENT_MOUSE_WHEEL;
    ev.mods = CL_MOD_NONE;
    ev.data.wheel.pos = pos;
    ev.data.wheel.dx = dx;
    ev.data.wheel.dy = dy;

    /* While a popup is open, the wheel is captured (does not scroll content). */
    if (win->overlay) {
        if (cl_rect_contains(win->overlay->rect, pos))
            cl_widget_dispatch(win->overlay, &ev);
        return;
    }

    if (!win->content)
        return;
    tooltip_dismiss(win); /* scrolling moves content out from under the bubble */
    target = cl_widget_hit(win->content, pos);
    if (target)
        cl_widget_dispatch(target, &ev);
}

void cl_window_set_focus(cl_window_t *win, cl_widget_t *w)
{
    cl_widget_t *old = win->focus;

    if (old != w) {
        win->focus = w;
        if (old) {
            if (old->cls->vtable && old->cls->vtable->focus_lost)
                old->cls->vtable->focus_lost(old);
            cl_window_mark_dirty(win);
        }
        if (w) {
            if (w->cls->vtable && w->cls->vtable->focus_gained)
                w->cls->vtable->focus_gained(w);
            cl_window_mark_dirty(win);
        }
    }
    /*
     * Scroll the focus into view if it sits in a scroller. Reveal fires even on
     * a redundant focus (old == w) so re-focusing a scrolled-away widget brings
     * it back, and is retried after the next layout (below) because the reveal
     * here runs against the current rects, which may still be stale/zero before
     * the first arrange.
     */
    if (w) {
        win->focus_reveal_pending = true;
        cl_widget_reveal(w);
    }
}

void cl_window_handle_key(cl_window_t *win, cl_platform_event_kind_t kind,
                          cl_key_t key, cl_key_mods_t mods)
{
    cl_event_t ev;
    bool handled = false;

    memset(&ev, 0, sizeof(ev));
    ev.type = kind == CL_PEV_KEY_DOWN ? CL_EVENT_KEY_DOWN : CL_EVENT_KEY_UP;
    ev.mods = mods;
    ev.data.key.key = key;

    /* An open popup captures the keyboard (menu navigation, Escape). */
    if (win->overlay) {
        cl_widget_dispatch(win->overlay, &ev);
        return;
    }

    if (kind == CL_PEV_KEY_DOWN)
        tooltip_dismiss(win); /* keyboard activity dismisses a hover tooltip */
    if (win->focus)
        handled = cl_widget_dispatch(win->focus, &ev);
    if (!handled && kind == CL_PEV_KEY_DOWN && key == CL_KEY_TAB)
        cl_window_focus_next(win, !(mods & CL_MOD_SHIFT));
}

void cl_window_handle_text(cl_window_t *win, const char *utf8)
{
    cl_event_t ev;

    if (win->overlay || !win->focus || !utf8 || !utf8[0])
        return; /* a popup swallows text input */
    memset(&ev, 0, sizeof(ev));
    ev.type = CL_EVENT_TEXT_INPUT;
    ev.data.text.utf8 = utf8;
    cl_widget_dispatch(win->focus, &ev);
}

void cl_window_handle_text_edit(cl_window_t *win, const char *utf8, int cursor)
{
    cl_event_t ev;

    /* An empty composition string is meaningful: it clears the pre-edit. */
    if (win->overlay || !win->focus || !utf8)
        return;
    memset(&ev, 0, sizeof(ev));
    ev.type = CL_EVENT_TEXT_EDIT;
    ev.data.edit.utf8 = utf8;
    ev.data.edit.cursor = cursor;
    cl_widget_dispatch(win->focus, &ev);
}

static const uint32_t CL_FOCUS_NEED =
    CL_WF_FOCUSABLE | CL_WF_VISIBLE | CL_WF_ENABLED;

static int count_focusable(cl_widget_t *w)
{
    cl_widget_t *c;
    int n;

    if (!(w->flags & CL_WF_VISIBLE))
        return 0;
    n = (w->flags & CL_FOCUS_NEED) == CL_FOCUS_NEED ? 1 : 0;
    for (c = w->first_child; c; c = c->next_sibling)
        n += count_focusable(c);
    return n;
}

static void collect_focusable(cl_widget_t *w, cl_widget_t **arr, int *n)
{
    cl_widget_t *c;

    if (!(w->flags & CL_WF_VISIBLE))
        return;
    if ((w->flags & CL_FOCUS_NEED) == CL_FOCUS_NEED)
        arr[(*n)++] = w;
    for (c = w->first_child; c; c = c->next_sibling)
        collect_focusable(c, arr, n);
}

void cl_window_focus_next(cl_window_t *win, bool forward)
{
    cl_widget_t **arr;
    int total;
    int n = 0;
    int cur = -1;
    int next;
    int i;

    if (!win->content)
        return;
    total = count_focusable(win->content);
    if (total == 0)
        return;
    arr = cl_alloc(&win->app->alloc, (size_t)total * sizeof(*arr));
    if (!arr)
        return;
    collect_focusable(win->content, arr, &n);
    for (i = 0; i < n; i++) {
        if (arr[i] == win->focus) {
            cur = i;
            break;
        }
    }
    if (cur < 0)
        next = forward ? 0 : n - 1;
    else if (forward)
        next = (cur + 1) % n;
    else
        next = (cur - 1 + n) % n;
    cl_window_set_focus(win, arr[next]);
    cl_free(&win->app->alloc, arr);
}
