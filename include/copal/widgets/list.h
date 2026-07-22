/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_LIST_H
#define CL_WIDGETS_LIST_H

#include <stddef.h>

#include <copal/widget.h>
#include <copal/error.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_list_desc {
    uint32_t abi_version;
    size_t struct_size;
} cl_list_desc_t;

#define CL_LIST_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_list_desc_t)
#define CL_LIST_DESC_INIT { CL_LIST_DESC_INIT_FIELDS }

/* index is the affected item, or -1 when the selection was cleared. */
typedef void (*cl_list_fn)(cl_widget_t *list, int index, void *user);

/**
 * cl_list_create() - a selectable vertical list of text items.
 *
 * Click selects (and focuses); Up/Down/Home/End/PageUp/PageDown move the
 * selection; double-click or Enter "activates" the selected item. The list
 * measures to its full content: put it inside a cl_scrollview for long
 * content.
 */
CL_API cl_widget_t *cl_list_create(cl_application_t *app,
                                   const cl_list_desc_t *desc);

/** cl_list_add_item() - append an item (text copied). */
CL_API cl_result_t cl_list_add_item(cl_widget_t *list, const char *text);

/** cl_list_remove() - remove item `index` (selection is adjusted). */
CL_API cl_result_t cl_list_remove(cl_widget_t *list, size_t index);

/** cl_list_clear() - remove every item and clear the selection. */
CL_API void cl_list_clear(cl_widget_t *list);

/** cl_list_count() - number of items. */
CL_API size_t cl_list_count(cl_widget_t *list);

/** cl_list_item_text() - item text (borrowed; valid until the list changes),
 *  or NULL for a bad index. */
CL_API const char *cl_list_item_text(cl_widget_t *list, size_t index);

/** cl_list_selected() - selected index, or -1. */
CL_API int cl_list_selected(cl_widget_t *list);

/** cl_list_set_selected() - select an index (-1 clears); does NOT fire
 *  on_select. */
CL_API void cl_list_set_selected(cl_widget_t *list, int index);

/** cl_list_set_on_select() - selection changed by the user (click, keys). */
CL_API void cl_list_set_on_select(cl_widget_t *list, cl_list_fn fn,
                                  void *user);

/** cl_list_set_on_activate() - double-click or Enter on the selection. */
CL_API void cl_list_set_on_activate(cl_widget_t *list, cl_list_fn fn,
                                    void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_LIST_H */
