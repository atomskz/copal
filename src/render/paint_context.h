/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_PAINT_CONTEXT_INTERNAL_H
#define CL_PAINT_CONTEXT_INTERNAL_H

#include <copal/render.h>

#include "render/renderer.h"

/* Concrete paint context: forwards drawing to the renderer, carries the theme. */
struct cl_paint_context {
    cl_renderer_t *renderer;
    cl_theme_t *theme;
    /*
     * Damage-region cull (partial redraw). When cull_on, a widget whose rect
     * does not touch `cull` skips its own paint, and a clipping container whose
     * rect is off-region skips its whole subtree (its children are bounded by
     * its clip). Off for a full frame.
     */
    cl_rect_t cull;
    bool cull_on;
};

#endif /* CL_PAINT_CONTEXT_INTERNAL_H */
