/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widget.h>
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/version.h>

#include <string.h>

#include "core/foundation/foundation_internal.h"
#include "widget/widget_internal.h"
#include "widget/widget_host.h"
#include "render/paint_context.h" /* cl_paint_context cull fields (do_paint) */

/* ---- construction / RTTI ------------------------------------------------ */

void cl_widget_init_base(cl_widget_t *w, cl_application_t *app,
                         const cl_widget_class_t *cls)
{
    w->cls = cls;
    w->app = app;
    w->flags = CL_WF_VISIBLE | CL_WF_ENABLED;
    w->align_h = CL_ALIGN_AUTO; /* defer to the container's align_cross */
    w->align_v = CL_ALIGN_AUTO;
}

cl_widget_t *cl_widget_alloc(cl_application_t *app, const cl_widget_class_t *cls)
{
    const cl_allocator_t *a = cl_application_allocator(app);
    cl_widget_t *w;

    /* The class must agree on the vtable layout (see cl_widget_class). */
    if (cls->vtable && cls->vtable_size != sizeof(cl_widget_vtable_t)) {
        cl_set_last_error(CL_ERROR_ABI_MISMATCH);
        return NULL;
    }
    w = cl_alloc(a, cls->instance_size);
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
    /* A live widget of the wrong class means mixed-up handles: record the
     * cause of the resulting no-op (probe with cl_widget_is_a instead). */
    cl_set_last_error(CL_ERROR_INVALID_ARGUMENT);
    return NULL;
}

bool cl_widget_is_a(cl_widget_t *w, const cl_widget_class_t *cls)
{
    const cl_widget_class_t *c;

    for (c = w ? w->cls : NULL; c; c = c->base) {
        if (c == cls)
            return true;
    }
    return false;
}

/* ---- invalidation ------------------------------------------------------- */

void cl_widget_invalidate(cl_widget_t *w)
{
    cl_widget_host_t *h;

    if (!w || !w->window)
        return;
    h = cl_widget_host(w);
    /* A laid-out widget only damages its own box (inflated by a pixel:
     * anti-aliasing and strokes bleed just outside the rect). Before the
     * first arrange the rect is empty - repaint everything. */
    if (w->rect.w > 0.0f && w->rect.h > 0.0f) {
        cl_rect_t r = { w->rect.x - 1.0f, w->rect.y - 1.0f, w->rect.w + 2.0f,
                        w->rect.h + 2.0f };

        h->ops->damage(h, r);
    } else {
        h->ops->mark_dirty(h);
    }
}

void cl_widget_invalidate_layout(cl_widget_t *w)
{
    if (w && w->window)
        cl_widget_host(w)->ops->mark_layout_dirty(cl_widget_host(w));
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
        cl_widget_host_t *h = (cl_widget_host_t *)old;

        if (h->ops->focused(h) == w)
            h->ops->set_focus(h, NULL); /* with the focus_lost callback */
        h->ops->widget_gone(h, w); /* every remaining weak ref, silently */
    }
    w->window = win;
    for (c = w->first_child; c; c = c->next_sibling)
        cl_widget_set_window(c, win);
}

/* Height of w's subtree (w itself counts as 1), never descending more than
 * `limit` levels. Because cl_widget_add_child enforces the depth cap on every
 * link, a subtree can never legitimately exceed it; the bound keeps this
 * measuring walk itself from overflowing the stack if that ever failed. */
static int subtree_depth(const cl_widget_t *w, int limit)
{
    int deepest = 0;
    const cl_widget_t *c;

    if (limit <= 0)
        return 1;
    for (c = w->first_child; c; c = c->next_sibling) {
        int d = subtree_depth(c, limit - 1);

        if (d > deepest)
            deepest = d;
    }
    return deepest + 1;
}

