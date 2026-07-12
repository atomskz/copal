/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_APP_INTERNAL_H
#define CL_APP_INTERNAL_H

#include <stdbool.h>

#include <copal/application.h>
#include <copal/window.h>
#include <copal/timer.h>
#include <copal/allocator.h>

#include "platform/platform.h"
#include "render/renderer.h"
#include "core/foundation/mutex_internal.h"
#include "widget/widget_host.h"

typedef struct cl_task cl_task_t; /* cross-thread task node (application.c) */

struct cl_application {
    cl_allocator_t alloc; /* value copy so &alloc is stable */
    cl_platform_t *platform;
    cl_renderer_t *renderer;
    cl_theme_t *theme;
    cl_window_t *window; /* single window in MVP */
    cl_timer_t *timers;  /* owned; linked list of active timers */
    bool timer_firing;   /* true while firing: defers timer reaping */
    cl_mutex_t *task_mutex; /* guards the cross-thread task queue */
    cl_task_t *task_head;   /* FIFO of posted tasks (guarded) */
    cl_task_t *task_tail;
    cl_widget_t *dead; /* deferred-destruction chain (via next_sibling) */
    bool quit;
    int exit_code;
    /* CL_RENDER_AUTO with built-in GL backends: a failed GL window may be
     * retried once with the software pair (cl_app_software_fallback). */
    bool soft_fallback_ok;
};

/* Run and free all queued cross-thread tasks (cl_application_post) on the loop
 * thread. Defined in application.c, driven by run()/step(). */
void cl_app_run_tasks(cl_application_t *app);

/* Replace the built-in GL platform/renderer pair with the software pair
 * (one shot; only when soft_fallback_ok). Returns true when swapped. */
bool cl_app_software_fallback(cl_application_t *app);

/* Deferred widget destruction: queue a DEAD subtree root; free the whole
 * queue (application.c, driven once per loop iteration and at shutdown). */
void cl_app_defer_widget_free(cl_application_t *app, cl_widget_t *w);
void cl_app_reap_dead(cl_application_t *app);

/* Timer subsystem (src/app/timer.c), driven by the application loop. */
int cl_app_timers_timeout(cl_application_t *app);  /* ms to next, or -1 */
void cl_app_timers_poll(cl_application_t *app);     /* fire due timers */
void cl_app_timers_free_all(cl_application_t *app); /* free all at shutdown */

#define CL_WINDOW_MAX_OVERLAYS 8

struct cl_window {
    /* MUST stay first: the widget layer reaches its host by casting the
     * window pointer (src/widget/widget_host.h). */
    cl_widget_host_t host;
    cl_application_t *app;      /* weak */
    cl_platform_window_t *native; /* backend window handle (create_window) */
    cl_widget_t *content;       /* owned */
    /*
     * Overlay stack: popups/menus/dialogs painted over the content, index 0
     * at the bottom. `owned` entries are destroyed on close; non-owned ones
     * (menu submenus, menubar menus) are detached back to their owner for
     * reuse. `closing` defers the close to the post-dispatch reap so queued
     * events in the same iteration cannot leak to the content.
     */
    struct cl_overlay {
        cl_widget_t *widget; /* owned when `owned`, else borrowed */
        cl_widget_t *owner;  /* weak; opener, tears the entry down with it */
        cl_point_t anchor;   /* requested position (pre-clamp) */
        bool owned;
        bool modal;          /* outside clicks are swallowed, not dismissing */
        bool center;         /* ignore anchor; centre in the window */
        bool closing;        /* reaped after event dispatch */
    } overlays[CL_WINDOW_MAX_OVERLAYS];
    int overlay_count;
    cl_widget_t *mouse_target;   /* weak; basic pointer capture */
    cl_widget_t *hover;          /* weak; widget under the pointer */
    cl_cursor_t cursor;          /* shape currently applied to the platform */
    cl_widget_t *focus;          /* weak; keyboard focus */
    bool focus_reveal_pending;   /* reveal focus once the next layout is fresh */
    cl_widget_t *tooltip;        /* owned; the shown tooltip bubble, or NULL */
    cl_widget_t *tooltip_target; /* weak; hovered widget being timed/shown */
    cl_timer_t *tooltip_timer;   /* pending dwell timer, or NULL */
    cl_point_t tooltip_anchor;   /* cursor position to place the tooltip near */
    cl_size_t size;              /* logical px */
    float scale;
    bool dirty;
    bool layout_dirty;
    cl_window_close_fn on_close;
    void *on_close_user;
};

/* Window internals (defined in window.c), driven by the application loop. */
void cl_window_render(cl_window_t *win);
void cl_window_handle_mouse(cl_window_t *win, cl_platform_event_kind_t kind,
                            cl_point_t pos, cl_mouse_button_t button,
                            cl_key_mods_t mods, int clicks);
void cl_window_handle_wheel(cl_window_t *win, cl_point_t pos, float dx,
                            float dy, cl_key_mods_t mods);
void cl_window_handle_key(cl_window_t *win, cl_platform_event_kind_t kind,
                          cl_key_t key, cl_key_mods_t mods);
void cl_window_handle_text(cl_window_t *win, const char *utf8);
void cl_window_handle_text_edit(cl_window_t *win, const char *utf8, int cursor);
void cl_window_set_focus(cl_window_t *win, cl_widget_t *w);
void cl_window_focus_next(cl_window_t *win, bool forward);
void cl_window_resize(cl_window_t *win, cl_size_t size);
void cl_window_mark_dirty(cl_window_t *win);
void cl_window_mark_layout_dirty(cl_window_t *win);
void cl_window_reap_overlay(cl_window_t *win); /* destroy closed popups safely */
/* Tie the top popup's lifetime to the widget that opened it. */
void cl_window_set_overlay_owner(cl_window_t *win, cl_widget_t *owner);
/* Tear down every overlay opened by w or being w (w is going away). */
void cl_window_owner_destroyed(cl_window_t *win, cl_widget_t *w);
/* Push a popup on TOP of the stack without closing what is open. The window
 * does NOT take ownership: on close the widget is detached, not destroyed
 * (menu submenus and menubar menus are reused across opens). */
void cl_window_push_popup(cl_window_t *win, cl_widget_t *owner,
                          cl_widget_t *popup, cl_point_t at);
/* Request-close only the topmost overlay (Escape in a submenu). */
void cl_window_pop_popup(cl_window_t *win);
/* If w is the hovered tooltip target, dismiss it (called when w is destroyed). */
void cl_window_tooltip_target_gone(cl_window_t *win, cl_widget_t *w);

/*
 * Clipboard access for widgets. cl_app_clipboard_get returns a UTF-8 copy
 * allocated with the app allocator (free with cl_application_allocator), or
 * NULL if empty. cl_app_clipboard_set copies utf8 to the system clipboard.
 */
char *cl_app_clipboard_get(cl_application_t *app);
void cl_app_clipboard_set(cl_application_t *app, const char *utf8);

/* Position the IME candidate window near a caret (no-op if unsupported). */
void cl_app_set_ime_rect(cl_application_t *app, cl_rect_t rect);

#endif /* CL_APP_INTERNAL_H */
