/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform/sdl/platform_sdl.h"
#include "core/foundation/foundation_internal.h"

#include <string.h>

/*
 * copal wraps SDL as a library: the consuming application owns main(), so tell
 * SDL not to redefine it (SDL_main) and mark main as ready ourselves. This also
 * avoids needing to link SDL2main on Windows.
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>

typedef struct sdl_platform {
    cl_platform_t base;
    const cl_allocator_t *a;
    SDL_Window *window;
    SDL_GLContext glctx;
    SDL_Surface *surface; /* window surface locked for software drawing */
    cl_size_t size;       /* logical px */
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
        case SDLK_SPACE:     return CL_KEY_SPACE;

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

    if (desc->min_width > 0 || desc->min_height > 0)
        SDL_SetWindowMinimumSize(s->window,
                                 desc->min_width > 0 ? desc->min_width : 1,
                                 desc->min_height > 0 ? desc->min_height : 1);

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
                out->clicks = e.button.clicks;
                out->mods = map_mods(SDL_GetModState());
                return true;

            case SDL_MOUSEMOTION:
                out->kind = CL_PEV_MOUSE_MOVE;
                out->pos.x = (float)e.motion.x;
                out->pos.y = (float)e.motion.y;
                out->mods = map_mods(SDL_GetModState());
                return true;

            case SDL_MOUSEWHEEL: {
                int mx = 0;
                int my = 0;
                float dir = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED
                                ? -1.0f
                                : 1.0f;

                SDL_GetMouseState(&mx, &my);
                out->kind = CL_PEV_MOUSE_WHEEL;
                out->pos.x = (float)mx;
                out->pos.y = (float)my;
                out->wheel_x = (float)e.wheel.x * dir;
                out->wheel_y = (float)e.wheel.y * dir;
                out->mods = map_mods(SDL_GetModState());
                return true;
            }

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

            case SDL_TEXTEDITING:
                out->kind = CL_PEV_TEXT_EDIT;
                memcpy(out->text, e.edit.text, sizeof(out->text));
                out->text[sizeof(out->text) - 1] = '\0';
                out->edit_cursor = e.edit.start;
                return true;

            default:
                break;
        }
    }
    return false;
}

