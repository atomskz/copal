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

/*
 * Truncation contract: cl_vsnprintf always NUL-terminates within @cap, never
 * writes at or past @cap, and returns the untruncated length (like libc). The
 * output buffer sits inside a larger '#'-filled guard so an over-write past
 * @cap is caught.
 */
static void chk_trunc(size_t cap, const char *fmt, ...)
{
    char guard[64];
    char ref[64];
    size_t rc;
    int rref;
    va_list ap;

    memset(guard, '#', sizeof guard);
    va_start(ap, fmt);
    rc = cl_vsnprintf(guard, cap, fmt, ap);
    va_end(ap);
    va_start(ap, fmt);
    rref = vsnprintf(ref, sizeof ref, fmt, ap);
    va_end(ap);

    if (rc != (size_t)rref) {
        fprintf(stderr, "FAIL trunc fmt=\"%s\" cap=%zu: rc=%zu vs libc=%d\n",
                fmt, cap, rc, rref);
        failures++;
    }
    if (guard[cap] != '#') { /* wrote at or past @cap */
        fprintf(stderr, "FAIL trunc fmt=\"%s\" cap=%zu: wrote past size\n", fmt,
                cap);
        failures++;
    }
    if (cap > 0) {
        if (guard[cap - 1] != '\0') {
            fprintf(stderr, "FAIL trunc fmt=\"%s\" cap=%zu: not terminated\n",
                    fmt, cap);
            failures++;
        }
        if (strncmp(guard, ref, cap - 1) != 0) {
            fprintf(stderr, "FAIL trunc content fmt=\"%s\" cap=%zu: \"%s\"\n",
                    fmt, cap, guard);
            failures++;
        }
    } else if (guard[0] != '#') { /* cap==0 must write nothing */
        fprintf(stderr, "FAIL trunc fmt=\"%s\" cap=0: wrote into empty buf\n",
                fmt);
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

    /* truncation and boundary behaviour */
    chk_trunc(0, "hello");           /* size 0: writes nothing */
    chk_trunc(1, "hello");           /* only the terminator fits */
    chk_trunc(3, "hello");           /* "he" + NUL */
    chk_trunc(5, "hello");           /* one short of the full output */
    chk_trunc(6, "hello");           /* exact fit */
    chk_trunc(4, "num=%d", 42);      /* truncated mid-string */
    chk_trunc(3, "%d", 123456);      /* truncated number */
    chk_trunc(2, "%s%s", "ab", "cd");

    if (!failures)
        printf("cl_vsnprintf matches libc for supported specifiers\n");
    return failures ? 1 : 0;
}
