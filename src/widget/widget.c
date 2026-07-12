/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widget.h>
#include <copal/widget_impl.h>
#include <copal/application.h>

#include <string.h>

#include "widget/widget_internal.h"
#include "app/app_internal.h"

/* ---- construction / RTTI ------------------------------------------------ */

void cl_widget_init_base(cl_widget_t *w, cl_application_t *app,
                         const cl_widget_class_t *cls)
{
    w->cls = cls;
    w->app = app;
    w->flags = CL_WF_VISIBLE | CL_WF_ENABLED;
    w->align_h = CL_ALIGN_START;
    w->align_v = CL_ALIGN_START;
    w->generation = 0;
}

cl_widget_t *cl_widget_alloc(cl_application_t *app, const cl_widget_class_t *cls)
{
    const cl_allocator_t *a = cl_application_allocator(app);
    cl_widget_t *w = cl_alloc(a, cls->instance_size);

    if (!w)
        return NULL;
    memset(w, 0, cls->instance_size);
    cl_widget_init_base(w, app, cls);
    return w;
}

void *cl_widget_check_cast(cl_widget_t *w, const cl_widget_class_t *cls)
{
    const cl_widget_class_t *c;

    if (!w)
        return NULL;
    for (c = w->cls; c; c = c->base) {
        if (c == cls)
            return w;
    }
    return NULL;
}

bool cl_widget_is_a(cl_widget_t *w, const cl_widget_class_t *cls)
{
    return cl_widget_check_cast(w, cls) != NULL;
}

/* ---- invalidation ------------------------------------------------------- */

void cl_widget_invalidate(cl_widget_t *w)
{
    if (w && w->window)
        cl_window_mark_dirty(w->window);
}

void cl_widget_invalidate_layout(cl_widget_t *w)
{
    if (w && w->window)
        cl_window_mark_layout_dirty(w->window);
}

/* ---- tree --------------------------------------------------------------- */

void cl_widget_set_window(cl_widget_t *w, cl_window_t *win)
{
    cl_widget_t *c;
    cl_window_t *old = w->window;

    /*
     * Detaching from a window: drop any focus/pointer capture the old window
     * still holds on this node BEFORE the link is torn down, so those weak
     * pointers can never dangle into detached or freed memory.
     */
    if (old && old != win) {
        if (old->focus == w)
            cl_window_set_focus(old, NULL);
        if (old->mouse_target == w)
            old->mouse_target = NULL;
        cl_window_owner_destroyed(old, w);   /* tear down a popup w opened */
        cl_window_tooltip_target_gone(old, w); /* drop its hover tooltip */
    }
    w->window = win;
    for (c = w->first_child; c; c = c->next_sibling)
        cl_widget_set_window(c, win);
}

cl_result_t cl_widget_add_child(cl_widget_t *parent, cl_widget_t *child)
{
    cl_widget_t *anc;

    if (!parent || !child || child->parent)
        return CL_ERROR_INVALID_ARGUMENT;
    /* Reject self-adoption and adopting an ancestor: a cycle would recurse
     * forever in cl_widget_set_window (and every later tree walk). */
    for (anc = parent; anc; anc = anc->parent) {
        if (anc == child)
            return CL_ERROR_INVALID_ARGUMENT;
    }

    child->parent = parent;
    child->next_sibling = NULL;
    if (parent->last_child)
        parent->last_child->next_sibling = child;
    else
        parent->first_child = child;
    parent->last_child = child;

    cl_widget_set_window(child, parent->window);
    cl_widget_invalidate_layout(parent);
    return CL_OK;
}

cl_result_t cl_widget_remove_child(cl_widget_t *parent, cl_widget_t *child)
{
    cl_widget_t *prev = NULL;
    cl_widget_t *cur;

    if (!parent || !child || child->parent != parent)
        return CL_ERROR_INVALID_ARGUMENT;

    for (cur = parent->first_child; cur && cur != child;
         cur = cur->next_sibling)
        prev = cur;
    if (!cur)
        return CL_ERROR_INVALID_ARGUMENT;

    if (prev)
        prev->next_sibling = child->next_sibling;
    else
        parent->first_child = child->next_sibling;
    if (parent->last_child == child)
        parent->last_child = prev;

    child->parent = NULL;
    child->next_sibling = NULL;
    cl_widget_set_window(child, NULL);
    cl_widget_invalidate_layout(parent);
    return CL_OK;
}

