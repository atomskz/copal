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
CL_API void cl_widget_destroy(cl_widget_t *w);
CL_API cl_widget_t *cl_widget_parent(cl_widget_t *w);
CL_API cl_window_t *cl_widget_window(cl_widget_t *w);

/* Geometry / state. */
CL_API cl_rect_t cl_widget_rect(cl_widget_t *w);
CL_API void cl_widget_set_visible(cl_widget_t *w, bool v);
CL_API bool cl_widget_is_visible(cl_widget_t *w);
CL_API void cl_widget_set_enabled(cl_widget_t *w, bool e);
CL_API bool cl_widget_is_enabled(cl_widget_t *w);
CL_API void cl_widget_set_preferred_size(cl_widget_t *w, cl_size_t s);
CL_API void cl_widget_set_margin(cl_widget_t *w, cl_insets_t m);
CL_API void cl_widget_set_align(cl_widget_t *w, cl_align_t h, cl_align_t v);
CL_API void cl_widget_set_flex(cl_widget_t *w, float weight);

/* Invalidation. */
CL_API void cl_widget_invalidate(cl_widget_t *w);
CL_API void cl_widget_invalidate_layout(cl_widget_t *w);

/* User data. */
CL_API void cl_widget_set_userdata(cl_widget_t *w, void *user);
CL_API void *cl_widget_userdata(cl_widget_t *w);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGET_H */
