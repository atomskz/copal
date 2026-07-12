/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Multiline TextBox test: exercises width wrapping and explicit newlines, Enter
 * inserting a line break (vs submitting), Up/Down and per-line Home/End
 * navigation, click line placement, and vertical scrolling (caret-follow and
 * the wheel) observed through the recorded draw commands.
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

static void key(cl_platform_t *p, cl_key_t k, cl_key_mods_t mods)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_KEY_DOWN;
    ev.key = k;
    ev.mods = mods;
    cl_platform_mock_push(p, ev);
}

static void type_text(cl_platform_t *p, const char *s)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_TEXT_INPUT;
    snprintf(ev.text, sizeof(ev.text), "%s", s);
    cl_platform_mock_push(p, ev);
}

static void mouse_down(cl_platform_t *p, float x, float y)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_MOUSE_DOWN;
    ev.pos.x = x;
    ev.pos.y = y;
    ev.button = CL_MOUSE_LEFT;
    cl_platform_mock_push(p, ev);
}

static void wheel(cl_platform_t *p, float x, float y, float dy)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_MOUSE_WHEEL;
    ev.pos.x = x;
    ev.pos.y = y;
    ev.wheel_y = dy;
    cl_platform_mock_push(p, ev);
}

static bool rendered(cl_renderer_t *r, const char *needle)
{
    size_t n = cl_renderer_mock_count(r);
    size_t i;

    for (i = 0; i < n; i++) {
        const cl_mock_command_t *c = cl_renderer_mock_get(r, i);

        if (c->kind == CL_MOCK_TEXT && strcmp(c->text, needle) == 0)
            return true;
    }
    return false;
}

static int submit_hits;

