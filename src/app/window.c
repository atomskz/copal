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
static void window_update_hover(cl_window_t *win, cl_widget_t *w);
static void overlay_drop(cl_window_t *win, int i); /* overlay stack below */
static void overlay_request_close_from(cl_window_t *win, int from);
static int overlay_chain_base(cl_window_t *win);

/* ---- the widget-host interface (widget_host.h) --------------------------- */
/* The host object is the window's first member, so the cast is the identity. */

static cl_window_t *host_win(cl_widget_host_t *h)
{
    return (cl_window_t *)h;
}

static void host_mark_dirty(cl_widget_host_t *h)
{
    cl_window_mark_dirty(host_win(h));
}

static void host_mark_layout_dirty(cl_widget_host_t *h)
{
    cl_window_mark_layout_dirty(host_win(h));
}

static void host_damage(cl_widget_host_t *h, cl_rect_t rect)
{
    cl_window_damage(host_win(h), rect);
}

static void host_set_focus(cl_widget_host_t *h, cl_widget_t *w)
{
    cl_window_set_focus(host_win(h), w);
}

static cl_widget_t *host_focused(cl_widget_host_t *h)
{
    return host_win(h)->focus;
}

static void host_open_popup(cl_widget_host_t *h, cl_widget_t *owner,
                            cl_widget_t *popup, cl_point_t anchor)
{
    cl_window_open_popup(host_win(h), popup, anchor);
    cl_window_set_overlay_owner(host_win(h), owner);
}

static void host_close_popup(cl_widget_host_t *h)
{
    /* Widgets dismiss the popup CHAIN they live in; a modal below the chain
     * (a dialog hosting the combobox/menu) stays open. Dialogs close
     * themselves through the public cl_window_close_popup instead. */
    cl_window_t *win = host_win(h);

    overlay_request_close_from(win, overlay_chain_base(win));
}

static void host_push_popup(cl_widget_host_t *h, cl_widget_t *owner,
                            cl_widget_t *popup, cl_point_t anchor)
{
    cl_window_push_popup(host_win(h), owner, popup, anchor);
}

static void host_pop_popup(cl_widget_host_t *h)
{
    cl_window_pop_popup(host_win(h));
}

static void host_widget_gone(cl_widget_host_t *h, cl_widget_t *w)
{
    cl_window_t *win = host_win(h);

    if (win->mouse_target == w)
        win->mouse_target = NULL;
    if (win->hover == w)
        win->hover = NULL; /* silent: no mouse_leave on a dying widget */
    if (win->focus == w)
        win->focus = NULL; /* silent: no focus_lost on a dying widget */
    if (win->content == w)
        win->content = NULL; /* the root is going away underneath us */
    cl_window_owner_destroyed(win, w);     /* tear down its popup, if any */
    cl_window_tooltip_target_gone(win, w); /* drop its hover tooltip */
}

static char *host_clipboard_get(cl_widget_host_t *h)
{
    return cl_app_clipboard_get(host_win(h)->app);
}

static void host_clipboard_set(cl_widget_host_t *h, const char *utf8)
{
    cl_app_clipboard_set(host_win(h)->app, utf8);
}

static void host_set_ime_rect(cl_widget_host_t *h, cl_rect_t rect)
{
    cl_app_set_ime_rect(host_win(h)->app, rect);
}

static void host_defer_destroy(cl_widget_host_t *h, cl_widget_t *w)
{
    cl_app_defer_widget_free(host_win(h)->app, w);
}

static const cl_widget_host_ops_t window_host_ops = {
    .mark_dirty = host_mark_dirty,
    .mark_layout_dirty = host_mark_layout_dirty,
    .damage = host_damage,
    .set_focus = host_set_focus,
    .focused = host_focused,
    .open_popup = host_open_popup,
    .close_popup = host_close_popup,
    .push_popup = host_push_popup,
    .pop_popup = host_pop_popup,
    .widget_gone = host_widget_gone,
    .clipboard_get = host_clipboard_get,
    .clipboard_set = host_clipboard_set,
    .set_ime_rect = host_set_ime_rect,
    .defer_destroy = host_defer_destroy,
};

