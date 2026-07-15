/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/error.h>

#include <stdarg.h>
#include <stddef.h>
#ifdef CL_HOSTED
#include <stdio.h>  /* stderr fallback when no log sink is installed */
#include <stdlib.h> /* abort() in the default assert handler */
#endif

#include "foundation_internal.h"

#if defined(_MSC_VER)
#  define CL_THREAD_LOCAL __declspec(thread)
#else
#  define CL_THREAD_LOCAL _Thread_local
#endif

static CL_THREAD_LOCAL cl_result_t g_last_error = CL_OK;
static cl_log_fn g_log_fn;
static void *g_log_user;
static cl_assert_fn g_assert_fn;

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

void cl_set_assert_handler(cl_assert_fn fn)
{
    g_assert_fn = fn;
}

void cl_assert_fail(const char *expr, const char *file, int line)
{
    if (g_assert_fn) {
        g_assert_fn(expr, file, line);
        return;
    }
#ifdef CL_HOSTED
    cl_log(CL_LOG_ERROR, "assertion failed: %s (%s:%d)", expr, file, line);
    abort();
#else
    /* Freestanding with no handler installed: nothing we can safely do but
     * stop. The embedder should install one via cl_set_assert_handler. */
    (void)expr;
    (void)file;
    (void)line;
    for (;;) {
    }
#endif
}

void cl_log(cl_log_level_t level, const char *fmt, ...)
{
    char msg[256];
    va_list ap;

#ifndef CL_HOSTED
    /* Freestanding: the callback is the only sink. With none installed the
     * message is dropped, so skip formatting it entirely. The embedder should
     * install one via cl_set_log_callback to surface diagnostics. */
    if (!g_log_fn) {
        (void)level;
        (void)fmt;
        return;
    }
#endif
    va_start(ap, fmt);
    cl_vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (g_log_fn) {
        g_log_fn(level, msg, g_log_user);
        return;
    }
#ifdef CL_HOSTED
    /* No sink installed: keep problems visible on the hosted build. */
    if (level >= CL_LOG_WARN)
        fprintf(stderr, "copal: %s\n", msg);
#endif
}
