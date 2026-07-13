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
static int value_count;
static float last_value;

static void on_toggle(cl_widget_t *w, bool checked, void *user)
{
    (void)w;
    (void)user;
    toggle_count++;
    last_state = checked;
}

static void on_value(cl_widget_t *w, float value, void *user)
{
    (void)w;
    (void)user;
    value_count++;
    last_value = value;
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

/* imageview: measures to the pixel size and emits a draw_image command. */
static void test_imageview(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = 100, .height = 100 };
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS };
    cl_platform_t *plat = cl_platform_mock_create(cl_allocator_default());
    cl_renderer_t *rend = cl_renderer_mock_create(cl_allocator_default());
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *box;
    cl_widget_t *iv;
    cl_image_t *img;
    static const unsigned char px[3 * 2 * 4] = { 0 };
    bool saw_image = false;
    size_t i, n;

    ad.platform = plat;
    ad.renderer = rend;
    app = cl_application_create(&ad);
    img = cl_image_create(app, 3, 2, px);
    CHECK(img != NULL);
    CHECK(cl_image_pixels(img) != NULL);
    CHECK(cl_image_create(app, 0, 2, px) == NULL); /* bad size rejected */
    CHECK(cl_last_error() == CL_ERROR_INVALID_ARGUMENT);

    win = cl_window_create(app, &wd);
    box = cl_vbox_create(app, &vd);
    iv = cl_imageview_create(
        app, &(cl_imageview_desc_t){ CL_IMAGEVIEW_DESC_INIT_FIELDS,
                                     .image = img });
    CHECK(cl_imageview_image(iv) == img);
    cl_widget_add_child(box, iv);
    cl_window_set_content(win, box);
    cl_application_step(app, false);

    /* natural size = pixel size */
    CHECK(cl_widget_rect(iv).w == 3.0f);
    CHECK(cl_widget_rect(iv).h == 2.0f);
    n = cl_renderer_mock_count(rend);
    for (i = 0; i < n; i++)
        if (cl_renderer_mock_get(rend, i)->kind == CL_MOCK_IMAGE) {
            saw_image = true;
            CHECK(cl_renderer_mock_get(rend, i)->image == img);
        }
    CHECK(saw_image);

    cl_imageview_set_image(iv, NULL); /* allowed: paints nothing */
    cl_application_step(app, false);

    cl_image_release(img);
    cl_application_destroy(app);
}

static int sel_fires, sel_last, act_fires, act_last;

static void on_sel(cl_widget_t *l, int idx, void *user)
{
    (void)l;
    (void)user;
    sel_fires++;
    sel_last = idx;
}

static void on_act(cl_widget_t *l, int idx, void *user)
{
    (void)l;
    (void)user;
    act_fires++;
    act_last = idx;
}

/* list: selection via mouse and keyboard, activation, mutation. */
static void test_list(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = 300, .height = 300 };
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS,
                          .align_cross = CL_ALIGN_STRETCH };
    cl_platform_t *plat = cl_platform_mock_create(cl_allocator_default());
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *box;
    cl_widget_t *list;
    cl_rect_t r;
    /* fallback row height: 16 + 2*4 = 24; top pad 4 */
    const float row_h = 24.0f, top_pad = 4.0f;
    cl_platform_event_t pe = { 0 };

    ad.platform = plat;
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);
    win = cl_window_create(app, &wd);
    box = cl_vbox_create(app, &vd);
    list = cl_list_create(app, &(cl_list_desc_t){ CL_LIST_DESC_INIT_FIELDS });
    CHECK(cl_list_add_item(list, "alpha") == CL_OK);
    CHECK(cl_list_add_item(list, "beta") == CL_OK);
    CHECK(cl_list_add_item(list, "gamma") == CL_OK);
    CHECK(cl_list_count(list) == 3);
    CHECK(strcmp(cl_list_item_text(list, 1), "beta") == 0);
    CHECK(cl_list_item_text(list, 9) == NULL);
    cl_list_set_on_select(list, on_sel, NULL);
    cl_list_set_on_activate(list, on_act, NULL);
    cl_widget_add_child(box, list);
    cl_window_set_content(win, box);
    cl_application_step(app, false);

    /* click row 1 selects it (and focuses the list) */
    r = cl_widget_rect(list);
    click(plat, r.x + 10.0f, r.y + top_pad + 1.5f * row_h);
    cl_application_step(app, false);
    CHECK(cl_list_selected(list) == 1);
    CHECK(sel_fires == 1 && sel_last == 1);
    CHECK(cl_widget_has_focus(list));

    /* keyboard: Down, End, Home, PageDown */
    press(plat, CL_KEY_DOWN);
    cl_application_step(app, false);
    CHECK(cl_list_selected(list) == 2);
    press(plat, CL_KEY_HOME);
    cl_application_step(app, false);
    CHECK(cl_list_selected(list) == 0);
    press(plat, CL_KEY_PAGE_DOWN); /* clamps to the last row */
    cl_application_step(app, false);
    CHECK(cl_list_selected(list) == 2);

    /* Enter activates the selection */
    press(plat, CL_KEY_ENTER);
    cl_application_step(app, false);
    CHECK(act_fires == 1 && act_last == 2);

    /* double-click activates too */
    pe.kind = CL_PEV_MOUSE_DOWN;
    pe.pos = (cl_point_t){ r.x + 10.0f, r.y + top_pad + 2.5f * row_h };
    pe.button = CL_MOUSE_LEFT;
    pe.clicks = 2;
    cl_platform_mock_push(plat, pe);
    cl_application_step(app, false);
    CHECK(act_fires == 2 && act_last == 2);

    /* removal keeps the selection consistent */
    cl_list_set_selected(list, 2);
    CHECK(cl_list_remove(list, 0) == CL_OK);
    CHECK(cl_list_selected(list) == 1); /* shifted up */
    CHECK(cl_list_remove(list, 1) == CL_OK);
    CHECK(cl_list_selected(list) == -1); /* the selected row was removed */
    cl_list_clear(list);
    CHECK(cl_list_count(list) == 0);

    cl_application_destroy(app);
}

