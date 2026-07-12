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
 * Enter activates it and closes the menu chain; Escape dismisses the topmost
 * menu, a click outside every menu dismisses the chain. Build with
 * cl_menu_add_item / cl_menu_add_submenu, then hand it to
 * cl_window_open_popup, which takes ownership.
 */
CL_API cl_widget_t *cl_menu_create(cl_application_t *app);

/** cl_menu_add_item() - append an item; fn(menu, user) runs when it is chosen. */
CL_API cl_result_t cl_menu_add_item(cl_widget_t *menu, const char *text,
                                    cl_action_fn fn, void *user);

/**
 * cl_menu_add_submenu() - append an item that opens `submenu` (another menu
 * widget) beside it on click, Enter or Right. The parent menu takes
 * ownership of `submenu`; the widget is reused across opens.
 */
CL_API cl_result_t cl_menu_add_submenu(cl_widget_t *menu, const char *text,
                                       cl_widget_t *submenu);

/** cl_menu_count() - number of items. */
CL_API size_t cl_menu_count(cl_widget_t *menu);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_MENU_H */
