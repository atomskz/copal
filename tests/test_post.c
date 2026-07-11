/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Cross-thread task queue test (cl_application_post). Single-threaded cases
 * cover draining on step, FIFO order, a task posting another task, argument
 * validation, and dropping undrained tasks at destroy. A final case posts from
 * real worker threads to exercise the mutex-guarded enqueue against the loop
 * thread's drain (functional correctness; ASan/UBSan also watch for corruption).
 */
#include <copal/copal.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

/* All task callbacks below run on the main (loop) thread, so their shared
 * state needs no synchronisation of its own. */
static int order[8];
static int order_n;

static void record(void *user)
{
    if (order_n < (int)(sizeof(order) / sizeof(order[0])))
        order[order_n++] = (int)(intptr_t)user;
}

static cl_application_t *g_app;
static int chain_count;

static void chain_b(void *user)
{
    (void)user;
    chain_count++;
}

static void chain_a(void *user)
{
    (void)user;
    chain_count++;
    cl_application_post(g_app, chain_b, NULL); /* posts more work while draining */
}

static int received;

static void inc_received(void *user)
{
    (void)user;
    received++;
}

struct poster {
    cl_application_t *app;
    int count;
};

static void *post_worker(void *arg)
{
    struct poster *p = arg;
    int i;

    for (i = 0; i < p->count; i++)
        cl_application_post(p->app, inc_received, NULL);
    return NULL;
}

static cl_application_t *make_app(cl_platform_t **plat_out)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_platform_t *plat = cl_platform_mock_create(a);
    cl_renderer_t *rend = cl_renderer_mock_create(a);
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;

    ad.platform = plat;
    ad.renderer = rend;
    if (plat_out)
        *plat_out = plat;
    return cl_application_create(&ad);
}

int main(void)
{
    cl_application_t *app = make_app(NULL);

    CHECK(app != NULL);
    if (!app)
        return 1;

    /* Argument validation. */
    CHECK(cl_application_post(NULL, record, NULL) == CL_ERROR_INVALID_ARGUMENT);
    CHECK(cl_application_post(app, NULL, NULL) == CL_ERROR_INVALID_ARGUMENT);

    /* Posted tasks run on the next drain, in FIFO order, exactly once. */
    order_n = 0;
    CHECK(cl_application_post(app, record, (void *)(intptr_t)1) == CL_OK);
    CHECK(cl_application_post(app, record, (void *)(intptr_t)2) == CL_OK);
    CHECK(cl_application_post(app, record, (void *)(intptr_t)3) == CL_OK);
    CHECK(order_n == 0); /* not run until the loop drains */
    cl_application_step(app, false);
    CHECK(order_n == 3);
    CHECK(order[0] == 1 && order[1] == 2 && order[2] == 3);
    cl_application_step(app, false);
    CHECK(order_n == 3); /* each task ran only once */

    /* A task may post another task; the new one runs on the following drain. */
    g_app = app;
    chain_count = 0;
    CHECK(cl_application_post(app, chain_a, NULL) == CL_OK);
    cl_application_step(app, false);
    CHECK(chain_count == 1); /* chain_a ran; chain_b queued, not yet run */
    cl_application_step(app, false);
    CHECK(chain_count == 2); /* chain_b ran on the next pass */

    cl_application_destroy(app);

    /* Tasks posted but never drained are freed (not run) at destroy. */
    {
        cl_application_t *app2 = make_app(NULL);

        order_n = 0;
        CHECK(cl_application_post(app2, record, (void *)(intptr_t)9) == CL_OK);
        CHECK(cl_application_post(app2, record, (void *)(intptr_t)9) == CL_OK);
        cl_application_destroy(app2); /* drops both; ASan checks for leaks */
        CHECK(order_n == 0);          /* neither task ran */
    }

    /* Concurrency: two worker threads post while the loop thread drains. */
    {
        cl_application_t *app3 = make_app(NULL);
        struct poster pa = { app3, 500 };
        struct poster pb = { app3, 500 };
        pthread_t ta;
        pthread_t tb;
        int spins = 0;

        received = 0;
        CHECK(pthread_create(&ta, NULL, post_worker, &pa) == 0);
        CHECK(pthread_create(&tb, NULL, post_worker, &pb) == 0);
        /* Drain until every posted task has run (bounded to avoid a hang). */
        while (received < 1000 && spins < 2000000) {
            cl_application_step(app3, false);
            spins++;
        }
        pthread_join(ta, NULL);
        pthread_join(tb, NULL);
        cl_application_step(app3, false); /* drain any stragglers */
        CHECK(received == 1000);

        cl_application_destroy(app3);
    }

    if (failures == 0)
        printf("all post tests passed\n");
    return failures == 0 ? 0 : 1;
}