cl_window_t *cl_window_create(cl_application_t *app, const cl_window_desc_t *desc)
{
    cl_window_t *win;
    cl_platform_window_t *native = NULL;
    cl_result_t r;

    if (!app)
        return NULL;
    cl_window_desc_t norm;

    if (!desc) {
        cl_set_last_error(CL_ERROR_ABI_MISMATCH);
        return NULL;
    }
    if (!cl_abi_ok(desc->abi_version, desc->struct_size, CL_DESC_MIN_SIZE))
        return NULL;
    cl_desc_fill(&norm, sizeof norm, desc, desc->struct_size);
    desc = &norm;
    if (app->window) {
        cl_set_last_error(CL_ERROR_UNSUPPORTED); /* single window in MVP */
        return NULL;
    }

    r = app->platform->ops->create_window(app->platform, desc, &native);
    /* CL_RENDER_AUTO promises "OpenGL if available": when the GL window
     * cannot be created (no driver, headless, RDP), retry in software. */
    if (r != CL_OK && cl_app_software_fallback(app))
        r = app->platform->ops->create_window(app->platform, desc, &native);
    if (r != CL_OK) {
        cl_set_last_error(r);
        return NULL;
    }

    win = cl_alloc(&app->alloc, sizeof(*win));
    if (!win) {
        /* roll the native window back so the single slot stays reusable */
        if (app->platform->ops->destroy_window)
            app->platform->ops->destroy_window(app->platform, native);
        return NULL;
    }
    memset(win, 0, sizeof(*win));
    win->host.ops = &window_host_ops;
    win->app = app;
    win->native = native;
    /* Ask the platform for the size it actually created: the SDL backends
     * default width/height <= 0 to 640x480, and layout must agree with the
     * real surface (SDL sends no initial RESIZE to correct a stale 0x0). */
    win->size = app->platform->ops->drawable_size(app->platform, native);
    win->scale = app->platform->ops->scale(app->platform, native);
    if (win->scale <= 0.0f)
        win->scale = 1.0f;
    win->dirty = true;
    win->layout_dirty = true;
    win->damage_all = true;
    app->window = win;
    app->platform->ops->start_text_input(app->platform, native, true);
    return win;
}

void cl_window_destroy(cl_window_t *win)
{
    cl_application_t *app;

    if (!win)
        return;
    tooltip_dismiss(win); /* cancel the dwell timer and free the bubble first */
    while (win->overlay_count > 0)
        overlay_drop(win, win->overlay_count - 1);
    if (win->content)
        cl_widget_destroy(win->content);
    app = win->app;
    if (app->window == win)
        app->window = NULL;
    cl_free(&app->alloc, win);
}

cl_application_t *cl_window_application(cl_window_t *win)
{
    return win ? win->app : NULL;
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
    win->app->platform->ops->set_title(win->app->platform, win->native, utf8);
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
    win->damage_all = true;
}

void cl_window_mark_layout_dirty(cl_window_t *win)
{
    win->layout_dirty = true;
    win->dirty = true;
    win->damage_all = true; /* layout moves widgets: repaint everything */
}

void cl_window_damage(cl_window_t *win, cl_rect_t rect)
{
    if (rect.w <= 0.0f || rect.h <= 0.0f)
        return;
    if (!win->damage_all) {
        if (win->dirty && win->damage.w > 0.0f) {
            /* union bounding rect with the damage accumulated so far */
            float x0 = rect.x < win->damage.x ? rect.x : win->damage.x;
            float y0 = rect.y < win->damage.y ? rect.y : win->damage.y;
            float x1 = rect.x + rect.w > win->damage.x + win->damage.w
                           ? rect.x + rect.w
                           : win->damage.x + win->damage.w;
            float y1 = rect.y + rect.h > win->damage.y + win->damage.h
                           ? rect.y + rect.h
                           : win->damage.y + win->damage.h;

            win->damage = (cl_rect_t){ x0, y0, x1 - x0, y1 - y0 };
        } else {
            win->damage = rect;
        }
    }
    win->dirty = true;
}

