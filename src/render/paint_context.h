/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_PAINT_CONTEXT_INTERNAL_H
#define CL_PAINT_CONTEXT_INTERNAL_H

#include <copal/render.h>

#include "render/renderer.h"

/* Concrete paint context: forwards drawing to the renderer, carries the theme. */
struct cl_paint_context {
    cl_renderer_t *renderer;
    cl_theme_t *theme;
};

#endif /* CL_PAINT_CONTEXT_INTERNAL_H */
