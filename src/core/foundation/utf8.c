/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "foundation_internal.h"

size_t cl_utf8_next(const char *s, uint32_t *cp)
{
    const unsigned char *u = (const unsigned char *)s;
    unsigned char c = u[0];

    if (c == 0) {
        *cp = 0;
        return 0;
    }

    if (c < 0x80) {
        *cp = c;
        return 1;
    }

    if ((c & 0xE0) == 0xC0) {
        if ((u[1] & 0xC0) == 0x80) {
            uint32_t v = ((uint32_t)(c & 0x1F) << 6) | (u[1] & 0x3F);
            if (v >= 0x80) {
                *cp = v;
                return 2;
            }
        }
    } else if ((c & 0xF0) == 0xE0) {
        if ((u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
            uint32_t v = ((uint32_t)(c & 0x0F) << 12) |
                         ((uint32_t)(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
            if (v >= 0x800 && !(v >= 0xD800 && v <= 0xDFFF)) {
                *cp = v;
                return 3;
            }
        }
    } else if ((c & 0xF8) == 0xF0) {
        if ((u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 &&
            (u[3] & 0xC0) == 0x80) {
            uint32_t v = ((uint32_t)(c & 0x07) << 18) |
                         ((uint32_t)(u[1] & 0x3F) << 12) |
                         ((uint32_t)(u[2] & 0x3F) << 6) | (u[3] & 0x3F);
            if (v >= 0x10000 && v <= 0x10FFFF) {
                *cp = v;
                return 4;
            }
        }
    }

    /* Invalid: substitute U+FFFD, consume one byte. */
    *cp = 0xFFFD;
    return 1;
}

size_t cl_utf8_next_n(const char *s, size_t avail, uint32_t *cp)
{
    const unsigned char *u = (const unsigned char *)s;
    unsigned char c;

    if (avail == 0) {
        *cp = 0;
        return 0;
    }
    c = u[0];
    if (c == 0) {
        *cp = 0;
        return 0;
    }

    if (c < 0x80) {
        *cp = c;
        return 1;
    }

    if ((c & 0xE0) == 0xC0) {
        if (avail >= 2 && (u[1] & 0xC0) == 0x80) {
            uint32_t v = ((uint32_t)(c & 0x1F) << 6) | (u[1] & 0x3F);
            if (v >= 0x80) {
                *cp = v;
                return 2;
            }
        }
    } else if ((c & 0xF0) == 0xE0) {
        if (avail >= 3 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
            uint32_t v = ((uint32_t)(c & 0x0F) << 12) |
                         ((uint32_t)(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
            if (v >= 0x800 && !(v >= 0xD800 && v <= 0xDFFF)) {
                *cp = v;
                return 3;
            }
        }
    } else if ((c & 0xF8) == 0xF0) {
        if (avail >= 4 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 &&
            (u[3] & 0xC0) == 0x80) {
            uint32_t v = ((uint32_t)(c & 0x07) << 18) |
                         ((uint32_t)(u[1] & 0x3F) << 12) |
                         ((uint32_t)(u[2] & 0x3F) << 6) | (u[3] & 0x3F);
            if (v >= 0x10000 && v <= 0x10FFFF) {
                *cp = v;
                return 4;
            }
        }
    }

    *cp = 0xFFFD;
    return 1;
}
