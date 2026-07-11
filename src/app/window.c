/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/window.h>
#include <copal/allocator.h>

#include <string.h>

#include "app/app_internal.h"
#include "widget/widget_internal.h"
#include "render/paint_context.h"
#include "core/foundation/foundation_internal.h"

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
    if (win->overlay)
        cl_widget_destroy(win->overlay);
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
    if (win->overlay && win->overlay != popup)
        cl_widget_destroy(win->overlay); /* replace any existing popup */
    win->overlay = popup;
    win->overlay_anchor = at;
    win->overlay_closing = false;
    cl_widget_set_window(popup, win);
    place_overlay(win);
    cl_window_mark_dirty(win);
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
        win->overlay_closing = false;
        cl_widget_destroy(o);
        cl_window_mark_dirty(win);
    }
}

void cl_window_render(cl_window_t *win)
{
    cl_application_t *app = win->app;
    struct cl_paint_context ctx;

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
        win->layout_dirty = false;
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
    target = cl_widget_hit(win->content, pos);
    if (target)
        cl_widget_dispatch(target, &ev);
}

void cl_window_set_focus(cl_window_t *win, cl_widget_t *w)
{
    cl_widget_t *old = win->focus;

    if (old == w)
        return;
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
