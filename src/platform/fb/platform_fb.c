/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/platform/fb.h>

#include "platform/platform.h"
#include "core/foundation/foundation_internal.h"

#include <string.h>

#define CL_FB_QUEUE 64

typedef struct fb_platform {
    cl_platform_t base;
    const cl_allocator_t *a;
    cl_pixmap_t fb; /* the caller's target; the pixel memory is borrowed */
    cl_platform_event_t queue[CL_FB_QUEUE];
    size_t head;
    size_t tail;
    size_t dropped; /* events lost to a full queue */
} fb_platform_t;

static cl_result_t fb_create_window(cl_platform_t *p,
                                    const cl_window_desc_t *desc,
                                    cl_platform_window_t **out)
{
    (void)desc; /* the framebuffer's own size wins over the requested size */
    /* Single-window backend: the platform itself stands in for the handle. */
    *out = (cl_platform_window_t *)p;
    return CL_OK;
}

static cl_size_t fb_drawable_size(cl_platform_t *p, cl_platform_window_t *win)
{
    fb_platform_t *f = (fb_platform_t *)p;

    (void)win;
    /* Scale is 1.0, so the logical size equals the physical framebuffer. */
    return (cl_size_t){ (float)f->fb.w, (float)f->fb.h };
}

static float fb_scale(cl_platform_t *p, cl_platform_window_t *win)
{
    (void)p;
    (void)win;
    return 1.0f;
}

static bool fb_poll(cl_platform_t *p, cl_platform_event_t *out)
{
    fb_platform_t *f = (fb_platform_t *)p;

    if (f->head == f->tail)
        return false;
    *out = f->queue[f->head];
    f->head = (f->head + 1) % CL_FB_QUEUE;
    return true;
}

static void fb_wait(cl_platform_t *p, int timeout_ms)
{
    (void)p;
    (void)timeout_ms; /* no OS event source: the embedder drives via step() */
}

static void fb_present(cl_platform_t *p, cl_platform_window_t *win)
{
    (void)p;
    (void)win; /* the renderer already wrote into the caller's buffer */
}

static bool fb_lock_framebuffer(cl_platform_t *p, cl_platform_window_t *win,
                                cl_pixmap_t *out)
{
    fb_platform_t *f = (fb_platform_t *)p;

    (void)win; /* NULL selects the only window (see backend/platform.h) */
    *out = f->fb;
    return true;
}

static void fb_unlock_framebuffer(cl_platform_t *p, cl_platform_window_t *win)
{
    (void)p;
    (void)win; /* nothing to flush: the buffer belongs to the caller */
}

static void fb_destroy(cl_platform_t *p)
{
    fb_platform_t *f = (fb_platform_t *)p;

    cl_free(f->a, f);
}

static const cl_platform_ops_t fb_ops = {
    .struct_size = sizeof(cl_platform_ops_t),
    .abi_version = COPAL_VERSION,
    .create_window = fb_create_window,
    .drawable_size = fb_drawable_size,
    .scale = fb_scale,
    .poll = fb_poll,
    .wait = fb_wait,
    .present = fb_present,
    .destroy = fb_destroy,
    .lock_framebuffer = fb_lock_framebuffer,
    .unlock_framebuffer = fb_unlock_framebuffer,
    /* set_title, start_text_input, set_cursor, set_ime_rect, present_region,
     * wakeup, clipboard_*, destroy_window, gl_get_proc, now_ms = NULL: this is
     * the minimal software backend, and every one of those is optional or (for
     * now_ms) conditional. */
};

cl_platform_t *cl_platform_fb_create(const cl_allocator_t *a,
                                     const cl_pixmap_t *fb)
{
    fb_platform_t *f;

    if (!fb || !fb->pixels || fb->w <= 0 || fb->h <= 0 ||
        fb->pitch < fb->w * 4) {
        cl_log(CL_LOG_ERROR, "cl_platform_fb_create: framebuffer must have "
                             "non-NULL pixels, w/h > 0 and pitch >= w*4");
        cl_set_last_error(CL_ERROR_INVALID_ARGUMENT);
        return NULL;
    }
    f = cl_alloc(a, sizeof(*f));
    if (!f)
        return NULL; /* cl_alloc recorded CL_ERROR_OUT_OF_MEMORY */
    memset(f, 0, sizeof(*f));
    f->base.ops = &fb_ops;
    f->a = a;
    f->fb = *fb;
    return &f->base;
}

void cl_platform_fb_push_event(cl_platform_t *p, cl_platform_event_t ev)
{
    fb_platform_t *f = (fb_platform_t *)p;
    size_t next = (f->tail + 1) % CL_FB_QUEUE;

    if (next == f->head) {
        f->dropped++; /* the caller pushed more than the queue holds */
        return;
    }
    f->queue[f->tail] = ev;
    f->tail = next;
}

bool cl_pixmap_set_format(cl_pixmap_t *fb, cl_pixel_format_t fmt)
{
    if (!fb)
        return false;
    switch (fmt) {
        case CL_PIXEL_ARGB8888:
            fb->r_mask = 0x00FF0000u;
            fb->g_mask = 0x0000FF00u;
            fb->b_mask = 0x000000FFu;
            fb->a_mask = 0xFF000000u;
            return true;

        case CL_PIXEL_XRGB8888:
            fb->r_mask = 0x00FF0000u;
            fb->g_mask = 0x0000FF00u;
            fb->b_mask = 0x000000FFu;
            fb->a_mask = 0u;
            return true;

        case CL_PIXEL_ABGR8888:
            fb->r_mask = 0x000000FFu;
            fb->g_mask = 0x0000FF00u;
            fb->b_mask = 0x00FF0000u;
            fb->a_mask = 0xFF000000u;
            return true;

        case CL_PIXEL_XBGR8888:
            fb->r_mask = 0x000000FFu;
            fb->g_mask = 0x0000FF00u;
            fb->b_mask = 0x00FF0000u;
            fb->a_mask = 0u;
            return true;

        case CL_PIXEL_RGBA8888:
            fb->r_mask = 0xFF000000u;
            fb->g_mask = 0x00FF0000u;
            fb->b_mask = 0x0000FF00u;
            fb->a_mask = 0x000000FFu;
            return true;

        case CL_PIXEL_BGRA8888:
            fb->r_mask = 0x0000FF00u;
            fb->g_mask = 0x00FF0000u;
            fb->b_mask = 0xFF000000u;
            fb->a_mask = 0x000000FFu;
            return true;

        default:
            return false;
    }
}
