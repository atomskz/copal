/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Headless overlay/menu test. Deterministic without a font: the menu item
 * height uses a 16px fallback line height, so item hit rows are exact.
 * Also exercises the deferred-close path (a menu item closes its own menu)
 * under ASan to prove there is no use-after-free.
 */
#include <copal/copal.h>

#include <stdint.h>
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

/* Item height with no font: 16 (fallback) + 2*5 (ITEM_VPAD) = 26; top pad 5. */
#define ITEM_H 26.0f
#define TOP_PAD 5.0f

static int menu_fires;
static int last_index;
static int cb_toggles;

static void on_item(cl_widget_t *w, void *user)
{
    (void)w;
    menu_fires++;
    last_index = (int)(intptr_t)user;
}

static void on_toggle(cl_widget_t *w, bool checked, void *user)
{
    (void)w;
    (void)checked;
    (void)user;
    cb_toggles++;
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

static void press(cl_platform_t *p, cl_key_t key)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_KEY_DOWN;
    ev.key = key;
    cl_platform_mock_push(p, ev);
}

static cl_widget_t *make_menu(cl_application_t *app)
{
    cl_widget_t *menu = cl_menu_create(app);

    cl_menu_add_item(menu, "Cut", on_item, (void *)(intptr_t)0);
    cl_menu_add_item(menu, "Copy", on_item, (void *)(intptr_t)1);
    cl_menu_add_item(menu, "Paste", on_item, (void *)(intptr_t)2);
    return menu;
}

