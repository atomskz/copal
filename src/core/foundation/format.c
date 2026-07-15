/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Minimal vsnprintf for the freestanding core: the log path formats its message
 * without libc. Supports %s %d/%i %u %o %x/%X %p %c %% with the l/ll/z length
 * modifiers; flags/width/precision are parsed and ignored (copal's log strings
 * use none). copal formats no floating point, but a float conversion
 * (%f/%F/%e/%E/%g/%G/%a/%A) still consumes its double argument and echoes the
 * specifier verbatim, so any later conversions stay aligned. Any other unknown
 * conversion is echoed verbatim and consumes no argument (callers must not pass
 * one). Always NUL-terminates and never writes past @size; returns the length
 * the output would have had (like C vsnprintf).
 */
#include "core/foundation/foundation_internal.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static void put(char *buf, size_t size, size_t *n, char c)
{
    if (*n + 1 < size)
        buf[*n] = c;
    (*n)++;
}

static void put_str(char *buf, size_t size, size_t *n, const char *s)
{
    if (!s)
        s = "(null)";
    while (*s)
        put(buf, size, n, *s++);
}

static void put_uint(char *buf, size_t size, size_t *n, unsigned long long v,
                     unsigned base, int upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[24];
    int i = 0;

    if (v == 0)
        tmp[i++] = '0';
    while (v) {
        tmp[i++] = digits[v % base];
        v /= base;
    }
    while (i)
        put(buf, size, n, tmp[--i]);
}

static void put_int(char *buf, size_t size, size_t *n, long long v)
{
    unsigned long long u;

    if (v < 0) {
        put(buf, size, n, '-');
        u = (unsigned long long)(-(v + 1)) + 1ull; /* avoid LLONG_MIN overflow */
    } else {
        u = (unsigned long long)v;
    }
    put_uint(buf, size, n, u, 10, 0);
}

size_t cl_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    size_t n = 0;

    while (*fmt) {
        int lng; /* 0 int, 1 long, 2 long long, 3 size_t */
        char c;

        if (*fmt != '%') {
            put(buf, size, &n, *fmt++);
            continue;
        }
        fmt++;
        /* flags / width / precision: parsed, not applied */
        while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#' ||
               *fmt == '0')
            fmt++;
        while (*fmt >= '0' && *fmt <= '9')
            fmt++;
        if (*fmt == '.') {
            fmt++;
            while (*fmt >= '0' && *fmt <= '9')
                fmt++;
        }
        lng = 0;
        if (*fmt == 'z') {
            lng = 3;
            fmt++;
        } else if (*fmt == 'l') {
            lng = 1;
            fmt++;
            if (*fmt == 'l') {
                lng = 2;
                fmt++;
            }
        } else if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h')
                fmt++;
        }
        c = *fmt ? *fmt++ : '\0';
        switch (c) {
            case 's':
                put_str(buf, size, &n, va_arg(ap, const char *));
                break;
            case 'd':
            case 'i': {
                long long v = lng == 3   ? (long long)va_arg(ap, ptrdiff_t)
                              : lng == 2 ? va_arg(ap, long long)
                              : lng == 1 ? va_arg(ap, long)
                                         : va_arg(ap, int);
                put_int(buf, size, &n, v);
                break;
            }
            case 'o':
            case 'u':
            case 'x':
            case 'X': {
                unsigned base = c == 'o' ? 8u : c == 'u' ? 10u : 16u;
                unsigned long long v =
                    lng == 3   ? (unsigned long long)va_arg(ap, size_t)
                    : lng == 2 ? va_arg(ap, unsigned long long)
                    : lng == 1 ? va_arg(ap, unsigned long)
                               : va_arg(ap, unsigned int);
                put_uint(buf, size, &n, v, base, c == 'X');
                break;
            }
            case 'p':
                /* "0x" + lowercase hex, no zero-padding (NULL prints as 0x0).
                 * Stable across targets; not byte-identical to every libc %p. */
                put_str(buf, size, &n, "0x");
                put_uint(buf, size, &n, (unsigned long long)(uintptr_t)va_arg(
                                            ap, void *),
                         16, 0);
                break;
            case 'c':
                put(buf, size, &n, (char)va_arg(ap, int));
                break;
            case '%':
                put(buf, size, &n, '%');
                break;
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            case 'a':
            case 'A':
                /* No floating-point formatting (freestanding, by design), but
                 * consume the double so later conversions stay aligned, and
                 * echo the specifier verbatim. */
                (void)va_arg(ap, double);
                put(buf, size, &n, '%');
                put(buf, size, &n, c);
                break;
            case '\0':
                fmt--; /* trailing '%': stop cleanly on the next loop test */
                break;
            default: /* unknown conversion: emit it verbatim (consumes no arg) */
                put(buf, size, &n, '%');
                put(buf, size, &n, c);
                break;
        }
    }
    if (size)
        buf[n < size ? n : size - 1] = '\0';
    return n;
}
