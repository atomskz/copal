/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * A tour of copal's widgets. The window is assembled from small builder
 * helpers so that main() reads as a short outline rather than one long block.
 */
#include <copal/copal.h>

#include <stdio.h>

#include "example_util.h"

/* --- callbacks ------------------------------------------------------------ */

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

static void on_menu_button(cl_widget_t *w, void *user)
{
    cl_application_t *app = user;
    cl_window_t *win = cl_widget_window(w);
    cl_rect_t r = cl_widget_rect(w);
    cl_widget_t *menu = cl_menu_create(app, &(cl_menu_desc_t){ CL_MENU_DESC_INIT_FIELDS });

    cl_menu_add_item(menu, "New", NULL, NULL);
    cl_menu_add_item(menu, "Open...", NULL, NULL);
    cl_menu_add_item(menu, "Save", NULL, NULL);
    cl_window_open_popup(win, menu, (cl_point_t){ r.x, r.y + r.h });
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

/* --- section builders ----------------------------------------------------- */

static cl_widget_t *label(cl_application_t *app, const char *text)
{
    return cl_label_create(
        app, &(cl_label_desc_t){ CL_LABEL_DESC_INIT_FIELDS, .text = text });
}

static cl_widget_t *make_checkboxes(cl_application_t *app)
{
    cl_widget_t *row = cl_hbox_create(
        app, &(cl_hbox_desc_t){ CL_HBOX_DESC_INIT_FIELDS, .spacing = 16,
                                .align_cross = CL_ALIGN_CENTER });
    cl_widget_t *dark;

    cl_widget_add_child(
        row, cl_checkbox_create(
                 app, &(cl_checkbox_desc_t){ CL_CHECKBOX_DESC_INIT_FIELDS,
                                             .text = "Enabled",
                                             .checked = true }));
    cl_widget_add_child(
        row, cl_checkbox_create(
                 app, &(cl_checkbox_desc_t){ CL_CHECKBOX_DESC_INIT_FIELDS,
                                             .text = "Verbose" }));
    dark = cl_checkbox_create(
        app, &(cl_checkbox_desc_t){ CL_CHECKBOX_DESC_INIT_FIELDS,
                                    .text = "Dark mode" });
    cl_checkbox_set_on_toggle(dark, on_dark_toggle, app);
    cl_widget_add_child(row, dark);
    return row;
}

static cl_widget_t *make_radios(cl_application_t *app)
{
    cl_widget_t *row = cl_hbox_create(
        app, &(cl_hbox_desc_t){ CL_HBOX_DESC_INIT_FIELDS, .spacing = 16,
                                .align_cross = CL_ALIGN_CENTER });

    cl_widget_add_child(
        row, cl_radiobutton_create(
                 app, &(cl_radiobutton_desc_t){ CL_RADIOBUTTON_DESC_INIT_FIELDS,
                                                .text = "Small", .group = 1,
                                                .selected = true }));
    cl_widget_add_child(
        row, cl_radiobutton_create(
                 app, &(cl_radiobutton_desc_t){ CL_RADIOBUTTON_DESC_INIT_FIELDS,
                                                .text = "Large", .group = 1 }));
    return row;
}

static cl_widget_t *make_combo(cl_application_t *app)
{
    cl_widget_t *combo = cl_combobox_create(
        app, &(cl_combobox_desc_t){ CL_COMBOBOX_DESC_INIT_FIELDS,
                                    .placeholder = "Choose a fruit" });

    cl_combobox_add_item(combo, "Apple");
    cl_combobox_add_item(combo, "Banana");
    cl_combobox_add_item(combo, "Cherry");
    return combo;
}

static cl_widget_t *make_vscroll(cl_application_t *app)
{
    cl_widget_t *scroll = cl_scrollview_create(
        app, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS,
                                      .smooth = true });
    cl_widget_t *body = cl_vbox_create(
        app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS, .spacing = 4 });
    int i;

    cl_widget_set_preferred_size(scroll, (cl_size_t){ 240, 120 });
    for (i = 0; i < 20; i++) {
        char buf[32];

        snprintf(buf, sizeof(buf), "scrollable row %d", i + 1);
        cl_widget_add_child(body, label(app, buf));
    }
    cl_scrollview_set_content(scroll, body);
    return scroll;
}

