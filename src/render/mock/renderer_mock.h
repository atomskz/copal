/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_RENDERER_MOCK_H
#define CL_RENDERER_MOCK_H

/* Headless record renderer: captures draw commands for tests (ADR-010).
 * Draw commands are recorded with the CURRENT transform already applied to
 * their geometry (rect/pos/radius/width) and the group opacity multiplied
 * into color.a, so tests assert final on-screen values. The push/pop calls
 * themselves are also recorded (with their raw, local parameters). */
#include <stddef.h>

#include <copal/allocator.h>

#include "render/renderer.h"

typedef enum cl_mock_cmd_kind {
    CL_MOCK_FILL_RECT,
    CL_MOCK_FILL_ROUND,
    CL_MOCK_STROKE_ROUND,
    CL_MOCK_TEXT,
    CL_MOCK_IMAGE,
    CL_MOCK_PUSH_CLIP,
    CL_MOCK_POP_CLIP,
    CL_MOCK_PUSH_TRANSFORM, /* pos = offset (local), width = scale */
    CL_MOCK_POP_TRANSFORM,
    CL_MOCK_PUSH_OPACITY,   /* width = alpha (local) */
    CL_MOCK_POP_OPACITY
} cl_mock_cmd_kind_t;

typedef struct cl_mock_command {
    cl_mock_cmd_kind_t kind;
    cl_rect_t rect;
    cl_color_t color;
    float radius;
    float width;
    cl_point_t pos;
    char text[64];
    const cl_image_t *image; /* CL_MOCK_IMAGE: which image was drawn */
    cl_rect_t clip; /* effective clip in force when the command was recorded */
} cl_mock_command_t;

cl_renderer_t *cl_renderer_mock_create(const cl_allocator_t *a);
size_t cl_renderer_mock_count(cl_renderer_t *r);

/* Commands dropped this frame because the buffer filled up. Negative asserts
 * ("nothing drew X") are only meaningful when this is 0. */
size_t cl_renderer_mock_dropped(cl_renderer_t *r);
const cl_mock_command_t *cl_renderer_mock_get(cl_renderer_t *r, size_t i);
cl_color_t cl_renderer_mock_clear_color(cl_renderer_t *r);

/* Whether the last begun frame declared a damage region (set_damage), and
 * which one - false means a full redraw. */
bool cl_renderer_mock_frame_damaged(cl_renderer_t *r);
cl_rect_t cl_renderer_mock_frame_damage(cl_renderer_t *r);

#endif /* CL_RENDERER_MOCK_H */
