/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_APP_INTERNAL_H
#define CL_APP_INTERNAL_H

#include <stdbool.h>

#include <copal/application.h>
#include <copal/window.h>
#include <copal/allocator.h>

#include "platform/platform.h"
#include "render/renderer.h"

struct cl_application {
    cl_allocator_t alloc; /* value copy so &alloc is stable */
    cl_platform_t *platform;
    cl_renderer_t *renderer;
    cl_theme_t *theme;
    cl_window_t *window; /* single window in MVP */
    cl_log_fn log_fn;
    void *log_user;
    bool quit;
    int exit_code;
};

struct cl_window {
    cl_application_t *app;      /* weak */
    cl_widget_t *content;       /* owned */
    cl_widget_t *overlay;       /* owned; active popup, or NULL */
    cl_point_t overlay_anchor;  /* requested popup position (pre-clamp) */
    bool overlay_closing;       /* deferred-close flag for the overlay */
    cl_widget_t *mouse_target;  /* weak; basic pointer capture */
    cl_widget_t *focus;         /* weak; keyboard focus */
    cl_size_t size;             /* logical px */
    float scale;
    bool dirty;
    bool layout_dirty;
    cl_window_close_fn on_close;
    void *on_close_user;
};

/* Window internals (defined in window.c), driven by the application loop. */
void cl_window_render(cl_window_t *win);
void cl_window_handle_mouse(cl_window_t *win, cl_platform_event_kind_t kind,
                            cl_point_t pos, cl_mouse_button_t button);
void cl_window_handle_wheel(cl_window_t *win, cl_point_t pos, float dx,
                            float dy);
void cl_window_handle_key(cl_window_t *win, cl_platform_event_kind_t kind,
                          cl_key_t key, cl_key_mods_t mods);
void cl_window_handle_text(cl_window_t *win, const char *utf8);
void cl_window_set_focus(cl_window_t *win, cl_widget_t *w);
void cl_window_focus_next(cl_window_t *win, bool forward);
void cl_window_resize(cl_window_t *win, cl_size_t size);
void cl_window_mark_dirty(cl_window_t *win);
void cl_window_mark_layout_dirty(cl_window_t *win);
void cl_window_reap_overlay(cl_window_t *win); /* destroy a closed popup safely */

/*
 * Clipboard access for widgets. cl_app_clipboard_get returns a UTF-8 copy
 * allocated with the app allocator (free with cl_application_allocator), or
 * NULL if empty. cl_app_clipboard_set copies utf8 to the system clipboard.
 */
char *cl_app_clipboard_get(cl_application_t *app);
void cl_app_clipboard_set(cl_application_t *app, const char *utf8);

#endif /* CL_APP_INTERNAL_H */
