/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_RENDERER_SOFT_H
#define CL_RENDERER_SOFT_H

#include <copal/allocator.h>

#include "render/renderer.h"
#include "platform/platform.h"

/*
 * Create the CPU (software) renderer. It draws into the pixel buffer that the
 * platform `p` exposes via lock_framebuffer/unlock_framebuffer (a non-GL
 * backend); the platform's present() then blits it. NULL on failure.
 */
cl_renderer_t *cl_renderer_soft_create(const cl_allocator_t *a,
                                       cl_platform_t *p);

#endif /* CL_RENDERER_SOFT_H */
