/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * IME composition test: a pre-edit (composition) string is tracked and shown at
 * the caret without entering the buffer or firing on_changed until the input
 * method commits it; an empty composition, a commit, a click, or focus loss all
 * clear it; read-only boxes ignore composition; multiline works the same way.
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

static void compose(cl_platform_t *p, const char *s, int cursor)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_TEXT_EDIT;
    snprintf(ev.text, sizeof(ev.text), "%s", s);
    ev.edit_cursor = cursor;
    cl_platform_mock_push(p, ev);
}

static void commit(cl_platform_t *p, const char *s)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_TEXT_INPUT;
    snprintf(ev.text, sizeof(ev.text), "%s", s);
    cl_platform_mock_push(p, ev);
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

static void click(cl_platform_t *p, float x, float y)
{
    cl_platform_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.kind = CL_PEV_MOUSE_DOWN;
    ev.pos.x = x;
    ev.pos.y = y;
    ev.button = CL_MOUSE_LEFT;
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

static int changed_hits;

static void on_changed(cl_widget_t *w, const char *text, void *user)
{
    (void)w;
    (void)text;
    (void)user;
    changed_hits++;
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
    cl_widget_t *tb;
    cl_widget_t *tb_ro;
    cl_widget_t *ml;
    cl_widget_t *btn;
    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;

    ad.platform = plat;
    ad.renderer = rend;
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    if (!app)
        return 1;
    font = load_any_font(app); /* optional: only the render check needs it */
    if (font)
        cl_theme_set_font(cl_application_theme(app), font);

    wd.width = 260;
    wd.height = 220;
    win = cl_window_create(app, &wd);
    root = cl_vbox_create(app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS,
                                                  .spacing = 6,
                                                  .padding = { 8, 8, 8, 8 } });
    tb = cl_textbox_create(app,
                           &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS });
    cl_textbox_set_on_changed(tb, on_changed, NULL);
    tb_ro = cl_textbox_create(
        app, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS, .readonly = true,
                                   .text = "ro" });
    ml = cl_textbox_create(
        app, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS, .multiline = true,
                                   .text = "line\n" });
    btn = cl_button_create(app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS,
                                                     .text = "focus me" });
    cl_widget_add_child(root, tb);
    cl_widget_add_child(root, tb_ro);
    cl_widget_add_child(root, ml);
    cl_widget_add_child(root, btn);
    cl_window_set_content(win, root);
    cl_application_step(app, false);

    CHECK(cl_widget_focus(tb));

    /* A composition is tracked but does not enter the buffer or notify. */
    changed_hits = 0;
    compose(plat, "ab", 2);
    cl_application_step(app, false);
    CHECK(cl_textbox_preedit(tb) != NULL &&
          strcmp(cl_textbox_preedit(tb), "ab") == 0);
    CHECK(strcmp(cl_textbox_text(tb), "") == 0);
    CHECK(changed_hits == 0);

    /* Updating the composition replaces it (still not in the buffer). */
    compose(plat, "abc", 3);
    cl_application_step(app, false);
    CHECK(strcmp(cl_textbox_preedit(tb), "abc") == 0);
    CHECK(strcmp(cl_textbox_text(tb), "") == 0);
    if (font)
        CHECK(rendered(rend, "abc")); /* the composition is drawn at the caret */

    /* Committing inserts the text and clears the composition, firing once. */
    commit(plat, "X");
    cl_application_step(app, false);
    CHECK(strcmp(cl_textbox_text(tb), "X") == 0);
    CHECK(cl_textbox_preedit(tb) == NULL);
    CHECK(changed_hits == 1);

    /* An empty composition clears the pre-edit. */
    compose(plat, "zz", 2);
    cl_application_step(app, false);
    CHECK(cl_textbox_preedit(tb) != NULL);
    compose(plat, "", 0);
    cl_application_step(app, false);
    CHECK(cl_textbox_preedit(tb) == NULL);

    /* A click cancels an in-progress composition. */
    compose(plat, "q", 1);
    cl_application_step(app, false);
    CHECK(cl_textbox_preedit(tb) != NULL);
    {
        cl_rect_t r = cl_widget_rect(tb);

        click(plat, r.x + 4.0f, r.y + r.h * 0.5f);
        cl_application_step(app, false);
    }
    CHECK(cl_textbox_preedit(tb) == NULL);

    /* Focus leaving the box abandons the composition. */
    compose(plat, "w", 1);
    cl_application_step(app, false);
    CHECK(cl_textbox_preedit(tb) != NULL);
    CHECK(cl_widget_focus(btn));
    cl_application_step(app, false);
    CHECK(cl_textbox_preedit(tb) == NULL);
    CHECK(strcmp(cl_textbox_text(tb), "X") == 0); /* text unchanged by the abort */

    /* A read-only box ignores composition entirely. */
    CHECK(cl_widget_focus(tb_ro));
    compose(plat, "no", 2);
    cl_application_step(app, false);
    CHECK(cl_textbox_preedit(tb_ro) == NULL);
    CHECK(strcmp(cl_textbox_text(tb_ro), "ro") == 0);

    /* Multiline composes and commits the same way. */
    CHECK(cl_widget_focus(ml));
    compose(plat, "cd", 2);
    cl_application_step(app, false);
    CHECK(cl_textbox_preedit(ml) != NULL &&
          strcmp(cl_textbox_preedit(ml), "cd") == 0);
    CHECK(strcmp(cl_textbox_text(ml), "line\n") == 0); /* unchanged */
    commit(plat, "cd");
    cl_application_step(app, false);
    CHECK(strcmp(cl_textbox_text(ml), "line\ncd") == 0);
    CHECK(cl_textbox_preedit(ml) == NULL);

    /* A negative composition cursor clamps to the start (caret at the left of
     * the composition, not shifted right by its whole width). */
    if (font) {
        float lh = cl_font_metrics(font).line_height;
        cl_rect_t r;
        float caret_x = -1.0f;
        size_t i;
        size_t n;

        cl_textbox_set_text(tb, "");
        CHECK(cl_widget_focus(tb));
        r = cl_widget_rect(tb);
        compose(plat, "abc", -1);
        cl_application_step(app, false);
        n = cl_renderer_mock_count(rend);
        for (i = 0; i < n; i++) {
            const cl_mock_command_t *c = cl_renderer_mock_get(rend, i);

            if (c->kind == CL_MOCK_FILL_RECT && c->rect.w == 1.0f &&
                c->rect.h >= lh - 0.5f && c->rect.h <= lh + 0.5f)
                caret_x = c->rect.x; /* the 1px-wide, line-tall caret */
        }
        CHECK(caret_x >= 0.0f && caret_x <= r.x + 6.0f + 2.0f);
    }

    /* A live selection is not highlighted while composing (commit replaces it). */
    if (font) {
        cl_color_t sc =
            cl_theme_color(cl_application_theme(app), CL_COLOR_SELECTION);
        bool saw_selection = false;
        size_t i;
        size_t n;

        cl_textbox_set_text(tb, "hello");
        CHECK(cl_widget_focus(tb));
        key(plat, CL_KEY_A, CL_MOD_CTRL); /* select all */
        compose(plat, "mn", 2);
        cl_application_step(app, false);
        n = cl_renderer_mock_count(rend);
        for (i = 0; i < n; i++) {
            const cl_mock_command_t *c = cl_renderer_mock_get(rend, i);

            if (c->kind == CL_MOCK_FILL_RECT && c->color.r == sc.r &&
                c->color.g == sc.g && c->color.b == sc.b && c->color.a == sc.a)
                saw_selection = true;
        }
        CHECK(!saw_selection);
    }

    if (font)
        cl_font_release(font);
    cl_application_destroy(app);

    if (failures == 0)
        printf("all ime tests passed\n");
    return failures == 0 ? 0 : 1;
}
