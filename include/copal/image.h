/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_IMAGE_H
#define CL_IMAGE_H

#include <stddef.h>

#include <copal/export.h>
#include <copal/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;
typedef struct cl_image cl_image_t;

/**
 * cl_image_create() - create an image from raw RGBA8 pixels.
 * @app:  owning application (provides the allocator, evicts renderer caches).
 * @w:    width in pixels (> 0).
 * @h:    height in pixels (> 0).
 * @rgba: w*h*4 bytes, row-major, straight (non-premultiplied) alpha; copied.
 *
 * The library does not decode image files; decode with your codec of choice
 * (or embed pixel arrays for icons) and hand the raw pixels here.
 *
 * Dimensions whose byte size (w*h*4) would overflow size_t are rejected with
 * CL_ERROR_INVALID_ARGUMENT (this can only bite on a 32-bit size_t).
 *
 * Return: an image handle, or NULL on error (see cl_last_error()).
 */
CL_API cl_image_t *cl_image_create(cl_application_t *app, int w, int h,
                                   const void *rgba);

/**
 * cl_image_release() - release an image.
 *
 * Evicts the renderer's cached texture for this image, so releasing and
 * creating images at run time is safe. Release images BEFORE destroying the
 * application that created them.
 */
CL_API void cl_image_release(cl_image_t *img);

/** cl_image_size() - image size in pixels (drawn 1:1 in logical px). */
CL_API cl_size_t cl_image_size(const cl_image_t *img);

/** cl_image_pixels() - the RGBA8 pixel data (for custom renderer backends). */
CL_API const void *cl_image_pixels(const cl_image_t *img);

#ifdef __cplusplus
}
#endif

#endif /* CL_IMAGE_H */
