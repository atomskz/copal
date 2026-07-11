/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Headless HBox + Checkbox test. Both are deterministic without a font: the
 * checkbox indicator is a fixed 16x16 box, so layout positions are exact.
 */
#include <copal/copal.h>

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

static int toggle_count;
static bool last_state;

static void on_toggle(cl_widget_t *w, bool checked, void *user)
{
    (void)w;
    (void)user;
    toggle_count++;
    last_state = checked;
}

static void press(cl_platform_t *p, cl_key_t key)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_KEY_DOWN;
    ev.key = key;
    cl_platform_mock_push(p, ev);
}

static void click_button(cl_platform_t *p, float x, float y,
                         cl_mouse_button_t button)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_MOUSE_DOWN;
    ev.pos.x = x;
    ev.pos.y = y;
    ev.button = button;
    cl_platform_mock_push(p, ev);
}

static void click(cl_platform_t *p, float x, float y)
{
    click_button(p, x, y, CL_MOUSE_LEFT);
}

static cl_widget_t *bare_checkbox(cl_application_t *app)
{
    return cl_checkbox_create(
        app, &(cl_checkbox_desc_t){ CL_CHECKBOX_DESC_INIT_FIELDS });
}

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();

    /* --- HBox lays children left-to-right with padding + spacing --- */
    {
        cl_platform_t *plat = cl_platform_mock_create(a);
        cl_renderer_t *rend = cl_renderer_mock_create(a);
        cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
        cl_application_t *app;
        cl_window_t *win;
        cl_widget_t *hbox;
        cl_widget_t *c0;
        cl_widget_t *c1;
        cl_widget_t *c2;
        cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
        cl_rect_t r0;
        cl_rect_t r1;
        cl_rect_t r2;

        ad.platform = plat;
        ad.renderer = rend;
        app = cl_application_create(&ad);
        wd.width = 200;
        wd.height = 100;
        win = cl_window_create(app, &wd);
        hbox = cl_hbox_create(app, &(cl_hbox_desc_t){ CL_HBOX_DESC_INIT_FIELDS,
                                                      .spacing = 8,
                                                      .padding = { 10, 10, 10,
                                                                   10 } });
        c0 = bare_checkbox(app);
        c1 = bare_checkbox(app);
        c2 = bare_checkbox(app);
        cl_widget_add_child(hbox, c0);
        cl_widget_add_child(hbox, c1);
        cl_widget_add_child(hbox, c2);
        cl_window_set_content(win, hbox);
        cl_application_step(app, false);

        r0 = cl_widget_rect(c0);
        r1 = cl_widget_rect(c1);
        r2 = cl_widget_rect(c2);
        CHECK(r0.x == 10.0f && r0.y == 10.0f); /* padding */
        CHECK(r0.w == 16.0f && r0.h == 16.0f); /* fixed indicator size */
        CHECK(r1.x == 34.0f);                  /* 10 + 16 + 8 spacing */
        CHECK(r2.x == 58.0f);                  /* 34 + 16 + 8 spacing */
        CHECK(r1.y == 10.0f && r2.y == 10.0f);
        cl_application_destroy(app);
    }

    /* --- Checkbox toggles on click, Space and Enter; set_checked is silent --- */
    {
        cl_platform_t *plat = cl_platform_mock_create(a);
        cl_renderer_t *rend = cl_renderer_mock_create(a);
        cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
        cl_application_t *app;
        cl_window_t *win;
        cl_widget_t *cb;
        cl_window_desc_t wd = CL_WINDOW_DESC_INIT;

        ad.platform = plat;
        ad.renderer = rend;
        app = cl_application_create(&ad);
        wd.width = 120;
        wd.height = 40;
        win = cl_window_create(app, &wd);
        cb = bare_checkbox(app);
        cl_checkbox_set_on_toggle(cb, on_toggle, NULL);
        cl_window_set_content(win, cb);
        cl_application_step(app, false);
        CHECK(cl_widget_focus(cb));

        toggle_count = 0;
        click(plat, 8.0f, 20.0f);
        cl_application_step(app, false);
        CHECK(cl_checkbox_is_checked(cb));
        CHECK(toggle_count == 1 && last_state == true);

        click(plat, 8.0f, 20.0f);
        cl_application_step(app, false);
        CHECK(!cl_checkbox_is_checked(cb));
        CHECK(toggle_count == 2 && last_state == false);

        press(plat, CL_KEY_SPACE);
        cl_application_step(app, false);
        CHECK(cl_checkbox_is_checked(cb));
        CHECK(toggle_count == 3);

        /* Enter is not a checkbox activator: no toggle, and it bubbles. */
        press(plat, CL_KEY_ENTER);
        cl_application_step(app, false);
        CHECK(cl_checkbox_is_checked(cb)); /* unchanged */
        CHECK(toggle_count == 3);

        /* Only the primary button toggles; a right click is ignored. */
        click_button(plat, 8.0f, 20.0f, CL_MOUSE_RIGHT);
        cl_application_step(app, false);
        CHECK(cl_checkbox_is_checked(cb)); /* unchanged */
        CHECK(toggle_count == 3);

        /* set_checked changes state but must not fire on_toggle */
        {
            int before = toggle_count;

            cl_checkbox_set_checked(cb, true);
            CHECK(cl_checkbox_is_checked(cb));
            CHECK(toggle_count == before);
            cl_checkbox_set_checked(cb, true); /* no-op, same value */
            CHECK(toggle_count == before);
        }
        cl_application_destroy(app);
    }

    if (failures == 0)
        printf("all widget tests passed\n");
    return failures == 0 ? 0 : 1;
}
