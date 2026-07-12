/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_FOUNDATION_INTERNAL_H
#define CL_FOUNDATION_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <copal/allocator.h>
#include <copal/error.h>

/* Internal (not exported): record the calling thread's last error code. */
void cl_set_last_error(cl_result_t result);

/*
 * Duplicate a NUL-terminated string with @a (NULL selects the default
 * allocator). Returns NULL for a NULL input or on allocation failure (the
 * latter records CL_ERROR_OUT_OF_MEMORY via cl_alloc).
 */
char *cl_strdup(const cl_allocator_t *a, const char *s);

/*
 * Diagnostics channel: formats the message and hands it to the process-wide
 * cl_set_log_callback() sink; without one, WARN and ERROR go to stderr.
 */
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void cl_log(cl_log_level_t level, const char *fmt, ...);

/*
 * Decode one UTF-8 codepoint from @s into @cp. Returns the number of bytes
 * consumed, or 0 at the terminating NUL. Invalid sequences yield U+FFFD and
 * consume one byte. @s must be NUL-terminated.
 */
size_t cl_utf8_next(const char *s, uint32_t *cp);

/*
 * Bounded variant: never reads past s[avail-1]. A multibyte sequence whose
 * continuation bytes would fall outside @avail is treated as invalid (U+FFFD,
 * one byte consumed). Returns 0 when @avail is 0 or at a NUL. Use for buffers
 * that are not NUL-terminated.
 */
size_t cl_utf8_next_n(const char *s, size_t avail, uint32_t *cp);

#endif /* CL_FOUNDATION_INTERNAL_H */
