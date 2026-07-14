/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGET_INTERNAL_H
#define CL_WIDGET_INTERNAL_H

#include <stdbool.h>

#include <copal/widget_impl.h>

static inline bool cl_rect_contains(cl_rect_t r, cl_point_t p)
{
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

/* Hard cap on widget-tree nesting depth, enforced by cl_widget_add_child so
 * the recursive tree walks (paint, hit-test, measure/arrange, free) cannot
 * overflow the C stack on a pathologically deep tree. Real UIs nest a few tens
 * of levels; this leaves generous headroom while staying stack-safe. */
#define CL_WIDGET_MAX_DEPTH 256

/* Desc ABI handshake shared by the widget constructors (ADR-005). Sets
 * CL_ERROR_ABI_MISMATCH on failure. A NULL desc means defaults: nothing to
 * verify. */
bool cl_desc_abi_check(uint32_t abi_version, size_t struct_size,
                       size_t expected);
#define CL_DESC_ABI_OK(desc, type)                                          \
    (!(desc) || cl_desc_abi_check((desc)->abi_version, (desc)->struct_size, \
                                  sizeof(type)))

/* Layout / paint / input plumbing, driven by the window (window.c).
 * do_measure/do_arrange/reveal are public (widget_impl.h): custom containers
 * need them too. */
void cl_widget_do_paint(cl_widget_t *w, cl_paint_context_t *ctx);
cl_widget_t *cl_widget_hit(cl_widget_t *w, cl_point_t p);
bool cl_widget_dispatch(cl_widget_t *w, const cl_event_t *ev);
/* Deliver a hover transition to w only (no bubbling): the mouse_enter/leave
 * slot, or CL_EVENT_MOUSE_ENTER/LEAVE through an on_event override. */
void cl_widget_send_hover(cl_widget_t *w, bool enter);
/* Free a destroyed (CL_WF_DEAD) subtree: children first, then the vtable
 * destroy callback and the memory. Driven by the host's deferred queue. */
void cl_widget_free_subtree(cl_widget_t *w);
void cl_widget_set_window(cl_widget_t *w, cl_window_t *win);

#endif /* CL_WIDGET_INTERNAL_H */
