/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_ANIMATION_H
#define CL_ANIMATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/types.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;
typedef struct cl_animation cl_animation_t;

/*
 * Time-based animations driven by the application loop.
 *
 * All running animations share one ~60 Hz ticker. Progress is computed from
 * elapsed wall-clock time (now_ms() - start), never from the number of ticks:
 * the ticker is a best-effort timer that coalesces missed ticks under load, so
 * a tick-counting animation would silently slow down; a time-based one just
 * skips ahead. Animations compose freely - any number may run concurrently,
 * and starting the next one from on_done chains them.
 *
 * Callbacks fire on the loop thread, between event dispatch and rendering
 * (like timers). An animation's callbacks may cancel or start any animation,
 * including its own.
 */

typedef enum cl_easing {
    CL_EASE_LINEAR = 0,
    CL_EASE_IN,     /* cubic: starts slow, finishes fast */
    CL_EASE_OUT,    /* cubic: starts fast, finishes slow */
    CL_EASE_IN_OUT  /* cubic: slow at both ends */
} cl_easing_t;

/** cl_animation_fn - progress callback. `t` is the EASED progress in [0, 1];
 *  the final call of a finished animation always receives exactly 1.0. */
typedef void (*cl_animation_fn)(cl_animation_t *anim, float t, void *user);

/** cl_animation_done_fn - completion callback. `finished` is true when the
 *  animation ran to the end, false when it was cancelled. */
typedef void (*cl_animation_done_fn)(cl_animation_t *anim, bool finished,
                                     void *user);

typedef struct cl_animation_desc {
    uint32_t abi_version;
    size_t struct_size;
    uint32_t duration_ms; /* 0 completes on the first tick */
    cl_easing_t easing;
    cl_animation_fn on_progress;  /* required */
    cl_animation_done_fn on_done; /* optional */
    void *user;
} cl_animation_desc_t;

#define CL_ANIMATION_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_animation_desc_t)
#define CL_ANIMATION_DESC_INIT { CL_ANIMATION_DESC_INIT_FIELDS }

/**
 * cl_animation_start() - begin an animation; the first on_progress fires on
 * the next loop iteration's tick.
 *
 * The animation frees itself when it completes or is cancelled: after on_done
 * has fired (or, without an on_done, after on_progress saw t == 1.0) the
 * handle is invalid. Keep the handle only if you may cancel; NULL it from
 * on_done. Animations still running when the application is destroyed are
 * freed without callbacks.
 *
 * Returns NULL on allocation failure, when the platform has no clock (like
 * cl_timer_create), when on_progress is missing, or on an ABI-mismatched desc.
 */
CL_API cl_animation_t *cl_animation_start(cl_application_t *app,
                                          const cl_animation_desc_t *desc);

/** cl_animation_cancel() - stop the animation without further on_progress
 *  calls; fires on_done(finished=false) and frees it. The handle is invalid
 *  afterwards. NULL is ignored. */
CL_API void cl_animation_cancel(cl_animation_t *anim);

/* ---- interpolation helpers (animatable values) --------------------------- */

/** cl_ease() - map linear progress t (clamped to [0, 1]) through a curve. */
CL_API float cl_ease(cl_easing_t easing, float t);

/** cl_lerp() - from + (to - from) * t. */
CL_API float cl_lerp(float from, float to, float t);

/** cl_color_lerp() - per-channel linear blend (t clamped to [0, 1]). */
CL_API cl_color_t cl_color_lerp(cl_color_t from, cl_color_t to, float t);

/** cl_rect_lerp() - per-field linear blend of position and size. */
CL_API cl_rect_t cl_rect_lerp(cl_rect_t from, cl_rect_t to, float t);

#ifdef __cplusplus
}
#endif

#endif /* CL_ANIMATION_H */
