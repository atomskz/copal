/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_TIMER_H
#define CL_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;
typedef struct cl_timer cl_timer_t;

/** cl_timer_fn - timer callback. `timer` is the firing timer, `user` its data. */
typedef void (*cl_timer_fn)(cl_timer_t *timer, void *user);

/*
 * Timers fire their callback from the application's event loop (cl_application_run
 * or cl_application_step), on the same thread, in between event dispatch and
 * rendering. Timing is best-effort: a firing may be late (never early), and a
 * repeating timer that falls behind coalesces missed ticks into one.
 *
 * A timer is owned by the application: it is freed by cl_timer_cancel() or, if
 * still alive, when the application is destroyed. The handle stays valid until
 * cancelled even after a one-shot has fired (so it can be re-armed with
 * cl_timer_restart); the only handle that must never be used again is one passed
 * to cl_timer_cancel().
 */

/**
 * cl_timer_create() - schedule a callback after interval_ms.
 * @repeat: if true the timer re-arms for another interval_ms after each firing;
 *          if false it fires once and then lies dormant until restarted.
 *
 * Returns NULL on allocation failure or when the platform has no clock. A
 * one-shot with interval_ms == 0 fires as soon as the loop next polls timers; a
 * repeating one floors the interval at 1 ms so it cannot busy-spin the loop.
 */
CL_API cl_timer_t *cl_timer_create(cl_application_t *app, uint32_t interval_ms,
                                   bool repeat, cl_timer_fn fn, void *user);

/** cl_timer_cancel() - stop and free the timer. Safe to call from its own
 *  callback. The handle is invalid afterwards. NULL is ignored. */
CL_API void cl_timer_cancel(cl_timer_t *timer);

/** cl_timer_restart() - re-arm the timer to fire interval_ms from now, whether
 *  it was dormant (a fired one-shot) or still pending. NULL is ignored. */
CL_API void cl_timer_restart(cl_timer_t *timer);

#ifdef __cplusplus
}
#endif

#endif /* CL_TIMER_H */
