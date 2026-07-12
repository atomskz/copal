/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Headless ComboBox test. Opens the dropdown (an overlay menu), selects an item
 * and verifies the selection + on_change, plus keyboard open and set_selected
 * being silent. Deterministic without a font (fixed widths / 26px item rows).
 */
#include <copal/copal.h>

#include <stdio.h>
#include <string.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"

/* Menu row geometry with no font: MENU_VPAD 5 top, 16 + 2*5 = 26 per item. */
#define ITEM_H 26.0f
#define TOP_PAD 5.0f

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

static int change_count;
static int last_index;

static void on_change(cl_widget_t *w, int index, void *user)
{
    (void)w;
    (void)user;
    change_count++;
    last_index = index;
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

static void click(cl_platform_t *p, float x, float y)
{
    mouse(p, CL_PEV_MOUSE_DOWN, x, y);
    mouse(p, CL_PEV_MOUSE_UP, x, y);
}

static void press(cl_platform_t *p, cl_key_t key)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_KEY_DOWN;
    ev.key = key;
    cl_platform_mock_push(p, ev);
}

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_platform_t *plat = cl_platform_mock_create(a);
    cl_renderer_t *rend = cl_renderer_mock_create(a);
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *root;
    cl_widget_t *combo;
    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
    cl_rect_t cr;
    cl_rect_t mr;
    cl_widget_t *menu;

    ad.platform = plat;
    ad.renderer = rend;
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    if (!app)
        return 1;
    wd.width = 220;
    wd.height = 220;
    win = cl_window_create(app, &wd);
    root = cl_vbox_create(app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS });
    combo = cl_combobox_create(
        app, &(cl_combobox_desc_t){ CL_COMBOBOX_DESC_INIT_FIELDS,
                                    .placeholder = "Pick one" });
    cl_combobox_add_item(combo, "Alpha");
    cl_combobox_add_item(combo, "Beta");
    cl_combobox_add_item(combo, "Gamma");
    cl_combobox_set_on_change(combo, on_change, NULL);
    cl_widget_add_child(root, combo);
    cl_window_set_content(win, root);
    cl_application_step(app, false);

    CHECK(cl_combobox_count(combo) == 3);
    CHECK(cl_combobox_selected(combo) == -1);
    CHECK(cl_combobox_selected_text(combo) == NULL);

    /* set_selected is silent. */
    cl_combobox_set_selected(combo, 1);
    CHECK(cl_combobox_selected(combo) == 1);
    CHECK(strcmp(cl_combobox_selected_text(combo), "Beta") == 0);
    CHECK(change_count == 0);

    /* Click the combo -> dropdown opens in the overlay layer. */
    cr = cl_widget_rect(combo);
    click(plat, cr.x + cr.w * 0.5f, cr.y + cr.h * 0.5f);
    cl_application_step(app, false);
    menu = cl_window_popup(win);
    CHECK(menu != NULL);

    /* Choose item 2 (Gamma) from the dropdown. */
    mr = cl_widget_rect(menu);
    change_count = 0;
    click(plat, mr.x + 10.0f, mr.y + TOP_PAD + 2.0f * ITEM_H + ITEM_H * 0.5f);
    cl_application_step(app, false);
    CHECK(cl_combobox_selected(combo) == 2);
    CHECK(strcmp(cl_combobox_selected_text(combo), "Gamma") == 0);
    CHECK(change_count == 1 && last_index == 2);
    CHECK(cl_window_popup(win) == NULL); /* dropdown closed */

    /* Keyboard: Down opens the dropdown when focused. */
    CHECK(cl_widget_focus(combo));
    press(plat, CL_KEY_DOWN);
    cl_application_step(app, false);
    CHECK(cl_window_popup(win) != NULL);
    press(plat, CL_KEY_ESCAPE); /* dismiss */
    cl_application_step(app, false);
    CHECK(cl_window_popup(win) == NULL);

    cl_application_destroy(app);

    /* The opening click must not select, even when the combo sits low enough
     * that the dropdown clamps up over it (regression for open-on-release). */
    {
        cl_platform_t *p2 = cl_platform_mock_create(a);
        cl_renderer_t *r2 = cl_renderer_mock_create(a);
        cl_application_desc_t ad2 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app2;
        cl_window_t *w2;
        cl_widget_t *cmb;
        cl_window_desc_t wd2 = CL_WINDOW_DESC_INIT;
        cl_rect_t r;

        ad2.platform = p2;
        ad2.renderer = r2;
        app2 = cl_application_create(&ad2);
        wd2.width = 220;
        wd2.height = 40; /* short -> the dropdown clamps upward over the combo */
        w2 = cl_window_create(app2, &wd2);
        cmb = cl_combobox_create(
            app2, &(cl_combobox_desc_t){ CL_COMBOBOX_DESC_INIT_FIELDS });
        cl_combobox_add_item(cmb, "One");
        cl_combobox_add_item(cmb, "Two");
        cl_combobox_add_item(cmb, "Three");
        cl_window_set_content(w2, cmb);
        cl_application_step(app2, false);
        r = cl_widget_rect(cmb);
        click(p2, r.x + r.w * 0.5f, r.y + r.h * 0.5f);
        cl_application_step(app2, false);
        CHECK(cl_combobox_selected(cmb) == -1);  /* opening click did not pick */
        CHECK(cl_window_popup(w2) != NULL);      /* dropdown is open */
        cl_application_destroy(app2);
    }

    /* Destroying a combo while its dropdown is open tears the popup down
     * (owner tracking), so no dangling overlay is left for a later event. */
    {
        cl_platform_t *p3 = cl_platform_mock_create(a);
        cl_renderer_t *r3 = cl_renderer_mock_create(a);
        cl_application_desc_t ad3 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app3;
        cl_window_t *w3;
        cl_widget_t *box3;
        cl_widget_t *cmb3;
        cl_window_desc_t wd3 = CL_WINDOW_DESC_INIT;
        cl_rect_t r;

        ad3.platform = p3;
        ad3.renderer = r3;
        app3 = cl_application_create(&ad3);
        wd3.width = 220;
        wd3.height = 220;
        w3 = cl_window_create(app3, &wd3);
        box3 = cl_vbox_create(app3,
                              &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS });
        cmb3 = cl_combobox_create(
            app3, &(cl_combobox_desc_t){ CL_COMBOBOX_DESC_INIT_FIELDS });
        cl_combobox_add_item(cmb3, "One");
        cl_combobox_add_item(cmb3, "Two");
        cl_widget_add_child(box3, cmb3);
        cl_window_set_content(w3, box3);
        cl_application_step(app3, false);

        r = cl_widget_rect(cmb3);
        click(p3, r.x + r.w * 0.5f, r.y + r.h * 0.5f);
        cl_application_step(app3, false);
        CHECK(cl_window_popup(w3) != NULL);

        cl_widget_destroy(cmb3); /* owner hook reaps the open dropdown */
        CHECK(cl_window_popup(w3) == NULL);
        press(p3, CL_KEY_ESCAPE); /* no overlay: nothing to dispatch, no UAF */
        cl_application_step(app3, false);
        cl_application_destroy(app3);
    }

    /* item_text / remove / clear keep indices and selection consistent */
    {
        cl_application_desc_t ad2 = { CL_APPLICATION_DESC_INIT_FIELDS };
        cl_application_t *app2;
        cl_widget_t *cb;

        ad2.platform = cl_platform_mock_create(cl_allocator_default());
        ad2.renderer = cl_renderer_mock_create(cl_allocator_default());
        app2 = cl_application_create(&ad2);
        cb = cl_combobox_create(
            app2, &(cl_combobox_desc_t){ CL_COMBOBOX_DESC_INIT_FIELDS });
        cl_combobox_add_item(cb, "one");
        cl_combobox_add_item(cb, "two");
        cl_combobox_add_item(cb, "three");
        CHECK(strcmp(cl_combobox_item_text(cb, 2), "three") == 0);
        CHECK(cl_combobox_item_text(cb, 3) == NULL);

        cl_combobox_set_selected(cb, 2);
        CHECK(cl_combobox_remove(cb, 0) == CL_OK);
        CHECK(cl_combobox_selected(cb) == 1); /* shifted */
        CHECK(strcmp(cl_combobox_selected_text(cb), "three") == 0);
        CHECK(cl_combobox_remove(cb, 1) == CL_OK); /* removes selection */
        CHECK(cl_combobox_selected(cb) == -1);
        cl_combobox_clear(cb);
        CHECK(cl_combobox_count(cb) == 0);
        cl_widget_destroy(cb);
        cl_application_destroy(app2);
    }

    if (failures == 0)
        printf("all combobox tests passed\n");
    return failures == 0 ? 0 : 1;
}