/* progressbar: clamping and the accent fill appearing per value. */
static void test_progressbar(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = 200, .height = 100 };
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS };
    cl_platform_t *plat = cl_platform_mock_create(cl_allocator_default());
    cl_renderer_t *rend = cl_renderer_mock_create(cl_allocator_default());
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *box;
    cl_widget_t *pb;
    cl_color_t accent;
    size_t i, n;
    bool saw_accent;

    ad.platform = plat;
    ad.renderer = rend;
    app = cl_application_create(&ad);
    accent = cl_theme_color(cl_application_theme(app), CL_COLOR_ACCENT);
    win = cl_window_create(app, &wd);
    box = cl_vbox_create(app, &vd);
    pb = cl_progressbar_create(
        app, &(cl_progressbar_desc_t){ CL_PROGRESSBAR_DESC_INIT_FIELDS,
                                       .value = 2.0f });
    CHECK(cl_progressbar_value(pb) == 1.0f); /* clamped */
    cl_progressbar_set_value(pb, -1.0f);
    CHECK(cl_progressbar_value(pb) == 0.0f);
    cl_widget_add_child(box, pb);
    cl_window_set_content(win, box);
    cl_application_step(app, false);

    /* value 0: no accent-coloured fill in the frame */
    saw_accent = false;
    n = cl_renderer_mock_count(rend);
    for (i = 0; i < n; i++) {
        const cl_mock_command_t *c = cl_renderer_mock_get(rend, i);

        if (c->kind == CL_MOCK_FILL_ROUND && c->color.r == accent.r &&
            c->color.g == accent.g && c->color.b == accent.b)
            saw_accent = true;
    }
    CHECK(!saw_accent);

    cl_progressbar_set_value(pb, 0.5f);
    cl_application_step(app, false);
    saw_accent = false;
    n = cl_renderer_mock_count(rend);
    for (i = 0; i < n; i++) {
        const cl_mock_command_t *c = cl_renderer_mock_get(rend, i);

        if (c->kind == CL_MOCK_FILL_ROUND && c->color.r == accent.r &&
            c->color.g == accent.g && c->color.b == accent.b) {
            saw_accent = true;
            /* half of the bar's width */
            CHECK(c->rect.w == cl_widget_rect(pb).w * 0.5f);
        }
    }
    CHECK(saw_accent);

    cl_application_destroy(app);
}

