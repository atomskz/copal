/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/application.h>

#include <string.h>

#include "app/app_internal.h"
#include "theme/theme_internal.h"
#include "core/foundation/foundation_internal.h"

#if defined(CL_ENABLE_SDL) && defined(CL_ENABLE_OPENGL)
/* Built-in native backends (provided by the SDL/GL backend TUs). */
cl_platform_t *cl_platform_sdl_create(const cl_allocator_t *a);
cl_renderer_t *cl_renderer_gl_create(const cl_allocator_t *a, cl_platform_t *p);
#endif

cl_application_t *cl_application_create(const cl_application_desc_t *desc)
{
    const cl_allocator_t *a;
    cl_application_t *app;

    if (!desc || desc->struct_size != sizeof(cl_application_desc_t) ||
        desc->abi_version != CL_VERSION) {
        cl_set_last_error(CL_ERROR_ABI_MISMATCH);
        return NULL;
    }

    a = desc->allocator ? desc->allocator : cl_allocator_default();
    app = cl_alloc(a, sizeof(*app));
    if (!app)
        return NULL;
    memset(app, 0, sizeof(*app));
    app->alloc = *a;
    app->platform = desc->platform;
    app->renderer = desc->renderer;
    app->log_fn = desc->log_fn;
    app->log_user = desc->log_user;

#if defined(CL_ENABLE_SDL) && defined(CL_ENABLE_OPENGL)
    if (!app->platform)
        app->platform = cl_platform_sdl_create(&app->alloc);
    if (!app->renderer && app->platform)
        app->renderer = cl_renderer_gl_create(&app->alloc, app->platform);
#endif

    if (!app->platform || !app->renderer) {
        cl_set_last_error(CL_ERROR_UNSUPPORTED);
        cl_free(a, app);
        return NULL;
    }

    app->theme = cl_theme_default(app);
    if (!app->theme) {
        cl_free(a, app);
        return NULL;
    }
    return app;
}

void cl_application_destroy(cl_application_t *app)
{
    cl_allocator_t a;

    if (!app)
        return;
    if (app->window)
        cl_window_destroy(app->window);
    if (app->theme)
        cl_theme_free(app->theme);
    if (app->renderer && app->renderer->ops->destroy)
        app->renderer->ops->destroy(app->renderer);
    if (app->platform && app->platform->ops->destroy)
        app->platform->ops->destroy(app->platform);
    a = app->alloc;
    cl_free(&a, app);
}

static void process_events(cl_application_t *app)
{
    cl_platform_event_t ev;

    while (app->platform->ops->poll(app->platform, &ev)) {
        switch (ev.kind) {
            case CL_PEV_QUIT:
                app->quit = true;
                break;

            case CL_PEV_RESIZE:
                if (app->window)
                    cl_window_resize(app->window, ev.size);
                break;

            case CL_PEV_MOUSE_DOWN:
            case CL_PEV_MOUSE_UP:
            case CL_PEV_MOUSE_MOVE:
                if (app->window)
                    cl_window_handle_mouse(app->window, ev.kind, ev.pos,
                                           ev.button);
                break;

            case CL_PEV_MOUSE_WHEEL:
                if (app->window)
                    cl_window_handle_wheel(app->window, ev.pos, ev.wheel_x,
                                           ev.wheel_y);
                break;

            case CL_PEV_KEY_DOWN:
            case CL_PEV_KEY_UP:
                if (app->window)
                    cl_window_handle_key(app->window, ev.kind, ev.key, ev.mods);
                break;

            case CL_PEV_TEXT_INPUT:
                if (app->window)
                    cl_window_handle_text(app->window, ev.text);
                break;

            default:
                break;
        }
    }
}

int cl_application_run(cl_application_t *app)
{
    while (!app->quit) {
        app->platform->ops->wait(app->platform, -1);
        process_events(app);
        if (app->quit)
            break;
        if (app->window && app->window->dirty)
            cl_window_render(app->window);
    }
    return app->exit_code;
}

bool cl_application_step(cl_application_t *app, bool wait)
{
    if (wait)
        app->platform->ops->wait(app->platform, 0);
    process_events(app);
    if (app->window && app->window->dirty)
        cl_window_render(app->window);
    return !app->quit;
}

void cl_application_quit(cl_application_t *app, int exit_code)
{
    app->quit = true;
    app->exit_code = exit_code;
    if (app->platform)
        app->platform->ops->wakeup(app->platform);
}

cl_result_t cl_application_post(cl_application_t *app, cl_task_fn fn, void *user)
{
    (void)app;
    (void)fn;
    (void)user;
    /* Cross-thread task queue lands in Stage 7 (threading). */
    return CL_ERROR_UNSUPPORTED;
}

char *cl_app_clipboard_get(cl_application_t *app)
{
    if (!app->platform->ops->clipboard_get)
        return NULL;
    return app->platform->ops->clipboard_get(app->platform, &app->alloc);
}

void cl_app_clipboard_set(cl_application_t *app, const char *utf8)
{
    if (app->platform->ops->clipboard_set)
        app->platform->ops->clipboard_set(app->platform, utf8);
}

cl_theme_t *cl_application_theme(cl_application_t *app)
{
    return app->theme;
}

const cl_allocator_t *cl_application_allocator(cl_application_t *app)
{
    return &app->alloc;
}
