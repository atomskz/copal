/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Headless tooltip test: drives the mock clock and pointer to exercise the
 * hover-dwell lifecycle. Covers: no bubble for a widget without a tooltip; a
 * bubble appearing only after the dwell; the bubble text actually rendering;
 * leaving the target before the dwell cancelling it; moving to another target
 * dismissing a shown bubble; a click, wheel, or key-press dismissing it; the
 * target being hidden/disabled dismissing it; edge placement (clamp on-screen
 * and flip-above); and destroying the hovered widget while the bubble is up
 * (no use-after-free).
 */
#include <copal/copal.h>

#include <stdio.h>
#include <string.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"

#define SKIP_CODE 77
#define DWELL 500

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

static void move_to(cl_platform_t *p, cl_point_t at)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_MOUSE_MOVE;
    ev.pos = at;
    cl_platform_mock_push(p, ev);
}

static void click_at(cl_platform_t *p, cl_platform_event_kind_t kind,
                     cl_point_t at)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = kind;
    ev.pos = at;
    ev.button = CL_MOUSE_LEFT;
    cl_platform_mock_push(p, ev);
}

static void wheel_ev(cl_platform_t *p, cl_point_t at, float dy)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_MOUSE_WHEEL;
    ev.pos = at;
    ev.wheel_y = dy;
    cl_platform_mock_push(p, ev);
}

static void key_ev(cl_platform_t *p, cl_key_t key)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_KEY_DOWN;
    ev.key = key;
    cl_platform_mock_push(p, ev);
}

static cl_point_t center(cl_widget_t *w)
{
    cl_rect_t r = cl_widget_rect(w);

    return (cl_point_t){ r.x + r.w * 0.5f, r.y + r.h * 0.5f };
}

/* Hover `w` and let the dwell elapse so its tooltip bubble is shown. */
static void show_tooltip(cl_application_t *app, cl_platform_t *p, cl_widget_t *w)
{
    move_to(p, center(w));
    cl_application_step(app, false);
    cl_platform_mock_advance(p, DWELL + 100);
    cl_application_step(app, false);
}