/* menu item_text/remove/clear and the symmetric widget getters. */
static void test_api_symmetry(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_application_t *app;
    cl_widget_t *menu;
    cl_widget_t *sub;
    cl_widget_t *box;

    ad.platform = cl_platform_mock_create(cl_allocator_default());
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);

    menu = cl_menu_create(app, &(cl_menu_desc_t){ CL_MENU_DESC_INIT_FIELDS });
    sub = cl_menu_create(app, &(cl_menu_desc_t){ CL_MENU_DESC_INIT_FIELDS });
    cl_menu_add_item(menu, "cut", NULL, NULL);
    cl_menu_add_submenu(menu, "more", sub);
    CHECK(cl_menu_count(menu) == 2);
    CHECK(strcmp(cl_menu_item_text(menu, 1), "more") == 0);
    CHECK(cl_menu_item_text(menu, 2) == NULL);
    CHECK(cl_menu_remove(menu, 1) == CL_OK); /* destroys the submenu */
    CHECK(cl_menu_count(menu) == 1);
    cl_menu_clear(menu);
    CHECK(cl_menu_count(menu) == 0);
    cl_widget_destroy(menu);

    box = cl_vbox_create(app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS });
    cl_widget_set_flex(box, 2.5f);
    CHECK(cl_widget_flex(box) == 2.5f);
    cl_widget_set_margin(box, (cl_insets_t){ 1, 2, 3, 4 });
    CHECK(cl_widget_margin(box).top == 2.0f);
    cl_widget_set_preferred_size(box, (cl_size_t){ 50, 60 });
    CHECK(cl_widget_preferred_size(box).h == 60.0f);
    cl_widget_set_align(box, CL_ALIGN_CENTER, CL_ALIGN_END);
    CHECK(cl_widget_align_h(box) == CL_ALIGN_CENTER);
    CHECK(cl_widget_align_v(box) == CL_ALIGN_END);
    CHECK(!cl_widget_is_focusable(box));
    cl_widget_set_focusable(box, true);
    CHECK(cl_widget_is_focusable(box));
    cl_widget_destroy(box);

    cl_application_destroy(app);
}

/* panel/spacer/radiogroup: grouping surface, flexible gap, exclusivity. */
static int rg_fires, rg_last;

static void on_rg(cl_widget_t *g, int idx, void *user)
{
    (void)g;
    (void)user;
    rg_fires++;
    rg_last = idx;
}

static void test_panel_spacer_radiogroup(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = 300, .height = 300 };
    cl_hbox_desc_t hd = { CL_HBOX_DESC_INIT_FIELDS };
    cl_platform_t *plat = cl_platform_mock_create(cl_allocator_default());
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *row;
    cl_widget_t *a;
    cl_widget_t *sp;
    cl_widget_t *b;
    cl_widget_t *panel;
    cl_widget_t *group;
    cl_widget_t *opt0;
    cl_widget_t *opt1;
    cl_rect_t r;

    ad.platform = plat;
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);
    win = cl_window_create(app, &wd);

    /* spacer with flex pushes the second child to the far edge */
    row = cl_hbox_create(app, &hd);
    a = bare_checkbox(app);
    sp = cl_spacer_create(
        app, &(cl_spacer_desc_t){ CL_SPACER_DESC_INIT_FIELDS, .flex = 1.0f });
    b = bare_checkbox(app);
    cl_widget_add_child(row, a);
    cl_widget_add_child(row, sp);
    cl_widget_add_child(row, b);
    cl_window_set_content(win, row);
    cl_application_step(app, false);
    r = cl_widget_rect(b);
    CHECK(r.x + r.w == 300.0f); /* pinned to the right edge */

    /* panel: children fill the padded content box */
    panel = cl_panel_create(
        app, &(cl_panel_desc_t){ CL_PANEL_DESC_INIT_FIELDS,
                                 .padding = { 10, 10, 10, 10 },
                                 .bordered = true });
    group = cl_radiogroup_create(
        app, &(cl_radiogroup_desc_t){ CL_RADIOGROUP_DESC_INIT_FIELDS,
                                      .spacing = 6.0f });
    opt0 = cl_radiogroup_add(group, "left");
    opt1 = cl_radiogroup_add(group, "right");
    CHECK(opt0 && opt1);
    CHECK(cl_radiogroup_count(group) == 2);
    CHECK(cl_radiogroup_selected(group) == -1);
    cl_radiogroup_set_on_change(group, on_rg, NULL);
    cl_widget_add_child(panel, group);
    cl_window_set_content(win, panel);
    cl_application_step(app, false);

    /* the group sits inside the panel's padding */
    CHECK(cl_widget_rect(group).x == cl_widget_rect(panel).x + 10.0f);

    /* clicking option 1 selects it exclusively and fires the callback */
    rg_fires = 0;
    rg_last = -1;
    r = cl_widget_rect(opt1);
    click(plat, r.x + 8.0f, r.y + r.h * 0.5f);
    cl_application_step(app, false);
    CHECK(cl_radiogroup_selected(group) == 1);
    CHECK(rg_fires == 1 && rg_last == 1);
    CHECK(!cl_radiobutton_is_selected(opt0));

    /* the programmatic setter is exclusive and silent */
    cl_radiogroup_set_selected(group, 0);
    CHECK(cl_radiogroup_selected(group) == 0);
    CHECK(!cl_radiobutton_is_selected(opt1));
    CHECK(rg_fires == 1); /* unchanged */

    cl_application_destroy(app);
}

