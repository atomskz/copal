/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_ERROR_H
#define CL_ERROR_H

#include <copal/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes. CL_OK is guaranteed to be 0. */
typedef enum cl_result {
    CL_OK = 0,
    CL_ERROR_INVALID_ARGUMENT,
    CL_ERROR_OUT_OF_MEMORY,
    CL_ERROR_PLATFORM,
    CL_ERROR_RENDERER,
    CL_ERROR_FONT,
    CL_ERROR_UNSUPPORTED,
    CL_ERROR_ABI_MISMATCH
} cl_result_t;

typedef enum cl_log_level {
    CL_LOG_DEBUG,
    CL_LOG_INFO,
    CL_LOG_WARN,
    CL_LOG_ERROR
} cl_log_level_t;

typedef void (*cl_log_fn)(cl_log_level_t level, const char *msg, void *user);

/** cl_last_error() - last error code recorded on the calling thread. */
CL_API cl_result_t cl_last_error(void);

/** cl_result_string() - static human-readable description of a result code. */
CL_API const char *cl_result_string(cl_result_t result);

/**
 * cl_set_log_callback() - install a process-wide log callback (NULL removes).
 *
 * Receives the library's diagnostics (backend init failures, GL shader
 * errors, rejected fonts, ...). Without a callback, WARN and ERROR messages
 * go to stderr. Install it before the first copal call and before spawning
 * threads; the sink itself is not synchronised.
 */
CL_API void cl_set_log_callback(cl_log_fn fn, void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_ERROR_H */
