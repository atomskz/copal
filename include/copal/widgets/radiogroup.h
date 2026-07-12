/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_RADIOGROUP_H
#define CL_WIDGETS_RADIOGROUP_H

#include <stddef.h>

#include <copal/widget.h>
#include <copal/error.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_radiogroup_desc {
    uint32_t abi_version;
    size_t struct_size;
    float spacing; /* vertical gap between the radio buttons */
} cl_radiogroup_desc_t;

#define CL_RADIOGROUP_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_radiogroup_desc_t)
#define CL_RADIOGROUP_DESC_INIT { CL_RADIOGROUP_DESC_INIT_FIELDS }

/* index of the newly selected option */
typedef void (*cl_radiogroup_fn)(cl_widget_t *group, int index, void *user);

/**
 * cl_radiogroup_create() - a vertical column of mutually exclusive radios.
 *
 * Options are created by the group (cl_radiogroup_add) and share an
 * automatically assigned exclusion group; do not override the radios' own
 * on_select - the group uses it and reports through its own callback.
 */
CL_API cl_widget_t *cl_radiogroup_create(cl_application_t *app,
                                         const cl_radiogroup_desc_t *desc);

/** cl_radiogroup_add() - append an option; returns its radio widget. */
CL_API cl_widget_t *cl_radiogroup_add(cl_widget_t *group, const char *text);

/** cl_radiogroup_count() - number of options. */
CL_API size_t cl_radiogroup_count(cl_widget_t *group);

/** cl_radiogroup_selected() - selected option index, or -1. */
CL_API int cl_radiogroup_selected(cl_widget_t *group);

/** cl_radiogroup_set_selected() - select by index (-1 clears); does NOT fire
 *  the callback. */
CL_API void cl_radiogroup_set_selected(cl_widget_t *group, int index);

/** cl_radiogroup_set_on_change() - user selection changed. */
CL_API void cl_radiogroup_set_on_change(cl_widget_t *group,
                                        cl_radiogroup_fn fn, void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_RADIOGROUP_H */
