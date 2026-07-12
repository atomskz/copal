/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/application.h>

#include <stdlib.h>
#include <string.h>

#include "app/app_internal.h"
#include "theme/theme_internal.h"
#include "core/foundation/foundation_internal.h"

#if defined(CL_ENABLE_SDL)
/* SDL software backend (no OpenGL dependency). */
cl_platform_t *cl_platform_sdl_soft_create(const cl_allocator_t *a);
cl_renderer_t *cl_renderer_soft_create(const cl_allocator_t *a, cl_platform_t *p);
#endif
#if defined(CL_ENABLE_OPENGL)
/* OpenGL window + renderer. */
cl_platform_t *cl_platform_sdl_create(const cl_allocator_t *a);
cl_renderer_t *cl_renderer_gl_create(const cl_allocator_t *a, cl_platform_t *p);
#endif

struct cl_task {
    cl_task_fn fn;
    void *user;
    cl_task_t *next;
};

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

#if defined(CL_ENABLE_SDL)
    {
        bool software = desc->render_backend == CL_RENDER_SOFTWARE;

#if !defined(CL_ENABLE_OPENGL)
        software = true; /* no OpenGL renderer compiled into this build */
#endif
        /* AUTO defaults to GL (when built) but honours a COPAL_RENDER=software
         * override, so software can be selected at runtime (e.g. over RDP). */
        if (desc->render_backend == CL_RENDER_AUTO) {
            const char *env = getenv("COPAL_RENDER");

            if (env && strcmp(env, "software") == 0)
                software = true;
        }
        if (software) {
            if (!app->platform)
                app->platform = cl_platform_sdl_soft_create(&app->alloc);
            /* The soft renderer needs a lockable CPU framebuffer; don't bind it
             * to an injected platform that cannot supply one (renderer stays
             * NULL -> CL_ERROR_UNSUPPORTED below). */
            if (!app->renderer && app->platform &&
                app->platform->ops->lock_framebuffer)
                app->renderer =
                    cl_renderer_soft_create(&app->alloc, app->platform);
        }
#if defined(CL_ENABLE_OPENGL)
        else {
            if (!app->platform)
                app->platform = cl_platform_sdl_create(&app->alloc);
            /* Mirror the software guard: the GL renderer resolves its entry
             * points through gl_get_proc, so an injected platform without one
             * would crash on the first frame (renderer stays NULL ->
             * CL_ERROR_UNSUPPORTED below). */
            if (!app->renderer && app->platform &&
                app->platform->ops->gl_get_proc)
                app->renderer =
                    cl_renderer_gl_create(&app->alloc, app->platform);
        }
#endif
    }
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

    app->task_mutex = cl_mutex_create(&app->alloc);
    if (!app->task_mutex) {
        cl_theme_free(app->theme);
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
    /* Destroy the window first: it cancels its own timers (e.g. the tooltip
     * dwell) via the live timer list before that list is torn down. */
    if (app->window)
        cl_window_destroy(app->window);
    cl_app_timers_free_all(app);
    /* Drop any tasks posted but never drained (they are not run at teardown). */
    if (app->task_mutex) {
        cl_task_t *t = app->task_head;

        while (t) {
            cl_task_t *next = t->next;

            cl_free(&app->alloc, t);
            t = next;
        }
        app->task_head = app->task_tail = NULL;
        cl_mutex_destroy(app->task_mutex);
        app->task_mutex = NULL;
    }
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
                /* The window's close callback may veto the request. */
                if (app->window && app->window->on_close &&
                    !app->window->on_close(app->window,
                                           app->window->on_close_user))
                    break;
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

            case CL_PEV_TEXT_EDIT:
                if (app->window)
                    cl_window_handle_text_edit(app->window, ev.text,
                                               ev.edit_cursor);
                break;

            default:
                break;
        }
    }
}

int cl_application_run(cl_application_t *app)
{
    while (!app->quit) {
        /* Block only until the next timer is due, so timers fire on time even
         * without input events. */
        app->platform->ops->wait(app->platform, cl_app_timers_timeout(app));
        process_events(app);
        cl_app_run_tasks(app);
        cl_app_timers_poll(app);
        if (app->window)
            cl_window_reap_overlay(app->window);
        if (app->quit)
            break;
        if (app->window && app->window->dirty)
            cl_window_render(app->window);
    }
    return app->exit_code;
}

bool cl_application_step(cl_application_t *app, bool wait)
{
    if (wait) {
        /* Bound the wait by the next timer, but never block indefinitely: a
         * single step must return to its caller (unlike run()'s own loop). */
        int timeout = cl_app_timers_timeout(app);

        app->platform->ops->wait(app->platform, timeout < 0 ? 0 : timeout);
    }
    process_events(app);
    cl_app_run_tasks(app);
    cl_app_timers_poll(app);
    if (app->window)
        cl_window_reap_overlay(app->window);
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
    cl_task_t *t;

    if (!app || !fn)
        return CL_ERROR_INVALID_ARGUMENT;
    if (!app->task_mutex)
        return CL_ERROR_UNSUPPORTED;
    t = cl_alloc(&app->alloc, sizeof(*t));
    if (!t)
        return CL_ERROR_OUT_OF_MEMORY;
    t->fn = fn;
    t->user = user;
    t->next = NULL;

    cl_mutex_lock(app->task_mutex);
    if (app->task_tail)
        app->task_tail->next = t;
    else
        app->task_head = t;
    app->task_tail = t;
    cl_mutex_unlock(app->task_mutex);

    /* Wake a blocked run loop so the task is drained promptly. */
    if (app->platform->ops->wakeup)
        app->platform->ops->wakeup(app->platform);
    return CL_OK;
}

void cl_app_run_tasks(cl_application_t *app)
{
    cl_task_t *t;

    if (!app->task_mutex)
        return;
    /* Detach the whole queue under the lock, then run tasks WITHOUT holding it
     * so a task may post more work (drained on the next pass) without deadlock. */
    cl_mutex_lock(app->task_mutex);
    t = app->task_head;
    app->task_head = NULL;
    app->task_tail = NULL;
    cl_mutex_unlock(app->task_mutex);

    while (t) {
        cl_task_t *next = t->next;

        t->fn(t->user);
        cl_free(&app->alloc, t);
        t = next;
    }
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

void cl_app_set_ime_rect(cl_application_t *app, cl_rect_t rect)
{
    if (app->platform->ops->set_ime_rect)
        app->platform->ops->set_ime_rect(app->platform, rect);
}

cl_theme_t *cl_application_theme(cl_application_t *app)
{
    return app->theme;
}

const cl_allocator_t *cl_application_allocator(cl_application_t *app)
{
    return &app->alloc;
}