static cl_widget_t *make_hscroll(cl_application_t *app)
{
    cl_widget_t *scroll = cl_scrollview_create(
        app, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS,
                                      .horizontal = true });
    cl_widget_t *body = cl_hbox_create(
        app, &(cl_hbox_desc_t){ CL_HBOX_DESC_INIT_FIELDS, .spacing = 6 });
    int i;

    cl_widget_set_preferred_size(scroll, (cl_size_t){ 240, 44 });
    for (i = 0; i < 10; i++) {
        char buf[32];

        snprintf(buf, sizeof(buf), "column %d", i + 1);
        cl_widget_add_child(
            body, cl_button_create(app, &(cl_button_desc_t){
                                            CL_BUTTON_DESC_INIT_FIELDS,
                                            .text = buf }));
    }
    cl_scrollview_set_content(scroll, body);
    return scroll;
}

static cl_widget_t *build_root(cl_application_t *app, struct uptime_ctx *uptime)
{
    cl_widget_t *root = cl_vbox_create(
        app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS, .spacing = 12,
                                .padding = { 20, 20, 20, 20 },
                                .align_cross = CL_ALIGN_CENTER });
    cl_widget_t *textbox = cl_textbox_create(
        app, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS,
                                   .placeholder = "type here (Ctrl+C/V works)" });
    cl_widget_t *notes = cl_textbox_create(
        app, &(cl_textbox_desc_t){
                 CL_TEXTBOX_DESC_INIT_FIELDS, .multiline = true,
                 .text = "Multi-line notes.\nEnter adds a line; long lines wrap "
                         "to the width of the box; Up/Down move between lines "
                         "and the wheel scrolls." });
    cl_widget_t *slider = cl_slider_create(
        app, &(cl_slider_desc_t){ CL_SLIDER_DESC_INIT_FIELDS, .min = 0,
                                  .max = 100, .value = 40 });
    cl_widget_t *menu_btn = cl_button_create(
        app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS,
                                  .text = "File \xE2\x96\xBE" });
    cl_widget_t *close = cl_button_create(
        app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS, .text = "Close" });

    cl_widget_set_preferred_size(textbox, (cl_size_t){ 240, 0 });
    cl_widget_set_preferred_size(notes, (cl_size_t){ 240, 76 });
    cl_widget_set_preferred_size(slider, (cl_size_t){ 240, 0 });
    cl_button_set_on_click(menu_btn, on_menu_button, app);
    cl_button_set_on_click(close, on_close, app);
    cl_widget_set_tooltip(close, "Quit the application");
    cl_widget_set_tooltip(textbox, "Type here: undo/redo and clipboard");
    cl_widget_set_tooltip(slider, "Drag to change the value");

    uptime->label = label(app, "uptime: 0 s");
    cl_timer_create(app, 1000, true, on_uptime_tick, uptime);

    cl_widget_add_child(root, label(app, "Hello from copal"));
    cl_widget_add_child(root, uptime->label);
    cl_widget_add_child(root, textbox);
    cl_widget_add_child(root, notes);
    cl_widget_add_child(root, make_checkboxes(app));
    cl_widget_add_child(root, make_radios(app));
    cl_widget_add_child(root, slider);
    cl_widget_add_child(root, make_combo(app));
    cl_widget_add_child(root, make_vscroll(app));
    cl_widget_add_child(root, make_hscroll(app));
    cl_widget_add_child(root, menu_btn);
    cl_widget_add_child(root, close);
    return root;
}

int main(int argc, char **argv)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .title = "copal - hello",
                            .width = 480, .height = 320, .resizable = true };
    struct uptime_ctx uptime = { NULL, 0 };
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
    font = example_load_font(app, 18.0f);
    if (font)
        cl_theme_set_font(cl_application_theme(app), font);

    win = cl_window_create(app, &wd);
    if (!win) {
        fprintf(stderr, "cannot create window: %s\n",
                cl_result_string(cl_last_error()));
        cl_application_destroy(app);
        return 1;
    }

    cl_window_set_content(win, build_root(app, &uptime));
    cl_window_show(win);

    rc = example_run(app);

    if (font)
        cl_font_release(font);
    cl_application_destroy(app);
    return rc;
}
