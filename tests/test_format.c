/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Checks the freestanding cl_vsnprintf (src/core/foundation/format.c) against
 * libc snprintf for the specifiers copal supports. Runs hosted (needs libc for
 * the reference).
 */
#include "core/foundation/foundation_internal.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void chk(const char *fmt, ...)
{
    char a[256], b[256];
    va_list ap;

    va_start(ap, fmt);
    cl_vsnprintf(a, sizeof a, fmt, ap);
    va_end(ap);
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strcmp(a, b) != 0) {
        fprintf(stderr, "FAIL fmt=\"%s\": cl=\"%s\" vs libc=\"%s\"\n", fmt, a, b);
        failures++;
    }
}

int main(void)
{
    chk("plain text, no conversions");
    chk("%s", "hello");
    chk("%s and %s", "a", "b");
    chk("num=%d", 42);
    chk("neg=%d", -7);
    chk("%d %d %d", 0, INT_MAX, INT_MIN);
    chk("%u", 4000000000u);
    chk("hex %x upper %X", 0xdeadbeefu, 0xABCu);
    chk("oct %o and %o", 0u, 511u);
    chk("%lo", 0777777UL);
    chk("%c%c", 'O', 'K');
    chk("100%% done");
    chk("null=%s", (char *)NULL); /* both print "(null)" */
    chk("%ld %lld", 123456789L, 1234567890123LL);
    chk("%zu items", (size_t)123456);

    if (!failures)
        printf("cl_vsnprintf matches libc for supported specifiers\n");
    return failures ? 1 : 0;
}
