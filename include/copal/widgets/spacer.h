/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_SPACER_H
#define CL_WIDGETS_SPACER_H

#include <stddef.h>

#include <copal/widget.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_spacer_desc {
    uint32_t abi_version;
    size_t struct_size;
    float width;  /* fixed size; 0 = none */
    float height;
    float flex;   /* > 0: grab that share of the leftover box space */
} cl_spacer_desc_t;

#define CL_SPACER_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_spacer_desc_t)
#define CL_SPACER_DESC_INIT { CL_SPACER_DESC_INIT_FIELDS }

/**
 * cl_spacer_create() - empty space inside a box.
 *
 * A fixed gap ({.width = 8}) or a flexible one ({.flex = 1} pushes the
 * following siblings to the far end of the box).
 */
CL_API cl_widget_t *cl_spacer_create(cl_application_t *app,
                                     const cl_spacer_desc_t *desc);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_SPACER_H */
