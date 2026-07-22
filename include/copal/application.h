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
 * Built-in rendering backend to use when platform/renderer are not injected.
 * The software backend uses a CPU rasterizer and does NOT create an OpenGL
 * context, so it starts faster and uses far less memory at the cost of GPU
 * acceleration (see ARCHITECTURE).
 */
typedef enum cl_render_backend {
    CL_RENDER_AUTO = 0, /* OpenGL if compiled in AND the GL window comes up,
                         * otherwise software (a failed GL window falls back
                         * at run time); COPAL_RENDER=software forces the
                         * software path (e.g. over RDP or in CI) */
    CL_RENDER_GL,       /* OpenGL renderer */
    CL_RENDER_SOFTWARE  /* CPU rasterizer, no GPU context */
} cl_render_backend_t;

/*
 * Application descriptor. The platform/renderer backends may be injected
 * (dependency injection, ARCHITECTURE §3.9). If NULL, a built-in SDL2 backend
 * is used when compiled in (COPAL_ENABLE_SDL/OPENGL); otherwise
 * cl_application_create() fails with CL_ERROR_UNSUPPORTED.
 *
 * Ownership of injected backends transfers to the application only when
 * cl_application_create() succeeds (they are then destroyed by
 * cl_application_destroy). When it returns NULL, injected backends are NOT
 * destroyed - they stay with the caller, who may retry or free them.
 */
/*
 * Injectable mutex for the cross-thread task queue (cl_application_post).
 * Optional on the hosted build - a pthread / critical-section default is used
 * when the desc leaves it NULL. On a freestanding build there is no default:
 * an embedder that wants cl_application_post must inject one (on UEFI this is
 * RaiseTPL/RestoreTPL, since a TPL notify callback can post into the loop, so
 * the queue still needs mutual exclusion). create() returns an opaque handle;
 * lock()/unlock() bracket a tiny critical section and must not allocate.
 */
typedef struct cl_mutex_iface {
    void *(*create)(void *user);
    void (*destroy)(void *user, void *handle);
    void (*lock)(void *user, void *handle);
    void (*unlock)(void *user, void *handle);
    void *user;
} cl_mutex_iface_t;

typedef struct cl_application_desc {
    uint32_t abi_version;
    size_t struct_size;
    const cl_allocator_t *allocator; /* NULL -> built-in malloc */
    cl_platform_t *platform; /* injected backend; see ownership note above */
    cl_renderer_t *renderer; /* injected backend; see ownership note above */
    cl_render_backend_t render_backend; /* built-in backend choice (0 = AUTO) */
    const cl_mutex_iface_t *mutex; /* NULL -> hosted default; see above */
} cl_application_desc_t;

/* Service fields for a designated initializer / compound literal (the same
 * idiom as the widget descs)... */
#define CL_APPLICATION_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_application_desc_t)
/* ...and the full default initializer built from it. */
#define CL_APPLICATION_DESC_INIT { CL_APPLICATION_DESC_INIT_FIELDS }

CL_API cl_application_t *cl_application_create(const cl_application_desc_t *desc);
CL_API void cl_application_destroy(cl_application_t *app);

/**
 * cl_application_run() - run the event loop until cl_application_quit() (or
 * an unvetoed window close request).
 *
 * Blocks between events (waking for due timers and posted tasks).
 *
 * Return: the exit code passed to cl_application_quit() (0 by default).
 */
CL_API int cl_application_run(cl_application_t *app);

/**
 * cl_application_step() - run one iteration of the event loop (for embedding
 * copal into an external loop).
 *
 * Processes pending events, posted tasks and due timers, then renders when
 * dirty. With @wait true the step first waits for activity, bounded by the
 * next timer deadline (or a ~100 ms slice when no timer is armed), so it
 * always returns to the caller and an idle while (step(app, true)) loop
 * does not spin a core.
 *
 * Return: true to keep stepping; false once quit was requested.
 */
CL_API bool cl_application_step(cl_application_t *app, bool wait);

/**
 * cl_application_quit() - request loop exit with @exit_code.
 *
 * Thread-safe rendezvous with a blocked loop: the platform is woken. The
 * loop finishes the current iteration and cl_application_run() returns
 * @exit_code.
 */
CL_API void cl_application_quit(cl_application_t *app, int exit_code);

/*
 * cl_application_post() - queue fn(user) to run on the loop (UI) thread.
 *
 * Thread-safe: may be called from any thread. The task runs once, in FIFO
 * order, from inside cl_application_run()/cl_application_step() (a blocked run
 * loop is woken so it drains promptly). A task may itself post more work. Tasks
 * still queued when the application is destroyed are dropped without running.
 * Requires the application's allocator to be thread-safe (the default is).
 *
 * Waking a blocked run loop needs the platform's wakeup op. The built-in
 * backends provide it; an injected platform that omits wakeup can still queue
 * tasks, but they are only drained on the next loop iteration that is already
 * awake (a step(wait=false) tick or the next event/timer). Supply a wakeup op
 * if you post from another thread while the loop blocks in run().
 *
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
