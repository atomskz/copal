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

static void on_dark_toggle(cl_widget_t *w, bool checked, void *user)
{
    cl_application_t *app = user;

    cl_theme_set_variant(cl_application_theme(app),
                         checked ? CL_THEME_DARK : CL_THEME_LIGHT);
    cl_widget_invalidate(w); /* repaint the window with the new palette */
}

/* Repeating 1 s timer: bump a counter and refresh a label with it. */
struct uptime_ctx {
    cl_widget_t *label;
    int seconds;
};

static void on_uptime_tick(cl_timer_t *timer, void *user)
{
    struct uptime_ctx *ctx = user;
    char buf[32];

    (void)timer;
    ctx->seconds++;
    snprintf(buf, sizeof(buf), "uptime: %d s", ctx->seconds);
    cl_label_set_text(ctx->label, buf);
}

static void on_menu_button(cl_widget_t *w, void *user)
{
    cl_application_t *app = user;
    cl_window_t *win = cl_widget_window(w);
    cl_rect_t r = cl_widget_rect(w);
    cl_widget_t *menu = cl_menu_create(app);

    cl_menu_add_item(menu, "New", NULL, NULL);
    cl_menu_add_item(menu, "Open...", NULL, NULL);
    cl_menu_add_item(menu, "Save", NULL, NULL);
    cl_window_open_popup(win, menu, (cl_point_t){ r.x, r.y + r.h });
}

