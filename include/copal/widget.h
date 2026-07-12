/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGET_H
#define CL_WIDGET_H

#include <stdbool.h>

#include <copal/export.h>
#include <copal/types.h>
#include <copal/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_widget cl_widget_t;
typedef struct cl_application cl_application_t;
typedef struct cl_window cl_window_t;

/* Tree / ownership (ARCHITECTURE §4). */
CL_API cl_result_t cl_widget_add_child(cl_widget_t *parent, cl_widget_t *child);
CL_API cl_result_t cl_widget_remove_child(cl_widget_t *parent,
                                          cl_widget_t *child);
/**
 * cl_widget_destroy() - destroy a widget and its whole subtree.
 *
 * The subtree is detached at once (removed from the tree, invisible to
 * hit-testing and events, every weak reference in the window dropped), but
 * for widgets attached to a window the memory is freed only at the end of
 * the current event-loop iteration. Destroying ANY widget from ANY callback
 * is therefore safe; a second destroy of the same widget is a no-op. A tree
 * that was never attached to a window is freed immediately.
 */
CL_API void cl_widget_destroy(cl_widget_t *w);
CL_API cl_widget_t *cl_widget_parent(cl_widget_t *w);
CL_API cl_window_t *cl_widget_window(cl_widget_t *w);

/* Geometry / state. */
CL_API cl_rect_t cl_widget_rect(cl_widget_t *w);
CL_API void cl_widget_set_visible(cl_widget_t *w, bool v);
CL_API bool cl_widget_is_visible(cl_widget_t *w);
CL_API void cl_widget_set_enabled(cl_widget_t *w, bool e);
CL_API bool cl_widget_is_enabled(cl_widget_t *w);
/*
 * Mouse cursor shown while the pointer hovers this widget. CL_CURSOR_DEFAULT
 * defers to the nearest ancestor with a non-default cursor (so a container
 * can set one for a whole subtree). The textbox defaults to CL_CURSOR_IBEAM.
 */
CL_API void cl_widget_set_cursor(cl_widget_t *w, cl_cursor_t cursor);
CL_API cl_cursor_t cl_widget_cursor(cl_widget_t *w);
/* Per-child layout attributes, consumed by the box containers (vbox/hbox):
 * - preferred_size overrides the widget's own measure on each axis > 0;
 * - flex > 0 receives that share of the leftover main-axis space (grow
 *   only; 0 = fixed);
 * - align overrides the container's cross-axis align_cross for this child
 *   (the CL_ALIGN_AUTO default defers to the container); the main-axis
 *   component is ignored by boxes. */
CL_API void cl_widget_set_preferred_size(cl_widget_t *w, cl_size_t s);
CL_API cl_size_t cl_widget_preferred_size(cl_widget_t *w);
CL_API void cl_widget_set_margin(cl_widget_t *w, cl_insets_t m);
CL_API cl_insets_t cl_widget_margin(cl_widget_t *w);
CL_API void cl_widget_set_align(cl_widget_t *w, cl_align_t h, cl_align_t v);
CL_API cl_align_t cl_widget_align_h(cl_widget_t *w);
CL_API cl_align_t cl_widget_align_v(cl_widget_t *w);
CL_API void cl_widget_set_flex(cl_widget_t *w, float weight);
CL_API float cl_widget_flex(cl_widget_t *w);

/* Focus. */
CL_API void cl_widget_set_focusable(cl_widget_t *w, bool focusable);
CL_API bool cl_widget_is_focusable(cl_widget_t *w);
CL_API bool cl_widget_focus(cl_widget_t *w);
CL_API bool cl_widget_has_focus(cl_widget_t *w);

/* Invalidation. */
CL_API void cl_widget_invalidate(cl_widget_t *w);
CL_API void cl_widget_invalidate_layout(cl_widget_t *w);

/* User data. */
CL_API void cl_widget_set_userdata(cl_widget_t *w, void *user);
CL_API void *cl_widget_userdata(cl_widget_t *w);

/* Hover tooltip. The text (copied) appears after the pointer dwells over the
 * widget; NULL or "" clears it. */
CL_API void cl_widget_set_tooltip(cl_widget_t *w, const char *utf8);
CL_API const char *cl_widget_tooltip(cl_widget_t *w);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGET_H */
