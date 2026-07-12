/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_MENUBAR_H
#define CL_WIDGETS_MENUBAR_H

#include <stddef.h>

#include <copal/widget.h>
#include <copal/error.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_menubar_desc {
    uint32_t abi_version;
    size_t struct_size;
} cl_menubar_desc_t;

#define CL_MENUBAR_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_menubar_desc_t)
#define CL_MENUBAR_DESC_INIT { CL_MENUBAR_DESC_INIT_FIELDS }

/**
 * cl_menubar_create() - a horizontal bar of menu titles.
 *
 * Clicking a title opens its menu right below it; clicking it again (or
 * anywhere outside the open menus) dismisses the chain. Typically the first
 * child of the window's root vbox, stretched across the top.
 */
CL_API cl_widget_t *cl_menubar_create(cl_application_t *app,
                                      const cl_menubar_desc_t *desc);

/**
 * cl_menubar_add_menu() - append a titled menu. The bar takes ownership of
 * `menu` (a cl_menu widget, possibly with submenus); the widget is reused
 * across opens.
 */
CL_API cl_result_t cl_menubar_add_menu(cl_widget_t *bar, const char *title,
                                       cl_widget_t *menu);

/** cl_menubar_count() - number of titles. */
CL_API size_t cl_menubar_count(cl_widget_t *bar);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_MENUBAR_H */
