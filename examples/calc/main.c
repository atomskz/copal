/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/copal.h>

#include <stdio.h>

#include "example_util.h"
#include "calc_engine.h"
#include "calc_widgets.h"

/* Controller: owns the model and the display it drives. */
typedef struct calc {
    calc_engine_t engine;
    cl_widget_t *display;
} calc_t;

static void on_key(cl_widget_t *key, int code, void *user)
{
    calc_t *calc = user;

    (void)key;
    calc_input(&calc->engine, code);
    calc_display_set_text(calc->display, calc_text(&calc->engine));
}

/* The 4x5 keypad, described as a table and assembled in one loop. */
static cl_widget_t *build_keypad(cl_application_t *app, calc_t *calc)
{
    static const struct {
        const char *label;
        int code;
        calc_role_t role;
    } keys[] = {
        { "C", CALC_CLEAR, CALC_ROLE_FUNC },
        { "\xC2\xB1", CALC_NEGATE, CALC_ROLE_FUNC },  /* +/- */
        { "%", CALC_PERCENT, CALC_ROLE_FUNC },
        { "\xC3\xB7", CALC_DIV, CALC_ROLE_OP },       /* division sign */
        { "7", '7', CALC_ROLE_DIGIT },
        { "8", '8', CALC_ROLE_DIGIT },
        { "9", '9', CALC_ROLE_DIGIT },
        { "\xC3\x97", CALC_MUL, CALC_ROLE_OP },       /* multiplication sign */
        { "4", '4', CALC_ROLE_DIGIT },
        { "5", '5', CALC_ROLE_DIGIT },
        { "6", '6', CALC_ROLE_DIGIT },
        { "\xE2\x88\x92", CALC_SUB, CALC_ROLE_OP },   /* minus sign */
        { "1", '1', CALC_ROLE_DIGIT },
        { "2", '2', CALC_ROLE_DIGIT },
        { "3", '3', CALC_ROLE_DIGIT },
        { "+", CALC_ADD, CALC_ROLE_OP },
        { "0", '0', CALC_ROLE_DIGIT },
        { ".", CALC_DOT, CALC_ROLE_DIGIT },
        { "\xE2\x8C\xAB", CALC_BACK, CALC_ROLE_FUNC }, /* erase-left */
        { "=", CALC_EQUALS, CALC_ROLE_OP },
    };
    const size_t n = sizeof(keys) / sizeof(keys[0]);
    cl_widget_t *pad = cl_vbox_create(
        app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS, .spacing = CALC_GAP,
                                .align_cross = CL_ALIGN_STRETCH });
    cl_widget_t *row = NULL;
    size_t i;

    for (i = 0; i < n; i++) {
        cl_widget_t *key;

        if (i % 4 == 0) {
            row = cl_hbox_create(
                app, &(cl_hbox_desc_t){ CL_HBOX_DESC_INIT_FIELDS,
                                        .spacing = CALC_GAP,
                                        .align_cross = CL_ALIGN_STRETCH });
            cl_widget_add_child(pad, row);
        }
        key = calc_key_create(app, keys[i].label, keys[i].code, keys[i].role);
        calc_key_set_handler(key, on_key, calc);
        cl_widget_add_child(row, key);
    }
    return pad;
}

static cl_widget_t *build_root(cl_application_t *app, calc_t *calc)
{
    cl_vbox_desc_t rd = { CL_VBOX_DESC_INIT_FIELDS };
    cl_widget_t *root;

    rd.spacing = CALC_ROW_GAP;
    rd.padding = (cl_insets_t){ CALC_PAD, CALC_PAD, CALC_PAD, CALC_PAD };
    rd.align_cross = CL_ALIGN_STRETCH;
    root = cl_vbox_create(app, &rd);

    calc->display = calc_display_create(app);
    calc_display_set_text(calc->display, calc_text(&calc->engine));
    cl_widget_add_child(root, calc->display);
    cl_widget_add_child(root, build_keypad(app, calc));
    return root;
}

int main(int argc, char **argv)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .title = "copal \xE2\x80\x94 calc" };
    calc_t calc = { 0 }; /* engine is reset below; display set during build */
    cl_application_t *app;
    cl_font_t *font;
    cl_window_t *win;
    int rc;

    ad.render_backend = example_backend(argc, argv); /* --software / --gl */
    app = cl_application_create(&ad);
    if (!app) {
        fprintf(stderr, "cannot create application: %s\n",
                cl_result_string(cl_last_error()));
        return 1;
    }
    font = example_load_font(app, 22.0f);
    if (font)
        cl_theme_set_font(cl_application_theme(app), font);
    calc_reset(&calc.engine);

    wd.width = 4 * CALC_KEY_W + 3 * CALC_GAP + 2 * CALC_PAD;
    wd.height = CALC_DISPLAY_H + CALC_ROW_GAP + 5 * CALC_KEY_H + 4 * CALC_GAP +
                2 * CALC_PAD;
    win = cl_window_create(app, &wd);
    if (!win) {
        fprintf(stderr, "cannot create window: %s\n",
                cl_result_string(cl_last_error()));
        cl_application_destroy(app);
        return 1;
    }

    cl_window_set_content(win, build_root(app, &calc));
    cl_window_show(win);

    rc = example_run(app);

    if (font)
        cl_font_release(font);
    cl_application_destroy(app);
    return rc;
}
