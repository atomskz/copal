/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGET_INTERNAL_H
#define CL_WIDGET_INTERNAL_H

#include <stdbool.h>

#include <copal/widget_impl.h>

static inline bool cl_rect_contains(cl_rect_t r, cl_point_t p)
{
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

/* Layout / paint / input plumbing, driven by the window (window.c). */
cl_size_t cl_widget_do_measure(cl_widget_t *w, cl_constraints_t c);
void cl_widget_do_arrange(cl_widget_t *w, cl_rect_t rect);
void cl_widget_do_paint(cl_widget_t *w, cl_paint_context_t *ctx);
cl_widget_t *cl_widget_hit(cl_widget_t *w, cl_point_t p);
bool cl_widget_dispatch(cl_widget_t *w, const cl_event_t *ev);
void cl_widget_reveal(cl_widget_t *w); /* scroll ancestors to reveal w */
void cl_widget_set_window(cl_widget_t *w, cl_window_t *win);

#endif /* CL_WIDGET_INTERNAL_H */
