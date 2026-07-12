/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_PROGRESSBAR_H
#define CL_WIDGETS_PROGRESSBAR_H

#include <stddef.h>

#include <copal/widget.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_progressbar_desc {
    uint32_t abi_version;
    size_t struct_size;
    float value; /* 0..1, clamped */
} cl_progressbar_desc_t;

#define CL_PROGRESSBAR_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_progressbar_desc_t)
#define CL_PROGRESSBAR_DESC_INIT { CL_PROGRESSBAR_DESC_INIT_FIELDS }

/** cl_progressbar_create() - a horizontal determinate progress bar. */
CL_API cl_widget_t *cl_progressbar_create(cl_application_t *app,
                                          const cl_progressbar_desc_t *desc);

/** cl_progressbar_set_value() - set progress (0..1, clamped) and repaint. */
CL_API void cl_progressbar_set_value(cl_widget_t *w, float value);

/** cl_progressbar_value() - current progress (0..1). */
CL_API float cl_progressbar_value(cl_widget_t *w);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_PROGRESSBAR_H */
