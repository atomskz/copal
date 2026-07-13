/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Damage-region test: cl_widget_invalidate accumulates a bounding rect and
 * the window declares it to the renderer (set_damage) instead of repainting
 * everything. Covers: the first frame and layout changes stay full redraws,
 * a single invalidate damages just the widget's inflated box, two
 * invalidates union, cl_window_mark_dirty forces a full frame, and the
 * region resets between frames.
 */
#include <copal/copal.h>

#include <stdio.h>
#include <string.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"
#include "app/app_internal.h" /* cl_window_mark_dirty */

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

#define WIN_W 200.0f
#define WIN_H 100.0f

/* The damage the window should declare for one invalidated widget: its rect
 * inflated by 1 px (AA bleed), clamped to the window. */
static cl_rect_t expect_damage(cl_rect_t r)
{
    float x0 = r.x - 1.0f;
    float y0 = r.y - 1.0f;
    float x1 = r.x + r.w + 1.0f;
    float y1 = r.y + r.h + 1.0f;

    if (x0 < 0.0f)
        x0 = 0.0f;
    if (y0 < 0.0f)
        y0 = 0.0f;
    if (x1 > WIN_W)
        x1 = WIN_W;
    if (y1 > WIN_H)
        y1 = WIN_H;
    return (cl_rect_t){ x0, y0, x1 - x0, y1 - y0 };
}

static bool rect_eq(cl_rect_t a, cl_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static cl_rect_t rect_union(cl_rect_t a, cl_rect_t b)
{
    float x0 = a.x < b.x ? a.x : b.x;
    float y0 = a.y < b.y ? a.y : b.y;
    float x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    float y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;

    return (cl_rect_t){ x0, y0, x1 - x0, y1 - y0 };
}

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_platform_t *plat = cl_platform_mock_create(a);
    cl_renderer_t *rend = cl_renderer_mock_create(a);
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = (int)WIN_W, .height = (int)WIN_H };
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS, .spacing = 4.0f };
    cl_spacer_desc_t sd = { CL_SPACER_DESC_INIT_FIELDS,
                            .width = 40.0f, .height = 30.0f };
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *box;
    cl_widget_t *s1;
    cl_widget_t *s2;
    cl_rect_t r1;
    cl_rect_t r2;

    ad.platform = plat;
    ad.renderer = rend;
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    if (!app)
        return 1;
    win = cl_window_create(app, &wd);
    box = cl_vbox_create(app, &vd);
    s1 = cl_spacer_create(app, &sd);
    s2 = cl_spacer_create(app, &sd);
    cl_widget_add_child(box, s1);
    cl_widget_add_child(box, s2);
    cl_window_set_content(win, box);

    /* The first frame lays out and redraws in full. */
    cl_application_step(app, false);
    CHECK(!cl_renderer_mock_frame_damaged(rend));
    r1 = cl_widget_rect(s1);
    r2 = cl_widget_rect(s2);
    CHECK(r1.w > 0.0f && r2.w > 0.0f && r1.y != r2.y);

    /* One invalidated widget damages only its inflated box. */
    cl_widget_invalidate(s1);
    cl_application_step(app, false);
    CHECK(cl_renderer_mock_frame_damaged(rend));
    CHECK(rect_eq(cl_renderer_mock_frame_damage(rend), expect_damage(r1)));

    /* Two invalidations union into one bounding rect. */
    cl_widget_invalidate(s1);
    cl_widget_invalidate(s2);
    cl_application_step(app, false);
    CHECK(cl_renderer_mock_frame_damaged(rend));
    CHECK(rect_eq(cl_renderer_mock_frame_damage(rend),
                  rect_union(expect_damage(r1), expect_damage(r2))));

    /* An explicit full repaint stays full even with rects accumulated. */
    cl_widget_invalidate(s1);
    cl_window_mark_dirty(win);
    cl_application_step(app, false);
    CHECK(!cl_renderer_mock_frame_damaged(rend));

    /* A layout change repaints in full (widgets may move anywhere). */
    cl_widget_invalidate(s2);
    cl_widget_invalidate_layout(s1);
    cl_application_step(app, false);
    CHECK(!cl_renderer_mock_frame_damaged(rend));

    /* The region resets between frames: a later invalidate starts fresh. */
    cl_widget_invalidate(s2);
    cl_application_step(app, false);
    CHECK(cl_renderer_mock_frame_damaged(rend));
    CHECK(rect_eq(cl_renderer_mock_frame_damage(rend), expect_damage(r2)));

    cl_application_destroy(app);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("test_damage: all checks passed\n");
    return 0;
}
