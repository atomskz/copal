/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Tree-depth cap (cl_widget_add_child / CL_WIDGET_MAX_DEPTH): a chain built to
 * the cap can be measured, arranged, painted, hit-tested and destroyed without
 * overflowing the C stack, and a link past the cap is refused. Run under ASan.
 */
#include <copal/copal.h>

#include <stdio.h>
#include <string.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"
#include "app/app_internal.h"          /* cl_window_render */
#include "widget/widget_internal.h"    /* CL_WIDGET_MAX_DEPTH */

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS, .width = 100,
                            .height = 100 };
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS, .padding = { 0, 0, 0, 0 } };
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *root, *cur, *extra;
    cl_platform_t *plat;
    int depth = 1;

    ad.platform = plat = cl_platform_mock_create(a);
    ad.renderer = cl_renderer_mock_create(a);
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    if (!app)
        return 1;
    win = cl_window_create(app, &wd);

    /* Build a single chain right up to the cap. */
    root = cur = cl_vbox_create(app, &vd);
    while (depth < CL_WIDGET_MAX_DEPTH) {
        cl_widget_t *n = cl_vbox_create(app, &vd);

        CHECK(cl_widget_add_child(cur, n) == CL_OK);
        cur = n;
        depth++;
    }
    CHECK(depth == CL_WIDGET_MAX_DEPTH);

    /* One link past the cap is refused, and the rejected node does not leak
     * (it is unattached, so destroying it frees it immediately). */
    extra = cl_vbox_create(app, &vd);
    CHECK(cl_widget_add_child(cur, extra) == CL_ERROR_INVALID_ARGUMENT);
    cl_widget_destroy(extra);

    /* Attaching the cap-deep chain and rendering exercises the recursive
     * measure/arrange (layout) and paint walks at full depth. */
    cl_window_set_content(win, root);
    cl_window_show(win);
    cl_window_render(win); /* layout + paint */
    cl_widget_invalidate_layout(root);
    cl_window_render(win); /* relayout + paint */

    /* Hit-test walk at full depth (a click at the origin). */
    {
        cl_platform_event_t ev;

        memset(&ev, 0, sizeof ev);
        ev.kind = CL_PEV_MOUSE_DOWN;
        ev.button = CL_MOUSE_LEFT;
        ev.pos.x = 1.0f;
        ev.pos.y = 1.0f;
        cl_platform_mock_push(plat, ev);
        cl_application_step(app, false);
    }

    /* Destroy walk at full depth: cl_application_destroy tears down the window
     * and the whole content chain. A stack overflow here would abort. */
    cl_application_destroy(app);
    return failures ? 1 : 0;
}
