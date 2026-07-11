/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_COMBOBOX_H
#define CL_WIDGETS_COMBOBOX_H

#include <stddef.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/version.h>
#include <copal/widget.h>
#include <copal/event.h>
#include <copal/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;

/*
 * Drop-down selector. Shows the selected item's text with a caret; clicking it
 * (or Space/Enter/Down while focused) opens a popup list in the window overlay
 * layer. Choosing an item updates the selection and fires on_change.
 */
typedef struct cl_combobox_desc {
    uint32_t abi_version;
    size_t struct_size;
    const char *placeholder; /* shown when nothing is selected; may be NULL */
} cl_combobox_desc_t;

#define CL_COMBOBOX_DESC_INIT_FIELDS \
    .abi_version = CL_VERSION, .struct_size = sizeof(cl_combobox_desc_t)

CL_API cl_widget_t *cl_combobox_create(cl_application_t *app,
                                       const cl_combobox_desc_t *desc);

/** cl_combobox_add_item() - append a selectable item (UTF-8). */
CL_API cl_result_t cl_combobox_add_item(cl_widget_t *combo, const char *text);

/** cl_combobox_count() - number of items. */
CL_API size_t cl_combobox_count(cl_widget_t *combo);

/** cl_combobox_set_selected() - select by index (-1 = none). Does NOT fire
 *  on_change. */
CL_API void cl_combobox_set_selected(cl_widget_t *combo, int index);

/** cl_combobox_selected() - selected index, or -1. */
CL_API int cl_combobox_selected(cl_widget_t *combo);

/** cl_combobox_selected_text() - selected item text, or NULL. */
CL_API const char *cl_combobox_selected_text(cl_widget_t *combo);

/** cl_combobox_set_on_change() - called only on user selection changes. */
CL_API void cl_combobox_set_on_change(cl_widget_t *combo, cl_selection_fn fn,
                                      void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_COMBOBOX_H */
