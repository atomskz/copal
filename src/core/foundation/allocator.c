/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/allocator.h>

#ifdef CL_HOSTED
#include <stdlib.h> /* the default allocator wraps the hosted malloc/free */
#endif
#include <string.h> /* memcpy (cl_strdup): a residual mem* the target provides */

#include "foundation_internal.h"

#ifdef CL_HOSTED
static void *default_alloc(void *userdata, size_t size)
{
    (void)userdata;
    return malloc(size);
}

static void *default_realloc(void *userdata, void *ptr, size_t size)
{
    (void)userdata;
    return realloc(ptr, size);
}

static void default_free(void *userdata, void *ptr)
{
    (void)userdata;
    free(ptr);
}

static const cl_allocator_t g_default_allocator = {
    .userdata = NULL,
    .alloc = default_alloc,
    .realloc = default_realloc,
    .free = default_free,
};

const cl_allocator_t *cl_allocator_default(void)
{
    return &g_default_allocator;
}
#else
/*
 * Freestanding: there is no hosted C runtime, so there is no default allocator.
 * The embedder must inject one via cl_application_desc_t.allocator; passing a
 * NULL allocator anywhere then fails closed (below) instead of calling malloc.
 */
const cl_allocator_t *cl_allocator_default(void)
{
    return NULL;
}
#endif /* CL_HOSTED */

void *cl_alloc(const cl_allocator_t *a, size_t size)
{
    void *p;

    if (!a)
        a = cl_allocator_default();
    if (!a) {
        cl_set_last_error(CL_ERROR_OUT_OF_MEMORY);
        return NULL; /* freestanding with no injected allocator */
    }
    /* malloc(0)/realloc(p, 0) are implementation-defined (and the latter is
     * UB in C23); normalising to 1 keeps "NULL means out of memory" true and
     * spares user allocators from having to handle a zero size. */
    if (size == 0)
        size = 1;

    p = a->alloc(a->userdata, size);
    if (!p)
        cl_set_last_error(CL_ERROR_OUT_OF_MEMORY);

    return p;
}

void *cl_realloc(const cl_allocator_t *a, void *ptr, size_t size)
{
    void *p;

    if (!a)
        a = cl_allocator_default();
    if (!a) {
        cl_set_last_error(CL_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
    if (size == 0)
        size = 1;

    p = a->realloc(a->userdata, ptr, size);
    if (!p)
        cl_set_last_error(CL_ERROR_OUT_OF_MEMORY);

    return p;
}

void cl_free(const cl_allocator_t *a, void *ptr)
{
    if (!a)
        a = cl_allocator_default();
    if (a)
        a->free(a->userdata, ptr);
}

char *cl_strdup(const cl_allocator_t *a, const char *s)
{
    size_t n;
    char *p;

    if (!s)
        return NULL;
    n = cl_strlen(s) + 1;
    p = cl_alloc(a, n);
    if (p)
        memcpy(p, s, n);
    return p;
}
