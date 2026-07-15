/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "core/foundation/mutex_internal.h"

#include <copal/allocator.h>

#include "core/foundation/foundation_internal.h"

#ifdef CL_HOSTED

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

#endif /* _WIN32 / POSIX */

/* Built-in iface: wrap the functions above. `user` carries the allocator. */
static void *builtin_create(void *user)
{
    return cl_mutex_create((const cl_allocator_t *)user);
}
static void builtin_destroy(void *user, void *h)
{
    (void)user;
    cl_mutex_destroy((cl_mutex_t *)h);
}
static void builtin_lock(void *user, void *h)
{
    (void)user;
    cl_mutex_lock((cl_mutex_t *)h);
}
static void builtin_unlock(void *user, void *h)
{
    (void)user;
    cl_mutex_unlock((cl_mutex_t *)h);
}

void cl_mutex_builtin_iface(cl_mutex_iface_t *out)
{
    out->create = builtin_create;
    out->destroy = builtin_destroy;
    out->lock = builtin_lock;
    out->unlock = builtin_unlock;
    out->user = NULL; /* the application sets this to its allocator */
}

#else /* !CL_HOSTED: no built-in mutex; the embedder injects one */

void cl_mutex_builtin_iface(cl_mutex_iface_t *out)
{
    out->create = NULL;
    out->destroy = NULL;
    out->lock = NULL;
    out->unlock = NULL;
    out->user = NULL;
}

#endif /* CL_HOSTED */
