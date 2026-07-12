/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WINDOW_H
#define CL_WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/types.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_window cl_window_t;
typedef struct cl_application cl_application_t;
typedef struct cl_widget cl_widget_t;

/* Return false to veto a close request. */
typedef bool (*cl_window_close_fn)(cl_window_t *win, void *user);

typedef struct cl_window_desc {
    uint32_t abi_version;
    size_t struct_size;
    const char *title; /* UTF-8; may be NULL */
    int32_t width, height;
    int32_t min_width, min_height;
    bool resizable;
} cl_window_desc_t;

/* Service fields for a designated initializer / compound literal (the same
 * idiom as the widget descs)... */
#define CL_WINDOW_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_window_desc_t)
/* ...and the full default initializer built from it. */
#define CL_WINDOW_DESC_INIT { CL_WINDOW_DESC_INIT_FIELDS }

/* MVP allows a single window; a second returns NULL + CL_ERROR_UNSUPPORTED. */
CL_API cl_window_t *cl_window_create(cl_application_t *app,
                                     const cl_window_desc_t *desc);
CL_API void cl_window_destroy(cl_window_t *win);

CL_API void cl_window_show(cl_window_t *win);
/**
 * cl_window_set_content() - set the window's root widget.
 *
 * The window takes ownership of @root; setting a new root destroys any
 * previous content subtree. NULL clears (and destroys) the content.
 */
CL_API void cl_window_set_content(cl_window_t *win, cl_widget_t *root);
CL_API cl_widget_t *cl_window_content(cl_window_t *win);
CL_API void cl_window_set_title(cl_window_t *win, const char *utf8);
CL_API cl_size_t cl_window_size(cl_window_t *win);
CL_API void cl_window_set_on_close(cl_window_t *win, cl_window_close_fn fn,
                                   void *user);

/*
 * Overlay popups. cl_window_open_popup() shows `popup` on top of the content
 * at window position `at` (clamped on-screen), takes ownership of it, and
 * replaces whatever popups were open. Popups stack: a menu can push its
 * submenu on top (cl_menu_add_submenu). A mouse press outside every popup,
 * or an explicit cl_window_close_popup(), dismisses the whole chain; a press
 * into a lower entry collapses the ones above it. Closing is deferred to a
 * safe point, so a popup's own handler may request the close.
 */
CL_API void cl_window_open_popup(cl_window_t *win, cl_widget_t *popup,
                                 cl_point_t at);
CL_API void cl_window_close_popup(cl_window_t *win);
/* The topmost open popup, or NULL. */
CL_API cl_widget_t *cl_window_popup(cl_window_t *win);

/*
 * cl_window_open_modal() - show `dialog` centred over the content, replacing
 * any open popups; ownership transfers to the window. Outside clicks are
 * swallowed instead of dismissing: close explicitly with
 * cl_window_close_popup() (e.g. from a dialog button). Building block for
 * message boxes and custom dialogs.
 */
CL_API void cl_window_open_modal(cl_window_t *win, cl_widget_t *dialog);

/* The tooltip bubble currently shown by the hover layer, or NULL. Ownership
 * stays with the window; mainly useful for introspection and testing. */
CL_API cl_widget_t *cl_window_tooltip(cl_window_t *win);

#ifdef __cplusplus
}
#endif

#endif /* CL_WINDOW_H */
