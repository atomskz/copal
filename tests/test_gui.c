/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Headless vertical-slice test: drives the whole pipeline with the mock
 * platform + mock renderer (ADR-010). Verifies layout, paint output, and the
 * full platform-event -> dispatch -> user callback path.
 */
#include <copal/copal.h>

#include <stdio.h>
#include <stdlib.h>
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

static bool clicked;

static void on_click(cl_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    clicked = true;
}

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
    cl_widget_t *label;
    cl_widget_t *button;
    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
    cl_rect_t br;
    cl_point_t center;
    size_t i;
    size_t n;
    bool saw_text = false;
    bool saw_round = false;

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

    /* Bounded measure must not read past a non-NUL-terminated buffer. */
    {
        char *b = malloc(3);

        memcpy(b, "ab\xC3", 3); /* trailing 2-byte lead with no continuation */
        CHECK(cl_text_measure_bytes(font, b, 3, CL_UNBOUNDED).w >= 0.0f);
        free(b);
    }

    /* An embedded NUL ends the measurement early (mirrors rendering). */
    CHECK(cl_text_measure_bytes(font, "ab\0cd", 5, CL_UNBOUNDED).w ==
          cl_text_measure_bytes(font, "ab", 2, CL_UNBOUNDED).w);

    wd.title = "test";
    wd.width = 320;
    wd.height = 240;
    win = cl_window_create(app, &wd);
    CHECK(win != NULL);

    root = cl_vbox_create(app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS,
                                                  .spacing = 8,
                                                  .padding = { 12, 12, 12,
                                                               12 } });
    label = cl_label_create(
        app, &(cl_label_desc_t){ CL_LABEL_DESC_INIT_FIELDS, .text = "Hello" });
    button = cl_button_create(
        app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS, .text = "Close" });
    cl_button_set_on_click(button, on_click, NULL);

    CHECK(cl_widget_add_child(root, label) == CL_OK);
    CHECK(cl_widget_add_child(root, button) == CL_OK);
    cl_window_set_content(win, root);
    cl_window_show(win);

    /* First frame: layout + paint. */
    cl_application_step(app, false);

    n = cl_renderer_mock_count(rend);
    CHECK(n > 0);
    for (i = 0; i < n; i++) {
        const cl_mock_command_t *c = cl_renderer_mock_get(rend, i);

        if (c->kind == CL_MOCK_TEXT)
            saw_text = true;
        if (c->kind == CL_MOCK_FILL_ROUND)
            saw_round = true;
    }
    CHECK(saw_text);  /* label + button text */
    CHECK(saw_round); /* button background */

    /* Click the button (down + up over its centre). */
    br = cl_widget_rect(button);
    CHECK(br.w > 0.0f && br.h > 0.0f);
    center.x = br.x + br.w * 0.5f;
    center.y = br.y + br.h * 0.5f;
    cl_platform_mock_push(plat, (cl_platform_event_t){ .kind = CL_PEV_MOUSE_DOWN,
                                                       .pos = center,
                                                       .button = CL_MOUSE_LEFT });
    cl_platform_mock_push(plat, (cl_platform_event_t){ .kind = CL_PEV_MOUSE_UP,
                                                       .pos = center,
                                                       .button = CL_MOUSE_LEFT });
    cl_application_step(app, false);
    CHECK(clicked);

    /* Resize propagates to the window. */
    cl_platform_mock_push(plat, (cl_platform_event_t){ .kind = CL_PEV_RESIZE,
                                                       .size = { 400, 300 } });
    cl_application_step(app, false);
    CHECK(cl_window_size(win).w == 400.0f);

    cl_font_release(font);
    cl_application_destroy(app);

    if (failures == 0)
        printf("all gui tests passed\n");
    return failures == 0 ? 0 : 1;
}