void cl_window_resize(cl_window_t *win, cl_size_t size)
{
    win->size = size;
    win->scale = win->app->platform->ops->scale(win->app->platform,
                                                 win->native);
    if (win->scale <= 0.0f)
        win->scale = 1.0f;
    win->layout_dirty = true;
    win->dirty = true;
}

/* ---- overlay popups ----------------------------------------------------- */

/* Measure an overlay and arrange it at its anchor (or centred), clamped
 * on-screen. */
static void place_overlay(cl_window_t *win, struct cl_overlay *ov)
{
    cl_constraints_t c;
    cl_size_t sz;
    float x;
    float y;

    c.min = (cl_size_t){ 0.0f, 0.0f };
    c.max = (cl_size_t){ CL_UNBOUNDED, CL_UNBOUNDED };
    sz = cl_widget_do_measure(ov->widget, c);

    if (ov->center) {
        x = (win->size.w - sz.w) * 0.5f;
        y = (win->size.h - sz.h) * 0.5f;
    } else {
        x = ov->anchor.x;
        y = ov->anchor.y;
    }
    /* Keep the popup on-screen: shift left/up on overflow, clamp to origin. */
    if (x + sz.w > win->size.w)
        x = win->size.w - sz.w;
    if (y + sz.h > win->size.h)
        y = win->size.h - sz.h;
    if (x < 0.0f)
        x = 0.0f;
    if (y < 0.0f)
        y = 0.0f;
    cl_widget_do_arrange(ov->widget, (cl_rect_t){ x, y, sz.w, sz.h });
}

/* Remove the entry at index i: destroy an owned widget, detach a borrowed
 * one (its owner reuses it on the next open). */
static void overlay_drop(cl_window_t *win, int i)
{
    struct cl_overlay ov = win->overlays[i];
    int k;

    for (k = i; k < win->overlay_count - 1; k++)
        win->overlays[k] = win->overlays[k + 1];
    win->overlay_count--;
    if (ov.widget) {
        if (ov.owned)
            cl_widget_destroy(ov.widget);
        else
            cl_widget_set_window(ov.widget, NULL);
    }
    cl_window_mark_dirty(win);
}

/* Request-close every entry from index `from` upward (reaped post-dispatch,
 * so the widgets keep capturing the rest of this iteration's events). */
static void overlay_request_close_from(cl_window_t *win, int from)
{
    int i;

    for (i = from; i < win->overlay_count; i++)
        win->overlays[i].closing = true;
    if (from < win->overlay_count)
        cl_window_mark_dirty(win);
}

/* The first index ABOVE the topmost modal entry: the base of the current
 * light-dismissable popup chain (0 when no modal is open). Modal entries are
 * barriers - chain operations never cross them. */
static int overlay_chain_base(cl_window_t *win)
{
    int i;

    for (i = win->overlay_count - 1; i >= 0; i--) {
        if (win->overlays[i].modal)
            return i + 1;
    }
    return 0;
}

static bool overlay_push(cl_window_t *win, cl_widget_t *owner,
                         cl_widget_t *popup, cl_point_t at, bool owned,
                         bool modal, bool center)
{
    struct cl_overlay *ov;

    if (win->overlay_count == CL_WINDOW_MAX_OVERLAYS) {
        /* Deeper than any sane menu/dialog chain. Surface it (rather than a
         * silent no-op) and destroy a popup the window was told to own, so an
         * over-cap request cannot leak the caller's widget. */
        cl_log(CL_LOG_WARN, "window: overlay stack full (max %d); popup ignored",
               CL_WINDOW_MAX_OVERLAYS);
        cl_set_last_error(CL_ERROR_UNSUPPORTED);
        if (owned)
            cl_widget_destroy(popup);
        return false;
    }
    tooltip_dismiss(win); /* a popup supersedes any hover tooltip */
    window_update_hover(win, NULL); /* pointer input diverts to the overlay */
    ov = &win->overlays[win->overlay_count++];
    ov->widget = popup;
    ov->owner = owner;
    ov->anchor = at;
    ov->owned = owned;
    ov->modal = modal;
    ov->center = center;
    ov->closing = false;
    cl_widget_set_window(popup, win);
    place_overlay(win, ov);
    cl_window_mark_dirty(win);
    return true;
}

