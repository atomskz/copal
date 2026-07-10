/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "render/gl/gl_loader.h"

bool cl_gl_load(struct gl_api *gl, cl_gl_get_proc_fn get, void *ctx)
{
    /*
     * Assign through a void ** to sidestep ISO C's object/function pointer
     * conversion rule (the standard dlsym/GetProcAddress idiom).
     */
#define CL_GL_LOAD(type, name)                     \
    do {                                           \
        void *sym = get(ctx, "gl" #name);          \
        if (!sym)                                  \
            return false;                          \
        *(void **)(&gl->name) = sym;               \
    } while (0);
    CL_GL_FUNCS(CL_GL_LOAD)
#undef CL_GL_LOAD
    return true;
}
