/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_BUTTON_H
#define CL_WIDGETS_BUTTON_H

#include <stddef.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/version.h>
#include <copal/widget.h>
#include <copal/event.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;

typedef struct cl_button_desc {
    uint32_t abi_version;
    size_t struct_size;
    const char *text; /* UTF-8; may be NULL */
} cl_button_desc_t;

#define CL_BUTTON_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_button_desc_t)
#define CL_BUTTON_DESC_INIT { CL_BUTTON_DESC_INIT_FIELDS }

CL_API cl_widget_t *cl_button_create(cl_application_t *app,
                                     const cl_button_desc_t *desc);
CL_API void cl_button_set_text(cl_widget_t *button, const char *utf8);
CL_API void cl_button_set_on_click(cl_widget_t *button, cl_action_fn fn,
                                   void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_BUTTON_H */