void cl_window_open_popup(cl_window_t *win, cl_widget_t *popup, cl_point_t at)
{
    int base;
    int i;

    if (!win || !popup)
        return;
    /*
     * Replace whatever is open - but only within the current chain: a modal
     * below stays (a combobox inside a dialog must not destroy the dialog,
     * and with it the very widget that is dispatching right now).
     */
    base = overlay_chain_base(win);
    for (i = win->overlay_count - 1; i >= base; i--) {
        if (win->overlays[i].widget != popup)
            overlay_drop(win, i);
    }
    if (win->overlay_count > base && win->overlays[base].widget == popup) {
        win->overlays[base].closing = false; /* re-opened: undoom it */
        return;
    }
    overlay_push(win, NULL, popup, at, true, false, false);
}

void cl_window_open_modal(cl_window_t *win, cl_widget_t *dialog)
{
    int i;

    if (!win || !dialog)
        return;
    for (i = win->overlay_count - 1; i >= 0; i--)
        overlay_drop(win, i);
    overlay_push(win, NULL, dialog, (cl_point_t){ 0, 0 }, true, true, true);
}

void cl_window_push_popup(cl_window_t *win, cl_widget_t *owner,
                          cl_widget_t *popup, cl_point_t at)
{
    if (!win || !popup)
        return;
    overlay_push(win, owner, popup, at, false, false, false);
}

void cl_window_set_overlay_owner(cl_window_t *win, cl_widget_t *owner)
{
    if (win && win->overlay_count > 0)
        win->overlays[win->overlay_count - 1].owner = owner;
}

void cl_window_owner_destroyed(cl_window_t *win, cl_widget_t *w)
{
    int i;

    if (!win)
        return;
    /* Entries opened by w - and everything stacked above them - go down with
     * it; entries whose WIDGET is w just vanish (the widget is dying). */
    for (i = win->overlay_count - 1; i >= 0; i--) {
        if (win->overlays[i].widget == w) {
            struct cl_overlay *ov = &win->overlays[i];
            int k;

            ov->widget = NULL; /* dying already: neither destroy nor detach */
            for (k = win->overlay_count - 1; k >= i; k--)
                overlay_drop(win, k);
        } else if (win->overlays[i].owner == w) {
            int k;

            for (k = win->overlay_count - 1; k >= i; k--)
                overlay_drop(win, k);
        }
    }
}

void cl_window_close_popup(cl_window_t *win)
{
    if (win)
        overlay_request_close_from(win, 0);
}

void cl_window_pop_popup(cl_window_t *win)
{
    if (win && win->overlay_count > 0)
        overlay_request_close_from(win, win->overlay_count - 1);
}

cl_widget_t *cl_window_popup(cl_window_t *win)
{
    if (!win || win->overlay_count == 0)
        return NULL;
    return win->overlays[win->overlay_count - 1].widget;
}