static void on_submit(cl_widget_t *w, const char *text, void *user)
{
    (void)w;
    (void)text;
    (void)user;
    submit_hits++;
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
    cl_widget_t *tb;
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
    tb = cl_textbox_create(app, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS,
                                                      .multiline = true,
                                                      .text = "one\ntwo\nthree" });
    cl_window_set_content(win, tb); /* fills the window */
    cl_application_step(app, false);

    /* Explicit newlines produce three visual lines. */
    CHECK(cl_textbox_line_count(tb) == 3);
    CHECK(cl_textbox_cursor_line(tb) == 2); /* caret starts at the end */

    /* Enter inserts a newline (does not submit) and grows the line count. */
    CHECK(cl_widget_focus(tb));
    cl_textbox_set_on_submit(tb, on_submit, NULL);
    submit_hits = 0;
    type_text(plat, "a");
    key(plat, CL_KEY_ENTER, CL_MOD_NONE);
    type_text(plat, "b");
    cl_application_step(app, false);
    CHECK(strcmp(cl_textbox_text(tb), "one\ntwo\nthreea\nb") == 0);
    CHECK(cl_textbox_line_count(tb) == 4);
    CHECK(submit_hits == 0); /* multiline Enter never submits */

    /* Undo removes the last typed run, then the inserted newline. */
    key(plat, CL_KEY_Z, CL_MOD_CTRL);
    cl_application_step(app, false);
    CHECK(strcmp(cl_textbox_text(tb), "one\ntwo\nthreea\n") == 0);
    key(plat, CL_KEY_Z, CL_MOD_CTRL);
    cl_application_step(app, false);
    CHECK(strcmp(cl_textbox_text(tb), "one\ntwo\nthreea") == 0);

    /* Up/Down move between visual lines and clamp at the ends. */
    cl_textbox_set_text(tb, "aaa\nbbb\nccc");
    cl_application_step(app, false);
    CHECK(cl_textbox_cursor_line(tb) == 2); /* set_text puts caret at the end */
    key(plat, CL_KEY_UP, CL_MOD_NONE);
    cl_application_step(app, false);
    CHECK(cl_textbox_cursor_line(tb) == 1);
    key(plat, CL_KEY_UP, CL_MOD_NONE);
    cl_application_step(app, false);
    CHECK(cl_textbox_cursor_line(tb) == 0);
    key(plat, CL_KEY_UP, CL_MOD_NONE); /* clamp at the top */
    cl_application_step(app, false);
    CHECK(cl_textbox_cursor_line(tb) == 0);
    key(plat, CL_KEY_DOWN, CL_MOD_NONE);
    key(plat, CL_KEY_DOWN, CL_MOD_NONE);
    key(plat, CL_KEY_DOWN, CL_MOD_NONE); /* clamp at the bottom */
    cl_application_step(app, false);
    CHECK(cl_textbox_cursor_line(tb) == 2);

    /* Per-line Home/End: type after Home lands at the line start. */
    cl_textbox_set_text(tb, "abc\ndef");
    cl_application_step(app, false);
    key(plat, CL_KEY_HOME, CL_MOD_NONE); /* caret on last line -> its start */
    type_text(plat, "X");
    cl_application_step(app, false);
    CHECK(strcmp(cl_textbox_text(tb), "abc\nXdef") == 0);
    key(plat, CL_KEY_END, CL_MOD_NONE); /* end of the last line */
    type_text(plat, "Z");
    cl_application_step(app, false);
    CHECK(strcmp(cl_textbox_text(tb), "abc\nXdefZ") == 0);

    /* Soft wrapping: a long line with no newlines wraps to several lines in a
     * box narrower than the text. */
    cl_textbox_set_text(tb, "wrap wrap wrap wrap wrap wrap wrap wrap wrap wrap");
    cl_application_step(app, false);
    CHECK(cl_textbox_line_count(tb) > 1);

    cl_font_release(font);
    cl_application_destroy(app);

    /* Vertical scrolling in a short box: the caret is kept visible and the
     * wheel scrolls the content, observed via the recorded line draws. */
    {
        cl_platform_t *p2 = cl_platform_mock_create(a);
        cl_renderer_t *r2 = cl_renderer_mock_create(a);
        cl_application_desc_t ad2 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app2;
        cl_font_t *font2;
        cl_window_t *w2;
        cl_widget_t *tb2;
        cl_window_desc_t wd2 = CL_WINDOW_DESC_INIT;

        ad2.platform = p2;
        ad2.renderer = r2;
        app2 = cl_application_create(&ad2);
        font2 = load_any_font(app2);
        if (!font2) {
            cl_application_destroy(app2);
            fprintf(stderr, "SKIP: no TrueType font found\n");
            return SKIP_CODE; /* skip, do not claim success */
        }
        cl_theme_set_font(cl_application_theme(app2), font2);
        wd2.width = 200;
        wd2.height = 70; /* only a few lines tall */
        w2 = cl_window_create(app2, &wd2);
        tb2 = cl_textbox_create(
            app2, &(cl_textbox_desc_t){
                      CL_TEXTBOX_DESC_INIT_FIELDS, .multiline = true,
                      .text = "L0\nL1\nL2\nL3\nL4\nL5\nL6\nL7" });
        cl_window_set_content(w2, tb2);
        cl_widget_focus(tb2);
        cl_application_step(app2, false);
        CHECK(cl_textbox_line_count(tb2) == 8);

        /* Ctrl+End scrolls to reveal the last line; the first is off-screen. */
        key(p2, CL_KEY_END, CL_MOD_CTRL);
        cl_application_step(app2, false);
        CHECK(cl_textbox_cursor_line(tb2) == 7);
        CHECK(cl_renderer_mock_dropped(r2) == 0); /* full command capture */
        CHECK(rendered(r2, "L7"));
        CHECK(!rendered(r2, "L0"));

        /* Ctrl+Home scrolls back to the top. */
        key(p2, CL_KEY_HOME, CL_MOD_CTRL);
        cl_application_step(app2, false);
        CHECK(rendered(r2, "L0"));
        CHECK(!rendered(r2, "L7"));

        /* The wheel scrolls the content down, off the first line. */
        wheel(p2, 20.0f, 20.0f, -3.0f);
        cl_application_step(app2, false);
        CHECK(!rendered(r2, "L0"));

        cl_font_release(font2);
        cl_application_destroy(app2);
    }

    /* Click places the caret on the line under the pointer. */
    {
        cl_platform_t *p3 = cl_platform_mock_create(a);
        cl_renderer_t *r3 = cl_renderer_mock_create(a);
        cl_application_desc_t ad3 = CL_APPLICATION_DESC_INIT;
        cl_application_t *app3;
        cl_font_t *font3;
        cl_window_t *w3;
        cl_widget_t *tb3;
        cl_window_desc_t wd3 = CL_WINDOW_DESC_INIT;
        float lh;

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
        lh = cl_font_metrics(font3).line_height;
        wd3.width = 200;
        wd3.height = 200;
        w3 = cl_window_create(app3, &wd3);
        tb3 = cl_textbox_create(
            app3, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS,
                                        .multiline = true,
                                        .text = "L0\nL1\nL2\nL3" });
        cl_window_set_content(w3, tb3);
        cl_widget_focus(tb3);
        cl_application_step(app3, false);

        /* Click within the third row (y ~ padding + 2.5 line heights). */
        mouse_down(p3, 20.0f, 5.0f + 2.5f * lh);
        cl_application_step(app3, false);
        CHECK(cl_textbox_cursor_line(tb3) == 2);

        cl_font_release(font3);
        cl_application_destroy(app3);
    }

    if (failures == 0)
        printf("all multiline tests passed\n");
    return failures == 0 ? 0 : 1;
}
