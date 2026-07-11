/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_APPLICATION_H
#define CL_APPLICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/types.h>
#include <copal/error.h>
#include <copal/allocator.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;
typedef struct cl_platform cl_platform_t;
typedef struct cl_renderer cl_renderer_t;
typedef struct cl_theme cl_theme_t;

typedef void (*cl_task_fn)(void *user);

/*
 * Application descriptor. The platform/renderer backends may be injected
 * (dependency injection, ARCHITECTURE §3.9). If NULL, the built-in SDL2 +
 * OpenGL backends are used when compiled in (COPAL_ENABLE_SDL/OPENGL);
 * otherwise cl_application_create() fails with CL_ERROR_UNSUPPORTED.
 */
typedef struct cl_application_desc {
    uint32_t abi_version;
    size_t struct_size;
    const cl_allocator_t *allocator; /* NULL -> built-in malloc */
    cl_log_fn log_fn;
    void *log_user;
    cl_platform_t *platform; /* injected backend; ownership transfers to app */
    cl_renderer_t *renderer; /* injected backend; ownership transfers to app */
} cl_application_desc_t;

#define CL_APPLICATION_DESC_INIT \
    { .abi_version = CL_VERSION, .struct_size = sizeof(cl_application_desc_t) }

CL_API cl_application_t *cl_application_create(const cl_application_desc_t *desc);
CL_API void cl_application_destroy(cl_application_t *app);

CL_API int cl_application_run(cl_application_t *app);
CL_API bool cl_application_step(cl_application_t *app, bool wait);
CL_API void cl_application_quit(cl_application_t *app, int exit_code);

/*
 * cl_application_post() - queue fn(user) to run on the loop (UI) thread.
 *
 * Thread-safe: may be called from any thread. The task runs once, in FIFO
 * order, from inside cl_application_run()/cl_application_step() (a blocked run
 * loop is woken so it drains promptly). A task may itself post more work. Tasks
 * still queued when the application is destroyed are dropped without running.
 * Requires the application's allocator to be thread-safe (the default is).
 * Returns CL_OK, or CL_ERROR_INVALID_ARGUMENT / CL_ERROR_OUT_OF_MEMORY.
 */
CL_API cl_result_t cl_application_post(cl_application_t *app, cl_task_fn fn,
                                       void *user);

CL_API cl_theme_t *cl_application_theme(cl_application_t *app);
CL_API const cl_allocator_t *cl_application_allocator(cl_application_t *app);

#ifdef __cplusplus
}
#endif

#endif /* CL_APPLICATION_H */
