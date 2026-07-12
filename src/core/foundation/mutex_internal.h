/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_MUTEX_INTERNAL_H
#define CL_MUTEX_INTERNAL_H

/*
 * Minimal cross-platform mutex (pthreads on POSIX, a critical section on
 * Windows). Opaque and heap-allocated so no platform headers leak into the
 * rest of the tree. Used to guard the cross-thread task queue.
 */
#include <copal/allocator.h>

typedef struct cl_mutex cl_mutex_t;

/*
 * Create a mutex allocated with @a (NULL means the default allocator).
 * Returns NULL on failure and sets the last error: CL_ERROR_OUT_OF_MEMORY
 * from the allocation, CL_ERROR_PLATFORM if the OS refuses to initialise.
 */
cl_mutex_t *cl_mutex_create(const cl_allocator_t *a);
/* Free the mutex (must be unlocked). NULL is ignored. */
void cl_mutex_destroy(cl_mutex_t *m);
void cl_mutex_lock(cl_mutex_t *m);
void cl_mutex_unlock(cl_mutex_t *m);

#endif /* CL_MUTEX_INTERNAL_H */
