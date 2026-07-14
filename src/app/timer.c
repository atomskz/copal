/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/timer.h>
#include <copal/application.h>

#include <limits.h>

#include "app/app_internal.h"
#include "core/foundation/foundation_internal.h"

struct cl_timer {
    cl_application_t *app;
    cl_timer_t *next;    /* app->timers list */
    uint64_t deadline;   /* absolute ms (platform clock) */
    uint32_t interval_ms;
    bool repeat;
    bool armed; /* false: dormant one-shot awaiting restart/cancel */
    bool dead;  /* cancelled; unlinked and freed after the fire pass */
    cl_timer_fn fn;
    void *user;
};

static uint64_t app_now(cl_application_t *app)
{
    const cl_platform_ops_t *ops = app->platform->ops;

    return ops->now_ms ? ops->now_ms(app->platform) : 0;
}

/* Unlink and free every cancelled timer. Never called while firing. */
static void reap(cl_application_t *app)
{
    cl_timer_t **pp = &app->timers;

    while (*pp) {
        cl_timer_t *t = *pp;

        if (t->dead) {
            *pp = t->next;
            cl_free(&app->alloc, t);
        } else {
            pp = &t->next;
        }
    }
}

cl_timer_t *cl_timer_create(cl_application_t *app, uint32_t interval_ms,
                            bool repeat, cl_timer_fn fn, void *user)
{
    cl_timer_t *t;

    if (!app || !fn) {
        cl_set_last_error(CL_ERROR_INVALID_ARGUMENT);
        return NULL;
    }
    if (!app->platform->ops->now_ms) {
        cl_set_last_error(CL_ERROR_PLATFORM);
        return NULL;
    }
    /* A repeating timer must advance each tick; a zero interval would re-arm to
     * the current time forever and busy-spin the loop, so floor it at 1 ms. */
    if (repeat && interval_ms == 0)
        interval_ms = 1;
    t = cl_alloc(&app->alloc, sizeof(*t));
    if (!t)
        return NULL;
    t->app = app;
    t->deadline = app_now(app) + interval_ms;
    t->interval_ms = interval_ms;
    t->repeat = repeat;
    t->armed = true;
    t->dead = false;
    t->fn = fn;
    t->user = user;
    t->next = app->timers;
    app->timers = t;
    return t;
}

void cl_timer_cancel(cl_timer_t *timer)
{
    if (!timer || timer->dead)
        return;
    timer->dead = true;
    /* Defer the free if a fire pass is walking the list; reap it afterwards. */
    if (!timer->app->timer_firing)
        reap(timer->app);
}

void cl_timer_restart(cl_timer_t *timer)
{
    if (!timer || timer->dead)
        return;
    timer->deadline = app_now(timer->app) + timer->interval_ms;
    timer->armed = true;
}

int cl_app_timers_timeout(cl_application_t *app)
{
    uint64_t now = app_now(app);
    int best = -1;
    cl_timer_t *t;

    for (t = app->timers; t; t = t->next) {
        uint64_t delta;

        if (t->dead || !t->armed)
            continue;
        if (t->deadline <= now)
            return 0; /* already due */
        delta = t->deadline - now;
        if (delta > (uint64_t)INT_MAX)
            delta = (uint64_t)INT_MAX;
        if (best < 0 || (int)delta < best)
            best = (int)delta;
    }
    return best;
}

void cl_app_timers_poll(cl_application_t *app)
{
    uint64_t now = app_now(app);
    bool was_firing = app->timer_firing;
    cl_timer_t *t;

    /*
     * Save/restore the firing flag rather than clearing it, so a callback that
     * re-enters the loop (a nested cl_application_step/run) does not reap timers
     * an outer poll is still walking: only the outermost pass reaps.
     */
    app->timer_firing = true;
    for (t = app->timers; t; t = t->next) {
        cl_timer_fn fn;
        void *user;

        if (t->dead || !t->armed || t->deadline > now)
            continue;
        /*
         * Re-arm/disarm BEFORE the callback so a restart() or cancel() inside
         * it takes precedence. A repeating timer schedules from `now` (not
         * deadline += interval) so a stalled loop coalesces missed ticks into
         * a single firing instead of bursting to catch up.
         */
        if (t->repeat)
            t->deadline = now + t->interval_ms;
        else
            t->armed = false;
        fn = t->fn;
        user = t->user;
        fn(t, user);
    }
    app->timer_firing = was_firing;
    if (!was_firing)
        reap(app);
}

void cl_app_timers_free_all(cl_application_t *app)
{
    cl_timer_t *t = app->timers;

    while (t) {
        cl_timer_t *next = t->next;

        cl_free(&app->alloc, t);
        t = next;
    }
    app->timers = NULL;
}
