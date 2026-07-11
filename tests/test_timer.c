/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Headless timer test: drives the mock platform clock deterministically and
 * fires timers via cl_application_step(). Covers one-shot vs repeating firing,
 * coalescing of missed ticks, cancel (incl. from within a callback), creating a
 * timer from a callback, restart of dormant/pending timers, independent timers,
 * NULL-argument guards, and that live timers are freed at app destroy (ASan).
 */
#include <copal/copal.h>

#include <stdio.h>
#include <string.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void bump(cl_timer_t *t, void *user)
{
    (void)t;
    (*(int *)user)++;
}

static void self_cancel(cl_timer_t *t, void *user)
{
    (*(int *)user)++;
    cl_timer_cancel(t); /* cancel from within its own callback */
}

struct spawn_ctx {
    cl_application_t *app;
    int *child_count;
    int fired;
    cl_timer_t *child;
};

static void spawner(cl_timer_t *t, void *user)
{
    struct spawn_ctx *c = user;

    (void)t;
    c->fired++;
    if (!c->child)
        c->child = cl_timer_create(c->app, 50, false, bump, c->child_count);
}

struct reenter_ctx {
    cl_application_t *app;
    int n;
};

static void reenter_cb(cl_timer_t *t, void *user)
{
    struct reenter_ctx *c = user;

    c->n++;
    cl_timer_cancel(t);                 /* self-cancel: deferred while firing */
    cl_application_step(c->app, false); /* nested poll must not free `t` */
}

