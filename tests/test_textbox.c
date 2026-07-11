/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Headless TextBox editing test. Drives focus + keyboard + text input through
 * the mock platform; editing logic does not require a font.
 */
#include <copal/copal.h>

#include <stdio.h>
#include <string.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"
#include "app/app_internal.h"

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

static int changed_count;
static int submit_count;

static void on_changed(cl_widget_t *w, const char *utf8, void *user)
{
    (void)w;
    (void)utf8;
    (void)user;
    changed_count++;
}

static void on_submit(cl_widget_t *w, const char *utf8, void *user)
{
    (void)w;
    (void)utf8;
    (void)user;
    submit_count++;
}

static void type_text(cl_platform_t *p, const char *s)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_TEXT_INPUT;
    strncpy(ev.text, s, sizeof(ev.text) - 1);
    cl_platform_mock_push(p, ev);
}

static void press(cl_platform_t *p, cl_key_t key, cl_key_mods_t mods)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_KEY_DOWN;
    ev.key = key;
    ev.mods = mods;
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
    cl_widget_t *tb;
    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;

    ad.platform = plat;
    ad.renderer = rend;
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    if (!app)
        return 1;

    wd.width = 240;
    wd.height = 40;
    win = cl_window_create(app, &wd);
    CHECK(win != NULL);

    tb = cl_textbox_create(app, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS });
    cl_textbox_set_on_changed(tb, on_changed, NULL);
    cl_textbox_set_on_submit(tb, on_submit, NULL);
    cl_window_set_content(win, tb);
    cl_application_step(app, false);

    CHECK(cl_widget_focus(tb));
    CHECK(cl_widget_has_focus(tb));

    /* no-op Backspace on an empty box must not fire on_changed */
    press(plat, CL_KEY_BACKSPACE, CL_MOD_NONE);
    cl_application_step(app, false);
    CHECK(changed_count == 0);

    /* type "Hello" */
    type_text(plat, "Hello");
    cl_application_step(app, false);
    CHECK(strcmp(cl_textbox_text(tb), "Hello") == 0);
    CHECK(changed_count > 0);

    /* Left, Backspace: "Hello" -> cursor before 'o' -> delete 'l' -> "Helo" */
    press(plat, CL_KEY_LEFT, CL_MOD_NONE);
    press(plat, CL_KEY_BACKSPACE, CL_MOD_NONE);
    cl_application_step(app, false);
    CHECK(strcmp(cl_textbox_text(tb), "Helo") == 0);

    /* Select all via Home + Shift+End, replace with "Bye" */
    press(plat, CL_KEY_HOME, CL_MOD_NONE);
    press(plat, CL_KEY_END, CL_MOD_SHIFT);
    type_text(plat, "Bye");
    cl_application_step(app, false);
    CHECK(strcmp(cl_textbox_text(tb), "Bye") == 0);

    /* Ctrl+A then Delete clears the box */
    press(plat, CL_KEY_A, CL_MOD_CTRL);
    press(plat, CL_KEY_DELETE, CL_MOD_NONE);
    cl_application_step(app, false);
    CHECK(strlen(cl_textbox_text(tb)) == 0);

    /* UTF-8: two Cyrillic codepoints, End, Backspace removes one codepoint */
    {
        int before = changed_count;

        cl_textbox_set_text(tb, "\xD0\xB0\xD0\xB1"); /* "аб" */
        CHECK(changed_count == before); /* set_text must not fire on_changed */
    }
    press(plat, CL_KEY_END, CL_MOD_NONE);
    press(plat, CL_KEY_BACKSPACE, CL_MOD_NONE);
    cl_application_step(app, false);
    CHECK(strcmp(cl_textbox_text(tb), "\xD0\xB0") == 0); /* "а" */

    /* Enter fires on_submit */
    press(plat, CL_KEY_ENTER, CL_MOD_NONE);
    cl_application_step(app, false);
    CHECK(submit_count == 1);

    /* Tab from a single focusable stays on it (wrap) */
    press(plat, CL_KEY_TAB, CL_MOD_NONE);
    cl_application_step(app, false);
    CHECK(cl_widget_has_focus(tb));

    cl_application_destroy(app);

    /* max_length caps codepoints */
    {
        cl_platform_t *p2 = cl_platform_mock_create(a);
        cl_renderer_t *r2 = cl_renderer_mock_create(a);
        cl_application_desc_t ad2 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app2;
        cl_window_t *w2;
        cl_widget_t *tb2;
        cl_window_desc_t wd2 = CL_WINDOW_DESC_INIT;

        ad2.platform = p2;
        ad2.renderer = r2;
        app2 = cl_application_create(&ad2);
        wd2.width = 200;
        wd2.height = 40;
        w2 = cl_window_create(app2, &wd2);
        tb2 = cl_textbox_create(
            app2, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS,
                                        .max_length = 3 });
        cl_window_set_content(w2, tb2);
        cl_application_step(app2, false);
        cl_widget_focus(tb2);
        type_text(p2, "abcdef");
        cl_application_step(app2, false);
        CHECK(strcmp(cl_textbox_text(tb2), "abc") == 0);
        cl_application_destroy(app2);
    }

    /* Focus lifecycle: destroying a focused child clears focus (no UAF). */
    {
        cl_platform_t *p3 = cl_platform_mock_create(a);
        cl_renderer_t *r3 = cl_renderer_mock_create(a);
        cl_application_desc_t ad3 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app3;
        cl_window_t *w3;
        cl_widget_t *root3;
        cl_widget_t *tb3;
        cl_window_desc_t wd3 = CL_WINDOW_DESC_INIT;

        ad3.platform = p3;
        ad3.renderer = r3;
        app3 = cl_application_create(&ad3);
        wd3.width = 200;
        wd3.height = 60;
        w3 = cl_window_create(app3, &wd3);
        root3 = cl_vbox_create(app3,
                               &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS });
        tb3 = cl_textbox_create(
            app3, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS });
        cl_widget_add_child(root3, tb3);
        cl_window_set_content(w3, root3);
        cl_application_step(app3, false);

        CHECK(cl_widget_focus(tb3));
        cl_widget_destroy(tb3); /* focus must be cleared here */
        press(p3, CL_KEY_LEFT, CL_MOD_NONE);
        type_text(p3, "x");
        cl_application_step(app3, false); /* would UAF before the fix */
        cl_application_destroy(app3);
    }

    /* Clipboard: copy / cut / paste roundtrip through the mock platform. */
    {
        cl_platform_t *p5 = cl_platform_mock_create(a);
        cl_renderer_t *r5 = cl_renderer_mock_create(a);
        cl_application_desc_t ad5 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app5;
        cl_window_t *w5;
        cl_widget_t *tb5;
        cl_window_desc_t wd5 = CL_WINDOW_DESC_INIT;

        ad5.platform = p5;
        ad5.renderer = r5;
        app5 = cl_application_create(&ad5);
        wd5.width = 240;
        wd5.height = 40;
        w5 = cl_window_create(app5, &wd5);
        tb5 = cl_textbox_create(
            app5, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS });
        cl_window_set_content(w5, tb5);
        cl_application_step(app5, false);
        cl_widget_focus(tb5);

        /* type, select all, copy */
        type_text(p5, "Hello");
        press(p5, CL_KEY_HOME, CL_MOD_NONE);
        press(p5, CL_KEY_END, CL_MOD_SHIFT);
        press(p5, CL_KEY_C, CL_MOD_CTRL);
        cl_application_step(app5, false);

        /* cursor to end, paste -> "HelloHello" */
        press(p5, CL_KEY_END, CL_MOD_NONE);
        press(p5, CL_KEY_V, CL_MOD_CTRL);
        cl_application_step(app5, false);
        CHECK(strcmp(cl_textbox_text(tb5), "HelloHello") == 0);

        /* select all, cut -> empty; clipboard holds "HelloHello" */
        press(p5, CL_KEY_A, CL_MOD_CTRL);
        press(p5, CL_KEY_X, CL_MOD_CTRL);
        cl_application_step(app5, false);
        CHECK(strlen(cl_textbox_text(tb5)) == 0);
        press(p5, CL_KEY_V, CL_MOD_CTRL);
        cl_application_step(app5, false);
        CHECK(strcmp(cl_textbox_text(tb5), "HelloHello") == 0);

        cl_application_destroy(app5);
    }

    /* Clipboard: password box must not copy; paste strips line breaks. */
    {
        cl_platform_t *p6 = cl_platform_mock_create(a);
        cl_renderer_t *r6 = cl_renderer_mock_create(a);
        cl_application_desc_t ad6 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app6;
        cl_window_t *w6;
        cl_widget_t *tb6;
        cl_window_desc_t wd6 = CL_WINDOW_DESC_INIT;

        ad6.platform = p6;
        ad6.renderer = r6;
        app6 = cl_application_create(&ad6);
        wd6.width = 240;
        wd6.height = 40;
        w6 = cl_window_create(app6, &wd6);

        /* seed the clipboard with a known value */
        p6->ops->clipboard_set(p6, "seed");

        tb6 = cl_textbox_create(
            app6, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS,
                                        .password = true });
        cl_window_set_content(w6, tb6);
        cl_application_step(app6, false);
        cl_widget_focus(tb6);

        /* type a secret, select all, Ctrl+C: clipboard must stay "seed" */
        type_text(p6, "secret");
        press(p6, CL_KEY_HOME, CL_MOD_NONE);
        press(p6, CL_KEY_END, CL_MOD_SHIFT);
        press(p6, CL_KEY_C, CL_MOD_CTRL);
        cl_application_step(app6, false);
        {
            char *cb = cl_app_clipboard_get(app6);

            CHECK(cb && strcmp(cb, "seed") == 0);
            cl_free(a, cb);
        }

        /* paste with embedded newlines strips them */
        press(p6, CL_KEY_A, CL_MOD_CTRL);
        press(p6, CL_KEY_DELETE, CL_MOD_NONE);
        p6->ops->clipboard_set(p6, "a\nb\r\nc");
        press(p6, CL_KEY_V, CL_MOD_CTRL);
        cl_application_step(app6, false);
        CHECK(strcmp(cl_textbox_text(tb6), "abc") == 0);

        cl_application_destroy(app6);
    }

    /* Focus lifecycle: removing a focused child clears focus. */
    {
        cl_platform_t *p4 = cl_platform_mock_create(a);
        cl_renderer_t *r4 = cl_renderer_mock_create(a);
        cl_application_desc_t ad4 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app4;
        cl_window_t *w4;
        cl_widget_t *root4;
        cl_widget_t *tb4;
        cl_window_desc_t wd4 = CL_WINDOW_DESC_INIT;

        ad4.platform = p4;
        ad4.renderer = r4;
        app4 = cl_application_create(&ad4);
        wd4.width = 200;
        wd4.height = 60;
        w4 = cl_window_create(app4, &wd4);
        root4 = cl_vbox_create(app4,
                               &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS });
        tb4 = cl_textbox_create(
            app4, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS });
        cl_widget_add_child(root4, tb4);
        cl_window_set_content(w4, root4);
        cl_application_step(app4, false);

        CHECK(cl_widget_focus(tb4));
        CHECK(cl_widget_has_focus(tb4));
        cl_widget_remove_child(root4, tb4); /* caller now owns tb4 */
        CHECK(!cl_widget_has_focus(tb4));   /* focus cleared on detach */
        press(p4, CL_KEY_LEFT, CL_MOD_NONE);
        cl_application_step(app4, false);
        cl_widget_destroy(tb4);
        cl_application_destroy(app4);
    }

    /* Paste of a newline-only clipboard over a selection is a no-op and must
     * not fire on_changed. */
    {
        cl_platform_t *p7 = cl_platform_mock_create(a);
        cl_renderer_t *r7 = cl_renderer_mock_create(a);
        cl_application_desc_t ad7 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app7;
        cl_window_t *w7;
        cl_widget_t *tb7;
        cl_window_desc_t wd7 = CL_WINDOW_DESC_INIT;

        ad7.platform = p7;
        ad7.renderer = r7;
        app7 = cl_application_create(&ad7);
        wd7.width = 200;
        wd7.height = 40;
        w7 = cl_window_create(app7, &wd7);
        tb7 = cl_textbox_create(
            app7, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS });
        cl_textbox_set_on_changed(tb7, on_changed, NULL);
        cl_window_set_content(w7, tb7);
        cl_application_step(app7, false);
        cl_widget_focus(tb7);

        type_text(p7, "hi");
        cl_application_step(app7, false); /* commit the "hi" edit first */

        press(p7, CL_KEY_HOME, CL_MOD_NONE);
        press(p7, CL_KEY_END, CL_MOD_SHIFT); /* select "hi" (no text change) */
        p7->ops->clipboard_set(p7, "\r\n");  /* clipboard is only line breaks */
        changed_count = 0;
        press(p7, CL_KEY_V, CL_MOD_CTRL);
        cl_application_step(app7, false);
        CHECK(strcmp(cl_textbox_text(tb7), "hi") == 0); /* unchanged */
        CHECK(changed_count == 0);                      /* no spurious notify */

        cl_application_destroy(app7);
    }

    /* Undo / redo. */
    {
        cl_platform_t *p8 = cl_platform_mock_create(a);
        cl_renderer_t *r8 = cl_renderer_mock_create(a);
        cl_application_desc_t ad8 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app8;
        cl_window_t *w8;
        cl_widget_t *tb8;
        cl_window_desc_t wd8 = CL_WINDOW_DESC_INIT;

        ad8.platform = p8;
        ad8.renderer = r8;
        app8 = cl_application_create(&ad8);
        wd8.width = 240;
        wd8.height = 40;
        w8 = cl_window_create(app8, &wd8);
        tb8 = cl_textbox_create(
            app8, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS });
        cl_textbox_set_on_changed(tb8, on_changed, NULL);
        cl_window_set_content(w8, tb8);
        cl_application_step(app8, false);
        cl_widget_focus(tb8);

        /* Consecutive typing coalesces into a single undo step. */
        type_text(p8, "a");
        type_text(p8, "b");
        type_text(p8, "c");
        cl_application_step(app8, false);
        CHECK(strcmp(cl_textbox_text(tb8), "abc") == 0);
        press(p8, CL_KEY_Z, CL_MOD_CTRL); /* undo the whole run */
        cl_application_step(app8, false);
        CHECK(strlen(cl_textbox_text(tb8)) == 0);
        press(p8, CL_KEY_Y, CL_MOD_CTRL); /* redo */
        cl_application_step(app8, false);
        CHECK(strcmp(cl_textbox_text(tb8), "abc") == 0);

        /* A delete is a separate undo step from the preceding typing. */
        press(p8, CL_KEY_BACKSPACE, CL_MOD_NONE);
        cl_application_step(app8, false);
        CHECK(strcmp(cl_textbox_text(tb8), "ab") == 0);
        press(p8, CL_KEY_Z, CL_MOD_CTRL);
        cl_application_step(app8, false);
        CHECK(strcmp(cl_textbox_text(tb8), "abc") == 0); /* undo the delete */
        press(p8, CL_KEY_Z, CL_MOD_CTRL);
        cl_application_step(app8, false);
        CHECK(strlen(cl_textbox_text(tb8)) == 0); /* undo the typing */

        /* A new edit after an undo clears the redo history. */
        type_text(p8, "x");
        cl_application_step(app8, false);
        press(p8, CL_KEY_Z, CL_MOD_CTRL); /* -> "" */
        cl_application_step(app8, false);
        type_text(p8, "y"); /* new edit clears redo */
        cl_application_step(app8, false);
        press(p8, CL_KEY_Y, CL_MOD_CTRL); /* redo is empty: no-op */
        cl_application_step(app8, false);
        CHECK(strcmp(cl_textbox_text(tb8), "y") == 0);

        /* set_text resets the history: there is nothing to undo. */
        cl_textbox_set_text(tb8, "foo");
        press(p8, CL_KEY_Z, CL_MOD_CTRL);
        cl_application_step(app8, false);
        CHECK(strcmp(cl_textbox_text(tb8), "foo") == 0);

        /* A real undo and redo each fire on_changed once; an empty-history
         * Ctrl+Z fires nothing. */
        cl_textbox_set_text(tb8, "");
        type_text(p8, "z");
        cl_application_step(app8, false);
        changed_count = 0;
        press(p8, CL_KEY_Z, CL_MOD_CTRL); /* real undo -> "" */
        cl_application_step(app8, false);
        CHECK(strlen(cl_textbox_text(tb8)) == 0);
        CHECK(changed_count == 1);
        press(p8, CL_KEY_Y, CL_MOD_CTRL); /* real redo -> "z" */
        cl_application_step(app8, false);
        CHECK(strcmp(cl_textbox_text(tb8), "z") == 0);
        CHECK(changed_count == 2);
        press(p8, CL_KEY_Y, CL_MOD_CTRL); /* nothing to redo */
        cl_application_step(app8, false);
        CHECK(changed_count == 2); /* no spurious notify */

        cl_application_destroy(app8);
    }

    if (failures == 0)
        printf("all textbox tests passed\n");
    return failures == 0 ? 0 : 1;
}
