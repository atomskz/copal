/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_CHECKBOX_H
#define CL_WIDGETS_CHECKBOX_H

#include <stdbool.h>
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

/*
 * Two-state checkbox with an optional inline label. Toggles on a left click or
 * the Space key while focused.
 */
typedef struct cl_checkbox_desc {
    uint32_t abi_version;
    size_t struct_size;
    const char *text; /* label (UTF-8); may be NULL */
    bool checked;     /* initial state */
} cl_checkbox_desc_t;

#define CL_CHECKBOX_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_checkbox_desc_t)
#define CL_CHECKBOX_DESC_INIT { CL_CHECKBOX_DESC_INIT_FIELDS }

CL_API cl_widget_t *cl_checkbox_create(cl_application_t *app,
                                       const cl_checkbox_desc_t *desc);

/** cl_checkbox_set_checked() - set state. Does NOT fire on_toggle. */
CL_API void cl_checkbox_set_checked(cl_widget_t *cb, bool checked);

/** cl_checkbox_is_checked() - current state. */
CL_API bool cl_checkbox_is_checked(cl_widget_t *cb);

/** cl_checkbox_set_text() - set the label. */
CL_API void cl_checkbox_set_text(cl_widget_t *cb, const char *utf8);

/** cl_checkbox_set_on_toggle() - called only on user toggles (not set_checked). */
CL_API void cl_checkbox_set_on_toggle(cl_widget_t *cb, cl_toggled_fn fn,
                                      void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_CHECKBOX_H */