/* Try $COPAL_FONT, then a few common system fonts across platforms. */
static cl_font_t *load_default_font(cl_application_t *app)
{
    static const char *candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
    };
    const char *env = getenv("COPAL_FONT");
    cl_font_t *font;
    size_t i;

    if (env) {
        font = cl_font_load_file(app, env, 18.0f);
        if (font)
            return font;
        fprintf(stderr, "warning: COPAL_FONT=%s could not be loaded\n", env);
    }
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        font = cl_font_load_file(app, candidates[i], 18.0f);
        if (font)
            return font;
    }
    return NULL;
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
    cl_widget_t *notes;
    cl_widget_t *checks;
    cl_widget_t *dark;
    cl_widget_t *radios;
    cl_widget_t *slider;
    cl_widget_t *combo;
    cl_widget_t *scroll;
    cl_widget_t *scroll_body;
    cl_widget_t *hscroll;
    cl_widget_t *hscroll_body;
    cl_widget_t *menu_btn;
    cl_widget_t *button;
    cl_widget_t *uptime;
    struct uptime_ctx uptime_ctx = { NULL, 0 };
    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
    int i;
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

    font = load_default_font(app);
    if (font)
        cl_theme_set_font(cl_application_theme(app), font);
    else
        fprintf(stderr, "warning: no usable system font found "
                        "(set COPAL_FONT=/path/to/font.ttf); text will not "
                        "render\n");

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

    notes = cl_textbox_create(
        app, &(cl_textbox_desc_t){
                 CL_TEXTBOX_DESC_INIT_FIELDS, .multiline = true,
                 .text = "Multi-line notes.\nEnter adds a line; long lines wrap "
                         "to the width of the box; Up/Down move between lines "
                         "and the wheel scrolls." });
    cl_widget_set_preferred_size(notes, (cl_size_t){ 240, 76 });

    checks = cl_hbox_create(
        app, &(cl_hbox_desc_t){ CL_HBOX_DESC_INIT_FIELDS, .spacing = 16,
                                .align_cross = CL_ALIGN_CENTER });
    cl_widget_add_child(
        checks, cl_checkbox_create(
                    app, &(cl_checkbox_desc_t){ CL_CHECKBOX_DESC_INIT_FIELDS,
                                                .text = "Enabled",
                                                .checked = true }));
    cl_widget_add_child(
        checks, cl_checkbox_create(
                    app, &(cl_checkbox_desc_t){ CL_CHECKBOX_DESC_INIT_FIELDS,
                                                .text = "Verbose" }));
    dark = cl_checkbox_create(
        app, &(cl_checkbox_desc_t){ CL_CHECKBOX_DESC_INIT_FIELDS,
                                    .text = "Dark mode" });
    cl_checkbox_set_on_toggle(dark, on_dark_toggle, app);
    cl_widget_add_child(checks, dark);

    radios = cl_hbox_create(
        app, &(cl_hbox_desc_t){ CL_HBOX_DESC_INIT_FIELDS, .spacing = 16,
                                .align_cross = CL_ALIGN_CENTER });
    cl_widget_add_child(
        radios,
        cl_radiobutton_create(
            app, &(cl_radiobutton_desc_t){ CL_RADIOBUTTON_DESC_INIT_FIELDS,
                                           .text = "Small", .group = 1,
                                           .selected = true }));
    cl_widget_add_child(
        radios, cl_radiobutton_create(
                    app, &(cl_radiobutton_desc_t){
                             CL_RADIOBUTTON_DESC_INIT_FIELDS, .text = "Large",
                             .group = 1 }));

    slider = cl_slider_create(
        app, &(cl_slider_desc_t){ CL_SLIDER_DESC_INIT_FIELDS, .min = 0,
                                  .max = 100, .value = 40 });
    cl_widget_set_preferred_size(slider, (cl_size_t){ 240, 0 });

    combo = cl_combobox_create(
        app, &(cl_combobox_desc_t){ CL_COMBOBOX_DESC_INIT_FIELDS,
                                    .placeholder = "Choose a fruit" });
    cl_combobox_add_item(combo, "Apple");
    cl_combobox_add_item(combo, "Banana");
    cl_combobox_add_item(combo, "Cherry");

    scroll = cl_scrollview_create(
        app, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS,
                                      .smooth = true });
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

    /* Horizontal scrolling: a row of buttons wider than the viewport. */
    hscroll = cl_scrollview_create(
        app, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS,
                                      .horizontal = true });
    cl_widget_set_preferred_size(hscroll, (cl_size_t){ 240, 44 });
    hscroll_body = cl_hbox_create(
        app, &(cl_hbox_desc_t){ CL_HBOX_DESC_INIT_FIELDS, .spacing = 6 });
    for (i = 0; i < 10; i++) {
        char buf[32];

        snprintf(buf, sizeof(buf), "column %d", i + 1);
        cl_widget_add_child(
            hscroll_body,
            cl_button_create(app, &(cl_button_desc_t){
                                      CL_BUTTON_DESC_INIT_FIELDS, .text = buf }));
    }
    cl_scrollview_set_content(hscroll, hscroll_body);

    menu_btn = cl_button_create(
        app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS,
                                  .text = "File \xE2\x96\xBE" });
    cl_button_set_on_click(menu_btn, on_menu_button, app);

    button = cl_button_create(
        app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS, .text = "Close" });
    cl_button_set_on_click(button, on_close, app);
    cl_widget_set_tooltip(button, "Quit the application");
    cl_widget_set_tooltip(textbox, "Type here — supports undo/redo and clipboard");
    cl_widget_set_tooltip(slider, "Drag to change the value");

    uptime = cl_label_create(
        app, &(cl_label_desc_t){ CL_LABEL_DESC_INIT_FIELDS, .text = "uptime: 0 s" });
    uptime_ctx.label = uptime;
    cl_timer_create(app, 1000, true, on_uptime_tick, &uptime_ctx);

    cl_widget_add_child(root, label);
    cl_widget_add_child(root, uptime);
    cl_widget_add_child(root, textbox);
    cl_widget_add_child(root, notes);
    cl_widget_add_child(root, checks);
    cl_widget_add_child(root, radios);
    cl_widget_add_child(root, slider);
    cl_widget_add_child(root, combo);
    cl_widget_add_child(root, scroll);
    cl_widget_add_child(root, hscroll);
    cl_widget_add_child(root, menu_btn);
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
