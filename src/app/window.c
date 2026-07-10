/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/window.h>
#include <copal/allocator.h>

#include <string.h>

#include "app/app_internal.h"
#include "widget/widget_internal.h"
#include "render/paint_context.h"
#include "core/foundation/foundation_internal.h"

cl_window_t *cl_window_create(cl_application_t *app, const cl_window_desc_t *desc)
{
    cl_window_t *win;
    cl_result_t r;

    if (!app)
        return NULL;
    if (!desc || desc->struct_size != sizeof(cl_window_desc_t) ||
        desc->abi_version != CL_VERSION) {
        cl_set_last_error(CL_ERROR_ABI_MISMATCH);
        return NULL;
    }
    if (app->window) {
        cl_set_last_error(CL_ERROR_UNSUPPORTED); /* single window in MVP */
        return NULL;
    }

    r = app->platform->ops->create_window(app->platform, desc);
    if (r != CL_OK) {
        cl_set_last_error(r);
        return NULL;
    }

    win = cl_alloc(&app->alloc, sizeof(*win));
    if (!win)
        return NULL;
    memset(win, 0, sizeof(*win));
    win->app = app;
    win->size.w = (float)desc->width;
    win->size.h = (float)desc->height;
    win->scale = app->platform->ops->scale(app->platform);
    if (win->scale <= 0.0f)
        win->scale = 1.0f;
    win->dirty = true;
    win->layout_dirty = true;
    app->window = win;
    return win;
}

void cl_window_destroy(cl_window_t *win)
{
    cl_application_t *app;

    if (!win)
        return;
    if (win->content)
        cl_widget_destroy(win->content);
    app = win->app;
    if (app->window == win)
        app->window = NULL;
    cl_free(&app->alloc, win);
}

void cl_window_show(cl_window_t *win)
{
    win->dirty = true;
}

void cl_window_set_content(cl_window_t *win, cl_widget_t *root)
{
    win->content = root;
    if (root)
        cl_widget_set_window(root, win);
    win->layout_dirty = true;
    win->dirty = true;
}

cl_widget_t *cl_window_content(cl_window_t *win)
{
    return win->content;
}

void cl_window_set_title(cl_window_t *win, const char *utf8)
{
    win->app->platform->ops->set_title(win->app->platform, utf8);
}

cl_size_t cl_window_size(cl_window_t *win)
{
    return win->size;
}

void cl_window_set_on_close(cl_window_t *win, cl_window_close_fn fn, void *user)
{
    win->on_close = fn;
    win->on_close_user = user;
}

void cl_window_mark_dirty(cl_window_t *win)
{
    win->dirty = true;
}

void cl_window_mark_layout_dirty(cl_window_t *win)
{
    win->layout_dirty = true;
    win->dirty = true;
}

void cl_window_resize(cl_window_t *win, cl_size_t size)
{
    win->size = size;
    win->layout_dirty = true;
    win->dirty = true;
}

void cl_window_render(cl_window_t *win)
{
    cl_application_t *app = win->app;
    struct cl_paint_context ctx;

    if (!win->content) {
        win->dirty = false;
        return;
    }

    if (win->layout_dirty) {
        cl_constraints_t c;

        c.min = (cl_size_t){ 0, 0 };
        c.max = win->size;
        cl_widget_do_measure(win->content, c);
        cl_widget_do_arrange(win->content,
                             (cl_rect_t){ 0, 0, win->size.w, win->size.h });
        win->layout_dirty = false;
    }

    app->renderer->ops->begin_frame(app->renderer, win->size, win->scale);
    ctx.renderer = app->renderer;
    ctx.theme = app->theme;
    cl_widget_do_paint(win->content, &ctx);
    app->renderer->ops->end_frame(app->renderer);
    app->platform->ops->present(app->platform);
    win->dirty = false;
}

void cl_window_handle_mouse(cl_window_t *win, cl_platform_event_kind_t kind,
                            cl_point_t pos, cl_mouse_button_t button)
{
    cl_event_t ev;
    cl_widget_t *target;

    if (!win->content)
        return;

    memset(&ev, 0, sizeof(ev));
    ev.mods = CL_MOD_NONE;
    ev.data.mouse.pos = pos;
    ev.data.mouse.button = button;

    if (kind == CL_PEV_MOUSE_DOWN) {
        ev.type = CL_EVENT_MOUSE_DOWN;
        target = cl_widget_hit(win->content, pos);
        win->mouse_target = target;
    } else if (kind == CL_PEV_MOUSE_UP) {
        ev.type = CL_EVENT_MOUSE_UP;
        target = win->mouse_target ? win->mouse_target
                                   : cl_widget_hit(win->content, pos);
        win->mouse_target = NULL;
    } else {
        ev.type = CL_EVENT_MOUSE_MOVE;
        target = win->mouse_target ? win->mouse_target
                                   : cl_widget_hit(win->content, pos);
    }

    if (target)
        cl_widget_dispatch(target, &ev);
}
