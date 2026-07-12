/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_ALLOCATOR_H
#define CL_ALLOCATOR_H

#include <stddef.h>

#include <copal/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * User-supplied allocator. Passed to the application at creation; a NULL
 * allocator anywhere in the API selects the built-in malloc-based default.
 *
 * All three function pointers are required: the library calls each of them
 * without checking for NULL. The callbacks are never invoked with size 0
 * (the wrappers below normalise it to 1), so returning NULL always means
 * out of memory. realloc(userdata, NULL, size) must behave like alloc, and
 * free(userdata, NULL) must be a no-op - the malloc-family semantics.
 */
typedef struct cl_allocator {
    void *userdata;
    void *(*alloc)(void *userdata, size_t size);
    void *(*realloc)(void *userdata, void *ptr, size_t size);
    void  (*free)(void *userdata, void *ptr);
} cl_allocator_t;

/** cl_allocator_default() - the built-in malloc/realloc/free allocator. */
CL_API const cl_allocator_t *cl_allocator_default(void);

/*
 * Thin wrappers over an allocator. A NULL allocator uses the default; a zero
 * size is normalised to 1 (so NULL unambiguously means failure). On
 * allocation failure the wrappers record CL_ERROR_OUT_OF_MEMORY (cl_last_error).
 */
CL_API void *cl_alloc(const cl_allocator_t *a, size_t size);
CL_API void *cl_realloc(const cl_allocator_t *a, void *ptr, size_t size);
CL_API void  cl_free(const cl_allocator_t *a, void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* CL_ALLOCATOR_H */
