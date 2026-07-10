/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_SCROLLVIEW_H
#define CL_WIDGETS_SCROLLVIEW_H

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
 * Vertical scroll container (MVP). Holds a single content widget, clips it to
 * the viewport, and scrolls it vertically via the mouse wheel or a draggable
 * scrollbar. The content is laid out at the viewport width and its natural
 * height.
 *
 * Not yet implemented (documented limitations): horizontal scrolling,
 * scroll-to-focus, and smooth/animated scrolling.
 */
typedef struct cl_scrollview_desc {
    uint32_t abi_version;
    size_t struct_size;
} cl_scrollview_desc_t;

#define CL_SCROLLVIEW_DESC_INIT_FIELDS \
    .abi_version = CL_VERSION, .struct_size = sizeof(cl_scrollview_desc_t)

CL_API cl_widget_t *cl_scrollview_create(cl_application_t *app,
                                         const cl_scrollview_desc_t *desc);

/** cl_scrollview_set_content() - set the single content widget (takes
 *  ownership); destroys any previous content and resets the scroll offset. */
CL_API void cl_scrollview_set_content(cl_widget_t *sv, cl_widget_t *content);

/** cl_scrollview_content() - the current content widget, or NULL. */
CL_API cl_widget_t *cl_scrollview_content(cl_widget_t *sv);

/** cl_scrollview_scroll_to() - set the vertical scroll offset (clamped). */
CL_API void cl_scrollview_scroll_to(cl_widget_t *sv, float y);

/** cl_scrollview_scroll_y() - the current vertical scroll offset in pixels. */
CL_API float cl_scrollview_scroll_y(cl_widget_t *sv);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_SCROLLVIEW_H */
