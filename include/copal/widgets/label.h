/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_LABEL_H
#define CL_WIDGETS_LABEL_H

#include <stddef.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/version.h>
#include <copal/widget.h>
#include <copal/theme.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;

/*
 * A single-line text label. It measures to its natural text width; there is no
 * wrapping or ellipsis, so a label given less width than that by its parent is
 * clipped to its own rect (the text is cut off rather than drawn over its
 * neighbours). Size it to its measured width, or place it in a wider/scrolling
 * container, when the whole string must be visible.
 */
typedef struct cl_label_desc {
    uint32_t abi_version;
    size_t struct_size;
    const char *text;                /* UTF-8; may be NULL */
    const cl_text_style_t *style;    /* NULL -> theme defaults */
} cl_label_desc_t;

#define CL_LABEL_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_label_desc_t)

CL_API cl_widget_t *cl_label_create(cl_application_t *app,
                                    const cl_label_desc_t *desc);
CL_API void cl_label_set_text(cl_widget_t *label, const char *utf8);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_LABEL_H */
