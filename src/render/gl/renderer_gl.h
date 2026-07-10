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

#endif /* CL_RENDERER_GL_H */
