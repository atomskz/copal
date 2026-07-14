/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_FOUNDATION_INTERNAL_H
#define CL_FOUNDATION_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <copal/allocator.h>
#include <copal/error.h>

/* Internal (not exported): record the calling thread's last error code. */
void cl_set_last_error(cl_result_t result);

/* Smallest valid desc/ops: just the {abi_version, struct_size} handshake. */
typedef struct {
    uint32_t abi_version;
    size_t struct_size;
} cl_desc_header_t;
#define CL_DESC_MIN_SIZE (sizeof(cl_desc_header_t))

/*
 * ABI handshake for versioned desc/ops structs (ADR-005). A caller is
 * compatible when it stamps the same MAJOR version and declares a struct of at
 * least @min_size bytes. Within a major the library only appends fields at the
 * tail, so a shorter (older) or longer (newer) struct still interoperates:
 * data descs default the missing tail (see cl_desc_fill, @min_size ==
 * CL_DESC_MIN_SIZE), ops tables must carry the whole baseline (@min_size ==
 * sizeof the ops table) since a missing op cannot be called. Records
 * CL_ERROR_ABI_MISMATCH and returns false on a mismatch.
 */
bool cl_abi_ok(uint32_t abi_version, size_t struct_size, size_t min_size);

/*
 * Normalise a caller desc into a zeroed, full-size local: copy the first
 * min(@src_size, @dst_size) bytes, leaving a tail the library knows but the
 * caller omitted at zero (its default) and ignoring a tail the caller added
 * but this library does not know.
 */
void cl_desc_fill(void *dst, size_t dst_size, const void *src, size_t src_size);

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
