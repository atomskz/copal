/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/application.h>

#include <stdlib.h>
#include <string.h>

#include "app/app_internal.h"
#include "theme/theme_internal.h"
#include "widget/widget_internal.h"
#include "core/foundation/foundation_internal.h"

#if defined(CL_ENABLE_SDL)
#include "platform/sdl/platform_sdl.h"
#include "render/soft/renderer_soft.h"
#endif
#if defined(CL_ENABLE_OPENGL)
#include "render/gl/renderer_gl.h"
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

    cl_application_desc_t norm;

    if (!desc) {
        cl_set_last_error(CL_ERROR_ABI_MISMATCH);
        return NULL;
    }
    if (!cl_abi_ok(desc->abi_version, desc->struct_size, CL_DESC_MIN_SIZE))
        return NULL;
    cl_desc_fill(&norm, sizeof norm, desc, desc->struct_size);
    desc = &norm;

    /* Injected backends carry an ops-level handshake (copal/backend/): refuse a
     * table shaped by different headers before calling through it. An ops table
     * must carry the whole baseline (a missing op cannot be called), so a
     * shorter table is rejected; a longer one from a newer backend is fine. */
    if ((desc->platform &&
         !cl_abi_ok(desc->platform->ops->abi_version,
                    desc->platform->ops->struct_size,
                    sizeof(cl_platform_ops_t))) ||
        (desc->renderer &&
         !cl_abi_ok(desc->renderer->ops->abi_version,
                    desc->renderer->ops->struct_size,
                    sizeof(cl_renderer_ops_t)))) {
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

            if (env && cl_strcmp(env, "software") == 0)
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
            /* AUTO with both backends built in may retry a failed GL window
             * in software (cl_app_software_fallback); an explicit GL request
             * or injected backends must fail loudly instead. */
            app->soft_fallback_ok = desc->render_backend == CL_RENDER_AUTO &&
                                    !desc->platform && !desc->renderer;
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
        cl_log(CL_LOG_ERROR,
               "application: no usable %s backend; build with COPAL_ENABLE_SDL "
               "(+COPAL_ENABLE_OPENGL for GL) or inject one via "
               "cl_application_desc_t",
               !app->platform ? "platform" : "renderer");
        cl_set_last_error(CL_ERROR_UNSUPPORTED);
        goto fail;
    }

    app->theme = cl_theme_default(app);
    if (!app->theme)
        goto fail;

    /* Cross-thread task queue mutex: an injected iface wins; otherwise the
     * hosted built-in (NULL on freestanding, where cl_application_post is then
     * unsupported unless the embedder injects one). */
    if (desc->mutex && desc->mutex->create) {
        app->mtx = *desc->mutex;
    } else {
        cl_mutex_builtin_iface(&app->mtx);
        app->mtx.user = &app->alloc;
    }
    if (app->mtx.create) {
        app->task_mutex = app->mtx.create(app->mtx.user);
        if (!app->task_mutex)
            goto fail;
    }
    return app;

fail:
    /* Unwind in reverse creation order. Backends the library created are
     * destroyed; injected ones stay with the caller (application.h). */
    if (app->theme)
        cl_theme_free(app->theme);
    if (app->renderer && app->renderer != desc->renderer &&
        app->renderer->ops->destroy)
        app->renderer->ops->destroy(app->renderer);
    if (app->platform && app->platform != desc->platform &&
        app->platform->ops->destroy)
        app->platform->ops->destroy(app->platform);
    cl_free(a, app);
    return NULL;
}

