/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Headless animation test: drives the mock platform clock deterministically
 * and ticks animations via cl_application_step(). Covers time-based progress
 * (a stalled loop skips ahead instead of slowing down), easing curves, the
 * final t == 1.0 call, on_done for both outcomes, cancel (incl. from within
 * on_progress), chaining from on_done, concurrent animations, the shared
 * ticker being released when idle, interpolation helpers, argument guards,
 * and that live animations are freed at app destroy (ASan).
 */
#include <copal/copal.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"
#include "app/app_internal.h"

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

#define CHECK_NEAR(x, y) CHECK(fabsf((x) - (y)) < 1e-5f)

static void advance_step(cl_application_t *app, cl_platform_t *p, uint64_t ms)
{
    cl_platform_mock_advance(p, ms);
    cl_application_step(app, false);
}

/* Records every on_progress value and the on_done outcome. */
struct track {
    float values[32];
    int calls;
    int done_calls;
    bool finished;
};

static void track_progress(cl_animation_t *anim, float t, void *user)
{
    struct track *tr = user;

    (void)anim;
    if (tr->calls < (int)(sizeof(tr->values) / sizeof(tr->values[0])))
        tr->values[tr->calls] = t;
    tr->calls++;
}

static void track_done(cl_animation_t *anim, bool finished, void *user)
{
    struct track *tr = user;

    (void)anim;
    tr->done_calls++;
    tr->finished = finished;
}

/* Cancels itself on the first on_progress call. */
static void cancel_self(cl_animation_t *anim, float t, void *user)
{
    struct track *tr = user;

    (void)t;
    tr->calls++;
    cl_animation_cancel(anim);
}

/* on_done handler that chains a follow-up animation. */
struct chain {
    cl_application_t *app;
    struct track second;
    bool started_second;
};

static void chain_done(cl_animation_t *anim, bool finished, void *user)
{
    struct chain *c = user;
    cl_animation_desc_t d = { CL_ANIMATION_DESC_INIT_FIELDS,
                              .duration_ms = 50,
                              .on_progress = track_progress,
                              .on_done = track_done,
                              .user = &c->second };

    (void)anim;
    if (finished && !c->started_second)
        c->started_second = cl_animation_start(c->app, &d) != NULL;
}

