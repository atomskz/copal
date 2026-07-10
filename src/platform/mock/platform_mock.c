/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform/mock/platform_mock.h"

#include <string.h>

#define CL_MOCK_QUEUE 64

typedef struct mock_platform {
    cl_platform_t base;
    const cl_allocator_t *a;
    cl_size_t size;
    float scale;
    cl_platform_event_t queue[CL_MOCK_QUEUE];
    size_t head;
    size_t tail;
} mock_platform_t;

static cl_result_t mock_create_window(cl_platform_t *p,
                                      const cl_window_desc_t *desc)
{
    mock_platform_t *m = (mock_platform_t *)p;

    m->size.w = (float)desc->width;
    m->size.h = (float)desc->height;
    return CL_OK;
}

static void mock_set_title(cl_platform_t *p, const char *utf8)
{
    (void)p;
    (void)utf8;
}

static cl_size_t mock_drawable_size(cl_platform_t *p)
{
    return ((mock_platform_t *)p)->size;
}

static float mock_scale(cl_platform_t *p)
{
    return ((mock_platform_t *)p)->scale;
}

static bool mock_poll(cl_platform_t *p, cl_platform_event_t *out)
{
    mock_platform_t *m = (mock_platform_t *)p;

    if (m->head == m->tail)
        return false;
    *out = m->queue[m->head];
    m->head = (m->head + 1) % CL_MOCK_QUEUE;
    return true;
}

static void mock_wait(cl_platform_t *p, int timeout_ms)
{
    (void)p;
    (void)timeout_ms;
}

static void mock_present(cl_platform_t *p)
{
    (void)p;
}

static void mock_wakeup(cl_platform_t *p)
{
    (void)p;
}

static void mock_destroy(cl_platform_t *p)
{
    mock_platform_t *m = (mock_platform_t *)p;

    cl_free(m->a, m);
}

static const cl_platform_ops_t mock_ops = {
    .create_window = mock_create_window,
    .set_title = mock_set_title,
    .drawable_size = mock_drawable_size,
    .scale = mock_scale,
    .poll = mock_poll,
    .wait = mock_wait,
    .present = mock_present,
    .wakeup = mock_wakeup,
    .destroy = mock_destroy,
};

cl_platform_t *cl_platform_mock_create(const cl_allocator_t *a)
{
    mock_platform_t *m = cl_alloc(a, sizeof(*m));

    if (!m)
        return NULL;
    memset(m, 0, sizeof(*m));
    m->base.ops = &mock_ops;
    m->a = a;
    m->scale = 1.0f;
    return &m->base;
}

void cl_platform_mock_push(cl_platform_t *p, cl_platform_event_t ev)
{
    mock_platform_t *m = (mock_platform_t *)p;
    size_t next = (m->tail + 1) % CL_MOCK_QUEUE;

    if (next == m->head)
        return; /* queue full: drop */
    m->queue[m->tail] = ev;
    m->tail = next;
}
