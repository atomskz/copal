/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/image.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <string.h>

#include "app/app_internal.h"
#include "core/foundation/foundation_internal.h"
#include "render/image_internal.h"

cl_image_t *cl_image_create(cl_application_t *app, int w, int h,
                            const void *rgba)
{
    const cl_allocator_t *a;
    cl_image_t *img;
    size_t bytes;

    if (!app || !rgba || w <= 0 || h <= 0) {
        cl_set_last_error(CL_ERROR_INVALID_ARGUMENT);
        return NULL;
    }
    a = cl_application_allocator(app);
    img = cl_alloc(a, sizeof(*img));
    if (!img)
        return NULL;
    memset(img, 0, sizeof(*img));
    bytes = (size_t)w * (size_t)h * 4u;
    img->rgba = cl_alloc(a, bytes);
    if (!img->rgba) {
        cl_free(a, img);
        return NULL;
    }
    memcpy(img->rgba, rgba, bytes);
    img->app = app;
    img->a = a;
    img->w = w;
    img->h = h;
    return img;
}

void cl_image_release(cl_image_t *img)
{
    const cl_allocator_t *a;

    if (!img)
        return;
    /* The renderers key texture caches by the raw image pointer; a later
     * image can reuse this address, so their entries must go now. */
    if (img->app && img->app->renderer &&
        img->app->renderer->ops->evict_image)
        img->app->renderer->ops->evict_image(img->app->renderer, img);
    a = img->a;
    cl_free(a, img->rgba);
    cl_free(a, img);
}

cl_size_t cl_image_size(const cl_image_t *img)
{
    cl_size_t s = { 0, 0 };

    if (img) {
        s.w = (float)img->w;
        s.h = (float)img->h;
    }
    return s;
}

const void *cl_image_pixels(const cl_image_t *img)
{
    return img ? img->rgba : NULL;
}