bool cl_app_software_fallback(cl_application_t *app)
{
#if defined(CL_ENABLE_SDL)
    cl_platform_t *plat;
    cl_renderer_t *rend;

    if (!app->soft_fallback_ok)
        return false;
    app->soft_fallback_ok = false; /* one shot */
    plat = cl_platform_sdl_soft_create(&app->alloc);
    if (!plat)
        return false;
    rend = cl_renderer_soft_create(&app->alloc, plat);
    if (!rend) {
        plat->ops->destroy(plat);
        return false;
    }
    /* Swap out the GL pair (created by us: soft_fallback_ok excludes DI). */
    if (app->renderer && app->renderer->ops->destroy)
        app->renderer->ops->destroy(app->renderer);
    if (app->platform && app->platform->ops->destroy)
        app->platform->ops->destroy(app->platform);
    app->platform = plat;
    app->renderer = rend;
    cl_log(CL_LOG_WARN,
           "application: OpenGL window failed; falling back to the software "
           "renderer");
    return true;
#else
    (void)app;
    return false;
#endif
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
    cl_app_reap_dead(app); /* free anything still on the deferred queue */
    cl_app_animations_free_all(app); /* before the timer list: cancels none */
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
        if (app->mtx.destroy)
            app->mtx.destroy(app->mtx.user, app->task_mutex);
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

            case CL_PEV_EXPOSE:
                if (app->window)
                    cl_window_mark_dirty(app->window);
                break;

            case CL_PEV_MOUSE_DOWN:
            case CL_PEV_MOUSE_UP:
            case CL_PEV_MOUSE_MOVE:
                if (app->window)
                    cl_window_handle_mouse(app->window, ev.kind, ev.pos,
                                           ev.button, ev.mods, ev.clicks);
                break;

            case CL_PEV_MOUSE_WHEEL:
                if (app->window)
                    cl_window_handle_wheel(app->window, ev.pos, ev.wheel_x,
                                           ev.wheel_y, ev.mods);
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
        cl_app_reap_dead(app); /* free widgets destroyed this iteration */
        if (app->quit)
            break;
        if (app->window && app->window->dirty)
            cl_window_render(app->window);
    }
    return app->exit_code;
}

void cl_app_defer_widget_free(cl_application_t *app, cl_widget_t *w)
{
    /* LIFO; order does not matter, the subtrees are disjoint and detached */
    w->next_sibling = app->dead;
    app->dead = w;
}

void cl_app_reap_dead(cl_application_t *app)
{
    /* Freeing may enqueue more (a widget destroy callback destroying another
     * detached tree), so drain until empty. */
    while (app->dead) {
        cl_widget_t *w = app->dead;

        app->dead = w->next_sibling;
        w->next_sibling = NULL;
        cl_widget_free_subtree(w);
    }
}

bool cl_application_step(cl_application_t *app, bool wait)
{
    if (wait) {
        /* Bound the wait by the next timer, but never block indefinitely: a
         * single step must return to its caller (unlike run()'s own loop).
         * With no timer armed, wait a bounded slice instead of 0 - a plain
         * while (step(app, true)) embedding loop must not spin a core. */
        int timeout = cl_app_timers_timeout(app);

        app->platform->ops->wait(app->platform, timeout < 0 ? 100 : timeout);
    }
    process_events(app);
    cl_app_run_tasks(app);
    cl_app_timers_poll(app);
    if (app->window)
        cl_window_reap_overlay(app->window);
    cl_app_reap_dead(app); /* free widgets destroyed during this iteration */
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

    app->mtx.lock(app->mtx.user, app->task_mutex);
    if (app->task_tail)
        app->task_tail->next = t;
    else
        app->task_head = t;
    app->task_tail = t;
    app->mtx.unlock(app->mtx.user, app->task_mutex);

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
    app->mtx.lock(app->mtx.user, app->task_mutex);
    t = app->task_head;
    app->task_head = NULL;
    app->task_tail = NULL;
    app->mtx.unlock(app->mtx.user, app->task_mutex);

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
        app->platform->ops->set_ime_rect(app->platform,
                                         app->window ? app->window->native
                                                     : NULL,
                                         rect);
}

cl_theme_t *cl_application_theme(cl_application_t *app)
{
    return app->theme;
}

const cl_allocator_t *cl_application_allocator(cl_application_t *app)
{
    return &app->alloc;
}
