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
    /* Honours COPAL_FONT (set it in CI), then probes the system fonts. */
    return cl_font_load_system(app, 16.0f);
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

static void key_down(cl_platform_t *p, cl_key_t key, cl_key_mods_t mods)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_KEY_DOWN;
    ev.key = key;
    ev.mods = mods;
    cl_platform_mock_push(p, ev);
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
            fprintf(stderr, "SKIP: no TrueType font found\n");
            return SKIP_CODE; /* skip, do not claim success */
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
            fprintf(stderr, "SKIP: no TrueType font found\n");
            return SKIP_CODE; /* skip, do not claim success */
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

    /* Scroll-to-focus: focusing a descendant (directly, via Tab, or via the
     * explicit API) scrolls it into the viewport; horizontal works too. */
    {
        cl_platform_t *p5 = cl_platform_mock_create(a);
        cl_renderer_t *r5 = cl_renderer_mock_create(a);
        cl_application_desc_t ad5 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app5;
        cl_font_t *font5;
        cl_window_t *w5;
        cl_widget_t *sv5;
        cl_widget_t *body5;
        cl_widget_t *btns[12];
        cl_window_desc_t wd5 = CL_WINDOW_DESC_INIT;
        cl_rect_t svr5;
        cl_rect_t b;

        ad5.platform = p5;
        ad5.renderer = r5;
        app5 = cl_application_create(&ad5);
        font5 = load_any_font(app5);
        if (!font5) {
            cl_application_destroy(app5);
            fprintf(stderr, "SKIP: no TrueType font found\n");
            return SKIP_CODE; /* skip, do not claim success */
        }
        cl_theme_set_font(cl_application_theme(app5), font5);

        wd5.width = 240;
        wd5.height = 120;
        w5 = cl_window_create(app5, &wd5);
        sv5 = cl_scrollview_create(
            app5, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS });
        cl_widget_set_preferred_size(sv5, (cl_size_t){ 240, 100 });
        body5 = cl_vbox_create(
            app5, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS, .spacing = 4 });
        for (i = 0; i < 12; i++) {
            char buf[24];

            snprintf(buf, sizeof(buf), "row %d", (int)i);
            btns[i] = cl_button_create(
                app5, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS,
                                           .text = buf });
            cl_widget_add_child(body5, btns[i]);
        }
        cl_scrollview_set_content(sv5, body5);
        cl_window_set_content(w5, sv5);
        cl_application_step(app5, false);

        svr5 = cl_widget_rect(sv5);

        /* The first button is visible at the top: focusing it keeps offset 0. */
        CHECK(cl_widget_focus(btns[0]));
        CHECK(cl_scrollview_scroll_y(sv5) == 0.0f);

        /* Focusing the last button scrolls down until it is fully in view. */
        CHECK(cl_widget_focus(btns[11]));
        CHECK(cl_scrollview_scroll_y(sv5) > 0.0f);
        b = cl_widget_rect(btns[11]);
        CHECK(b.y >= svr5.y - 0.5f);
        CHECK(b.y + b.h <= svr5.y + svr5.h + 0.5f);

        /* Focusing back to the top scrolls all the way up again. */
        CHECK(cl_widget_focus(btns[0]));
        CHECK(cl_scrollview_scroll_y(sv5) == 0.0f);

        /* The explicit API reveals a middle button. */
        cl_scrollview_scroll_to_widget(sv5, btns[6]);
        b = cl_widget_rect(btns[6]);
        CHECK(b.y >= svr5.y - 0.5f);
        CHECK(b.y + b.h <= svr5.y + svr5.h + 0.5f);

        /* Tab navigation from the top through every button ends on the last
         * one and leaves it revealed (each Tab reveals through set_focus).
         * Focus is already on btns[0], so reset the offset explicitly (a
         * re-focus reveal-no-ops from here); the Tab loop must be the only
         * thing that can drive scroll_y positive below. */
        cl_scrollview_scroll_to_widget(sv5, btns[0]);
        CHECK(cl_scrollview_scroll_y(sv5) == 0.0f);
        for (i = 0; i < 11; i++)
            key_down(p5, CL_KEY_TAB, CL_MOD_NONE);
        cl_application_step(app5, false);
        CHECK(cl_widget_has_focus(btns[11]));
        CHECK(cl_scrollview_scroll_y(sv5) > 0.0f);
        b = cl_widget_rect(btns[11]);
        CHECK(b.y + b.h <= svr5.y + svr5.h + 0.5f);

        /* Re-focusing an already-focused widget that has since been scrolled
         * out of view brings it back (reveal runs even when focus is
         * unchanged). */
        CHECK(cl_widget_focus(btns[0])); /* real change away from btns[11] */
        CHECK(cl_scrollview_scroll_y(sv5) == 0.0f);
        cl_scrollview_scroll_to(sv5, 100000.0f); /* scroll btns[0] off the top */
        CHECK(cl_scrollview_scroll_y(sv5) > 0.0f);
        cl_widget_focus(btns[0]); /* same widget: reveal must still fire */
        CHECK(cl_scrollview_scroll_y(sv5) == 0.0f);

        /* Revealing a target taller than the viewport is idempotent: it pins
         * the near edge rather than flip-flopping between top and bottom. */
        cl_scrollview_scroll_to(sv5, 100000.0f); /* bottom-aligned to start */
        cl_scrollview_scroll_to_widget(sv5, body5); /* body is over-tall */
        {
            float first = cl_scrollview_scroll_y(sv5);

            cl_scrollview_scroll_to_widget(sv5, body5);
            CHECK(cl_scrollview_scroll_y(sv5) == first); /* stable, no jitter */
        }

        cl_font_release(font5);
        cl_application_destroy(app5);
    }

    /* Horizontal scroll-to-widget: revealing a right-hand child scrolls
     * sideways. */
    {
        cl_platform_t *p6 = cl_platform_mock_create(a);
        cl_renderer_t *r6 = cl_renderer_mock_create(a);
        cl_application_desc_t ad6 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app6;
        cl_font_t *font6;
        cl_window_t *w6;
        cl_widget_t *sv6;
        cl_widget_t *body6;
        cl_widget_t *last = NULL;
        cl_window_desc_t wd6 = CL_WINDOW_DESC_INIT;
        cl_rect_t svr6;
        cl_rect_t b;

        ad6.platform = p6;
        ad6.renderer = r6;
        app6 = cl_application_create(&ad6);
        font6 = load_any_font(app6);
        if (!font6) {
            cl_application_destroy(app6);
            fprintf(stderr, "SKIP: no TrueType font found\n");
            return SKIP_CODE; /* skip, do not claim success */
        }
        cl_theme_set_font(cl_application_theme(app6), font6);

        wd6.width = 200;
        wd6.height = 80;
        w6 = cl_window_create(app6, &wd6);
        sv6 = cl_scrollview_create(
            app6, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS,
                                           .horizontal = true });
        cl_widget_set_preferred_size(sv6, (cl_size_t){ 160, 60 });
        body6 = cl_hbox_create(
            app6, &(cl_hbox_desc_t){ CL_HBOX_DESC_INIT_FIELDS, .spacing = 6 });
        for (i = 0; i < 10; i++) {
            char buf[24];

            snprintf(buf, sizeof(buf), "col %d", (int)i);
            last = cl_button_create(
                app6, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS,
                                           .text = buf });
            cl_widget_add_child(body6, last);
        }
        cl_scrollview_set_content(sv6, body6);
        cl_window_set_content(w6, sv6);
        cl_application_step(app6, false);

        svr6 = cl_widget_rect(sv6);
        CHECK(cl_scrollview_scroll_x(sv6) == 0.0f);
        cl_scrollview_scroll_to_widget(sv6, last);
        CHECK(cl_scrollview_scroll_x(sv6) > 0.0f);
        b = cl_widget_rect(last);
        CHECK(b.x + b.w <= svr6.x + svr6.w + 0.5f); /* right edge now in view */

        cl_font_release(font6);
        cl_application_destroy(app6);
    }

    /* Scroll-to-focus survives being requested BEFORE the first layout: rects
     * are zero until the deferred arrange, so the reveal is retried then. */
    {
        cl_platform_t *p7 = cl_platform_mock_create(a);
        cl_renderer_t *r7 = cl_renderer_mock_create(a);
        cl_application_desc_t ad7 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app7;
        cl_font_t *font7;
        cl_window_t *w7;
        cl_widget_t *sv7;
        cl_widget_t *body7;
        cl_widget_t *rows[12];
        cl_window_desc_t wd7 = CL_WINDOW_DESC_INIT;
        cl_rect_t svr7;
        cl_rect_t b;

        ad7.platform = p7;
        ad7.renderer = r7;
        app7 = cl_application_create(&ad7);
        font7 = load_any_font(app7);
        if (!font7) {
            cl_application_destroy(app7);
            fprintf(stderr, "SKIP: no TrueType font found\n");
            return SKIP_CODE; /* skip, do not claim success */
        }
        cl_theme_set_font(cl_application_theme(app7), font7);

        wd7.width = 240;
        wd7.height = 120;
        w7 = cl_window_create(app7, &wd7);
        sv7 = cl_scrollview_create(
            app7, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS });
        body7 = cl_vbox_create(
            app7, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS, .spacing = 4 });
        for (i = 0; i < 12; i++) {
            char buf[24];

            snprintf(buf, sizeof(buf), "row %d", (int)i);
            rows[i] = cl_button_create(
                app7, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS,
                                           .text = buf });
            cl_widget_add_child(body7, rows[i]);
        }
        cl_scrollview_set_content(sv7, body7);
        cl_window_set_content(w7, sv7);

        /* Focus the last row before any step: rects are still zero, so the
         * immediate reveal in set_focus can do nothing. */
        CHECK(cl_widget_focus(rows[11]));
        CHECK(cl_scrollview_scroll_y(sv7) == 0.0f);

        /* The first layout retries the pending reveal and the row comes in. */
        cl_application_step(app7, false);
        CHECK(cl_scrollview_scroll_y(sv7) > 0.0f);
        svr7 = cl_widget_rect(sv7);
        b = cl_widget_rect(rows[11]);
        CHECK(b.y + b.h <= svr7.y + svr7.h + 0.5f);

        cl_font_release(font7);
        cl_application_destroy(app7);
    }

    /* Smooth scrolling eases toward the wheel target over several ticks instead
     * of jumping, settles exactly on the target, and yields to an instant op. */
    {
        cl_platform_t *p8 = cl_platform_mock_create(a);
        cl_renderer_t *r8 = cl_renderer_mock_create(a);
        cl_application_desc_t ad8 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app8;
        cl_font_t *font8;
        cl_window_t *w8;
        cl_widget_t *sv8;
        cl_widget_t *body8;
        cl_window_desc_t wd8 = CL_WINDOW_DESC_INIT;
        cl_point_t at = { 20.0f, 20.0f };
        float prev;

        ad8.platform = p8;
        ad8.renderer = r8;
        app8 = cl_application_create(&ad8);
        font8 = load_any_font(app8);
        if (!font8) {
            cl_application_destroy(app8);
            fprintf(stderr, "SKIP: no TrueType font found\n");
            return SKIP_CODE; /* skip, do not claim success */
        }
        cl_theme_set_font(cl_application_theme(app8), font8);
        wd8.width = 240;
        wd8.height = 160;
        w8 = cl_window_create(app8, &wd8);
        sv8 = cl_scrollview_create(
            app8, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS,
                                           .smooth = true });
        body8 = cl_vbox_create(
            app8, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS, .spacing = 4 });
        for (i = 0; i < 20; i++)
            cl_widget_add_child(
                body8, cl_label_create(
                           app8, &(cl_label_desc_t){ CL_LABEL_DESC_INIT_FIELDS,
                                                     .text = "smooth row" }));
        cl_scrollview_set_content(sv8, body8);
        cl_window_set_content(w8, sv8);
        cl_application_step(app8, false);

        /* Wheel down: the target is 40, but the current offset stays 0 until the
         * animation timer ticks (proves it is not the instant path). */
        wheel(p8, at, -1.0f);
        cl_application_step(app8, false);
        CHECK(cl_scrollview_scroll_y(sv8) == 0.0f);

        /* One tick eases partway; the next moves further (monotonic approach). */
        cl_platform_mock_advance(p8, 16);
        cl_application_step(app8, false);
        prev = cl_scrollview_scroll_y(sv8);
        CHECK(prev > 0.0f && prev < 40.0f);
        cl_platform_mock_advance(p8, 16);
        cl_application_step(app8, false);
        CHECK(cl_scrollview_scroll_y(sv8) > prev);

        /* It settles exactly on the target and then stops moving. */
        for (i = 0; i < 40 && cl_scrollview_scroll_y(sv8) != 40.0f; i++) {
            cl_platform_mock_advance(p8, 16);
            cl_application_step(app8, false);
        }
        CHECK(cl_scrollview_scroll_y(sv8) == 40.0f);
        cl_platform_mock_advance(p8, 200);
        cl_application_step(app8, false);
        CHECK(cl_scrollview_scroll_y(sv8) == 40.0f); /* animation stopped */

        /* An instant scroll_to during a fresh animation snaps and cancels it. */
        wheel(p8, at, -1.0f); /* new target 80, animating from 40 */
        cl_application_step(app8, false);
        cl_platform_mock_advance(p8, 16);
        cl_application_step(app8, false);
        CHECK(cl_scrollview_scroll_y(sv8) > 40.0f &&
              cl_scrollview_scroll_y(sv8) < 80.0f);
        cl_scrollview_scroll_to(sv8, 12.0f);
        CHECK(cl_scrollview_scroll_y(sv8) == 12.0f);
        cl_platform_mock_advance(p8, 200);
        cl_application_step(app8, false);
        CHECK(cl_scrollview_scroll_y(sv8) == 12.0f); /* no drift after override */

        cl_font_release(font8);
        cl_application_destroy(app8);
    }

    /* Destroying a scrollview mid-animation must cancel its timer, so a later
     * poll cannot tick a freed widget (ASan/UBSan catches the use-after-free). */
    {
        cl_platform_t *p9 = cl_platform_mock_create(a);
        cl_renderer_t *r9 = cl_renderer_mock_create(a);
        cl_application_desc_t ad9 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app9;
        cl_font_t *font9;
        cl_window_t *w9;
        cl_widget_t *root9;
        cl_widget_t *sv9;
        cl_widget_t *body9;
        cl_window_desc_t wd9 = CL_WINDOW_DESC_INIT;

        ad9.platform = p9;
        ad9.renderer = r9;
        app9 = cl_application_create(&ad9);
        font9 = load_any_font(app9);
        if (!font9) {
            cl_application_destroy(app9);
            fprintf(stderr, "SKIP: no TrueType font found\n");
            return SKIP_CODE; /* skip, do not claim success */
        }
        cl_theme_set_font(cl_application_theme(app9), font9);
        wd9.width = 240;
        wd9.height = 160;
        w9 = cl_window_create(app9, &wd9);
        root9 = cl_vbox_create(app9,
                               &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS });
        sv9 = cl_scrollview_create(
            app9, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS,
                                           .smooth = true });
        cl_widget_set_preferred_size(sv9, (cl_size_t){ 200, 120 });
        body9 = cl_vbox_create(app9,
                               &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS });
        for (i = 0; i < 20; i++)
            cl_widget_add_child(
                body9, cl_label_create(
                           app9, &(cl_label_desc_t){ CL_LABEL_DESC_INIT_FIELDS,
                                                     .text = "row" }));
        cl_scrollview_set_content(sv9, body9);
        cl_widget_add_child(root9, sv9);
        cl_window_set_content(w9, root9);
        cl_application_step(app9, false);

        wheel(p9, (cl_point_t){ 20.0f, 20.0f }, -1.0f);
        cl_application_step(app9, false);
        cl_platform_mock_advance(p9, 16);
        cl_application_step(app9, false); /* animating: the timer is live */
        cl_widget_destroy(sv9);           /* must cancel the animation timer */
        /* Poll again: a dangling timer would tick the freed scrollview here.
         * Reaching the end clean (no ASan abort) is the assertion. */
        cl_platform_mock_advance(p9, 16);
        cl_application_step(app9, false);
        cl_platform_mock_advance(p9, 16);
        cl_application_step(app9, false);

        cl_font_release(font9);
        cl_application_destroy(app9);
    }

    if (failures == 0)
        printf("all scrollview tests passed\n");
    return failures == 0 ? 0 : 1;
}