static void noop_progress(cl_animation_t *anim, float t, void *user)
{
    (void)anim;
    (void)t;
    (void)user;
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

    /* Easing curves: fixed points and boundaries. */
    CHECK_NEAR(cl_ease(CL_EASE_LINEAR, 0.25f), 0.25f);
    CHECK_NEAR(cl_ease(CL_EASE_IN, 0.5f), 0.125f);
    CHECK_NEAR(cl_ease(CL_EASE_OUT, 0.5f), 0.875f);
    CHECK_NEAR(cl_ease(CL_EASE_IN_OUT, 0.5f), 0.5f);
    CHECK_NEAR(cl_ease(CL_EASE_IN_OUT, 0.25f), 0.0625f);
    CHECK_NEAR(cl_ease(CL_EASE_IN_OUT, 0.75f), 0.9375f);
    {
        cl_easing_t e;

        for (e = CL_EASE_LINEAR; e <= CL_EASE_IN_OUT; e++) {
            CHECK_NEAR(cl_ease(e, 0.0f), 0.0f);
            CHECK_NEAR(cl_ease(e, 1.0f), 1.0f);
            CHECK_NEAR(cl_ease(e, -1.0f), 0.0f); /* clamped */
            CHECK_NEAR(cl_ease(e, 2.0f), 1.0f);
        }
    }

    /* Interpolation helpers. */
    CHECK_NEAR(cl_lerp(10.0f, 20.0f, 0.5f), 15.0f);
    CHECK_NEAR(cl_lerp(20.0f, 10.0f, 1.0f), 10.0f);
    {
        cl_color_t c = cl_color_lerp(cl_rgba(0, 100, 255, 0),
                                     cl_rgba(255, 100, 0, 255), 0.5f);

        CHECK(c.r == 128 && c.g == 100 && c.b == 128 && c.a == 128);
        c = cl_color_lerp(cl_rgba(0, 0, 0, 0), cl_rgba(255, 255, 255, 255),
                          2.0f); /* t clamped */
        CHECK(c.r == 255 && c.a == 255);
    }
    {
        cl_rect_t r = cl_rect_lerp((cl_rect_t){ 0, 0, 100, 50 },
                                   (cl_rect_t){ 10, 20, 200, 100 }, 0.5f);

        CHECK_NEAR(r.x, 5.0f);
        CHECK_NEAR(r.y, 10.0f);
        CHECK_NEAR(r.w, 150.0f);
        CHECK_NEAR(r.h, 75.0f);
    }

    /* Argument guards. */
    {
        cl_animation_desc_t d = { CL_ANIMATION_DESC_INIT_FIELDS,
                                  .duration_ms = 100,
                                  .on_progress = noop_progress };
        cl_animation_desc_t bad = d;

        CHECK(cl_animation_start(NULL, &d) == NULL);
        CHECK(cl_animation_start(app, NULL) == NULL);
        CHECK(cl_last_error() == CL_ERROR_INVALID_ARGUMENT);
        d.on_progress = NULL;
        CHECK(cl_animation_start(app, &d) == NULL);
        CHECK(cl_last_error() == CL_ERROR_INVALID_ARGUMENT);
        bad.abi_version = COPAL_VERSION_ENCODE(COPAL_VERSION_MAJOR + 1, 0, 0);
        CHECK(cl_animation_start(app, &bad) == NULL); /* incompatible major */
        CHECK(cl_last_error() == CL_ERROR_ABI_MISMATCH);
        cl_animation_cancel(NULL); /* must not crash */
    }

    /* Progress is computed from elapsed time and ends exactly at 1.0. */
    {
        struct track tr = { 0 };
        cl_animation_desc_t d = { CL_ANIMATION_DESC_INIT_FIELDS,
                                  .duration_ms = 100,
                                  .easing = CL_EASE_LINEAR,
                                  .on_progress = track_progress,
                                  .on_done = track_done,
                                  .user = &tr };

        CHECK(cl_animation_start(app, &d) != NULL);
        CHECK(cl_app_timers_timeout(app) >= 0); /* ticker armed */
        advance_step(app, plat, 16);
        CHECK(tr.calls == 1);
        CHECK_NEAR(tr.values[0], 0.16f);
        advance_step(app, plat, 34); /* now 50/100 */
        CHECK(tr.calls == 2);
        CHECK_NEAR(tr.values[1], 0.5f);
        advance_step(app, plat, 60); /* past the end: clamps to 1.0 */
        CHECK(tr.calls == 3);
        CHECK_NEAR(tr.values[2], 1.0f);
        CHECK(tr.done_calls == 1);
        CHECK(tr.finished);
        advance_step(app, plat, 100); /* freed: no further calls */
        CHECK(tr.calls == 3);
        CHECK(cl_app_timers_timeout(app) == -1); /* ticker released */
    }

    /* A stalled loop coalesces: one late tick jumps straight to t == 1.0. */
    {
        struct track tr = { 0 };
        cl_animation_desc_t d = { CL_ANIMATION_DESC_INIT_FIELDS,
                                  .duration_ms = 100,
                                  .on_progress = track_progress,
                                  .on_done = track_done,
                                  .user = &tr };

        CHECK(cl_animation_start(app, &d) != NULL);
        advance_step(app, plat, 1000);
        CHECK(tr.calls == 1);
        CHECK_NEAR(tr.values[0], 1.0f);
        CHECK(tr.done_calls == 1 && tr.finished);
    }

    /* Eased progress reaches on_progress (not the raw ratio). */
    {
        struct track tr = { 0 };
        cl_animation_desc_t d = { CL_ANIMATION_DESC_INIT_FIELDS,
                                  .duration_ms = 100,
                                  .easing = CL_EASE_IN,
                                  .on_progress = track_progress,
                                  .user = &tr };

        CHECK(cl_animation_start(app, &d) != NULL);
        advance_step(app, plat, 50);
        CHECK(tr.calls == 1);
        CHECK_NEAR(tr.values[0], 0.125f); /* ease-in(0.5) */
        advance_step(app, plat, 50);
        CHECK_NEAR(tr.values[1], 1.0f);
    }

    /* duration 0 completes on the first tick. */
    {
        struct track tr = { 0 };
        cl_animation_desc_t d = { CL_ANIMATION_DESC_INIT_FIELDS,
                                  .duration_ms = 0,
                                  .on_progress = track_progress,
                                  .on_done = track_done,
                                  .user = &tr };

        CHECK(cl_animation_start(app, &d) != NULL);
        advance_step(app, plat, 16);
        CHECK(tr.calls == 1);
        CHECK_NEAR(tr.values[0], 1.0f);
        CHECK(tr.done_calls == 1 && tr.finished);
    }

    /* Cancel stops progress and reports finished == false. */
    {
        struct track tr = { 0 };
        cl_animation_desc_t d = { CL_ANIMATION_DESC_INIT_FIELDS,
                                  .duration_ms = 100,
                                  .on_progress = track_progress,
                                  .on_done = track_done,
                                  .user = &tr };
        cl_animation_t *anim = cl_animation_start(app, &d);

        CHECK(anim != NULL);
        advance_step(app, plat, 16);
        CHECK(tr.calls == 1);
        cl_animation_cancel(anim);
        CHECK(tr.done_calls == 1);
        CHECK(!tr.finished);
        advance_step(app, plat, 200);
        CHECK(tr.calls == 1); /* no further progress after cancel */
    }

    /* Cancel from within on_progress is safe (the tick is walking the list). */
    {
        struct track tr = { 0 };
        cl_animation_desc_t d = { CL_ANIMATION_DESC_INIT_FIELDS,
                                  .duration_ms = 100,
                                  .on_progress = cancel_self,
                                  .on_done = track_done,
                                  .user = &tr };

        CHECK(cl_animation_start(app, &d) != NULL);
        advance_step(app, plat, 16);
        CHECK(tr.calls == 1);
        CHECK(tr.done_calls == 1);
        CHECK(!tr.finished); /* cancelled, not finished */
        advance_step(app, plat, 200);
        CHECK(tr.calls == 1);
    }

    /* Two concurrent animations progress independently. */
    {
        struct track t1 = { 0 };
        struct track t2 = { 0 };
        cl_animation_desc_t d1 = { CL_ANIMATION_DESC_INIT_FIELDS,
                                   .duration_ms = 50,
                                   .on_progress = track_progress,
                                   .on_done = track_done,
                                   .user = &t1 };
        cl_animation_desc_t d2 = { CL_ANIMATION_DESC_INIT_FIELDS,
                                   .duration_ms = 100,
                                   .on_progress = track_progress,
                                   .on_done = track_done,
                                   .user = &t2 };

        CHECK(cl_animation_start(app, &d1) != NULL);
        CHECK(cl_animation_start(app, &d2) != NULL);
        advance_step(app, plat, 50);
        CHECK(t1.done_calls == 1 && t1.finished);
        CHECK(t2.done_calls == 0);
        CHECK_NEAR(t2.values[0], 0.5f);
        advance_step(app, plat, 50);
        CHECK(t2.done_calls == 1 && t2.finished);
        CHECK_NEAR(t2.values[1], 1.0f);
    }

    /* Chaining: on_done starts the next animation, which then runs. */
    {
        struct chain c = { 0 };
        cl_animation_desc_t d = { CL_ANIMATION_DESC_INIT_FIELDS,
                                  .duration_ms = 50,
                                  .on_progress = noop_progress,
                                  .on_done = chain_done,
                                  .user = &c };

        c.app = app;
        CHECK(cl_animation_start(app, &d) != NULL);
        advance_step(app, plat, 50); /* first finishes, second starts */
        CHECK(c.started_second);
        CHECK(c.second.done_calls == 0);
        advance_step(app, plat, 50); /* second finishes */
        CHECK(c.second.done_calls == 1 && c.second.finished);
        CHECK_NEAR(c.second.values[c.second.calls - 1], 1.0f);
    }

    /* An animation still running at destroy is freed without callbacks. */
    {
        static struct track tr; /* outlives the app teardown */
        cl_animation_desc_t d = { CL_ANIMATION_DESC_INIT_FIELDS,
                                  .duration_ms = 10000,
                                  .on_progress = track_progress,
                                  .on_done = track_done,
                                  .user = &tr };

        CHECK(cl_animation_start(app, &d) != NULL);
    }

    cl_application_destroy(app);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("test_animation: all checks passed\n");
    return 0;
}
