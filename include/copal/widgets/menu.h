/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_MENU_H
#define CL_WIDGETS_MENU_H

#include <stddef.h>

#include <copal/export.h>
#include <copal/widget.h>
#include <copal/event.h>
#include <copal/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;

/*
 * Popup menu: a vertical list of text items shown in the window overlay layer
 * (see cl_window_open_popup). Hovering highlights an item; a left click or
 * Enter activates it and closes the menu; Escape or a click outside dismisses
 * it. Build with cl_menu_add_item, then hand it to cl_window_open_popup, which
 * takes ownership.
 *
 * Not yet supported: submenus, and opening another popup from within an item's
 * callback (the menu is torn down after the callback returns).
 */
CL_API cl_widget_t *cl_menu_create(cl_application_t *app);

/** cl_menu_add_item() - append an item; fn(menu, user) runs when it is chosen. */
CL_API cl_result_t cl_menu_add_item(cl_widget_t *menu, const char *text,
                                    cl_action_fn fn, void *user);

/** cl_menu_count() - number of items. */
CL_API size_t cl_menu_count(cl_widget_t *menu);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_MENU_H */
