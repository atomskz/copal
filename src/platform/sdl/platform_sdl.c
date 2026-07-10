/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform/sdl/platform_sdl.h"

#include <string.h>

#include <SDL.h>

typedef struct sdl_platform {
    cl_platform_t base;
    const cl_allocator_t *a;
    SDL_Window *window;
    SDL_GLContext glctx;
    cl_size_t size; /* logical px */
    float scale;
} sdl_platform_t;

static cl_mouse_button_t map_button(Uint8 b)
{
    switch (b) {
        case SDL_BUTTON_RIGHT:
            return CL_MOUSE_RIGHT;

        case SDL_BUTTON_MIDDLE:
            return CL_MOUSE_MIDDLE;

        default:
            return CL_MOUSE_LEFT;
    }
}

static cl_key_t map_key(SDL_Keycode k)
{
    if (k >= SDLK_a && k <= SDLK_z)
        return (cl_key_t)(CL_KEY_A + (k - SDLK_a));

    switch (k) {
        case SDLK_LEFT:      return CL_KEY_LEFT;
        case SDLK_RIGHT:     return CL_KEY_RIGHT;
        case SDLK_UP:        return CL_KEY_UP;
        case SDLK_DOWN:      return CL_KEY_DOWN;
        case SDLK_HOME:      return CL_KEY_HOME;
        case SDLK_END:       return CL_KEY_END;
        case SDLK_BACKSPACE: return CL_KEY_BACKSPACE;
        case SDLK_DELETE:    return CL_KEY_DELETE;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  return CL_KEY_ENTER;
        case SDLK_TAB:       return CL_KEY_TAB;
        case SDLK_ESCAPE:    return CL_KEY_ESCAPE;

        default:             return CL_KEY_UNKNOWN;
    }
}

static cl_key_mods_t map_mods(Uint16 m)
{
    unsigned mods = CL_MOD_NONE;

    if (m & KMOD_SHIFT)
        mods |= CL_MOD_SHIFT;
    if (m & KMOD_CTRL)
        mods |= CL_MOD_CTRL;
    if (m & KMOD_ALT)
        mods |= CL_MOD_ALT;
    if (m & KMOD_GUI)
        mods |= CL_MOD_SUPER;
    return (cl_key_mods_t)mods;
}

static cl_result_t sdl_create_window(cl_platform_t *p,
                                     const cl_window_desc_t *desc)
{
    sdl_platform_t *s = (sdl_platform_t *)p;
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
    int w = desc->width > 0 ? desc->width : 640;
    int h = desc->height > 0 ? desc->height : 480;
    int dw = w;
    int dh = h;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    if (desc->resizable)
        flags |= SDL_WINDOW_RESIZABLE;

    s->window = SDL_CreateWindow(desc->title ? desc->title : "copal",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 w, h, flags);
    if (!s->window)
        return CL_ERROR_PLATFORM;

    s->glctx = SDL_GL_CreateContext(s->window);
    if (!s->glctx) {
        SDL_DestroyWindow(s->window);
        s->window = NULL;
        return CL_ERROR_PLATFORM;
    }
    SDL_GL_MakeCurrent(s->window, s->glctx);
    SDL_GL_SetSwapInterval(1);

    s->size.w = (float)w;
    s->size.h = (float)h;
    SDL_GL_GetDrawableSize(s->window, &dw, &dh);
    s->scale = w > 0 ? (float)dw / (float)w : 1.0f;
    return CL_OK;
}

static void sdl_set_title(cl_platform_t *p, const char *utf8)
{
    sdl_platform_t *s = (sdl_platform_t *)p;

    if (s->window)
        SDL_SetWindowTitle(s->window, utf8 ? utf8 : "");
}

static cl_size_t sdl_drawable_size(cl_platform_t *p)
{
    return ((sdl_platform_t *)p)->size;
}

