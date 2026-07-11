/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_SLIDER_H
#define CL_WIDGETS_SLIDER_H

#include <stddef.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/version.h>
#include <copal/widget.h>
#include <copal/event.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;

/*
 * Horizontal slider: a draggable thumb over a track selecting a value in
 * [min, max]. Drag with the left button, or use Left/Right/Up/Down (by step)
 * and Home/End (min/max) while focused. If max <= min the range defaults to
 * [0, 1]; if step <= 0 it defaults to (max - min) / 20.
 */
typedef struct cl_slider_desc {
    uint32_t abi_version;
    size_t struct_size;
    float min;
    float max;
    float value;
    float step; /* keyboard increment; 0 = (max - min) / 20 */
} cl_slider_desc_t;

#define CL_SLIDER_DESC_INIT_FIELDS \
    .abi_version = CL_VERSION, .struct_size = sizeof(cl_slider_desc_t)

CL_API cl_widget_t *cl_slider_create(cl_application_t *app,
                                     const cl_slider_desc_t *desc);

/** cl_slider_set_value() - set the value (clamped). Does NOT fire on_change. */
CL_API void cl_slider_set_value(cl_widget_t *slider, float value);

/** cl_slider_value() - current value. */
CL_API float cl_slider_value(cl_widget_t *slider);

/** cl_slider_set_range() - set [min, max]; the value is re-clamped. Does NOT
 *  fire on_change. */
CL_API void cl_slider_set_range(cl_widget_t *slider, float min, float max);

/** cl_slider_set_on_change() - called only on user changes (drag/keys). */
CL_API void cl_slider_set_on_change(cl_widget_t *slider, cl_value_fn fn,
                                    void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_SLIDER_H */
