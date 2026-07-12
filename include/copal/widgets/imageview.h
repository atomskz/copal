/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_IMAGEVIEW_H
#define CL_WIDGETS_IMAGEVIEW_H

#include <copal/widget.h>
#include <copal/image.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_imageview_desc {
    uint32_t abi_version;
    size_t struct_size;
    cl_image_t *image; /* borrowed, not owned; may be NULL (paints nothing) */
} cl_imageview_desc_t;

#define CL_IMAGEVIEW_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_imageview_desc_t)
#define CL_IMAGEVIEW_DESC_INIT { CL_IMAGEVIEW_DESC_INIT_FIELDS }

/**
 * cl_imageview_create() - a widget that draws an image.
 *
 * Measures to the image's pixel size (override per axis with
 * cl_widget_set_preferred_size); the image is stretched to the widget's
 * rect when layout assigns a different size. The image is BORROWED: keep it
 * alive while the widget uses it and release it after the widget is gone.
 */
CL_API cl_widget_t *cl_imageview_create(cl_application_t *app,
                                        const cl_imageview_desc_t *desc);

/** cl_imageview_set_image() - swap the displayed image (borrowed; NULL ok). */
CL_API void cl_imageview_set_image(cl_widget_t *w, cl_image_t *image);

/** cl_imageview_image() - the currently displayed image, or NULL. */
CL_API cl_image_t *cl_imageview_image(cl_widget_t *w);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_IMAGEVIEW_H */
