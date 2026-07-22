/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_VERSION_H
#define CL_VERSION_H

#include <stdint.h>

#include <copal/export.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COPAL_VERSION_MAJOR 0
#define COPAL_VERSION_MINOR 3
#define COPAL_VERSION_PATCH 2

/* Pack a version into a single 0x00MMmmpp integer. */
#define COPAL_VERSION_ENCODE(major, minor, patch) \
    (((uint32_t)(major) << 16) | ((uint32_t)(minor) << 8) | (uint32_t)(patch))

#define COPAL_VERSION \
    COPAL_VERSION_ENCODE(COPAL_VERSION_MAJOR, COPAL_VERSION_MINOR, COPAL_VERSION_PATCH)

/**
 * cl_version_runtime() - version of the actually linked library.
 *
 * Return: the library version in COPAL_VERSION_ENCODE() form. Compare against the
 * compile-time COPAL_VERSION to detect a header/binary mismatch.
 */
CL_API uint32_t cl_version_runtime(void);

/** cl_version_string() - human-readable library version, e.g. "0.3.2". */
CL_API const char *cl_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* CL_VERSION_H */
