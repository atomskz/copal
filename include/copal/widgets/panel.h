/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_PANEL_H
#define CL_WIDGETS_PANEL_H

#include <stddef.h>

#include <copal/widget.h>
#include <copal/types.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_panel_desc {
    uint32_t abi_version;
    size_t struct_size;
    cl_insets_t padding;
    bool bordered; /* stroke a border around the surface */
} cl_panel_desc_t;

#define CL_PANEL_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_panel_desc_t)
#define CL_PANEL_DESC_INIT { CL_PANEL_DESC_INIT_FIELDS }

/**
 * cl_panel_create() - a framed grouping surface.
 *
 * Paints a rounded themed surface (optionally with a border) and arranges
 * every child to fill its padded content box - put a vbox/hbox inside for
 * real layout. Useful for visually grouping controls.
 */
CL_API cl_widget_t *cl_panel_create(cl_application_t *app,
                                    const cl_panel_desc_t *desc);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_PANEL_H */