cl_result_t cl_widget_add_child(cl_widget_t *parent, cl_widget_t *child)
{
    cl_widget_t *anc;
    int above = 0;

    if (!parent || !child || child->parent ||
        ((parent->flags | child->flags) & CL_WF_DEAD))
        return CL_ERROR_INVALID_ARGUMENT;
    /* Reject self-adoption and adopting an ancestor: a cycle would recurse
     * forever in cl_widget_set_window (and every later tree walk). Count
     * parent's depth from the root (1 at the root) as we walk. */
    for (anc = parent; anc; anc = anc->parent) {
        if (anc == child)
            return CL_ERROR_INVALID_ARGUMENT;
        above++;
    }
    /* Bound total nesting depth: parent's depth plus the height of child's own
     * subtree. Keeps the recursive paint/hit-test/measure/free walks off the
     * C stack's limit (see CL_WIDGET_MAX_DEPTH). */
    if (above + subtree_depth(child, CL_WIDGET_MAX_DEPTH) > CL_WIDGET_MAX_DEPTH)
        return CL_ERROR_INVALID_ARGUMENT;

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

void cl_widget_free_subtree(cl_widget_t *w)
{
    cl_widget_t *c = w->first_child;
    const cl_allocator_t *a;

    while (c) {
        cl_widget_t *next = c->next_sibling;

        cl_widget_free_subtree(c);
        c = next;
    }
    a = cl_application_allocator(w->app);
    if (w->cls->vtable && w->cls->vtable->destroy)
        w->cls->vtable->destroy(w);
    cl_free(a, w->tooltip);
    cl_free(a, w);
}

/* Mark the subtree dead: sweep the host's weak references silently (a dying
 * widget must not get callbacks), detach from the window and flag DEAD so
 * hit-testing, events and re-attachment all refuse the node. */
static void widget_mark_dead(cl_widget_t *w)
{
    cl_widget_t *c;

    for (c = w->first_child; c; c = c->next_sibling)
        widget_mark_dead(c);
    if (w->window)
        cl_widget_host(w)->ops->widget_gone(cl_widget_host(w), w);
    w->window = NULL;
    w->flags |= CL_WF_DEAD;
}

void cl_widget_destroy(cl_widget_t *w)
{
    cl_widget_host_t *host;

    if (!w || (w->flags & CL_WF_DEAD))
        return; /* already queued */
    host = cl_widget_host(w); /* before detach clears the window back-ref */
    /*
     * Flag the root dead up front, before the detach below can run user
     * callbacks (cl_widget_remove_child fires focus_lost). If such a callback
     * re-enters cl_widget_destroy on this same widget, the guard above now
     * trips and the nested call is a no-op, so the node is queued for free
     * exactly once instead of twice.
     */
    w->flags |= CL_WF_DEAD;
    if (w->parent)
        cl_widget_remove_child(w->parent, w); /* fires focus_lost via detach */
    widget_mark_dead(w);
    /*
     * Attached widgets are freed at the end of the current loop iteration
     * (the host's deferred queue), so handles stay valid while an event or
     * timer callback unwinds - destroying ANY widget from a callback is
     * safe. A detached tree cannot be referenced by the loop; free it now.
     */
    if (host)
        host->ops->defer_destroy(host, w);
    else
        cl_widget_free_subtree(w);
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
        if (w->window &&
            cl_widget_host(w)->ops->focused(cl_widget_host(w)) == w)
            cl_widget_host(w)->ops->set_focus(cl_widget_host(w), NULL);
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
        if (w->window &&
            cl_widget_host(w)->ops->focused(cl_widget_host(w)) == w)
            cl_widget_host(w)->ops->set_focus(cl_widget_host(w), NULL);
    }
    cl_widget_invalidate(w);
}

void cl_widget_set_focusable(cl_widget_t *w, bool focusable)
{
    if (focusable) {
        w->flags |= CL_WF_FOCUSABLE;
    } else {
        w->flags &= ~(uint32_t)CL_WF_FOCUSABLE;
        if (w->window &&
            cl_widget_host(w)->ops->focused(cl_widget_host(w)) == w)
            cl_widget_host(w)->ops->set_focus(cl_widget_host(w), NULL);
    }
}

bool cl_widget_focus(cl_widget_t *w)
{
    const uint32_t need = CL_WF_FOCUSABLE | CL_WF_VISIBLE | CL_WF_ENABLED;

    if (!w || !w->window || (w->flags & need) != need)
        return false;
    cl_widget_host(w)->ops->set_focus(cl_widget_host(w), w);
    return true;
}

bool cl_widget_has_focus(cl_widget_t *w)
{
    return w && w->window &&
           cl_widget_host(w)->ops->focused(cl_widget_host(w)) == w;
}