void cl_window_reap_overlay(cl_window_t *win)
{
    int i;

    if (!win)
        return;
    for (i = win->overlay_count - 1; i >= 0; i--) {
        if (win->overlays[i].closing)
            overlay_drop(win, i);
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
    cl_rect_t dmg;
    bool partial;

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
        {
            int i;

            for (i = 0; i < win->overlay_count; i++) {
                if (!win->overlays[i].closing)
                    place_overlay(win, &win->overlays[i]);
            }
        }
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

    /*
     * Partial redraw: when every invalidation this frame carried a rect and
     * the renderer supports damage clipping, only that region is cleared and
     * repainted (the whole tree is still walked - the renderer clips). GL
     * has no set_damage: its double-buffered target does not persist.
     */
    partial = !win->damage_all && app->renderer->ops->set_damage;
    dmg = win->damage;
    if (partial) {
        /* clamp to the window */
        if (dmg.x < 0.0f) {
            dmg.w += dmg.x;
            dmg.x = 0.0f;
        }
        if (dmg.y < 0.0f) {
            dmg.h += dmg.y;
            dmg.y = 0.0f;
        }
        if (dmg.x + dmg.w > win->size.w)
            dmg.w = win->size.w - dmg.x;
        if (dmg.y + dmg.h > win->size.h)
            dmg.h = win->size.h - dmg.y;
        if (dmg.w < 0.0f)
            dmg.w = 0.0f;
        if (dmg.h < 0.0f)
            dmg.h = 0.0f;
        app->renderer->ops->set_damage(app->renderer, dmg);
    }

    app->renderer->ops->begin_frame(app->renderer, win->size, win->scale,
                                    cl_theme_color(app->theme,
                                                   CL_COLOR_BACKGROUND));
    ctx.renderer = app->renderer;
    ctx.theme = app->theme;
    if (win->content)
        cl_widget_do_paint(win->content, &ctx);
    {
        int i;

        for (i = 0; i < win->overlay_count; i++) {
            if (!win->overlays[i].closing)
                cl_widget_do_paint(win->overlays[i].widget, &ctx);
        }
    }
    if (win->tooltip)
        cl_widget_do_paint(win->tooltip, &ctx); /* tooltip paints on top */
    app->renderer->ops->end_frame(app->renderer);
    if (partial && app->platform->ops->present_region)
        app->platform->ops->present_region(app->platform, win->native, dmg);
    else
        app->platform->ops->present(app->platform, win->native);
    win->dirty = false;
    win->damage_all = false;
    win->damage = (cl_rect_t){ 0.0f, 0.0f, 0.0f, 0.0f };
}

/* The root ancestor of w: the content root, an overlay entry, the tooltip. */
static cl_widget_t *widget_root(cl_widget_t *w)
{
    while (w->parent)
        w = w->parent;
    return w;
}

/* True when w lives inside one of the open overlay entries. */
static bool widget_in_overlays(cl_window_t *win, cl_widget_t *w)
{
    cl_widget_t *root;
    int i;

    if (!w)
        return false;
    root = widget_root(w);
    for (i = 0; i < win->overlay_count; i++) {
        if (win->overlays[i].widget == root)
            return true;
    }
    return false;
}

/* The effective cursor of a widget: its own shape, or the nearest ancestor's
 * non-default one (a container can set a cursor for a whole subtree). */
static cl_cursor_t effective_cursor(cl_widget_t *w)
{
    for (; w; w = w->parent) {
        if ((cl_cursor_t)w->cursor != CL_CURSOR_DEFAULT)
            return (cl_cursor_t)w->cursor;
    }
    return CL_CURSOR_DEFAULT;
}

static void window_apply_cursor(cl_window_t *win, cl_cursor_t cursor)
{
    cl_platform_t *p = win->app->platform;

    if (win->cursor == cursor)
        return;
    win->cursor = cursor;
    if (p->ops->set_cursor)
        p->ops->set_cursor(p, cursor);
}

/* Reconcile the hovered widget on pointer motion: leave the old, enter the
 * new. Skipped while dragging (pointer capture freezes hover). */
static void window_update_hover(cl_window_t *win, cl_widget_t *w)
{
    if (win->hover == w)
        return;
    if (win->hover)
        cl_widget_send_hover(win->hover, false);
    win->hover = w;
    if (w)
        cl_widget_send_hover(w, true);
    window_apply_cursor(win, effective_cursor(w));
}

void cl_window_handle_mouse(cl_window_t *win, cl_platform_event_kind_t kind,
                            cl_point_t pos, cl_mouse_button_t button,
                            cl_key_mods_t mods, int clicks)
{
    cl_event_t ev;
    cl_widget_t *target;

    memset(&ev, 0, sizeof(ev));
    ev.mods = mods;
    ev.data.mouse.pos = pos;
    ev.data.mouse.button = button;
    ev.data.mouse.clicks = clicks;

    /*
     * An open overlay stack captures pointer input. A press routes to the
     * topmost entry containing the point, closing everything stacked above
     * it (clicking back into a parent menu collapses its submenus); a press
     * outside every entry dismisses the whole chain - unless the top entry
     * is modal, which swallows outside clicks. Entries whose close was
     * requested this frame keep capturing until they are reaped (before the
     * next render), so no queued event leaks to the content.
     */
    if (win->overlay_count > 0) {
        int top = win->overlay_count - 1;
        int hit = -1;
        int i;

        for (i = top; i >= 0; i--) {
            if (cl_rect_contains(win->overlays[i].widget->rect, pos)) {
                hit = i;
                break;
            }
        }
        ev.type = kind == CL_PEV_MOUSE_DOWN ? CL_EVENT_MOUSE_DOWN
                  : kind == CL_PEV_MOUSE_UP ? CL_EVENT_MOUSE_UP
                                            : CL_EVENT_MOUSE_MOVE;
        if (kind == CL_PEV_MOUSE_DOWN) {
            cl_widget_t *tgt;

            win->mouse_target = NULL; /* any prior capture ends here */
            if (hit < 0) {
                /* Light-dismiss the chain above the topmost modal; with the
                 * modal itself on top this closes nothing - the press is
                 * swallowed either way. */
                overlay_request_close_from(win, overlay_chain_base(win));
                return;
            }
            overlay_request_close_from(win, hit + 1);
            /* hit-test INTO the entry: dialogs carry child widgets */
            tgt = cl_widget_hit(win->overlays[hit].widget, pos);
            if (!tgt)
                tgt = win->overlays[hit].widget;
            /*
             * A modal DIALOG behaves like content: the press moves focus to
             * the nearest focusable ancestor and captures the pointer, so
             * textboxes focus and sliders drag inside it. Menus keep their
             * capture-less routing (drag-to-select spans entries).
             */
            if (win->overlays[hit].modal) {
                cl_widget_t *f;

                win->mouse_target = tgt;
                for (f = tgt; f && !(f->flags & CL_WF_FOCUSABLE);
                     f = f->parent)
                    ;
                if (f)
                    cl_window_set_focus(win, f);
            }
            cl_widget_dispatch(tgt, &ev);
            return;
        }
        /* A press captured inside a modal entry routes its drag/release to
         * the captured widget, exactly like content capture. */
        if (win->mouse_target && widget_in_overlays(win, win->mouse_target)) {
            cl_widget_t *tgt = win->mouse_target;

            if (kind == CL_PEV_MOUSE_UP)
                win->mouse_target = NULL;
            cl_widget_dispatch(tgt, &ev);
            return;
        }
        /* moves/releases go to the entry under the pointer, falling back to
         * the top (menus reject outside points in their own handlers) */
        {
            cl_widget_t *ow = win->overlays[hit >= 0 ? hit : top].widget;
            cl_widget_t *tgt = hit >= 0 ? cl_widget_hit(ow, pos) : NULL;

            if (kind == CL_PEV_MOUSE_UP)
                win->mouse_target = NULL; /* a release ends content capture */
            else if (!win->mouse_target)
                /* hover/cursor inside overlays (button highlight, I-beam) */
                window_update_hover(win, hit >= 0 ? (tgt ? tgt : ow) : NULL);
            cl_widget_dispatch(tgt ? tgt : ow, &ev);
        }
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
        if (!win->mouse_target) {
            window_update_hover(win, target);
            tooltip_hover(win, pos); /* not dragging: update hover tooltip */
        }
    }

    if (target)
        cl_widget_dispatch(target, &ev);
}

void cl_window_handle_wheel(cl_window_t *win, cl_point_t pos, float dx,
                            float dy, cl_key_mods_t mods)
{
    cl_event_t ev;
    cl_widget_t *target;

    memset(&ev, 0, sizeof(ev));
    ev.type = CL_EVENT_MOUSE_WHEEL;
    ev.mods = mods;
    ev.data.wheel.pos = pos;
    ev.data.wheel.dx = dx;
    ev.data.wheel.dy = dy;

    /* While popups are open, the wheel is captured (does not scroll content). */
    if (win->overlay_count > 0) {
        int i;

        for (i = win->overlay_count - 1; i >= 0; i--) {
            if (cl_rect_contains(win->overlays[i].widget->rect, pos)) {
                cl_widget_dispatch(win->overlays[i].widget, &ev);
                break;
            }
        }
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
    if (win->overlay_count > 0) {
        struct cl_overlay *ov = &win->overlays[win->overlay_count - 1];

        /*
         * Focus scope: when the focus lives INSIDE the top overlay (a widget
         * in a dialog), keys route to it like content and bubble up to the
         * dialog root (Enter/Escape). Otherwise the overlay root itself gets
         * them (menu navigation). Tab cycles a modal dialog's focusables.
         */
        if (win->focus && widget_root(win->focus) == ov->widget)
            handled = cl_widget_dispatch(win->focus, &ev);
        else
            handled = cl_widget_dispatch(ov->widget, &ev);
        if (!handled && kind == CL_PEV_KEY_DOWN && key == CL_KEY_TAB &&
            ov->modal)
            cl_window_focus_next_in(win, ov->widget,
                                    !(mods & CL_MOD_SHIFT));
        return;
    }

    if (kind == CL_PEV_KEY_DOWN)
        tooltip_dismiss(win); /* keyboard activity dismisses a hover tooltip */
    if (win->focus)
        handled = cl_widget_dispatch(win->focus, &ev);
    if (!handled && kind == CL_PEV_KEY_DOWN && key == CL_KEY_TAB)
        cl_window_focus_next(win, !(mods & CL_MOD_SHIFT));
}

/* Text input reaches the focused widget when no overlay is open OR when the
 * focus lives inside the topmost overlay (a textbox in a dialog); any other
 * open popup swallows it. */
static bool text_input_allowed(cl_window_t *win)
{
    if (!win->focus)
        return false;
    if (win->overlay_count == 0)
        return true;
    return widget_root(win->focus) ==
           win->overlays[win->overlay_count - 1].widget;
}

void cl_window_handle_text(cl_window_t *win, const char *utf8)
{
    cl_event_t ev;

    if (!text_input_allowed(win) || !utf8 || !utf8[0])
        return;
    memset(&ev, 0, sizeof(ev));
    ev.type = CL_EVENT_TEXT_INPUT;
    ev.data.text.utf8 = utf8;
    cl_widget_dispatch(win->focus, &ev);
}

void cl_window_handle_text_edit(cl_window_t *win, const char *utf8, int cursor)
{
    cl_event_t ev;

    /* An empty composition string is meaningful: it clears the pre-edit. */
    if (!text_input_allowed(win) || !utf8)
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

void cl_window_focus_next_in(cl_window_t *win, cl_widget_t *root, bool forward)
{
    cl_widget_t **arr;
    int total;
    int n = 0;
    int cur = -1;
    int next;
    int i;

    if (!root)
        return;
    total = count_focusable(root);
    if (total == 0)
        return;
    arr = cl_alloc(&win->app->alloc, (size_t)total * sizeof(*arr));
    if (!arr)
        return;
    collect_focusable(root, arr, &n);
    for (i = 0; i < n; i++) {
        if (arr[i] == win->focus) {
            cur = i;
            break;
        }
    }
    /* A focus outside the scope (or none) enters at the first/last entry. */
    if (cur < 0)
        next = forward ? 0 : n - 1;
    else if (forward)
        next = (cur + 1) % n;
    else
        next = (cur - 1 + n) % n;
    cl_window_set_focus(win, arr[next]);
    cl_free(&win->app->alloc, arr);
}

void cl_window_focus_next(cl_window_t *win, bool forward)
{
    cl_window_focus_next_in(win, win->content, forward);
}
