/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_PLATFORM_SDL_H
#define CL_PLATFORM_SDL_H

#include <copal/allocator.h>

#include "platform/platform.h"

/* Create the SDL2 platform backend (initialises SDL video). NULL on failure. */
cl_platform_t *cl_platform_sdl_create(const cl_allocator_t *a);

#endif /* CL_PLATFORM_SDL_H */