bool cl_widget_is_enabled(cl_widget_t *w)
{
    return (w->flags & CL_WF_ENABLED) != 0;
}

cl_size_t cl_widget_preferred_size(cl_widget_t *w)
{
    return w ? w->pref_size : (cl_size_t){ 0.0f, 0.0f };
}

cl_insets_t cl_widget_margin(cl_widget_t *w)
{
    return w ? w->margin : (cl_insets_t){ 0.0f, 0.0f, 0.0f, 0.0f };
}

cl_align_t cl_widget_align_h(cl_widget_t *w)
{
    return w ? w->align_h : CL_ALIGN_AUTO;
}

cl_align_t cl_widget_align_v(cl_widget_t *w)
{
    return w ? w->align_v : CL_ALIGN_AUTO;
}

float cl_widget_flex(cl_widget_t *w)
{
    return w ? w->flex : 0.0f;
}

bool cl_widget_is_focusable(cl_widget_t *w)
{
    return w && (w->flags & CL_WF_FOCUSABLE) != 0;
}

void cl_widget_set_cursor(cl_widget_t *w, cl_cursor_t cursor)
{
    if (w)
        w->cursor = (uint32_t)cursor;
}

cl_cursor_t cl_widget_cursor(cl_widget_t *w)
{
    return w ? (cl_cursor_t)w->cursor : CL_CURSOR_DEFAULT;
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
        size_t n = cl_strlen(utf8) + 1;

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
    /* An explicit preferred size wins over the widget's own measure on
     * each axis where it is set (> 0). */
    if (w->pref_size.w > 0.0f)
        sz.w = w->pref_size.w;
    if (w->pref_size.h > 0.0f)
        sz.h = w->pref_size.h;
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

/* True if r (inflated 1px for the AA bleed that cl_widget_invalidate accounts
 * for) overlaps `region`. */
static bool rect_hits(cl_rect_t r, cl_rect_t region)
{
    return r.x - 1.0f < region.x + region.w && r.x + r.w + 1.0f > region.x &&
           r.y - 1.0f < region.y + region.h && r.y + r.h + 1.0f > region.y;
}

void cl_widget_do_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_widget_t *c;
    bool clip = (w->flags & CL_WF_CLIP) != 0 && w->first_child;
    bool in_region = !ctx->cull_on || rect_hits(w->rect, ctx->cull);

    if (!(w->flags & CL_WF_VISIBLE))
        return;
    /*
     * Damage cull. A clipping container bounds its subtree, so if its rect is
     * off the repainted region the whole subtree can be skipped. Otherwise a
     * child may overflow a non-clipping parent, so only the widget's own paint
     * (its pixels live within its rect) is skipped while children are still
     * walked - each one culls itself.
     */
    if (clip && !in_region)
        return;
    if (in_region && w->cls->vtable && w->cls->vtable->paint)
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
    const cl_widget_vtable_t *vt = w->cls->vtable;
    cl_widget_t *c;
    cl_widget_t *found = NULL;

    if (!(w->flags & CL_WF_VISIBLE))
        return NULL;
    if (!cl_rect_contains(w->rect, p))
        return NULL;
    /* A custom hit_test refines the shape within the rect (the rect test
     * above stays as the coarse bound); a miss skips the whole subtree. */
    if (vt && vt->hit_test && !vt->hit_test(w, p))
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

    /* A widget destroyed mid-dispatch (e.g. a handler that closed its own
     * dialog) is flagged dead but stays linked until the deferred reap; the
     * event bubble must not keep delivering to it or its dead ancestors. */
    if ((w->flags & CL_WF_DEAD) || !(w->flags & CL_WF_ENABLED) || !vt)
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

void cl_widget_send_hover(cl_widget_t *w, bool enter)
{
    const cl_widget_vtable_t *vt = w->cls->vtable;

    if (!vt)
        return;
    if (vt->on_event) {
        cl_event_t ev;

        memset(&ev, 0, sizeof(ev));
        ev.type = enter ? CL_EVENT_MOUSE_ENTER : CL_EVENT_MOUSE_LEAVE;
        vt->on_event(w, &ev);
        return;
    }
    if (enter) {
        if (vt->mouse_enter)
            vt->mouse_enter(w);
    } else {
        if (vt->mouse_leave)
            vt->mouse_leave(w);
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
