/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_TYPES_H
#define CL_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Public value types. Coordinates and sizes are in logical pixels (float);
 * conversion to physical pixels happens in the renderer (ARCHITECTURE §8.3).
 */
typedef struct cl_point  { float x, y; }                     cl_point_t;
typedef struct cl_size   { float w, h; }                     cl_size_t;
typedef struct cl_rect   { float x, y, w, h; }               cl_rect_t;
typedef struct cl_insets { float left, top, right, bottom; } cl_insets_t;

/* 8-bit-per-channel RGBA, NOT premultiplied. */
typedef struct cl_color { uint8_t r, g, b, a; } cl_color_t;

/* Layout constraints for measure(); a max component may be CL_UNBOUNDED. */
typedef struct cl_constraints {
    cl_size_t min;
    cl_size_t max;
} cl_constraints_t;

/* Mouse cursor shapes (mapped to system cursors by the platform backend). */
typedef enum cl_cursor {
    CL_CURSOR_DEFAULT = 0, /* arrow */
    CL_CURSOR_IBEAM,
    CL_CURSOR_HAND,
    CL_CURSOR_CROSSHAIR,
    CL_CURSOR_SIZE_H, /* horizontal resize */
    CL_CURSOR_SIZE_V, /* vertical resize */
    CL_CURSOR__COUNT
} cl_cursor_t;

/* Alignment along an axis. */
typedef enum cl_align {
    CL_ALIGN_START,
    CL_ALIGN_CENTER,
    CL_ALIGN_END,
    CL_ALIGN_STRETCH,
    CL_ALIGN_AUTO /* per-child value: defer to the container's alignment */
} cl_align_t;

/*
 * Reserved: no public API consumes an orientation yet (boxes are the separate
 * cl_vbox/cl_hbox creators and the slider is horizontal). Kept for a future
 * orientation-taking API; do not infer that an orientation knob already exists.
 */
typedef enum cl_orientation {
    CL_ORIENT_HORIZONTAL,
    CL_ORIENT_VERTICAL
} cl_orientation_t;

/* Marker for an unbounded (infinite) constraint; handled explicitly in measure. */
#define CL_UNBOUNDED (3.4e38f)

/** cl_rgba() - construct a colour from channel values. */
static inline cl_color_t cl_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    cl_color_t c = { r, g, b, a };
    return c;
}

#ifdef __cplusplus
}
#endif

#endif /* CL_TYPES_H */