static bool rendered_text(cl_renderer_t *r, const char *needle)
{
    size_t n = cl_renderer_mock_count(r);
    size_t i;

    for (i = 0; i < n; i++) {
        const cl_mock_command_t *c = cl_renderer_mock_get(r, i);

        if (c->kind == CL_MOCK_TEXT && strstr(c->text, needle))
            return true;
    }
    return false;
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
    cl_widget_t *tip;
    cl_widget_t *plain;
    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;

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

    wd.width = 300;
    wd.height = 200;
    win = cl_window_create(app, &wd);
    root = cl_vbox_create(app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS,
                                                  .spacing = 8,
                                                  .padding = { 10, 10, 10, 10 } });
    tip = cl_button_create(
        app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS, .text = "Hover me" });
    cl_widget_set_tooltip(tip, "Tooltip text");
    CHECK(cl_widget_tooltip(tip) != NULL &&
          strcmp(cl_widget_tooltip(tip), "Tooltip text") == 0);
    plain = cl_button_create(
        app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS, .text = "No tip" });
    cl_widget_add_child(root, tip);
    cl_widget_add_child(root, plain);
    cl_window_set_content(win, root);
    cl_application_step(app, false); /* layout */

    /* A widget without a tooltip never shows a bubble, even after the dwell. */
    move_to(plat, center(plain));
    cl_application_step(app, false);
    cl_platform_mock_advance(plat, DWELL + 100);
    cl_application_step(app, false);
    CHECK(cl_window_tooltip(win) == NULL);

    /* Hovering the tooltip widget shows the bubble only after the dwell. */
    move_to(plat, center(tip));
    cl_application_step(app, false);
    CHECK(cl_window_tooltip(win) == NULL); /* not yet */
    cl_platform_mock_advance(plat, DWELL - 200);
    cl_application_step(app, false);
    CHECK(cl_window_tooltip(win) == NULL); /* still under the dwell */
    cl_platform_mock_advance(plat, 300);   /* past the dwell now */
    cl_application_step(app, false);
    CHECK(cl_window_tooltip(win) != NULL); /* shown */
    CHECK(rendered_text(rend, "Tooltip text")); /* and the text painted */
    {
        cl_rect_t r = cl_widget_rect(cl_window_tooltip(win));

        CHECK(r.x >= 0.0f && r.y >= 0.0f);
        CHECK(r.x + r.w <= (float)wd.width && r.y + r.h <= (float)wd.height);
    }

    /* Moving to a different (untipped) target dismisses the shown bubble. */
    move_to(plat, center(plain));
    cl_application_step(app, false);
    CHECK(cl_window_tooltip(win) == NULL);

    /* Leaving the target before the dwell cancels the pending bubble. */
    move_to(plat, center(tip));
    cl_application_step(app, false);
    cl_platform_mock_advance(plat, 200); /* still under the dwell */
    cl_application_step(app, false);
    move_to(plat, center(plain)); /* leave early */
    cl_application_step(app, false);
    cl_platform_mock_advance(plat, DWELL + 100);
    cl_application_step(app, false);
    CHECK(cl_window_tooltip(win) == NULL); /* never appeared */

    /* A click dismisses a shown bubble. */
    show_tooltip(app, plat, tip);
    CHECK(cl_window_tooltip(win) != NULL);
    click_at(plat, CL_PEV_MOUSE_DOWN, center(tip));
    click_at(plat, CL_PEV_MOUSE_UP, center(tip));
    cl_application_step(app, false);
    CHECK(cl_window_tooltip(win) == NULL);

    /* The mouse wheel dismisses a shown bubble. */
    show_tooltip(app, plat, tip);
    CHECK(cl_window_tooltip(win) != NULL);
    wheel_ev(plat, center(tip), -1.0f);
    cl_application_step(app, false);
    CHECK(cl_window_tooltip(win) == NULL);

    /* A key press dismisses a shown bubble. */
    show_tooltip(app, plat, tip);
    CHECK(cl_window_tooltip(win) != NULL);
    key_ev(plat, CL_KEY_A);
    cl_application_step(app, false);
    CHECK(cl_window_tooltip(win) == NULL);

    /* Hiding the target with the pointer at rest (no move) drops the bubble at
     * the next render rather than leaving a ghost. */
    show_tooltip(app, plat, tip);
    CHECK(cl_window_tooltip(win) != NULL);
    cl_widget_set_visible(tip, false);
    cl_application_step(app, false); /* render reconciles the stale bubble */
    CHECK(cl_window_tooltip(win) == NULL);
    cl_widget_set_visible(tip, true);
    cl_application_step(app, false);

    /* Disabling the target likewise drops the bubble. */
    show_tooltip(app, plat, tip);
    CHECK(cl_window_tooltip(win) != NULL);
    cl_widget_set_enabled(tip, false);
    cl_application_step(app, false);
    CHECK(cl_window_tooltip(win) == NULL);
    cl_widget_set_enabled(tip, true);
    cl_application_step(app, false);

    /* Destroying the hovered widget while the bubble is up dismisses it with no
     * dangling target (ASan/UBSan would flag a use-after-free otherwise). */
    show_tooltip(app, plat, tip);
    CHECK(cl_window_tooltip(win) != NULL);
    cl_widget_destroy(tip);
    CHECK(cl_window_tooltip(win) == NULL);
    /* A further step must be clean (the dwell timer/target are gone). */
    cl_platform_mock_advance(plat, DWELL + 100);
    cl_application_step(app, false);
    CHECK(cl_window_tooltip(win) == NULL);

    cl_font_release(font);
    cl_application_destroy(app);

    /* Edge placement: a bubble near the right edge is clamped on-screen, and one
     * near the bottom flips above the cursor. A full-window tooltipped widget
     * lets the pointer rest near any edge while staying over the target. */
    {
        cl_platform_t *p2 = cl_platform_mock_create(a);
        cl_renderer_t *r2 = cl_renderer_mock_create(a);
        cl_application_desc_t ad2 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app2;
        cl_font_t *font2;
        cl_window_t *win2;
        cl_widget_t *big;
        cl_window_desc_t wd2 = CL_WINDOW_DESC_INIT;
        const float W = 200.0f;
        const float H = 150.0f;
        cl_rect_t r;

        ad2.platform = p2;
        ad2.renderer = r2;
        app2 = cl_application_create(&ad2);
        font2 = load_any_font(app2);
        if (!font2) {
            cl_application_destroy(app2);
            if (failures == 0)
                printf("all tooltip tests passed\n");
            return failures == 0 ? 0 : 1;
        }
        cl_theme_set_font(cl_application_theme(app2), font2);
        wd2.width = (int)W;
        wd2.height = (int)H;
        win2 = cl_window_create(app2, &wd2);
        big = cl_button_create(
            app2, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS, .text = "x" });
        cl_widget_set_tooltip(big, "Bubble"); /* narrow enough to fit in W */
        cl_window_set_content(win2, big);     /* fills the whole window */
        cl_application_step(app2, false);

        /* Right edge: the bubble would overflow, so it is pulled left. */
        move_to(p2, (cl_point_t){ W - 2.0f, 30.0f });
        cl_application_step(app2, false);
        cl_platform_mock_advance(p2, DWELL + 100);
        cl_application_step(app2, false);
        CHECK(cl_window_tooltip(win2) != NULL);
        r = cl_widget_rect(cl_window_tooltip(win2));
        CHECK(r.w > 2.0f);              /* would overflow without the clamp */
        CHECK(r.x + r.w <= W + 0.5f);   /* clamped on-screen */
        CHECK(r.x >= 0.0f);
        click_at(p2, CL_PEV_MOUSE_DOWN, (cl_point_t){ W - 2.0f, 30.0f });
        click_at(p2, CL_PEV_MOUSE_UP, (cl_point_t){ W - 2.0f, 30.0f });
        cl_application_step(app2, false); /* dismiss for the next case */

        /* Bottom edge: no room below the cursor, so the bubble flips above it. */
        move_to(p2, (cl_point_t){ 30.0f, H - 2.0f });
        cl_application_step(app2, false);
        cl_platform_mock_advance(p2, DWELL + 100);
        cl_application_step(app2, false);
        CHECK(cl_window_tooltip(win2) != NULL);
        r = cl_widget_rect(cl_window_tooltip(win2));
        CHECK(r.y + r.h <= (H - 2.0f) + 0.5f); /* above the cursor, not below */
        CHECK(r.y >= 0.0f);
        CHECK(r.y + r.h <= H + 0.5f);

        cl_font_release(font2);
        cl_application_destroy(app2);
    }

    if (failures == 0)
        printf("all tooltip tests passed\n");
    return failures == 0 ? 0 : 1;
}
