/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_RENDERER_GL_H
#define CL_RENDERER_GL_H

#include <copal/allocator.h>

#include "render/renderer.h"
#include "platform/platform.h"

/*
 * Create the OpenGL 3.3 renderer. The platform supplies the GL context and
 * proc loader; GL objects are created lazily on the first frame.
 */
cl_renderer_t *cl_renderer_gl_create(const cl_allocator_t *a, cl_platform_t *p);

/*
 * Test hook: mark the renderer so the next begin_frame treats the GL context as
 * reset and rebuilds its objects, exercising context-loss recovery without a
 * real GPU reset. Not part of the public API.
 */
void cl_renderer_gl_test_force_reset(cl_renderer_t *r);

#endif /* CL_RENDERER_GL_H */
