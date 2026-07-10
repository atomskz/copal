/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/copal.h>

#include <stdio.h>
#include <stdlib.h>

static void on_close(cl_widget_t *w, void *user)
{
    (void)w;
    cl_application_quit((cl_application_t *)user, 0);
}

int main(int argc, char **argv)
{
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT; /* NULL -> SDL + GL */
    cl_application_t *app;
    cl_font_t *font;
    cl_window_t *win;
    cl_widget_t *root;
    cl_widget_t *label;
    cl_widget_t *button;
    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
    const char *font_path;
    const char *max_frames;
    int rc;

    (void)argc;
    (void)argv;

    app = cl_application_create(&ad);
    if (!app) {
        fprintf(stderr, "cannot create application: %s\n",
                cl_result_string(cl_last_error()));
        return 1;
    }

    font_path = getenv("COPAL_FONT");
    if (!font_path)
        font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    font = cl_font_load_file(app, font_path, 18.0f);
    if (font)
        cl_theme_set_font(cl_application_theme(app), font);
    else
        fprintf(stderr, "warning: no font at %s (text will not render)\n",
                font_path);

    wd.title = "copal - hello";
    wd.width = 480;
    wd.height = 320;
    wd.resizable = true;
    win = cl_window_create(app, &wd);
    if (!win) {
        fprintf(stderr, "cannot create window: %s\n",
                cl_result_string(cl_last_error()));
        cl_application_destroy(app);
        return 1;
    }

    root = cl_vbox_create(app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS,
                                                  .spacing = 12,
                                                  .padding = { 20, 20, 20, 20 },
                                                  .align_cross =
                                                      CL_ALIGN_CENTER });
    label = cl_label_create(app, &(cl_label_desc_t){ CL_LABEL_DESC_INIT_FIELDS,
                                                     .text = "Hello from "
                                                             "copal" });
    button = cl_button_create(
        app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS, .text = "Close" });
    cl_button_set_on_click(button, on_close, app);

    cl_widget_add_child(root, label);
    cl_widget_add_child(root, button);
    cl_window_set_content(win, root);
    cl_window_show(win);

    /* COPAL_MAX_FRAMES=N renders N frames then exits (headless verification). */
    max_frames = getenv("COPAL_MAX_FRAMES");
    if (max_frames) {
        int n = atoi(max_frames);

        while (n-- > 0)
            cl_application_step(app, false);
        rc = 0;
    } else {
        rc = cl_application_run(app);
    }

    if (font)
        cl_font_release(font);
    cl_application_destroy(app);
    return rc;
}
