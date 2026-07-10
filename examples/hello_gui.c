/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/copal.h>

#include <stdio.h>
#include <stdlib.h>

/*
 * When built with AddressSanitizer, LeakSanitizer also flags process-lifetime
 * allocations made deep inside SDL, D-Bus and the Mesa GL driver during init.
 * Those are not copal leaks (copal frees everything it owns on shutdown), so
 * suppress them here to keep the leak report focused on real regressions.
 */
#if defined(__SANITIZE_ADDRESS__)
#  define COPAL_HAS_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define COPAL_HAS_ASAN 1
#  endif
#endif

#ifdef COPAL_HAS_ASAN
const char *__lsan_default_suppressions(void);
const char *__lsan_default_suppressions(void)
{
    return "leak:libdbus-1\n"
           "leak:libgallium\n"
           "leak:libSDL2\n"
           "leak:libEGL\n"
           "leak:libGLdispatch\n"
           "leak:libicuuc\n"
           "leak:swrast\n"
           "leak:dri\n";
}
#endif

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
    cl_widget_t *textbox;
    cl_widget_t *scroll;
    cl_widget_t *scroll_body;
    cl_widget_t *button;
    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
    int i;
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
    textbox = cl_textbox_create(
        app, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS,
                                   .placeholder = "type here (Ctrl+C/V works)" });
    cl_widget_set_preferred_size(textbox, (cl_size_t){ 240, 0 });

    scroll = cl_scrollview_create(
        app, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS });
    cl_widget_set_preferred_size(scroll, (cl_size_t){ 240, 120 });
    scroll_body = cl_vbox_create(
        app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS, .spacing = 4 });
    for (i = 0; i < 20; i++) {
        char buf[32];

        snprintf(buf, sizeof(buf), "scrollable row %d", i + 1);
        cl_widget_add_child(
            scroll_body,
            cl_label_create(app, &(cl_label_desc_t){ CL_LABEL_DESC_INIT_FIELDS,
                                                     .text = buf }));
    }
    cl_scrollview_set_content(scroll, scroll_body);

    button = cl_button_create(
        app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS, .text = "Close" });
    cl_button_set_on_click(button, on_close, app);

    cl_widget_add_child(root, label);
    cl_widget_add_child(root, textbox);
    cl_widget_add_child(root, scroll);
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
