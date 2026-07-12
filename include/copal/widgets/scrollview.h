/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_SCROLLVIEW_H
#define CL_WIDGETS_SCROLLVIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/version.h>
#include <copal/widget.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;

/*
 * Scroll container (MVP). Holds a single content widget, clips it to the
 * viewport, and scrolls it via the mouse wheel or draggable scrollbars.
 *
 * By default only vertical scrolling is enabled: the content is laid out at the
 * viewport width (so wrapping content reflows) and its natural height. Setting
 * `horizontal` also allows sideways overflow: the content keeps its natural
 * width and a horizontal scrollbar appears when it exceeds the viewport.
 *
 * A descendant that gains keyboard focus (via Tab or cl_widget_focus) is
 * scrolled into view automatically; cl_scrollview_scroll_to_widget() does the
 * same on demand.
 *
 * With `smooth` set, wheel scrolling eases toward its target over a few frames
 * (driven by a timer) instead of jumping; dragging, paging, scroll_to and
 * scroll-to-focus stay instant. cl_scrollview_scroll_x/y() report the current
 * (possibly animating) offset. Smooth scrolling needs a platform clock; without
 * one it falls back to instant.
 */
typedef struct cl_scrollview_desc {
    uint32_t abi_version;
    size_t struct_size;
    bool horizontal; /* allow horizontal overflow and scrolling */
    bool smooth;     /* animate wheel scrolling instead of jumping */
} cl_scrollview_desc_t;

#define CL_SCROLLVIEW_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_scrollview_desc_t)

CL_API cl_widget_t *cl_scrollview_create(cl_application_t *app,
                                         const cl_scrollview_desc_t *desc);

/** cl_scrollview_set_content() - set the single content widget (takes
 *  ownership); destroys any previous content and resets the scroll offset. */
CL_API void cl_scrollview_set_content(cl_widget_t *sv, cl_widget_t *content);

/** cl_scrollview_content() - the current content widget, or NULL. */
CL_API cl_widget_t *cl_scrollview_content(cl_widget_t *sv);

/** cl_scrollview_scroll_to() - set the vertical scroll offset (clamped). */
CL_API void cl_scrollview_scroll_to(cl_widget_t *sv, float y);

/** cl_scrollview_scroll_to_x() - set the horizontal scroll offset (clamped).
 *  Has no visible effect unless the scrollview was created with horizontal. */
CL_API void cl_scrollview_scroll_to_x(cl_widget_t *sv, float x);

/** cl_scrollview_scroll_y() - the current vertical scroll offset in pixels. */
CL_API float cl_scrollview_scroll_y(cl_widget_t *sv);

/** cl_scrollview_scroll_x() - the current horizontal scroll offset in pixels. */
CL_API float cl_scrollview_scroll_x(cl_widget_t *sv);

/** cl_scrollview_scroll_to_widget() - scroll the minimal amount so `descendant`
 *  (a widget somewhere inside the content) is visible within the viewport. */
CL_API void cl_scrollview_scroll_to_widget(cl_widget_t *sv,
                                           cl_widget_t *descendant);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_SCROLLVIEW_H */
