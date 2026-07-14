/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGET_INTERNAL_H
#define CL_WIDGET_INTERNAL_H

#include <stdbool.h>

#include <copal/widget_impl.h>

#include "core/foundation/foundation_internal.h"

static inline bool cl_rect_contains(cl_rect_t r, cl_point_t p)
{
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

/* Hard cap on widget-tree nesting depth, enforced by cl_widget_add_child so
 * the recursive tree walks (paint, hit-test, measure/arrange, free) cannot
 * overflow the C stack on a pathologically deep tree. Real UIs nest a few tens
 * of levels; this leaves generous headroom while staying stack-safe. */
#define CL_WIDGET_MAX_DEPTH 256

/*
 * Desc ABI handshake for the widget constructors (ADR-005). Validate a desc
 * header and normalise a non-NULL desc into `norm` (a zeroed, full-size local
 * of the desc type), repointing `desc` at it so the rest of the constructor
 * reads a full-size, tail-defaulted copy - an older caller's missing fields
 * default to zero, a newer caller's extra tail is ignored. A NULL desc means
 * "all defaults" and stays NULL for the constructor's own `if (desc)` guards.
 * Evaluates to false (CL_ERROR_ABI_MISMATCH) on an incompatible header.
 * Use as: `TYPE norm; if (!CL_DESC_NORM(desc, norm)) return NULL;`.
 */
#define CL_DESC_NORM(desc, norm)                                            \
    ((desc) == NULL                                                         \
         ? true                                                             \
         : (cl_abi_ok((desc)->abi_version, (desc)->struct_size,             \
                      CL_DESC_MIN_SIZE)                                      \
                ? (cl_desc_fill(&(norm), sizeof(norm), (desc),              \
                                (desc)->struct_size),                       \
                   (desc) = &(norm), true)                                  \
                : false))

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
