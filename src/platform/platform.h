/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_PLATFORM_INTERNAL_H
#define CL_PLATFORM_INTERNAL_H

/* Platform abstraction (single native window in MVP, ARCHITECTURE §3.2). */
#include <stdbool.h>

#include <copal/types.h>
#include <copal/error.h>
#include <copal/event.h>
#include <copal/window.h>

typedef struct cl_platform cl_platform_t;

typedef enum cl_platform_event_kind {
    CL_PEV_NONE,
    CL_PEV_QUIT,
    CL_PEV_RESIZE,
    CL_PEV_MOUSE_DOWN,
    CL_PEV_MOUSE_UP,
    CL_PEV_MOUSE_MOVE
} cl_platform_event_kind_t;

typedef struct cl_platform_event {
    cl_platform_event_kind_t kind;
    cl_size_t size;           /* CL_PEV_RESIZE (logical px) */
    cl_point_t pos;           /* mouse events (logical px) */
    cl_mouse_button_t button; /* mouse button events */
} cl_platform_event_t;

typedef struct cl_platform_ops {
    cl_result_t (*create_window)(cl_platform_t *p, const cl_window_desc_t *desc);
    void (*set_title)(cl_platform_t *p, const char *utf8);
    cl_size_t (*drawable_size)(cl_platform_t *p);
    float (*scale)(cl_platform_t *p);
    bool (*poll)(cl_platform_t *p, cl_platform_event_t *out);
    void (*wait)(cl_platform_t *p, int timeout_ms);
    void (*present)(cl_platform_t *p);
    void (*wakeup)(cl_platform_t *p);
    void (*destroy)(cl_platform_t *p);
    /* GL proc loader for the OpenGL renderer; NULL for non-GL backends. */
    void *(*gl_get_proc)(cl_platform_t *p, const char *name);
} cl_platform_ops_t;

/* Concrete backends embed this as their first member. */
struct cl_platform {
    const cl_platform_ops_t *ops;
};

#endif /* CL_PLATFORM_INTERNAL_H */
