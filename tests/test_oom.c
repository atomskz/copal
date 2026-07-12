/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Error-path tests. A fail-after-N allocator sweeps N across a full
 * create-use-destroy scenario, so every allocation site fails once: each
 * attempt must either succeed or fail cleanly with CL_ERROR_OUT_OF_MEMORY
 * recorded - and never crash or leak (run under ASan/LSan). Also checks the
 * font loader's error codes for junk data and missing files.
 */
#include <copal/copal.h>

#include <stdio.h>
#include <stdlib.h>
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

/* Allocator that fails every allocation once `remaining` hits zero.
 * remaining < 0 disables the failing behaviour. */
static int remaining;

static void *f_alloc(void *user, size_t size)
{
    (void)user;
    if (remaining == 0)
        return NULL;
    if (remaining > 0)
        remaining--;
    return malloc(size);
}

static void *f_realloc(void *user, void *ptr, size_t size)
{
    (void)user;
    if (remaining == 0)
        return NULL;
    if (remaining > 0)
        remaining--;
    return realloc(ptr, size);
}

static void f_free(void *user, void *ptr)
{
    (void)user;
    free(ptr);
}

static const cl_allocator_t failing = { NULL, f_alloc, f_realloc, f_free };

/*
 * One full scenario against the failing allocator. Returns true when it ran
 * to completion (i.e. N was larger than the total allocation count).
 * Everything created is destroyed on every path; ASan verifies no leaks.
 */
static bool scenario(int budget)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS,
                                 .allocator = &failing };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = 200, .height = 100 };
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS };
    cl_textbox_desc_t td = { CL_TEXTBOX_DESC_INIT_FIELDS, .text = "hello" };
    cl_platform_t *plat;
    cl_renderer_t *rend;
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *box;
    cl_widget_t *tb;
    bool complete = false;

    /* The mocks come from the default allocator: their creation is not part
     * of the failure sweep and they must be freed when app creation fails
     * (on failure ownership stays with the caller). */
    plat = cl_platform_mock_create(cl_allocator_default());
    rend = cl_renderer_mock_create(cl_allocator_default());
    CHECK(plat != NULL && rend != NULL);

    remaining = budget;
    ad.platform = plat;
    ad.renderer = rend;
    app = cl_application_create(&ad);
    if (!app) {
        CHECK(cl_last_error() == CL_ERROR_OUT_OF_MEMORY);
        plat->ops->destroy(plat);
        rend->ops->destroy(rend);
        remaining = -1;
        return false;
    }

    win = cl_window_create(app, &wd);
    box = win ? cl_vbox_create(app, &vd) : NULL;
    tb = box ? cl_textbox_create(app, &td) : NULL;
    if (tb) {
        cl_widget_add_child(box, tb);
        cl_window_set_content(win, box);
        if (cl_application_step(app, false)) {
            cl_textbox_set_text(tb, "a longer replacement text");
            cl_application_step(app, false);
            complete = true; /* every step ran within the budget */
        }
    } else {
        CHECK(cl_last_error() == CL_ERROR_OUT_OF_MEMORY);
        /* A widget created but never parented must be freed by the caller. */
        if (box && !tb)
            cl_widget_destroy(box);
    }

    remaining = -1; /* teardown must never fail */
    cl_application_destroy(app);
    return complete;
}

static void test_oom_sweep(void)
{
    int n;
    bool completed = false;

    /* Unlimited first: sanity that the scenario itself is green. */
    CHECK(scenario(-1));

    for (n = 0; n < 300 && !completed; n++)
        completed = scenario(n);
    CHECK(completed); /* the sweep must eventually run the whole scenario */
}

static void test_font_errors(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_application_t *app;
    unsigned char junk[64];

    ad.platform = cl_platform_mock_create(cl_allocator_default());
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);
    CHECK(app != NULL);

    memset(junk, 'A', sizeof(junk));
    CHECK(cl_font_load_memory(app, junk, sizeof(junk), 16.0f) == NULL);
    CHECK(cl_last_error() == CL_ERROR_FONT);
    CHECK(cl_font_load_memory(app, junk, 3, 16.0f) == NULL); /* < header */
    CHECK(cl_last_error() == CL_ERROR_FONT);
    CHECK(cl_font_load_memory(app, NULL, 16, 16.0f) == NULL);
    CHECK(cl_last_error() == CL_ERROR_INVALID_ARGUMENT);
    CHECK(cl_font_load_file(app, "/nonexistent/no.ttf", 16.0f) == NULL);
    CHECK(cl_last_error() == CL_ERROR_FONT);
    CHECK(cl_font_load_file(app, NULL, 16.0f) == NULL);
    CHECK(cl_last_error() == CL_ERROR_INVALID_ARGUMENT);

    cl_application_destroy(app);
}

int main(void)
{
    test_oom_sweep();
    test_font_errors();

    if (failures == 0)
        printf("all oom tests passed\n");
    return failures == 0 ? 0 : 1;
}