static void widget_destroy_subtree(cl_widget_t *w)
{
    cl_widget_t *c = w->first_child;
    const cl_allocator_t *a;

    while (c) {
        cl_widget_t *next = c->next_sibling;

        widget_destroy_subtree(c);
        c = next;
    }
    if (w->window && w->window->mouse_target == w)
        w->window->mouse_target = NULL;
    if (w->window && w->window->focus == w)
        w->window->focus = NULL;
    if (w->window) {
        cl_window_owner_destroyed(w->window, w); /* tear down its popup, if any */
        cl_window_tooltip_target_gone(w->window, w); /* drop its hover tooltip */
    }

    a = cl_application_allocator(w->app);
    if (w->cls->vtable && w->cls->vtable->destroy)
        w->cls->vtable->destroy(w);
    cl_free(a, w->tooltip);
    cl_free(a, w);
}

void cl_widget_destroy(cl_widget_t *w)
{
    if (!w)
        return;
    if (w->parent)
        cl_widget_remove_child(w->parent, w);
    widget_destroy_subtree(w);
}

cl_widget_t *cl_widget_parent(cl_widget_t *w)
{
    return w ? w->parent : NULL;
}

cl_window_t *cl_widget_window(cl_widget_t *w)
{
    return w ? w->window : NULL;
}

/* ---- geometry / state --------------------------------------------------- */

cl_rect_t cl_widget_rect(cl_widget_t *w)
{
    return w->rect;
}

void cl_widget_set_visible(cl_widget_t *w, bool v)
{
    if (v) {
        w->flags |= CL_WF_VISIBLE;
    } else {
        w->flags &= ~(uint32_t)CL_WF_VISIBLE;
        if (w->window && w->window->focus == w)
            cl_window_set_focus(w->window, NULL);
    }
    cl_widget_invalidate_layout(w);
}

bool cl_widget_is_visible(cl_widget_t *w)
{
    return (w->flags & CL_WF_VISIBLE) != 0;
}

void cl_widget_set_enabled(cl_widget_t *w, bool e)
{
    if (e) {
        w->flags |= CL_WF_ENABLED;
    } else {
        w->flags &= ~(uint32_t)CL_WF_ENABLED;
        if (w->window && w->window->focus == w)
            cl_window_set_focus(w->window, NULL);
    }
    cl_widget_invalidate(w);
}

void cl_widget_set_focusable(cl_widget_t *w, bool focusable)
{
    if (focusable) {
        w->flags |= CL_WF_FOCUSABLE;
    } else {
        w->flags &= ~(uint32_t)CL_WF_FOCUSABLE;
        if (w->window && w->window->focus == w)
            cl_window_set_focus(w->window, NULL);
    }
}

bool cl_widget_focus(cl_widget_t *w)
{
    const uint32_t need = CL_WF_FOCUSABLE | CL_WF_VISIBLE | CL_WF_ENABLED;

    if (!w || !w->window || (w->flags & need) != need)
        return false;
    cl_window_set_focus(w->window, w);
    return true;
}

bool cl_widget_has_focus(cl_widget_t *w)
{
    return w && w->window && w->window->focus == w;
}

bool cl_widget_is_enabled(cl_widget_t *w)
{
    return (w->flags & CL_WF_ENABLED) != 0;
}

void cl_widget_set_preferred_size(cl_widget_t *w, cl_size_t s)
{
    w->pref_size = s;
    cl_widget_invalidate_layout(w);
}

void cl_widget_set_margin(cl_widget_t *w, cl_insets_t m)
{
    w->margin = m;
    cl_widget_invalidate_layout(w);
}

void cl_widget_set_align(cl_widget_t *w, cl_align_t h, cl_align_t v)
{
    w->align_h = h;
    w->align_v = v;
    cl_widget_invalidate_layout(w);
}

void cl_widget_set_flex(cl_widget_t *w, float weight)
{
    w->flex = weight;
    cl_widget_invalidate_layout(w);
}

void cl_widget_set_userdata(cl_widget_t *w, void *user)
{
    w->userdata = user;
}

void *cl_widget_userdata(cl_widget_t *w)
{
    return w->userdata;
}

void cl_widget_set_tooltip(cl_widget_t *w, const char *utf8)
{
    const cl_allocator_t *a = cl_application_allocator(w->app);

    cl_free(a, w->tooltip);
    w->tooltip = NULL;
    if (utf8 && utf8[0]) {
        size_t n = strlen(utf8) + 1;

        w->tooltip = cl_alloc(a, n);
        if (w->tooltip)
            memcpy(w->tooltip, utf8, n);
    }
}

const char *cl_widget_tooltip(cl_widget_t *w)
{
    return w->tooltip;
}

