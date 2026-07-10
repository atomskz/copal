/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_THEME_INTERNAL_H
#define CL_THEME_INTERNAL_H

#include <copal/theme.h>
#include <copal/types.h>

typedef struct cl_application cl_application_t;

/* Create the built-in light theme, owned by the application. */
cl_theme_t *cl_theme_default(cl_application_t *app);
void cl_theme_free(cl_theme_t *theme);

/* Metrics used by the built-in widgets. */
float cl_theme_radius(cl_theme_t *theme);
cl_size_t cl_theme_button_padding(cl_theme_t *theme); /* w = x pad, h = y pad */

#endif /* CL_THEME_INTERNAL_H */
