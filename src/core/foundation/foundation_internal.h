/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_FOUNDATION_INTERNAL_H
#define CL_FOUNDATION_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <copal/error.h>

/* Internal (not exported): record the calling thread's last error code. */
void cl_set_last_error(cl_result_t result);

/*
 * Decode one UTF-8 codepoint from @s into @cp. Returns the number of bytes
 * consumed, or 0 at the terminating NUL. Invalid sequences yield U+FFFD and
 * consume one byte.
 */
size_t cl_utf8_next(const char *s, uint32_t *cp);

#endif /* CL_FOUNDATION_INTERNAL_H */
