/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Headless theme test: light/dark variants, per-role overrides, and that the
 * window paints a full-window background fill using the BACKGROUND role.
 */
#include <copal/copal.h>

#include <stdio.h>

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

static bool color_eq(cl_color_t a, cl_color_t b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

int main(void)
{
    const cl_allocator_t *a = cl_allocator_default();
    cl_platform_t *plat = cl_platform_mock_create(a);
    cl_renderer_t *rend = cl_renderer_mock_create(a);
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_application_t *app;
    cl_theme_t *theme;
    cl_window_t *win;
    cl_widget_t *cb;
    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
    cl_color_t bg_light;
    cl_color_t bg_dark;
    cl_color_t custom = cl_rgba(1, 2, 3, 255);

    ad.platform = plat;
    ad.renderer = rend;
    app = cl_application_create(&ad);
    CHECK(app != NULL);
    if (!app)
        return 1;
    theme = cl_application_theme(app);

    /* Default is the light variant; dark differs. */
    CHECK(cl_theme_variant(theme) == CL_THEME_LIGHT);
    bg_light = cl_theme_color(theme, CL_COLOR_BACKGROUND);
    cl_theme_set_variant(theme, CL_THEME_DARK);
    CHECK(cl_theme_variant(theme) == CL_THEME_DARK);
    bg_dark = cl_theme_color(theme, CL_COLOR_BACKGROUND);
    CHECK(!color_eq(bg_light, bg_dark));

    /* A per-role override sticks, but loading a variant replaces it. */
    cl_theme_set_variant(theme, CL_THEME_LIGHT);
    cl_theme_set_color(theme, CL_COLOR_ACCENT, custom);
    CHECK(color_eq(cl_theme_color(theme, CL_COLOR_ACCENT), custom));
    cl_theme_set_variant(theme, CL_THEME_LIGHT);
    CHECK(!color_eq(cl_theme_color(theme, CL_COLOR_ACCENT), custom));

    /* The window clears each frame to the BACKGROUND role. */
    wd.width = 200;
    wd.height = 100;
    win = cl_window_create(app, &wd);
    cb = cl_checkbox_create(
        app, &(cl_checkbox_desc_t){ CL_CHECKBOX_DESC_INIT_FIELDS });
    cl_window_set_content(win, cb);
    cl_window_show(win);
    cl_application_step(app, false);
    CHECK(color_eq(cl_renderer_mock_clear_color(rend),
                   cl_theme_color(theme, CL_COLOR_BACKGROUND)));

    /* Switching the variant and repainting changes the clear colour. */
    cl_theme_set_variant(theme, CL_THEME_DARK);
    cl_widget_invalidate(cb);
    cl_application_step(app, false);
    CHECK(color_eq(cl_renderer_mock_clear_color(rend), bg_dark));

    cl_application_destroy(app);

    if (failures == 0)
        printf("all theme tests passed\n");
    return failures == 0 ? 0 : 1;
}