/* ---- layout / paint / hit-test ----------------------------------------- */

cl_size_t cl_widget_do_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_size_t sz;

    if (w->cls->vtable && w->cls->vtable->measure)
        sz = w->cls->vtable->measure(w, c);
    else
        sz = w->pref_size;
    w->measured = sz;
    return sz;
}

void cl_widget_do_arrange(cl_widget_t *w, cl_rect_t rect)
{
    w->rect = rect;
    if (w->cls->vtable && w->cls->vtable->arrange)
        w->cls->vtable->arrange(w, rect);
}

/* Clip rect used for children (and hit-testing) when CL_WF_CLIP is set. */
static cl_rect_t widget_clip_rect(cl_widget_t *w)
{
    if (w->cls->vtable && w->cls->vtable->clip_rect)
        return w->cls->vtable->clip_rect(w);
    return w->rect;
}

void cl_widget_do_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_widget_t *c;
    bool clip = (w->flags & CL_WF_CLIP) != 0 && w->first_child;

    if (!(w->flags & CL_WF_VISIBLE))
        return;
    if (w->cls->vtable && w->cls->vtable->paint)
        w->cls->vtable->paint(w, ctx);
    if (clip)
        cl_paint_push_clip(ctx, widget_clip_rect(w));
    for (c = w->first_child; c; c = c->next_sibling)
        cl_widget_do_paint(c, ctx);
    if (clip)
        cl_paint_pop_clip(ctx);
}

void cl_widget_reveal(cl_widget_t *w)
{
    cl_widget_t *p;

    if (!w)
        return;
    /*
     * Walk ancestors outward, asking each that can (a scroll container) to
     * bring w into view. w->rect is re-read on every call: an inner container
     * that scrolls moves w, and the next outer one must see its new position.
     */
    for (p = w->parent; p; p = p->parent) {
        if (p->cls->vtable && p->cls->vtable->reveal)
            p->cls->vtable->reveal(p, w->rect);
    }
}

cl_widget_t *cl_widget_hit(cl_widget_t *w, cl_point_t p)
{
    cl_widget_t *c;
    cl_widget_t *found = NULL;

    if (!(w->flags & CL_WF_VISIBLE))
        return NULL;
    if (!cl_rect_contains(w->rect, p))
        return NULL;
    /*
     * Children are clipped to widget_clip_rect(): a point inside the widget but
     * outside that region (e.g. a scrollbar gutter, or content scrolled past
     * the viewport edge) hits this widget, not a child drawn underneath.
     */
    if ((w->flags & CL_WF_CLIP) && w->first_child &&
        !cl_rect_contains(widget_clip_rect(w), p))
        return w;
    for (c = w->first_child; c; c = c->next_sibling) {
        cl_widget_t *h = cl_widget_hit(c, p);

        if (h)
            found = h; /* last (topmost) child wins */
    }
    return found ? found : w;
}

static bool widget_handle(cl_widget_t *w, const cl_event_t *ev)
{
    const cl_widget_vtable_t *vt = w->cls->vtable;

    if (!(w->flags & CL_WF_ENABLED) || !vt)
        return false;
    if (vt->on_event)
        return vt->on_event(w, ev);

    switch (ev->type) {
        case CL_EVENT_MOUSE_DOWN:
            return vt->mouse_down ? vt->mouse_down(w, ev) : false;

        case CL_EVENT_MOUSE_UP:
            return vt->mouse_up ? vt->mouse_up(w, ev) : false;

        case CL_EVENT_MOUSE_MOVE:
            return vt->mouse_move ? vt->mouse_move(w, ev) : false;

        case CL_EVENT_MOUSE_WHEEL:
            return vt->mouse_wheel ? vt->mouse_wheel(w, ev) : false;

        case CL_EVENT_KEY_DOWN:
            return vt->key_down ? vt->key_down(w, ev) : false;

        case CL_EVENT_KEY_UP:
            return vt->key_up ? vt->key_up(w, ev) : false;

        case CL_EVENT_TEXT_INPUT:
            return vt->text_input ? vt->text_input(w, ev) : false;

        case CL_EVENT_TEXT_EDIT:
            return vt->text_edit ? vt->text_edit(w, ev) : false;

        default:
            return false;
    }
}

bool cl_widget_dispatch(cl_widget_t *w, const cl_event_t *ev)
{
    cl_widget_t *cur;

    for (cur = w; cur; cur = cur->parent) {
        if (widget_handle(cur, ev))
            return true;
    }
    return false;
}
