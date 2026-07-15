/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_MUTEX_INTERNAL_H
#define CL_MUTEX_INTERNAL_H

/*
 * The application's cross-thread task queue (cl_application_post) is guarded by
 * an injectable mutex (cl_mutex_iface_t, copal/application.h). The hosted build
 * ships a default here (pthreads on POSIX, a critical section on Windows);
 * a freestanding build has no default and the embedder must inject one.
 */
#include <copal/allocator.h>
#include <copal/application.h> /* cl_mutex_iface_t */

#ifdef CL_HOSTED
/* Built-in mutex, opaque and heap-allocated so no platform headers leak. */
typedef struct cl_mutex cl_mutex_t;

/*
 * Create a mutex allocated with @a (NULL means the default allocator). Returns
 * NULL on failure and sets the last error: CL_ERROR_OUT_OF_MEMORY from the
 * allocation, CL_ERROR_PLATFORM if the OS refuses to initialise.
 */
cl_mutex_t *cl_mutex_create(const cl_allocator_t *a);
/* Free the mutex (must be unlocked). NULL is ignored. */
void cl_mutex_destroy(cl_mutex_t *m);
void cl_mutex_lock(cl_mutex_t *m);
void cl_mutex_unlock(cl_mutex_t *m);
#endif /* CL_HOSTED */

/*
 * Fill @out with the built-in mutex iface. On the hosted build it wraps the
 * functions above (the caller sets out->user to its allocator); on a
 * freestanding build it is zeroed - there is no default mutex.
 */
void cl_mutex_builtin_iface(cl_mutex_iface_t *out);

#endif /* CL_MUTEX_INTERNAL_H */
