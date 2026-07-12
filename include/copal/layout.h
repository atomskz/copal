/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_LAYOUT_H
#define CL_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/types.h>
#include <copal/version.h>
#include <copal/widget.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Vertical box: stacks children top-to-bottom with spacing and padding. */
typedef struct cl_vbox_desc {
    uint32_t abi_version;
    size_t struct_size;
    float spacing;
    cl_insets_t padding;
    cl_align_t align_cross; /* cross-axis (horizontal) alignment of children */
} cl_vbox_desc_t;

#define CL_VBOX_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_vbox_desc_t)

CL_API cl_widget_t *cl_vbox_create(cl_application_t *app,
                                   const cl_vbox_desc_t *desc);

/* Horizontal box: lays children out left-to-right with spacing and padding. */
typedef struct cl_hbox_desc {
    uint32_t abi_version;
    size_t struct_size;
    float spacing;
    cl_insets_t padding;
    cl_align_t align_cross; /* cross-axis (vertical) alignment of children */
} cl_hbox_desc_t;

#define CL_HBOX_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_hbox_desc_t)

CL_API cl_widget_t *cl_hbox_create(cl_application_t *app,
                                   const cl_hbox_desc_t *desc);

#ifdef __cplusplus
}
#endif

#endif /* CL_LAYOUT_H */
