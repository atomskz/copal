/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "core/foundation/mutex_internal.h"

#include <copal/allocator.h>

#include "core/foundation/foundation_internal.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

struct cl_mutex {
    cl_allocator_t a; /* value copy so &a is stable for cl_free */
    CRITICAL_SECTION cs;
};

cl_mutex_t *cl_mutex_create(const cl_allocator_t *a)
{
    cl_mutex_t *m;

    if (!a)
        a = cl_allocator_default();
    m = cl_alloc(a, sizeof(*m));
    if (!m)
        return NULL;
    m->a = *a;
    InitializeCriticalSection(&m->cs);
    return m;
}

void cl_mutex_destroy(cl_mutex_t *m)
{
    cl_allocator_t a;

    if (!m)
        return;
    a = m->a;
    DeleteCriticalSection(&m->cs);
    cl_free(&a, m);
}

void cl_mutex_lock(cl_mutex_t *m)
{
    EnterCriticalSection(&m->cs);
}

void cl_mutex_unlock(cl_mutex_t *m)
{
    LeaveCriticalSection(&m->cs);
}

#else /* POSIX */
#  include <pthread.h>

struct cl_mutex {
    cl_allocator_t a;
    pthread_mutex_t m;
};

cl_mutex_t *cl_mutex_create(const cl_allocator_t *a)
{
    cl_mutex_t *m;

    if (!a)
        a = cl_allocator_default();
    m = cl_alloc(a, sizeof(*m));
    if (!m)
        return NULL;
    m->a = *a;
    if (pthread_mutex_init(&m->m, NULL) != 0) {
        cl_free(a, m);
        cl_set_last_error(CL_ERROR_PLATFORM);
        return NULL;
    }
    return m;
}

void cl_mutex_destroy(cl_mutex_t *m)
{
    cl_allocator_t a;

    if (!m)
        return;
    a = m->a;
    pthread_mutex_destroy(&m->m);
    cl_free(&a, m);
}

void cl_mutex_lock(cl_mutex_t *m)
{
    pthread_mutex_lock(&m->m);
}

void cl_mutex_unlock(cl_mutex_t *m)
{
    pthread_mutex_unlock(&m->m);
}

#endif