int main(void)
{
    test_api_symmetry();
    test_panel_spacer_radiogroup();
    test_imageview();
    test_list();
    test_progressbar();
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

    /* --- RadioButton group exclusivity --- */
    {
        cl_platform_t *plat = cl_platform_mock_create(a);
        cl_renderer_t *rend = cl_renderer_mock_create(a);
        cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
        cl_application_t *app;
        cl_window_t *win;
        cl_widget_t *box;
        cl_widget_t *r0;
        cl_widget_t *r1;
        cl_widget_t *r2;
        cl_widget_t *other; /* different group, must stay independent */
        cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
        cl_rect_t rr;

        ad.platform = plat;
        ad.renderer = rend;
        app = cl_application_create(&ad);
        wd.width = 120;
        wd.height = 160;
        win = cl_window_create(app, &wd);
        box = cl_vbox_create(app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS,
                                                     .spacing = 4 });
        r0 = cl_radiobutton_create(
            app, &(cl_radiobutton_desc_t){ CL_RADIOBUTTON_DESC_INIT_FIELDS,
                                           .group = 1 });
        r1 = cl_radiobutton_create(
            app, &(cl_radiobutton_desc_t){ CL_RADIOBUTTON_DESC_INIT_FIELDS,
                                           .group = 1 });
        r2 = cl_radiobutton_create(
            app, &(cl_radiobutton_desc_t){ CL_RADIOBUTTON_DESC_INIT_FIELDS,
                                           .group = 1 });
        other = cl_radiobutton_create(
            app, &(cl_radiobutton_desc_t){ CL_RADIOBUTTON_DESC_INIT_FIELDS,
                                           .group = 2, .selected = true });
        cl_radiobutton_set_on_select(r0, on_toggle, NULL);
        cl_radiobutton_set_on_select(r1, on_toggle, NULL);
        cl_widget_add_child(box, r0);
        cl_widget_add_child(box, r1);
        cl_widget_add_child(box, r2);
        cl_widget_add_child(box, other);
        cl_window_set_content(win, box);
        cl_application_step(app, false);

        toggle_count = 0;
        rr = cl_widget_rect(r0);
        click(plat, rr.x + rr.w * 0.5f, rr.y + rr.h * 0.5f);
        cl_application_step(app, false);
        CHECK(cl_radiobutton_is_selected(r0));
        CHECK(!cl_radiobutton_is_selected(r1));
        CHECK(!cl_radiobutton_is_selected(r2));
        CHECK(cl_radiobutton_is_selected(other)); /* group 2 untouched */
        CHECK(toggle_count == 1);

        rr = cl_widget_rect(r1);
        click(plat, rr.x + rr.w * 0.5f, rr.y + rr.h * 0.5f);
        cl_application_step(app, false);
        CHECK(!cl_radiobutton_is_selected(r0));
        CHECK(cl_radiobutton_is_selected(r1));
        CHECK(toggle_count == 2);

        /* set_selected selects + deselects the group but does not fire */
        cl_radiobutton_set_selected(r2, true);
        CHECK(cl_radiobutton_is_selected(r2));
        CHECK(!cl_radiobutton_is_selected(r1));
        CHECK(toggle_count == 2);

        /* Space selects the focused radio */
        CHECK(cl_widget_focus(r0));
        press(plat, CL_KEY_SPACE);
        cl_application_step(app, false);
        CHECK(cl_radiobutton_is_selected(r0));
        CHECK(!cl_radiobutton_is_selected(r2));
        CHECK(cl_radiobutton_is_selected(other));

        cl_application_destroy(app);
    }

    /* --- Slider value from click, keyboard, drag; set_value is silent --- */
    {
        cl_platform_t *plat = cl_platform_mock_create(a);
        cl_renderer_t *rend = cl_renderer_mock_create(a);
        cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
        cl_application_t *app;
        cl_window_t *win;
        cl_widget_t *sl;
        cl_window_desc_t wd = CL_WINDOW_DESC_INIT;

        ad.platform = plat;
        ad.renderer = rend;
        app = cl_application_create(&ad);
        wd.width = 120; /* track_left = 8, track_width = 104 */
        wd.height = 40;
        win = cl_window_create(app, &wd);
        sl = cl_slider_create(
            app, &(cl_slider_desc_t){ CL_SLIDER_DESC_INIT_FIELDS, .min = 0,
                                      .max = 100, .value = 0 });
        cl_slider_set_on_change(sl, on_value, NULL);
        cl_window_set_content(win, sl);
        cl_application_step(app, false);
        CHECK(cl_widget_focus(sl));

        value_count = 0;
        click(plat, 60.0f, 20.0f); /* frac (60-8)/104 = 0.5 -> 50 */
        cl_application_step(app, false);
        CHECK(cl_slider_value(sl) == 50.0f);
        CHECK(value_count == 1 && last_value == 50.0f);

        press(plat, CL_KEY_RIGHT); /* step = (100-0)/20 = 5 -> 55 */
        cl_application_step(app, false);
        CHECK(cl_slider_value(sl) == 55.0f);

        press(plat, CL_KEY_HOME);
        cl_application_step(app, false);
        CHECK(cl_slider_value(sl) == 0.0f);

        press(plat, CL_KEY_END);
        cl_application_step(app, false);
        CHECK(cl_slider_value(sl) == 100.0f);

        /* set_value clamps and is silent */
        {
            int before = value_count;

            cl_slider_set_value(sl, 25.0f);
            CHECK(cl_slider_value(sl) == 25.0f);
            cl_slider_set_value(sl, 999.0f); /* clamps to max */
            CHECK(cl_slider_value(sl) == 100.0f);
            CHECK(value_count == before);
        }

        /* drag from the far left to the far right */
        click(plat, 8.0f, 20.0f); /* value 0, begins drag */
        cl_application_step(app, false);
        CHECK(cl_slider_value(sl) == 0.0f);
        {
            cl_platform_event_t mv;

            memset(&mv, 0, sizeof(mv));
            mv.kind = CL_PEV_MOUSE_MOVE;
            mv.pos.x = 112.0f; /* track_left + track_width */
            mv.pos.y = 20.0f;
            cl_platform_mock_push(plat, mv);
        }
        cl_application_step(app, false);
        CHECK(cl_slider_value(sl) == 100.0f);

        /* set_range re-derives the auto step for the new range */
        cl_slider_set_range(sl, 0.0f, 10.0f);
        CHECK(cl_slider_value(sl) == 10.0f); /* clamped from 100 */
        cl_slider_set_value(sl, 5.0f);
        press(plat, CL_KEY_RIGHT); /* step (10-0)/20 = 0.5 -> 5.5 */
        cl_application_step(app, false);
        CHECK(cl_slider_value(sl) == 5.5f);

        cl_application_destroy(app);
    }

    /* --- Ungrouped radios (default/negative group) are independent --- */
    {
        cl_platform_t *plat = cl_platform_mock_create(a);
        cl_renderer_t *rend = cl_renderer_mock_create(a);
        cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
        cl_application_t *app;
        cl_window_t *win;
        cl_widget_t *box;
        cl_widget_t *a0;
        cl_widget_t *a1;
        cl_window_desc_t wd = CL_WINDOW_DESC_INIT;

        ad.platform = plat;
        ad.renderer = rend;
        app = cl_application_create(&ad);
        wd.width = 120;
        wd.height = 80;
        win = cl_window_create(app, &wd);
        box = cl_vbox_create(app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS });
        a0 = cl_radiobutton_create( /* no .group -> default -1 (ungrouped) */
            app, &(cl_radiobutton_desc_t){ CL_RADIOBUTTON_DESC_INIT_FIELDS });
        a1 = cl_radiobutton_create(
            app, &(cl_radiobutton_desc_t){ CL_RADIOBUTTON_DESC_INIT_FIELDS });
        cl_widget_add_child(box, a0);
        cl_widget_add_child(box, a1);
        cl_window_set_content(win, box);
        cl_application_step(app, false);

        cl_radiobutton_set_selected(a0, true);
        cl_radiobutton_set_selected(a1, true);
        CHECK(cl_radiobutton_is_selected(a0)); /* independent: both stay on */
        CHECK(cl_radiobutton_is_selected(a1));
        cl_radiobutton_set_selected(a0, false);
        CHECK(!cl_radiobutton_is_selected(a0));
        CHECK(cl_radiobutton_is_selected(a1));
        cl_application_destroy(app);
    }

    if (failures == 0)
        printf("all widget tests passed\n");
    return failures == 0 ? 0 : 1;
}
