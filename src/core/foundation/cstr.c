/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Freestanding string helpers (see foundation_internal.h). Trivial, but keeping
 * them copal-namespaced means the core references no libc str* symbol; a
 * freestanding target only has to provide mem* (which the compiler emits and
 * every toolchain supplies).
 */
#include "core/foundation/foundation_internal.h"

size_t cl_strlen(const char *s)
{
    const char *p = s;

    while (*p)
        p++;
    return (size_t)(p - s);
}

int cl_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
