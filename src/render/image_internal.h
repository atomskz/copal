/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_IMAGE_INTERNAL_H
#define CL_IMAGE_INTERNAL_H

#include <copal/image.h>
#include <copal/allocator.h>

typedef struct cl_application cl_application_t;

/* RGBA8, straight alpha, row-major, owned copy. Renderers read the pixels
 * directly (in-tree) or through cl_image_pixels() (custom backends). */
struct cl_image {
    cl_application_t *app; /* backref: cl_image_release evicts the renderer */
    const cl_allocator_t *a;
    int w, h;
    unsigned char *rgba; /* w * h * 4 bytes */
};

#endif /* CL_IMAGE_INTERNAL_H */
