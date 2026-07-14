/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Overlay-stack cap: filling it to CL_WINDOW_MAX_OVERLAYS is fine, and a push
 * past the cap is refused loudly - cl_last_error() becomes CL_ERROR_UNSUPPORTED
 * instead of a silent no-op - while leaving the stack and every widget intact
 * (ASan/LSan check there is no leak or corruption on the over-cap path).
 */
#include <copal/copal.h>

#include <stdio.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"
#include "app/app_internal.h" /* CL_WINDOW_MAX_OVERLAYS */

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void quiet_log(cl_log_level_t l, const char *m, void *u)
{
    (void)l;
    (void)m;
    (void)u; /* swallow the expected "overlay stack full" WARN */
}

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS, .width = 200,
                            .height = 150 };
    cl_label_desc_t ld = { CL_LABEL_DESC_INIT_FIELDS, .text = "p" };
    cl_widget_t *pops[CL_WINDOW_MAX_OVERLAYS];
    cl_widget_t *over;
    cl_application_t *app;
    cl_window_t *win;
    int i;

    cl_set_log_callback(quiet_log, NULL);
    ad.platform = cl_platform_mock_create(a);
    ad.renderer = cl_renderer_mock_create(a);
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    if (!app)
        return 1;
    win = cl_window_create(app, &wd);
    CHECK(win != NULL);

    /* Fill the stack. push_popup keeps ownership with the caller (submenu-style
     * stacking), so it can legitimately nest up to the cap. */
    for (i = 0; i < CL_WINDOW_MAX_OVERLAYS; i++) {
        pops[i] = cl_label_create(app, &ld);
        cl_window_push_popup(win, NULL, pops[i], (cl_point_t){ 0, 0 });
    }
    CHECK(cl_window_popup(win) == pops[CL_WINDOW_MAX_OVERLAYS - 1]);

    /* One past the cap: refused, and it says so. */
    (void)cl_last_error(); /* (no reset API; just re-read after the call) */
    over = cl_label_create(app, &ld);
    cl_window_push_popup(win, NULL, over, (cl_point_t){ 0, 0 });
    CHECK(cl_last_error() == CL_ERROR_UNSUPPORTED);
    /* The stack is unchanged: the topmost is still the last one that fit. */
    CHECK(cl_window_popup(win) == pops[CL_WINDOW_MAX_OVERLAYS - 1]);

    /* over was never adopted (push_popup is caller-owned); free it. */
    cl_widget_destroy(over);
    /* Dismiss the chain: non-owned entries are detached back to us, then freed
     * so nothing leaks. */
    cl_window_close_popup(win);
    for (i = 0; i < CL_WINDOW_MAX_OVERLAYS; i++)
        cl_widget_destroy(pops[i]);

    cl_application_destroy(app);
    return failures ? 1 : 0;
}
