/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WINDOW_H
#define CL_WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/types.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_window cl_window_t;
typedef struct cl_application cl_application_t;
typedef struct cl_widget cl_widget_t;

/* Return false to veto a close request. */
typedef bool (*cl_window_close_fn)(cl_window_t *win, void *user);

typedef struct cl_window_desc {
    uint32_t abi_version;
    size_t struct_size;
    const char *title; /* UTF-8; may be NULL */
    int32_t width, height;
    int32_t min_width, min_height;
    bool resizable;
} cl_window_desc_t;

#define CL_WINDOW_DESC_INIT \
    { .abi_version = CL_VERSION, .struct_size = sizeof(cl_window_desc_t) }

/* MVP allows a single window; a second returns NULL + CL_ERROR_UNSUPPORTED. */
CL_API cl_window_t *cl_window_create(cl_application_t *app,
                                     const cl_window_desc_t *desc);
CL_API void cl_window_destroy(cl_window_t *win);

CL_API void cl_window_show(cl_window_t *win);
CL_API void cl_window_set_content(cl_window_t *win, cl_widget_t *root);
CL_API cl_widget_t *cl_window_content(cl_window_t *win);
CL_API void cl_window_set_title(cl_window_t *win, const char *utf8);
CL_API cl_size_t cl_window_size(cl_window_t *win);
CL_API void cl_window_set_on_close(cl_window_t *win, cl_window_close_fn fn,
                                   void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_WINDOW_H */
