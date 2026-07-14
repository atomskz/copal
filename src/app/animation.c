/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/animation.h>
#include <copal/timer.h>

#include "app/app_internal.h"
#include "widget/widget_internal.h" /* CL_DESC_ABI_OK */
#include "core/foundation/foundation_internal.h"

#define CL_ANIMATION_TICK_MS 16 /* ~60 Hz; progress is time-based regardless */

struct cl_animation {
    cl_application_t *app;
    cl_animation_t *next; /* app->animations list */
    uint64_t start;       /* platform clock at start (ms) */
    uint32_t duration_ms;
    cl_easing_t easing;
    bool dead; /* finished or cancelled; unlinked and freed after the tick */
    cl_animation_fn on_progress;
    cl_animation_done_fn on_done;
    void *user;
};

/* ---- easing and interpolation -------------------------------------------- */

static float clamp01(float t)
{
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

float cl_ease(cl_easing_t easing, float t)
{
    t = clamp01(t);
    switch (easing) {
        case CL_EASE_IN:
            return t * t * t;

        case CL_EASE_OUT: {
            float u = 1.0f - t;

            return 1.0f - u * u * u;
        }

        case CL_EASE_IN_OUT:
            if (t < 0.5f)
                return 4.0f * t * t * t;
            {
                float u = -2.0f * t + 2.0f;

                return 1.0f - u * u * u * 0.5f;
            }

        case CL_EASE_LINEAR:
        default:
            return t;
    }
}

float cl_lerp(float from, float to, float t)
{
    return from + (to - from) * t;
}

static uint8_t chan_lerp(uint8_t from, uint8_t to, float t)
{
    float v = (float)from + ((float)to - (float)from) * t;

    return (uint8_t)(v + 0.5f); /* v stays in [0, 255] for t in [0, 1] */
}

cl_color_t cl_color_lerp(cl_color_t from, cl_color_t to, float t)
{
    cl_color_t out;

    t = clamp01(t);
    out.r = chan_lerp(from.r, to.r, t);
    out.g = chan_lerp(from.g, to.g, t);
    out.b = chan_lerp(from.b, to.b, t);
    out.a = chan_lerp(from.a, to.a, t);
    return out;
}

cl_rect_t cl_rect_lerp(cl_rect_t from, cl_rect_t to, float t)
{
    cl_rect_t out;

    out.x = cl_lerp(from.x, to.x, t);
    out.y = cl_lerp(from.y, to.y, t);
    out.w = cl_lerp(from.w, to.w, t);
    out.h = cl_lerp(from.h, to.h, t);
    return out;
}

/* ---- the shared ticker ---------------------------------------------------- */

static uint64_t app_now(cl_application_t *app)
{
    const cl_platform_ops_t *ops = app->platform->ops;

    return ops->now_ms ? ops->now_ms(app->platform) : 0;
}

/* Unlink and free every dead animation. Never called while ticking. */
static void anim_reap(cl_application_t *app)
{
    cl_animation_t **pp = &app->animations;

    while (*pp) {
        cl_animation_t *a = *pp;

        if (a->dead) {
            *pp = a->next;
            cl_free(&app->alloc, a);
        } else {
            pp = &a->next;
        }
    }
    /* Idle again: stop the ticker so an animation-free loop can sleep. */
    if (!app->animations && app->anim_timer) {
        cl_timer_cancel(app->anim_timer);
        app->anim_timer = NULL;
    }
}

static void anim_tick(cl_timer_t *timer, void *user)
{
    cl_application_t *app = user;
    uint64_t now = app_now(app);
    bool was_firing = app->anim_firing;
    cl_animation_t *a;

    (void)timer;
    /*
     * Save/restore the firing flag (not clear) so a callback that re-enters
     * the loop - a nested cl_application_step - cannot reap animations an
     * outer tick is still walking: only the outermost pass reaps.
     */
    app->anim_firing = true;
    for (a = app->animations; a; a = a->next) {
        float raw;
        float t;

        if (a->dead)
            continue;
        /* Progress is elapsed TIME over duration, never a tick count: the
         * ticker coalesces missed ticks under load, and a time-based
         * animation skips ahead instead of slowing down. */
        raw = a->duration_ms == 0
                  ? 1.0f
                  : (float)(now - a->start) / (float)a->duration_ms;
        if (raw >= 1.0f)
            raw = 1.0f;
        t = cl_ease(a->easing, raw);
        a->on_progress(a, t, a->user);
        /* The callback may have cancelled `a` (already dead and reported). */
        if (!a->dead && raw >= 1.0f) {
            a->dead = true;
            if (a->on_done)
                a->on_done(a, true, a->user);
        }
    }
    app->anim_firing = was_firing;
    if (!was_firing)
        anim_reap(app);
}

cl_animation_t *cl_animation_start(cl_application_t *app,
                                   const cl_animation_desc_t *desc)
{
    cl_animation_t *a;

    if (!app || !app->platform->ops->now_ms) {
        cl_set_last_error(CL_ERROR_UNSUPPORTED);
        return NULL;
    }
    cl_animation_desc_t norm;
    if (!CL_DESC_NORM(desc, norm))
        return NULL;
    if (!desc || !desc->on_progress) {
        cl_set_last_error(CL_ERROR_INVALID_ARGUMENT);
        return NULL;
    }
    if (!app->anim_timer) {
        app->anim_timer = cl_timer_create(app, CL_ANIMATION_TICK_MS, true,
                                          anim_tick, app);
        if (!app->anim_timer)
            return NULL;
    }
    a = cl_alloc(&app->alloc, sizeof(*a));
    if (!a)
        return NULL;
    a->app = app;
    a->start = app_now(app);
    a->duration_ms = desc->duration_ms;
    a->easing = desc->easing;
    a->dead = false;
    a->on_progress = desc->on_progress;
    a->on_done = desc->on_done;
    a->user = desc->user;
    a->next = app->animations;
    app->animations = a;
    return a;
}

void cl_animation_cancel(cl_animation_t *anim)
{
    cl_application_t *app;

    if (!anim || anim->dead)
        return;
    app = anim->app;
    anim->dead = true; /* before on_done: it may re-enter cancel/start */
    if (anim->on_done)
        anim->on_done(anim, false, anim->user);
    /* Defer the free if a tick is walking the list; reap it afterwards. */
    if (!app->anim_firing)
        anim_reap(app);
}

void cl_app_animations_free_all(cl_application_t *app)
{
    cl_animation_t *a = app->animations;

    while (a) {
        cl_animation_t *next = a->next;

        cl_free(&app->alloc, a);
        a = next;
    }
    app->animations = NULL;
    app->anim_timer = NULL; /* freed with the timer list */
}
