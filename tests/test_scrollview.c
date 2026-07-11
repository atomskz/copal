/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Headless ScrollView test: verifies content clipping (via recorded clip
 * rects), wheel scrolling with clamping, scrollbar-thumb dragging, and that
 * scrolling repositions the content children.
 */
#include <copal/copal.h>

#include <stdio.h>
#include <string.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"

#define SKIP_CODE 77

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

static cl_font_t *load_any_font(cl_application_t *app)
{
    static const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };
    size_t i;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        cl_font_t *f = cl_font_load_file(app, paths[i], 16.0f);

        if (f)
            return f;
    }
    return NULL;
}

static void wheel(cl_platform_t *p, cl_point_t pos, float dy)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_MOUSE_WHEEL;
    ev.pos = pos;
    ev.wheel_y = dy;
    cl_platform_mock_push(p, ev);
}

static void wheel_x(cl_platform_t *p, cl_point_t pos, float dx)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_MOUSE_WHEEL;
    ev.pos = pos;
    ev.wheel_x = dx;
    cl_platform_mock_push(p, ev);
}

static void mark_clicked(cl_widget_t *w, void *user)
{
    (void)w;
    *(bool *)user = true;
}

static void mouse(cl_platform_t *p, cl_platform_event_kind_t kind, float x,
                  float y)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    ev.pos.x = x;
    ev.pos.y = y;
    ev.button = CL_MOUSE_LEFT;
    cl_platform_mock_push(p, ev);
}

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_platform_t *plat = cl_platform_mock_create(a);
    cl_renderer_t *rend = cl_renderer_mock_create(a);
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_application_t *app;
    cl_font_t *font;
    cl_window_t *win;
    cl_widget_t *root;
    cl_widget_t *sv;
    cl_widget_t *inner;
    cl_widget_t *first_label = NULL;
    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
    cl_rect_t svr;
    cl_rect_t r0;
    cl_rect_t r1;
    float max_scroll;
    size_t i;
    size_t n;
    bool saw_push = false;
    bool saw_clipped_text = false;

    ad.platform = plat;
    ad.renderer = rend;
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    if (!app)
        return 1;

    font = load_any_font(app);
    if (!font) {
        fprintf(stderr, "SKIP: no TrueType font found\n");
        cl_application_destroy(app);
        return SKIP_CODE;
    }
    cl_theme_set_font(cl_application_theme(app), font);

    wd.width = 240;
    wd.height = 200;
    win = cl_window_create(app, &wd);
    CHECK(win != NULL);

    root = cl_vbox_create(app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS,
                                                  .padding = { 10, 10, 10, 10 },
                                                  .align_cross =
                                                      CL_ALIGN_STRETCH });
    sv = cl_scrollview_create(app, &(cl_scrollview_desc_t){
                                       CL_SCROLLVIEW_DESC_INIT_FIELDS });
    cl_widget_set_preferred_size(sv, (cl_size_t){ 0, 120 });

    inner = cl_vbox_create(app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS,
                                                   .spacing = 4 });
    for (i = 0; i < 14; i++) {
        cl_widget_t *lbl = cl_label_create(
            app, &(cl_label_desc_t){ CL_LABEL_DESC_INIT_FIELDS,
                                     .text = "scrollable row" });

        if (i == 0)
            first_label = lbl;
        cl_widget_add_child(inner, lbl);
    }
    cl_scrollview_set_content(sv, inner);
    CHECK(cl_scrollview_content(sv) == inner);

    cl_widget_add_child(root, sv);
    cl_window_set_content(win, root);
    cl_window_show(win);
    cl_application_step(app, false); /* layout + first paint */

    svr = cl_widget_rect(sv);
    CHECK(svr.h > 0.0f);

    /* Content (14 rows) must overflow the 120px viewport. */
    CHECK(cl_scrollview_scroll_y(sv) == 0.0f);

    /* Clipping: content text is recorded with a clip tighter than the window,
     * and a push_clip with the scrollview rect was issued. */
    n = cl_renderer_mock_count(rend);
    for (i = 0; i < n; i++) {
        const cl_mock_command_t *c = cl_renderer_mock_get(rend, i);

        if (c->kind == CL_MOCK_PUSH_CLIP && c->rect.h == svr.h &&
            c->rect.y == svr.y)
            saw_push = true;
        if (c->kind == CL_MOCK_TEXT && c->clip.h > 0.0f &&
            c->clip.h < (float)wd.height - 1.0f)
            saw_clipped_text = true;
    }
    CHECK(saw_push);
    CHECK(saw_clipped_text);

    /* Record first row position, then scroll and confirm it moved up. */
    r0 = cl_widget_rect(first_label);
    wheel(plat, (cl_point_t){ svr.x + 20.0f, svr.y + 20.0f }, -1.0f);
    cl_application_step(app, false);
    CHECK(cl_scrollview_scroll_y(sv) == 40.0f); /* one WHEEL_STEP down */
    r1 = cl_widget_rect(first_label);
    CHECK(r1.y == r0.y - 40.0f); /* child repositioned by the scroll offset */

    /* Wheel up past the top clamps at 0. */
    wheel(plat, (cl_point_t){ svr.x + 20.0f, svr.y + 20.0f }, 5.0f);
    cl_application_step(app, false);
    CHECK(cl_scrollview_scroll_y(sv) == 0.0f);

    /* scroll_to far past the end clamps to the maximum. */
    cl_scrollview_scroll_to(sv, 100000.0f);
    max_scroll = cl_scrollview_scroll_y(sv);
    CHECK(max_scroll > 40.0f);
    cl_scrollview_scroll_to(sv, 100000.0f);
    CHECK(cl_scrollview_scroll_y(sv) == max_scroll); /* idempotent clamp */

    /* Thumb drag: grab the thumb near the top of the track and drag down. */
    cl_scrollview_scroll_to(sv, 0.0f);
    cl_application_step(app, false);
    {
        float gutter_x = svr.x + svr.w - 6.0f; /* inside the scrollbar gutter */

        mouse(plat, CL_PEV_MOUSE_DOWN, gutter_x, svr.y + 2.0f);
        mouse(plat, CL_PEV_MOUSE_MOVE, gutter_x, svr.y + 40.0f);
        cl_application_step(app, false);
        CHECK(cl_scrollview_scroll_y(sv) > 0.0f); /* dragged down */

        mouse(plat, CL_PEV_MOUSE_UP, gutter_x, svr.y + 40.0f);
        cl_application_step(app, false);
        {
            float after = cl_scrollview_scroll_y(sv);

            /* After release, a stray move must not keep scrolling. */
            mouse(plat, CL_PEV_MOUSE_MOVE, gutter_x, svr.y + 80.0f);
            cl_application_step(app, false);
            CHECK(cl_scrollview_scroll_y(sv) == after);
        }
    }

    /* Replacing the content resets the offset and frees the old subtree. */
    {
        cl_widget_t *inner2 =
            cl_vbox_create(app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS });

        cl_scrollview_set_content(sv, inner2);
        CHECK(cl_scrollview_content(sv) == inner2);
        CHECK(cl_scrollview_scroll_y(sv) == 0.0f);
    }

    /* set_content takes ownership: a still-parented widget is refused by
     * add_child and must be freed (not leaked) rather than silently dropped. */
    {
        cl_widget_t *holder =
            cl_vbox_create(app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS });
        cl_widget_t *orphan = cl_label_create(
            app, &(cl_label_desc_t){ CL_LABEL_DESC_INIT_FIELDS, .text = "x" });

        cl_widget_add_child(holder, orphan); /* orphan is parented by holder */
        cl_scrollview_set_content(sv, orphan); /* refused + destroyed, no leak */
        CHECK(cl_scrollview_content(sv) == NULL);
        cl_widget_destroy(holder);
    }

    cl_font_release(font);
    cl_application_destroy(app);

    /* Clicking non-focusable chrome (a label) must not steal focus from an
     * active textbox. */
    {
        cl_platform_t *p2 = cl_platform_mock_create(a);
        cl_renderer_t *r2 = cl_renderer_mock_create(a);
        cl_application_desc_t ad2 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app2;
        cl_font_t *font2;
        cl_window_t *w2;
        cl_widget_t *root2;
        cl_widget_t *tb;
        cl_widget_t *lbl;
        cl_window_desc_t wd2 = CL_WINDOW_DESC_INIT;
        cl_rect_t lr;

        ad2.platform = p2;
        ad2.renderer = r2;
        app2 = cl_application_create(&ad2);
        font2 = load_any_font(app2);
        if (font2)
            cl_theme_set_font(cl_application_theme(app2), font2);
        wd2.width = 200;
        wd2.height = 120;
        w2 = cl_window_create(app2, &wd2);
        root2 = cl_vbox_create(
            app2, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS, .spacing = 6 });
        tb = cl_textbox_create(
            app2, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS });
        lbl = cl_label_create(
            app2, &(cl_label_desc_t){ CL_LABEL_DESC_INIT_FIELDS,
                                      .text = "click me" });
        cl_widget_add_child(root2, tb);
        cl_widget_add_child(root2, lbl);
        cl_window_set_content(w2, root2);
        cl_application_step(app2, false);

        CHECK(cl_widget_focus(tb));
        CHECK(cl_widget_has_focus(tb));
        lr = cl_widget_rect(lbl);
        mouse(p2, CL_PEV_MOUSE_DOWN, lr.x + lr.w * 0.5f, lr.y + lr.h * 0.5f);
        mouse(p2, CL_PEV_MOUSE_UP, lr.x + lr.w * 0.5f, lr.y + lr.h * 0.5f);
        cl_application_step(app2, false);
        CHECK(cl_widget_has_focus(tb)); /* focus survives the chrome click */

        if (font2)
            cl_font_release(font2);
        cl_application_destroy(app2);
    }

    /* Horizontal scrolling: an opt-in scrollview whose content overflows
     * sideways scrolls on wheel dx / drag / scroll_to_x, and its scrollbar
     * gutter wins clicks over content drawn underneath it. */
    {
        cl_platform_t *p3 = cl_platform_mock_create(a);
        cl_renderer_t *r3 = cl_renderer_mock_create(a);
        cl_application_desc_t ad3 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app3;
        cl_font_t *font3;
        cl_window_t *w3;
        cl_widget_t *root3;
        cl_widget_t *hsv;
        cl_widget_t *btn;
        cl_window_desc_t wd3 = CL_WINDOW_DESC_INIT;
        cl_rect_t hr;
        cl_rect_t b0;
        cl_rect_t b1;
        float max_x;
        bool clicked = false;

        ad3.platform = p3;
        ad3.renderer = r3;
        app3 = cl_application_create(&ad3);
        font3 = load_any_font(app3);
        if (!font3) {
            cl_application_destroy(app3);
            if (failures == 0)
                printf("all scrollview tests passed\n");
            return failures == 0 ? 0 : 1;
        }
        cl_theme_set_font(cl_application_theme(app3), font3);

        wd3.width = 160;
        wd3.height = 120;
        w3 = cl_window_create(app3, &wd3);
        root3 = cl_vbox_create(app3,
                               &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS,
                                                  .padding = { 8, 8, 8, 8 } });
        hsv = cl_scrollview_create(
            app3, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS,
                                           .horizontal = true });
        cl_widget_set_preferred_size(hsv, (cl_size_t){ 120, 28 });
        btn = cl_button_create(
            app3, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS,
                                       .text = "a very very very wide button" });
        cl_button_set_on_click(btn, mark_clicked, &clicked);
        cl_scrollview_set_content(hsv, btn);
        cl_widget_add_child(root3, hsv);
        cl_window_set_content(w3, root3);
        cl_application_step(app3, false);

        hr = cl_widget_rect(hsv);
        CHECK(cl_scrollview_scroll_x(hsv) == 0.0f);

        /* Content overflows horizontally: scroll_to_x past the end clamps. */
        cl_scrollview_scroll_to_x(hsv, 100000.0f);
        max_x = cl_scrollview_scroll_x(hsv);
        CHECK(max_x > 0.0f);

        /* The content child moves left by exactly the horizontal offset. */
        cl_scrollview_scroll_to_x(hsv, 0.0f);
        b0 = cl_widget_rect(btn);
        cl_scrollview_scroll_to_x(hsv, 30.0f);
        b1 = cl_widget_rect(btn);
        CHECK(b1.x == b0.x - 30.0f);

        /* Horizontal wheel (dx) scrolls sideways and clamps at 0. */
        cl_scrollview_scroll_to_x(hsv, 0.0f);
        wheel_x(p3, (cl_point_t){ hr.x + 10.0f, hr.y + 6.0f }, -1.0f);
        cl_application_step(app3, false);
        CHECK(cl_scrollview_scroll_x(hsv) == 40.0f); /* one WHEEL_STEP right */
        wheel_x(p3, (cl_point_t){ hr.x + 10.0f, hr.y + 6.0f }, 5.0f);
        cl_application_step(app3, false);
        CHECK(cl_scrollview_scroll_x(hsv) == 0.0f);

        /* A click in the bottom scrollbar gutter must drive the scrollbar, not
         * the wide button painted beneath it (clip-aware hit-testing). */
        {
            float gy = hr.y + hr.h - 4.0f; /* inside the horizontal gutter */
            float gx = hr.x + hr.w * 0.6f; /* right of the thumb, in the track */

            mouse(p3, CL_PEV_MOUSE_DOWN, gx, gy);
            mouse(p3, CL_PEV_MOUSE_UP, gx, gy);
            cl_application_step(app3, false);
            CHECK(!clicked);                           /* button never fired */
            CHECK(cl_scrollview_scroll_x(hsv) > 0.0f); /* paged right instead */
        }

        /* The empty corner where both gutters meet is inert chrome: a click
         * there must not page either axis. */
        cl_scrollview_scroll_to(hsv, 0.0f);
        cl_scrollview_scroll_to_x(hsv, 0.0f);
        {
            float cx = hr.x + hr.w - 3.0f; /* right gutter band */
            float cy = hr.y + hr.h - 3.0f; /* bottom gutter band */

            mouse(p3, CL_PEV_MOUSE_DOWN, cx, cy);
            mouse(p3, CL_PEV_MOUSE_UP, cx, cy);
            cl_application_step(app3, false);
            CHECK(cl_scrollview_scroll_y(hsv) == 0.0f); /* corner did not page */
            CHECK(cl_scrollview_scroll_x(hsv) == 0.0f);
        }

        cl_font_release(font3);
        cl_application_destroy(app3);
    }

    /* Nested scroll: a horizontal-only view inside a vertical container must
     * let a plain vertical wheel bubble to the outer container instead of
     * trapping it as sideways motion. */
    {
        cl_platform_t *p4 = cl_platform_mock_create(a);
        cl_renderer_t *r4 = cl_renderer_mock_create(a);
        cl_application_desc_t ad4 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app4;
        cl_font_t *font4;
        cl_window_t *w4;
        cl_widget_t *outer_sv;
        cl_widget_t *outer_body;
        cl_widget_t *inner_sv;
        cl_widget_t *inner_body;
        cl_window_desc_t wd4 = CL_WINDOW_DESC_INIT;
        cl_rect_t ir;

        ad4.platform = p4;
        ad4.renderer = r4;
        app4 = cl_application_create(&ad4);
        font4 = load_any_font(app4);
        if (!font4) {
            cl_application_destroy(app4);
            if (failures == 0)
                printf("all scrollview tests passed\n");
            return failures == 0 ? 0 : 1;
        }
        cl_theme_set_font(cl_application_theme(app4), font4);

        wd4.width = 220;
        wd4.height = 140;
        w4 = cl_window_create(app4, &wd4);
        outer_sv = cl_scrollview_create(
            app4, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS });
        cl_widget_set_preferred_size(outer_sv, (cl_size_t){ 200, 120 });
        outer_body = cl_vbox_create(
            app4, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS, .spacing = 4 });

        inner_sv = cl_scrollview_create(
            app4, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS,
                                           .horizontal = true });
        cl_widget_set_preferred_size(inner_sv, (cl_size_t){ 160, 60 });
        inner_body = cl_hbox_create(
            app4, &(cl_hbox_desc_t){ CL_HBOX_DESC_INIT_FIELDS, .spacing = 6 });
        for (i = 0; i < 8; i++) {
            char buf[24];

            snprintf(buf, sizeof(buf), "col %d", (int)(i + 1));
            cl_widget_add_child(
                inner_body, cl_button_create(
                                app4, &(cl_button_desc_t){
                                          CL_BUTTON_DESC_INIT_FIELDS,
                                          .text = buf }));
        }
        cl_scrollview_set_content(inner_sv, inner_body);
        cl_widget_add_child(outer_body, inner_sv);
        for (i = 0; i < 8; i++) {
            cl_widget_add_child(
                outer_body, cl_label_create(
                                app4, &(cl_label_desc_t){
                                          CL_LABEL_DESC_INIT_FIELDS,
                                          .text = "tall filler row" }));
        }
        cl_scrollview_set_content(outer_sv, outer_body);
        cl_window_set_content(w4, outer_sv);
        cl_application_step(app4, false);

        ir = cl_widget_rect(inner_sv);
        CHECK(cl_scrollview_scroll_x(inner_sv) == 0.0f);
        CHECK(cl_scrollview_scroll_y(outer_sv) == 0.0f);

        /* Plain vertical wheel (dy only) with the pointer over the inner strip. */
        wheel(p4, (cl_point_t){ ir.x + 12.0f, ir.y + 12.0f }, -1.0f);
        cl_application_step(app4, false);
        CHECK(cl_scrollview_scroll_x(inner_sv) == 0.0f);  /* inner kept still */
        CHECK(cl_scrollview_scroll_y(outer_sv) == 40.0f); /* outer scrolled */

        cl_font_release(font4);
        cl_application_destroy(app4);
    }

    if (failures == 0)
        printf("all scrollview tests passed\n");
    return failures == 0 ? 0 : 1;
}
