/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Box layout behaviour: flex distribution of leftover main-axis space,
 * per-child cross-axis alignment (and the CL_ALIGN_AUTO deferral), margins,
 * preferred-size overrides and nested boxes. Geometry is asserted to exact
 * pixel values against the mock backends.
 */
#include <copal/copal.h>

#include <math.h>
#include <stdio.h>

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

#define CHECK_EQF(a, b) CHECK(fabsf((a) - (b)) < 0.01f)

static cl_application_t *app;
static cl_window_t *win;
static cl_platform_t *plat;

/* A fresh spacer of the given preferred size. */
static cl_widget_t *spacer(float w, float h)
{
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS };
    cl_widget_t *s = cl_vbox_create(app, &vd);

    cl_widget_set_preferred_size(s, (cl_size_t){ w, h });
    return s;
}

static void relayout(void)
{
    cl_application_step(app, false);
}

/* Window is 400x340 in every case. */
static void test_vbox_flex(void)
{
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS };
    cl_widget_t *box = cl_vbox_create(app, &vd);
    cl_widget_t *fixed = spacer(100, 40);
    cl_widget_t *f1 = spacer(10, 0);
    cl_widget_t *f3 = spacer(10, 0);

    cl_widget_set_flex(f1, 1.0f);
    cl_widget_set_flex(f3, 3.0f);
    cl_widget_add_child(box, fixed);
    cl_widget_add_child(box, f1);
    cl_widget_add_child(box, f3);
    cl_window_set_content(win, box);
    relayout();

    /* leftover 340 - 40 = 300 splits 75 / 225 by the 1:3 weights */
    CHECK_EQF(cl_widget_rect(fixed).h, 40.0f);
    CHECK_EQF(cl_widget_rect(f1).h, 75.0f);
    CHECK_EQF(cl_widget_rect(f3).h, 225.0f);
    CHECK_EQF(cl_widget_rect(f3).y, 115.0f); /* stacked below 40 + 75 */
}

static void test_hbox_flex_and_spacing(void)
{
    cl_hbox_desc_t hd = { CL_HBOX_DESC_INIT_FIELDS, .spacing = 10 };
    cl_widget_t *box = cl_hbox_create(app, &hd);
    cl_widget_t *a = spacer(50, 20);
    cl_widget_t *b = spacer(50, 20);

    cl_widget_set_flex(b, 1.0f);
    cl_widget_add_child(box, a);
    cl_widget_add_child(box, b);
    cl_window_set_content(win, box);
    relayout();

    /* 400 - (50 + 50 + 10 spacing) = 290 leftover goes to b */
    CHECK_EQF(cl_widget_rect(a).w, 50.0f);
    CHECK_EQF(cl_widget_rect(b).w, 340.0f);
    CHECK_EQF(cl_widget_rect(b).x, 60.0f);
}

static void test_align_override(void)
{
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS,
                          .align_cross = CL_ALIGN_STRETCH };
    cl_widget_t *box = cl_vbox_create(app, &vd);
    cl_widget_t *autow = spacer(50, 10);  /* AUTO -> container STRETCH */
    cl_widget_t *center = spacer(50, 10); /* explicit CENTER wins */
    cl_widget_t *end = spacer(50, 10);    /* explicit END wins */

    cl_widget_set_align(center, CL_ALIGN_CENTER, CL_ALIGN_AUTO);
    cl_widget_set_align(end, CL_ALIGN_END, CL_ALIGN_AUTO);
    cl_widget_add_child(box, autow);
    cl_widget_add_child(box, center);
    cl_widget_add_child(box, end);
    cl_window_set_content(win, box);
    relayout();

    CHECK_EQF(cl_widget_rect(autow).w, 400.0f);
    CHECK_EQF(cl_widget_rect(center).w, 50.0f);
    CHECK_EQF(cl_widget_rect(center).x, 175.0f);
    CHECK_EQF(cl_widget_rect(end).x, 350.0f);
}

static void test_margin(void)
{
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS,
                          .align_cross = CL_ALIGN_STRETCH };
    cl_widget_t *box = cl_vbox_create(app, &vd);
    cl_widget_t *m = spacer(50, 30);

    cl_widget_set_margin(m, (cl_insets_t){ 12, 7, 8, 0 });
    cl_widget_add_child(box, m);
    cl_window_set_content(win, box);
    relayout();

    CHECK_EQF(cl_widget_rect(m).x, 12.0f);
    CHECK_EQF(cl_widget_rect(m).y, 7.0f);
    CHECK_EQF(cl_widget_rect(m).w, 400.0f - 12.0f - 8.0f);
}

static void test_nested(void)
{
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS,
                          .align_cross = CL_ALIGN_STRETCH };
    cl_hbox_desc_t hd = { CL_HBOX_DESC_INIT_FIELDS,
                          .align_cross = CL_ALIGN_STRETCH };
    cl_widget_t *col = cl_vbox_create(app, &vd);
    cl_widget_t *row = cl_hbox_create(app, &hd);
    cl_widget_t *left = spacer(30, 10);
    cl_widget_t *right = spacer(30, 10);
    cl_widget_t *bottom = spacer(10, 60);

    cl_widget_set_flex(row, 1.0f);
    cl_widget_set_flex(right, 1.0f);
    cl_widget_add_child(row, left);
    cl_widget_add_child(row, right);
    cl_widget_add_child(col, row);
    cl_widget_add_child(col, bottom);
    cl_window_set_content(win, col);
    relayout();

    /* the row grows to 340 - 60 = 280 tall; right fills 400 - 30 wide */
    CHECK_EQF(cl_widget_rect(row).h, 280.0f);
    CHECK_EQF(cl_widget_rect(right).w, 370.0f);
    CHECK_EQF(cl_widget_rect(right).h, 280.0f); /* row STRETCH cross-axis */
    CHECK_EQF(cl_widget_rect(bottom).y, 280.0f);
}

static void test_no_shrink_below_measured(void)
{
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS };
    cl_widget_t *box = cl_vbox_create(app, &vd);
    cl_widget_t *big1 = spacer(10, 300);
    cl_widget_t *big2 = spacer(10, 300);

    cl_widget_set_flex(big1, 1.0f);
    cl_widget_add_child(box, big1);
    cl_widget_add_child(box, big2);
    cl_window_set_content(win, box);
    relayout();

    /* 600 measured > 340 window: grow-only flex must not shrink anyone */
    CHECK_EQF(cl_widget_rect(big1).h, 300.0f);
    CHECK_EQF(cl_widget_rect(big2).h, 300.0f);
}

int main(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = 400, .height = 340 };

    plat = cl_platform_mock_create(cl_allocator_default());
    ad.platform = plat;
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    win = cl_window_create(app, &wd);
    CHECK(win != NULL);

    test_vbox_flex();
    test_hbox_flex_and_spacing();
    test_align_override();
    test_margin();
    test_nested();
    test_no_shrink_below_measured();

    cl_application_destroy(app);
    if (failures == 0)
        printf("all layout tests passed\n");
    return failures == 0 ? 0 : 1;
}
