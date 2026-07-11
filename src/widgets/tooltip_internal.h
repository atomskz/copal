/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_TOOLTIP_INTERNAL_H
#define CL_WIDGETS_TOOLTIP_INTERNAL_H

/* Internal tooltip bubble widget, created by the window's hover layer. Not part
 * of the public API: applications attach tooltips with cl_widget_set_tooltip(). */
#include <copal/widget.h>

typedef struct cl_application cl_application_t;

/* Create a non-interactive bubble showing `text` (copied). NULL on failure. */
cl_widget_t *cl_tooltip_create(cl_application_t *app, const char *text);

#endif /* CL_WIDGETS_TOOLTIP_INTERNAL_H */
