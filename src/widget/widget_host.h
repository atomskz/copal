/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGET_HOST_H
#define CL_WIDGET_HOST_H

/*
 * The narrow interface a widget may demand of whatever hosts it: repaint /
 * relayout scheduling, keyboard focus, the overlay popup, clipboard and IME.
 *
 * The interface is OWNED by the widget layer and implemented by the window,
 * which embeds cl_widget_host_t as its FIRST member - a cl_window_t* and a
 * cl_widget_host_t* are the same pointer. That inversion keeps the core
 * widget/layout modules compile-time independent of app/window
 * (ARCHITECTURE §2): nothing under src/widget, src/widgets or src/layout
 * includes app internals.
 */
#include <copal/types.h>
#include <copal/widget.h>
#include <copal/widget_impl.h>

typedef struct cl_widget_host cl_widget_host_t;

typedef struct cl_widget_host_ops {
    /* Schedule a repaint / a relayout for the next frame. */
    void (*mark_dirty)(cl_widget_host_t *h);
    void (*mark_layout_dirty)(cl_widget_host_t *h);

    /* Keyboard focus. set_focus(NULL) clears it; both fire the widgets'
     * focus_gained/focus_lost callbacks. */
    void (*set_focus)(cl_widget_host_t *h, cl_widget_t *w);
    cl_widget_t *(*focused)(cl_widget_host_t *h);

    /* Open `popup` as the overlay near `anchor`, tying its lifetime to
     * `owner` (destroying the owner tears the popup down). close_popup
     * dismisses the current overlay, whoever owns it. */
    void (*open_popup)(cl_widget_host_t *h, cl_widget_t *owner,
                       cl_widget_t *popup, cl_point_t anchor);
    void (*close_popup)(cl_widget_host_t *h);

    /*
     * Silently drop every weak reference the host holds to `w` (pointer
     * capture, focus, content root, popup ownership, hover tooltip): the
     * widget is being destroyed or detached and must not be called back.
     */
    void (*widget_gone)(cl_widget_host_t *h, cl_widget_t *w);

    /* Clipboard: get returns a UTF-8 copy allocated with the application's
     * allocator (caller frees), or NULL if empty. */
    char *(*clipboard_get)(cl_widget_host_t *h);
    void (*clipboard_set)(cl_widget_host_t *h, const char *utf8);

    /* Position the IME candidate window near the caret (logical px). */
    void (*set_ime_rect)(cl_widget_host_t *h, cl_rect_t rect);
} cl_widget_host_ops_t;

struct cl_widget_host {
    const cl_widget_host_ops_t *ops;
};

/* The host of an attached widget, or NULL while detached. */
static inline cl_widget_host_t *cl_widget_host(const cl_widget_t *w)
{
    return (cl_widget_host_t *)w->window;
}

#endif /* CL_WIDGET_HOST_H */
