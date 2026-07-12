/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/error.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "foundation_internal.h"

#if defined(_MSC_VER)
#  define CL_THREAD_LOCAL __declspec(thread)
#else
#  define CL_THREAD_LOCAL _Thread_local
#endif

static CL_THREAD_LOCAL cl_result_t g_last_error = CL_OK;
static cl_log_fn g_log_fn;
static void *g_log_user;

void cl_set_last_error(cl_result_t result)
{
    g_last_error = result;
}

cl_result_t cl_last_error(void)
{
    return g_last_error;
}

const char *cl_result_string(cl_result_t result)
{
    switch (result) {
        case CL_OK:                     return "OK";
        case CL_ERROR_INVALID_ARGUMENT: return "invalid argument";
        case CL_ERROR_OUT_OF_MEMORY:    return "out of memory";
        case CL_ERROR_PLATFORM:         return "platform error";
        case CL_ERROR_RENDERER:         return "renderer error";
        case CL_ERROR_FONT:             return "font error";
        case CL_ERROR_UNSUPPORTED:      return "unsupported";
        case CL_ERROR_ABI_MISMATCH:     return "ABI/version mismatch";

        default:                        return "unknown error";
    }
}

void cl_set_log_callback(cl_log_fn fn, void *user)
{
    g_log_fn = fn;
    g_log_user = user;
}

void cl_log(cl_log_level_t level, const char *fmt, ...)
{
    char msg[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (g_log_fn) {
        g_log_fn(level, msg, g_log_user);
        return;
    }
    /* No sink installed: keep problems visible. */
    if (level >= CL_LOG_WARN)
        fprintf(stderr, "copal: %s\n", msg);
}