static void sdl_wait(cl_platform_t *p, int timeout_ms)
{
    (void)p;
    /*
     * Block until an event is available (or the timeout elapses) WITHOUT
     * dequeuing it; process_events() then drains the queue with SDL_PollEvent.
     *
     * The previous SDL_WaitEvent(&e) + SDL_PushEvent(&e) dequeued the woken
     * event only to re-queue it. Under a steady event stream (e.g. the platform
     * delivering window/enter events) the queue was therefore never empty when
     * the loop returned to wait, so SDL_WaitEvent returned immediately every
     * iteration and the loop spun at 100% of one core instead of sleeping.
     */
    if (timeout_ms < 0)
        SDL_WaitEvent(NULL);
    else
        SDL_WaitEventTimeout(NULL, timeout_ms);
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

static void sdl_set_ime_rect(cl_platform_t *p, cl_rect_t rect)
{
    SDL_Rect r;

    (void)p;
    r.x = (int)rect.x;
    r.y = (int)rect.y;
    r.w = (int)rect.w;
    r.h = (int)rect.h;
    SDL_SetTextInputRect(&r);
}

static char *sdl_clipboard_get(cl_platform_t *p, const cl_allocator_t *a)
{
    char *sdl_text;
    char *out;
    size_t n;

    (void)p;
    if (!SDL_HasClipboardText())
        return NULL;
    sdl_text = SDL_GetClipboardText(); /* SDL-allocated, must SDL_free */
    if (!sdl_text)
        return NULL;
    if (sdl_text[0] == '\0') {
        SDL_free(sdl_text);
        return NULL;
    }
    n = strlen(sdl_text) + 1;
    out = cl_alloc(a, n);
    if (out)
        memcpy(out, sdl_text, n);
    SDL_free(sdl_text);
    return out;
}

static void sdl_clipboard_set(cl_platform_t *p, const char *utf8)
{
    (void)p;
    SDL_SetClipboardText(utf8 ? utf8 : "");
}

static void *sdl_gl_get_proc(cl_platform_t *p, const char *name)
{
    (void)p;
    return SDL_GL_GetProcAddress(name);
}

static uint64_t sdl_now_ms(cl_platform_t *p)
{
    (void)p;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    return SDL_GetTicks64();
#else
    return SDL_GetTicks(); /* 32-bit; wraps after ~49 days */
#endif
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
    .set_ime_rect = sdl_set_ime_rect,
    .clipboard_get = sdl_clipboard_get,
    .clipboard_set = sdl_clipboard_set,
    .destroy = sdl_destroy,
    .gl_get_proc = sdl_gl_get_proc,
    .now_ms = sdl_now_ms,
};

cl_platform_t *cl_platform_sdl_create(const cl_allocator_t *a)
{
    sdl_platform_t *s;

    SDL_SetMainReady(); /* we defined SDL_MAIN_HANDLED; app owns main() */
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

/* ---- software backend (no OpenGL; draws into the window's surface) ------- */

static cl_result_t sdl_create_window_soft(cl_platform_t *p,
                                          const cl_window_desc_t *desc)
{
    sdl_platform_t *s = (sdl_platform_t *)p;
    Uint32 flags = 0;
    int w = desc->width > 0 ? desc->width : 640;
    int h = desc->height > 0 ? desc->height : 480;

    if (desc->resizable)
        flags |= SDL_WINDOW_RESIZABLE;

    s->window = SDL_CreateWindow(desc->title ? desc->title : "copal",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 w, h, flags);
    if (!s->window)
        return CL_ERROR_PLATFORM;

    if (desc->min_width > 0 || desc->min_height > 0)
        SDL_SetWindowMinimumSize(s->window,
                                 desc->min_width > 0 ? desc->min_width : 1,
                                 desc->min_height > 0 ? desc->min_height : 1);

    /* The window surface is at logical size (no HighDPI in the MVP soft path). */
    s->size.w = (float)w;
    s->size.h = (float)h;
    s->scale = 1.0f;
    return CL_OK;
}

static void sdl_present_soft(cl_platform_t *p)
{
    sdl_platform_t *s = (sdl_platform_t *)p;

    if (s->window)
        SDL_UpdateWindowSurface(s->window);
}

static bool sdl_lock_framebuffer(cl_platform_t *p, cl_pixmap_t *out)
{
    sdl_platform_t *s = (sdl_platform_t *)p;
    SDL_Surface *surf;

    if (!s->window)
        return false;
    surf = SDL_GetWindowSurface(s->window); /* re-created on resize */
    if (!surf)
        return false;
    if (surf->format->BytesPerPixel != 4) {
        static bool warned;

        if (!warned) {
            warned = true;
            cl_log(CL_LOG_ERROR,
                   "software backend needs a 32-bit window surface "
                   "(got %d-byte); nothing will be drawn",
                   surf->format->BytesPerPixel);
        }
        return false;
    }
    if (SDL_MUSTLOCK(surf) && SDL_LockSurface(surf) != 0)
        return false;
    s->surface = surf;
    out->pixels = surf->pixels;
    out->w = surf->w;
    out->h = surf->h;
    out->pitch = surf->pitch;
    out->r_mask = surf->format->Rmask;
    out->g_mask = surf->format->Gmask;
    out->b_mask = surf->format->Bmask;
    out->a_mask = surf->format->Amask;
    return true;
}

static void sdl_unlock_framebuffer(cl_platform_t *p)
{
    sdl_platform_t *s = (sdl_platform_t *)p;

    if (s->surface) {
        if (SDL_MUSTLOCK(s->surface))
            SDL_UnlockSurface(s->surface);
        s->surface = NULL;
    }
}

static void sdl_destroy_soft(cl_platform_t *p)
{
    sdl_platform_t *s = (sdl_platform_t *)p;

    if (s->window)
        SDL_DestroyWindow(s->window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_Quit();
    cl_free(s->a, s);
}

static const cl_platform_ops_t sdl_ops_soft = {
    .create_window = sdl_create_window_soft,
    .set_title = sdl_set_title,
    .drawable_size = sdl_drawable_size,
    .scale = sdl_scale,
    .poll = sdl_poll,
    .wait = sdl_wait,
    .present = sdl_present_soft,
    .wakeup = sdl_wakeup,
    .start_text_input = sdl_start_text_input,
    .set_ime_rect = sdl_set_ime_rect,
    .clipboard_get = sdl_clipboard_get,
    .clipboard_set = sdl_clipboard_set,
    .destroy = sdl_destroy_soft,
    .gl_get_proc = NULL,
    .now_ms = sdl_now_ms,
    .lock_framebuffer = sdl_lock_framebuffer,
    .unlock_framebuffer = sdl_unlock_framebuffer,
};

cl_platform_t *cl_platform_sdl_soft_create(const cl_allocator_t *a)
{
    sdl_platform_t *s;

    SDL_SetMainReady();
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        return NULL;

    s = cl_alloc(a, sizeof(*s));
    if (!s) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    s->base.ops = &sdl_ops_soft;
    s->a = a;
    s->scale = 1.0f;
    return &s->base;
}