static void advance_step(cl_application_t *app, cl_platform_t *p, uint64_t ms)
{
    cl_platform_mock_advance(p, ms);
    cl_application_step(app, false);
}

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_platform_t *plat = cl_platform_mock_create(a);
    cl_renderer_t *rend = cl_renderer_mock_create(a);
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_application_t *app;

    ad.platform = plat;
    ad.renderer = rend;
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    if (!app)
        return 1;

    /* NULL-argument guards. */
    CHECK(cl_timer_create(app, 100, false, NULL, NULL) == NULL);
    cl_timer_cancel(NULL);  /* must not crash */
    cl_timer_restart(NULL); /* must not crash */

    /* One-shot: fires once, at/after its deadline, never before, then dormant. */
    {
        int n = 0;
        cl_timer_t *t = cl_timer_create(app, 100, false, bump, &n);

        CHECK(t != NULL);
        advance_step(app, plat, 50); /* now 50 < 100 */
        CHECK(n == 0);
        advance_step(app, plat, 60); /* now 110 >= 100 */
        CHECK(n == 1);
        advance_step(app, plat, 500); /* dormant: no further fire */
        CHECK(n == 1);
        cl_timer_cancel(t);
    }

    /* Repeating: fires once per elapsed interval; a big jump coalesces. */
    {
        int n = 0;
        cl_timer_t *t = cl_timer_create(app, 100, true, bump, &n);

        advance_step(app, plat, 100);
        CHECK(n == 1);
        advance_step(app, plat, 100);
        CHECK(n == 2);
        advance_step(app, plat, 250); /* +250 -> one fire, not two */
        CHECK(n == 3);
        /* Distinguish coalescing (deadline = now + interval) from a catch-up
         * (deadline += interval): after the +250 jump the deadline is re-based
         * to now+100, so a +50 nudge is not yet due (a += rule would fire). */
        advance_step(app, plat, 50);
        CHECK(n == 3);
        advance_step(app, plat, 100); /* re-based from now, fires again */
        CHECK(n == 4);
        cl_timer_cancel(t);
        advance_step(app, plat, 1000); /* cancelled: nothing more */
        CHECK(n == 4);
    }

    /* Cancel from within the callback is safe and stops future fires. */
    {
        int n = 0;

        cl_timer_create(app, 100, true, self_cancel, &n);
        advance_step(app, plat, 100);
        CHECK(n == 1);
        advance_step(app, plat, 500);
        CHECK(n == 1);
    }

    /* Restart re-arms a dormant one-shot and reschedules a pending one. */
    {
        int n = 0;
        cl_timer_t *t = cl_timer_create(app, 100, false, bump, &n);

        advance_step(app, plat, 80); /* now +80 < 100 */
        CHECK(n == 0);
        cl_timer_restart(t); /* reschedule 100 from now */
        advance_step(app, plat, 40); /* +40 < 100 since restart */
        CHECK(n == 0);
        advance_step(app, plat, 100); /* now past the restarted deadline */
        CHECK(n == 1);
        cl_timer_restart(t); /* re-arm the now-dormant one-shot */
        advance_step(app, plat, 100);
        CHECK(n == 2);
        cl_timer_cancel(t);
    }

    /* Creating a timer inside a callback is safe; the new one fires later. */
    {
        int child_n = 0;
        struct spawn_ctx ctx = { app, &child_n, 0, NULL };
        cl_timer_t *t = cl_timer_create(app, 100, false, spawner, &ctx);

        advance_step(app, plat, 100); /* spawner fires, creates child */
        CHECK(ctx.fired == 1);
        CHECK(child_n == 0); /* child not fired in the spawning pass */
        advance_step(app, plat, 50); /* child now due */
        CHECK(child_n == 1);
        cl_timer_cancel(t);
        if (ctx.child)
            cl_timer_cancel(ctx.child);
    }

    /* Two independent timers keep their own schedules. */
    {
        int fast = 0;
        int slow = 0;
        cl_timer_t *tf = cl_timer_create(app, 50, true, bump, &fast);
        cl_timer_t *ts = cl_timer_create(app, 200, true, bump, &slow);

        advance_step(app, plat, 50);
        CHECK(fast == 1);
        CHECK(slow == 0);
        advance_step(app, plat, 50);
        CHECK(fast == 2);
        CHECK(slow == 0);
        advance_step(app, plat, 100);
        CHECK(fast == 3);
        CHECK(slow == 1);
        cl_timer_cancel(tf);
        cl_timer_cancel(ts);
    }

    /* step(wait=true) must never block indefinitely: with no armed timer it
     * passes a clamped 0 (not -1) to wait(); with one, a bounded timeout. */
    {
        int n = 0;
        cl_timer_t *t;

        cl_application_step(app, true);
        CHECK(cl_platform_mock_last_wait_timeout(plat) == 0);
        t = cl_timer_create(app, 100, false, bump, &n);
        cl_application_step(app, true);
        CHECK(cl_platform_mock_last_wait_timeout(plat) > 0);
        cl_timer_cancel(t);
    }

    /* Re-entrancy: a callback that self-cancels and then re-enters the loop
     * must not let the nested poll free a node the outer walk still holds
     * (regression; the bug is a use-after-free flagged by ASan). */
    {
        struct reenter_ctx c = { app, 0 };
        int other = 0;
        cl_timer_t *tb = cl_timer_create(app, 100, false, bump, &other);
        cl_timer_t *ta = cl_timer_create(app, 100, false, reenter_cb, &c);

        (void)ta;
        advance_step(app, plat, 100); /* both due; A self-cancels + nested step */
        CHECK(c.n == 1);
        CHECK(other == 1); /* B fired once in the nested pass, then dormant */
        advance_step(app, plat, 200);
        CHECK(c.n == 1); /* A stayed cancelled, no resurrection */
        CHECK(other == 1);
        cl_timer_cancel(tb);
    }

    /* A repeating zero-interval timer is floored to 1 ms so the loop can still
     * idle between fires instead of busy-spinning. */
    {
        int n = 0;
        cl_timer_t *t = cl_timer_create(app, 0, true, bump, &n);

        advance_step(app, plat, 1); /* fires, then re-arms 1 ms out */
        CHECK(n == 1);
        cl_application_step(app, true); /* next fire is 1 ms away, not 0 */
        CHECK(cl_platform_mock_last_wait_timeout(plat) == 1);
        cl_timer_cancel(t);
    }

    /* A live timer left un-cancelled must be freed at app destroy (ASan). */
    {
        int n = 0;

        cl_timer_create(app, 100, true, bump, &n);
    }

    cl_application_destroy(app);

    if (failures == 0)
        printf("all timer tests passed\n");
    return failures == 0 ? 0 : 1;
}
