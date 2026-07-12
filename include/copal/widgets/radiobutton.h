/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_RADIOBUTTON_H
#define CL_WIDGETS_RADIOBUTTON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/version.h>
#include <copal/widget.h>
#include <copal/event.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;

/*
 * Radio button with an optional inline label. Selecting one deselects every
 * other radio button that shares the same POSITIVE `group` id within the same
 * widget tree (root). A non-positive group id (the default 0) means ungrouped:
 * the radio is independent and never deselects, or is deselected by, another.
 * Selects on a left click or Space; an already selected radio does not deselect
 * on click.
 *
 * Initialising more than one radio of the same group as selected is caller
 * error (the single-selection invariant is enforced on selection, not at
 * construction).
 */
typedef struct cl_radiobutton_desc {
    uint32_t abi_version;
    size_t struct_size;
    const char *text; /* label (UTF-8); may be NULL */
    int group;        /* mutual-exclusion group id; <= 0 means ungrouped */
    bool selected;    /* initial state */
} cl_radiobutton_desc_t;

#define CL_RADIOBUTTON_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_radiobutton_desc_t)

CL_API cl_widget_t *cl_radiobutton_create(cl_application_t *app,
                                          const cl_radiobutton_desc_t *desc);

/** cl_radiobutton_set_selected() - select/deselect. Selecting deselects the
 *  group. Does NOT fire on_select. */
CL_API void cl_radiobutton_set_selected(cl_widget_t *rb, bool selected);

/** cl_radiobutton_is_selected() - current state. */
CL_API bool cl_radiobutton_is_selected(cl_widget_t *rb);

/** cl_radiobutton_set_on_select() - called only when a user selection makes
 *  this radio the active one (not for programmatic changes). */
CL_API void cl_radiobutton_set_on_select(cl_widget_t *rb, cl_toggled_fn fn,
                                         void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_RADIOBUTTON_H */