/* Centre y of item i for a menu opened with its top-left at oy. */
static float item_y(float oy, int i)
{
    return oy + TOP_PAD + (float)i * ITEM_H + ITEM_H * 0.5f;
}

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_platform_t *plat = cl_platform_mock_create(a);
    cl_renderer_t *rend = cl_renderer_mock_create(a);
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *content;
    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;

    ad.platform = plat;
    ad.renderer = rend;
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    if (!app)
        return 1;
    wd.width = 200;
    wd.height = 200;
    win = cl_window_create(app, &wd);
    content = cl_checkbox_create(
        app, &(cl_checkbox_desc_t){ CL_CHECKBOX_DESC_INIT_FIELDS });
    cl_checkbox_set_on_toggle(content, on_toggle, NULL);
    cl_window_set_content(win, content);
    cl_application_step(app, false);

    /* Click an item: fires its callback and closes the menu (deferred reap). */
    menu_fires = 0;
    last_index = -1;
    cl_window_open_popup(win, make_menu(app), (cl_point_t){ 10.0f, 10.0f });
    CHECK(cl_window_popup(win) != NULL);
    CHECK(cl_menu_count(cl_window_popup(win)) == 3);
    mouse(plat, CL_PEV_MOUSE_DOWN, 20.0f, item_y(10.0f, 1));
    mouse(plat, CL_PEV_MOUSE_UP, 20.0f, item_y(10.0f, 1));
    cl_application_step(app, false);
    CHECK(menu_fires == 1);
    CHECK(last_index == 1);
    CHECK(cl_window_popup(win) == NULL); /* reaped, no use-after-free */

    /* Light dismiss: a press outside the menu closes it and does NOT reach the
     * content behind it. */
    cb_toggles = 0;
    cl_window_open_popup(win, make_menu(app), (cl_point_t){ 10.0f, 10.0f });
    mouse(plat, CL_PEV_MOUSE_DOWN, 180.0f, 180.0f); /* outside the menu */
    mouse(plat, CL_PEV_MOUSE_UP, 180.0f, 180.0f);
    cl_application_step(app, false);
    CHECK(cl_window_popup(win) == NULL);
    CHECK(cb_toggles == 0); /* the content was shielded by the popup */

    /* A closing (not yet reaped) popup still captures: a second press queued in
     * the same batch as the dismiss must not reach the content behind it. */
    cb_toggles = 0;
    cl_window_open_popup(win, make_menu(app), (cl_point_t){ 10.0f, 10.0f });
    mouse(plat, CL_PEV_MOUSE_DOWN, 180.0f, 180.0f); /* dismiss */
    mouse(plat, CL_PEV_MOUSE_DOWN, 180.0f, 60.0f);  /* over content, same batch */
    cl_application_step(app, false);
    CHECK(cl_window_popup(win) == NULL);
    CHECK(cb_toggles == 0); /* checkbox toggles on mouse-down; must stay shielded */

    /* Escape dismisses. */
    cl_window_open_popup(win, make_menu(app), (cl_point_t){ 10.0f, 10.0f });
    press(plat, CL_KEY_ESCAPE);
    cl_application_step(app, false);
    CHECK(cl_window_popup(win) == NULL);

    /* Keyboard navigation: Down, Down, Enter activates item 1. */
    menu_fires = 0;
    last_index = -1;
    cl_window_open_popup(win, make_menu(app), (cl_point_t){ 10.0f, 10.0f });
    press(plat, CL_KEY_DOWN); /* hover 0 */
    press(plat, CL_KEY_DOWN); /* hover 1 */
    press(plat, CL_KEY_ENTER);
    cl_application_step(app, false);
    CHECK(menu_fires == 1);
    CHECK(last_index == 1);
    CHECK(cl_window_popup(win) == NULL);

    /* A popup is re-clamped on-screen when the window resizes. */
    {
        cl_platform_event_t rz;
        cl_rect_t r;

        cl_window_open_popup(win, make_menu(app), (cl_point_t){ 150.0f, 20.0f });
        r = cl_widget_rect(cl_window_popup(win));
        CHECK(r.x == 60.0f); /* 140-wide menu at x=150 in a 200px window -> 60 */

        memset(&rz, 0, sizeof(rz));
        rz.kind = CL_PEV_RESIZE;
        rz.size.w = 400.0f;
        rz.size.h = 200.0f;
        cl_platform_mock_push(plat, rz);
        cl_application_step(app, false);
        r = cl_widget_rect(cl_window_popup(win));
        CHECK(r.x == 150.0f); /* wider window: the anchor now fits, no clamp */

        cl_window_close_popup(win);
        cl_application_step(app, false);
    }

    /* Opening a popup while one is open replaces it (old one freed). */
    cl_window_open_popup(win, make_menu(app), (cl_point_t){ 10.0f, 10.0f });
    cl_window_open_popup(win, make_menu(app), (cl_point_t){ 30.0f, 30.0f });
    CHECK(cl_window_popup(win) != NULL);
    cl_window_close_popup(win);
    cl_application_step(app, false);
    CHECK(cl_window_popup(win) == NULL);

    /* Submenus: a stacked chain opens, routes and collapses correctly. */
    {
        cl_widget_t *root = cl_menu_create(app);
        cl_widget_t *sub = cl_menu_create(app);
        cl_rect_t sr;

        cl_menu_add_item(sub, "Sub A", on_item, (void *)(intptr_t)7);
        cl_menu_add_item(root, "Top", on_item, (void *)(intptr_t)0);
        CHECK(cl_menu_add_submenu(root, "More", sub) == CL_OK);
        CHECK(cl_menu_count(root) == 2);

        menu_fires = 0;
        last_index = -1;
        cl_window_open_popup(win, root, (cl_point_t){ 10.0f, 10.0f });
        /* click item 1 ("More >") opens the submenu on top */
        mouse(plat, CL_PEV_MOUSE_DOWN, 20.0f, item_y(10.0f, 1));
        mouse(plat, CL_PEV_MOUSE_UP, 20.0f, item_y(10.0f, 1));
        cl_application_step(app, false);
        CHECK(cl_window_popup(win) == sub); /* the submenu is topmost */
        CHECK(menu_fires == 0);

        /* Escape pops only the submenu; the parent menu survives */
        press(plat, CL_KEY_ESCAPE);
        cl_application_step(app, false);
        CHECK(cl_window_popup(win) == root);
        CHECK(cl_widget_window(sub) == NULL); /* detached for reuse */

        /* re-open the submenu from the still-open parent (widget reused) */
        mouse(plat, CL_PEV_MOUSE_DOWN, 20.0f, item_y(10.0f, 1));
        mouse(plat, CL_PEV_MOUSE_UP, 20.0f, item_y(10.0f, 1));
        cl_application_step(app, false);
        CHECK(cl_window_popup(win) == sub);

        /* activate "Sub A" in the submenu: fires and collapses the chain
         * (the window owns the root menu: the whole chain is destroyed) */
        sr = cl_widget_rect(sub);
        mouse(plat, CL_PEV_MOUSE_DOWN, sr.x + 10.0f, item_y(sr.y, 0));
        mouse(plat, CL_PEV_MOUSE_UP, sr.x + 10.0f, item_y(sr.y, 0));
        cl_application_step(app, false);
        CHECK(menu_fires == 1);
        CHECK(last_index == 7);
        CHECK(cl_window_popup(win) == NULL);
    }

    /* Modal dialog: outside clicks do not dismiss. */
    {
        cl_widget_t *dlg = cl_vbox_create(
            app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS });

        cl_widget_set_preferred_size(dlg, (cl_size_t){ 80.0f, 40.0f });
        cl_window_open_modal(win, dlg);
        cl_application_step(app, false);
        CHECK(cl_window_popup(win) == dlg);
        /* centred in the 400x200 window: (400-80)/2 = 160 */
        CHECK(cl_widget_rect(dlg).x == 160.0f);

        mouse(plat, CL_PEV_MOUSE_DOWN, 5.0f, 195.0f); /* far outside */
        mouse(plat, CL_PEV_MOUSE_UP, 5.0f, 195.0f);
        cl_application_step(app, false);
        CHECK(cl_window_popup(win) == dlg); /* still open */

        cl_window_close_popup(win);
        cl_application_step(app, false);
        CHECK(cl_window_popup(win) == NULL);
    }

    /* Menubar: a click on a title opens its menu below the bar. */
    {
        cl_widget_t *bar = cl_menubar_create(
            app, &(cl_menubar_desc_t){ CL_MENUBAR_DESC_INIT_FIELDS });
        cl_widget_t *filemenu = cl_menu_create(app);
        cl_rect_t br;

        cl_menu_add_item(filemenu, "Quit", on_item, (void *)(intptr_t)9);
        CHECK(cl_menubar_add_menu(bar, "File", filemenu) == CL_OK);
        CHECK(cl_menubar_count(bar) == 1);
        cl_window_set_content(win, bar); /* replaces the checkbox */
        cl_application_step(app, false);

        br = cl_widget_rect(bar);
        menu_fires = 0;
        mouse(plat, CL_PEV_MOUSE_DOWN, br.x + 5.0f, br.y + br.h * 0.5f);
        mouse(plat, CL_PEV_MOUSE_UP, br.x + 5.0f, br.y + br.h * 0.5f);
        cl_application_step(app, false);
        CHECK(cl_window_popup(win) == filemenu);

        /* activate "Quit": fires, chain closes, menu detaches for reuse */
        {
            cl_rect_t mr = cl_widget_rect(filemenu);

            mouse(plat, CL_PEV_MOUSE_DOWN, mr.x + 10.0f, item_y(mr.y, 0));
            mouse(plat, CL_PEV_MOUSE_UP, mr.x + 10.0f, item_y(mr.y, 0));
        }
        cl_application_step(app, false);
        CHECK(menu_fires == 1);
        CHECK(last_index == 9);
        CHECK(cl_window_popup(win) == NULL);
        CHECK(cl_widget_window(filemenu) == NULL); /* detached, not freed */
    }

    /* A popup still open at window destruction is cleaned up (no leak). */
    cl_window_open_popup(win, make_menu(app), (cl_point_t){ 10.0f, 10.0f });
    cl_application_destroy(app);

    if (failures == 0)
        printf("all popup tests passed\n");
    return failures == 0 ? 0 : 1;
}