static float sdl_scale(cl_platform_t *p)
{
    return ((sdl_platform_t *)p)->scale;
}

static bool sdl_poll(cl_platform_t *p, cl_platform_event_t *out)
{
    sdl_platform_t *s = (sdl_platform_t *)p;
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                out->kind = CL_PEV_QUIT;
                return true;

            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    out->kind = CL_PEV_RESIZE;
                    out->size.w = (float)e.window.data1;
                    out->size.h = (float)e.window.data2;
                    s->size = out->size;
                    return true;
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                out->kind = e.type == SDL_MOUSEBUTTONDOWN ? CL_PEV_MOUSE_DOWN
                                                          : CL_PEV_MOUSE_UP;
                out->pos.x = (float)e.button.x;
                out->pos.y = (float)e.button.y;
                out->button = map_button(e.button.button);
                return true;

            case SDL_MOUSEMOTION:
                out->kind = CL_PEV_MOUSE_MOVE;
                out->pos.x = (float)e.motion.x;
                out->pos.y = (float)e.motion.y;
                return true;

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                out->kind = e.type == SDL_KEYDOWN ? CL_PEV_KEY_DOWN
                                                  : CL_PEV_KEY_UP;
                out->key = map_key(e.key.keysym.sym);
                out->mods = map_mods(e.key.keysym.mod);
                return true;

            case SDL_TEXTINPUT:
                out->kind = CL_PEV_TEXT_INPUT;
                memcpy(out->text, e.text.text, sizeof(out->text));
                out->text[sizeof(out->text) - 1] = '\0';
                return true;

            default:
                break;
        }
    }
    return false;
}

static void sdl_wait(cl_platform_t *p, int timeout_ms)
{
    SDL_Event e;

    (void)p;
    if (timeout_ms < 0) {
        if (SDL_WaitEvent(&e))
            SDL_PushEvent(&e);
    } else if (SDL_WaitEventTimeout(&e, timeout_ms)) {
        SDL_PushEvent(&e);
    }
}

static void sdl_present(cl_platform_t *p)
{
    sdl_platform_t *s = (sdl_platform_t *)p;

    if (s->window)
        SDL_GL_SwapWindow(s->window);
}

static void sdl_wakeup(cl_platform_t *p)
{
    SDL_Event e;

    (void)p;
    memset(&e, 0, sizeof(e));
    e.type = SDL_USEREVENT;
    SDL_PushEvent(&e);
}

static void sdl_start_text_input(cl_platform_t *p, bool enable)
{
    (void)p;
    if (enable)
        SDL_StartTextInput();
    else
        SDL_StopTextInput();
}

static void *sdl_gl_get_proc(cl_platform_t *p, const char *name)
{
    (void)p;
    return SDL_GL_GetProcAddress(name);
}

static void sdl_destroy(cl_platform_t *p)
{
    sdl_platform_t *s = (sdl_platform_t *)p;

    if (s->glctx)
        SDL_GL_DeleteContext(s->glctx);
    if (s->window)
        SDL_DestroyWindow(s->window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_Quit();
    cl_free(s->a, s);
}

static const cl_platform_ops_t sdl_ops = {
    .create_window = sdl_create_window,
    .set_title = sdl_set_title,
    .drawable_size = sdl_drawable_size,
    .scale = sdl_scale,
    .poll = sdl_poll,
    .wait = sdl_wait,
    .present = sdl_present,
    .wakeup = sdl_wakeup,
    .start_text_input = sdl_start_text_input,
    .destroy = sdl_destroy,
    .gl_get_proc = sdl_gl_get_proc,
};

cl_platform_t *cl_platform_sdl_create(const cl_allocator_t *a)
{
    sdl_platform_t *s;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        return NULL;

    s = cl_alloc(a, sizeof(*s));
    if (!s) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    s->base.ops = &sdl_ops;
    s->a = a;
    s->scale = 1.0f;
    return &s->base;
}
