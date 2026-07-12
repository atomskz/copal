/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/allocator.h>

#include <stdlib.h>

#include "foundation_internal.h"

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

void *cl_alloc(const cl_allocator_t *a, size_t size)
{
    void *p;

    if (!a)
        a = &g_default_allocator;
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
        a = &g_default_allocator;
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
        a = &g_default_allocator;

    a->free(a->userdata, ptr);
}
