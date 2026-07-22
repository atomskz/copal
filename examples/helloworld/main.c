/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * copal widget gallery - a demonstration application that exercises the whole
 * public API: every widget (label, button, checkbox, radio button/group,
 * slider, progress bar, textbox in all modes, combobox, list, image view,
 * panel, spacer, menu with submenus, menubar, message box, scroll views,
 * tooltips, a custom widget) and every subsystem (timers, animations with
 * easing and colour interpolation, transform/opacity painting, mouse cursors,
 * modal dialogs, the close-veto, posted tasks, the theme, clipboard-capable
 * text editing).
 *
 * The window is assembled from small builder helpers so the file reads as a
 * catalogue: one function per gallery section. A single file-scope `demo`
 * context keeps the callbacks short.
 */
#include <copal/copal.h>
#include <copal/widget_impl.h> /* the custom "pulse" widget below */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "example_util.h"

/* --- demo context ---------------------------------------------------------- */

static struct demo {
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *root;
    cl_widget_t *status;   /* status-bar label: every action reports here */
    cl_widget_t *uptime;   /* label fed by a repeating 1 s timer */
    int seconds;

    cl_widget_t *notes;     /* multiline textbox (File > New note clears it) */
    cl_widget_t *title_box; /* single-line textbox; focused at startup */
    cl_widget_t *hints;     /* keyboard-hints row (View > Toggle hints) */
    cl_widget_t *dark_box;  /* "Dark theme" checkbox, kept in sync with View */

    /* value & animation section */
    cl_widget_t *slider;
    cl_widget_t *progress;
    cl_widget_t *easing; /* combobox choosing the curve for "Play" */
    cl_animation_t *progress_anim;

    /* list section */
    cl_widget_t *list;
    int next_item;

    /* animated light <-> dark cross-fade */
    cl_color_t th_from[CL_COLOR__COUNT];
    cl_color_t th_to[CL_COLOR__COUNT];
    cl_theme_variant_t th_target;
    cl_animation_t *theme_anim;

    /* form-dialog widgets; valid only while the dialog is open */
    cl_widget_t *form_name;
    cl_widget_t *form_role;

    /* procedural images (owned; released before the app is destroyed) */
    cl_image_t *logo;
    cl_image_t *banner;
} demo;

static void status_msg(const char *fmt, ...)
{
    char buf[160];
    va_list ap;

    if (!demo.status)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cl_label_set_text(demo.status, buf);
}

/* --- small constructors ----------------------------------------------------- */

static cl_widget_t *label(const char *text)
{
    return cl_label_create(demo.app, &(cl_label_desc_t){
                                         CL_LABEL_DESC_INIT_FIELDS,
                                         .text = text });
}

static cl_widget_t *muted_label(const char *text)
{
    /* cl_text_style_t: a fixed colour that reads in both theme variants */
    static const cl_text_style_t style = { .color = { 140, 142, 150, 255 } };

    return cl_label_create(demo.app, &(cl_label_desc_t){
                                         CL_LABEL_DESC_INIT_FIELDS,
                                         .text = text, .style = &style });
}

static cl_widget_t *button(const char *text, cl_action_fn fn, void *user)
{
    cl_widget_t *b = cl_button_create(
        demo.app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS,
                                       .text = text });

    if (fn)
        cl_button_set_on_click(b, fn, user);
    return b;
}

static cl_widget_t *hbox(float spacing)
{
    return cl_hbox_create(demo.app, &(cl_hbox_desc_t){
                                        CL_HBOX_DESC_INIT_FIELDS,
                                        .spacing = spacing,
                                        .align_cross = CL_ALIGN_CENTER });
}

static cl_widget_t *vbox(float spacing)
{
    return cl_vbox_create(demo.app, &(cl_vbox_desc_t){
                                        CL_VBOX_DESC_INIT_FIELDS,
                                        .spacing = spacing,
                                        .align_cross = CL_ALIGN_STRETCH });
}

static cl_widget_t *flex_spacer(void)
{
    return cl_spacer_create(demo.app, &(cl_spacer_desc_t){
                                          CL_SPACER_DESC_INIT_FIELDS,
                                          .flex = 1.0f });
}

/* A titled, bordered panel around `body` - one gallery section. */
static cl_widget_t *section(const char *title, cl_widget_t *body)
{
    cl_widget_t *panel = cl_panel_create(
        demo.app, &(cl_panel_desc_t){ CL_PANEL_DESC_INIT_FIELDS,
                                      .padding = { 12, 10, 12, 12 },
                                      .bordered = true });
    cl_widget_t *col = vbox(8);

    cl_widget_add_child(col, label(title));
    cl_widget_add_child(col, body);
    cl_widget_add_child(panel, col);
    return panel;
}

/* --- procedural images ------------------------------------------------------ */

/* A soft round blob on transparent background - the "logo". */
static cl_image_t *make_logo(void)
{
    enum { N = 32 };
    static unsigned char px[N * N * 4];
    int x;
    int y;

    for (y = 0; y < N; y++) {
        for (x = 0; x < N; x++) {
            unsigned char *p = px + (size_t)(y * N + x) * 4;
            float fx = ((float)x + 0.5f) / N - 0.5f;
            float fy = ((float)y + 0.5f) / N - 0.5f;
            float d = 0.5f - (fx * fx + fy * fy) * 2.0f; /* 0.5 centre, <0 rim */
            float a = d * 6.0f; /* soft edge */

            if (a < 0.0f)
                a = 0.0f;
            if (a > 1.0f)
                a = 1.0f;
            p[0] = (unsigned char)(90 + x * 4);  /* R: left-right ramp */
            p[1] = (unsigned char)(70 + y * 3);  /* G: top-bottom ramp */
            p[2] = 220;                          /* B */
            p[3] = (unsigned char)(a * 255.0f);
        }
    }
    return cl_image_create(demo.app, N, N, px);
}

/* A wide RGB sweep - shows scaling and per-pixel colours in the image path. */
static cl_image_t *make_banner(void)
{
    enum { W = 220, H = 36 };
    static unsigned char px[W * H * 4];
    int x;
    int y;

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            unsigned char *p = px + (size_t)(y * W + x) * 4;
            float t = (float)x / (W - 1);
            float v = 1.0f - 0.5f * (float)y / (H - 1); /* darken downwards */

            p[0] = (unsigned char)(v * (40.0f + 200.0f * t));
            p[1] = (unsigned char)(v * (80.0f + 120.0f * (1.0f - t)));
            p[2] = (unsigned char)(v * (230.0f - 160.0f * t));
            p[3] = 255;
        }
    }
    return cl_image_create(demo.app, W, H, px);
}

/* --- custom widget: an animated "pulse" ------------------------------------- */
/*
 * Custom-widget authoring (widget_impl.h) plus the transform/opacity paint
 * primitives, driven by a chained animation: each cycle eases in-out, the
 * on_done handler flips the direction and starts the next one.
 */

typedef struct demo_pulse {
    cl_widget_t base;
    float phase; /* eased 0..1, ping-pongs */
    bool back;
    cl_animation_t *anim;
} demo_pulse_t;

static cl_size_t pulse_measure(cl_widget_t *w, cl_constraints_t c)
{
    (void)w;
    (void)c;
    return (cl_size_t){ 22.0f, 22.0f };
}

static void pulse_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    demo_pulse_t *p = (demo_pulse_t *)w;
    float k = 0.55f + 0.45f * p->phase; /* scale factor of this frame */
    float cx = w->rect.x + w->rect.w * 0.5f;
    float cy = w->rect.y + w->rect.h * 0.5f;

    /* Scale around the widget centre: p' = p*k + c*(1-k) keeps c fixed. */
    cl_paint_push_transform(ctx, (cl_point_t){ cx * (1.0f - k),
                                               cy * (1.0f - k) }, k);
    cl_paint_push_opacity(ctx, 0.35f + 0.65f * p->phase);
    cl_paint_fill_round_rect(ctx, w->rect, w->rect.w * 0.3f,
                             cl_paint_theme_color(ctx, CL_COLOR_ACCENT));
    cl_paint_pop_opacity(ctx);
    cl_paint_pop_transform(ctx);
}

static void pulse_destroy(cl_widget_t *w)
{
    demo_pulse_t *p = (demo_pulse_t *)w;

    if (p->anim)
        cl_animation_cancel(p->anim); /* fires on_done(false), see below */
}

static const cl_widget_vtable_t pulse_vtable = {
    .destroy = pulse_destroy,
    .measure = pulse_measure,
    .paint = pulse_paint,
};

static const cl_widget_class_t demo_pulse_class = {
    .name = "demo_pulse",
    .type_id = 0x70756c73u, /* 'puls' */
    .instance_size = sizeof(demo_pulse_t),
    .vtable = &pulse_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static void pulse_start(demo_pulse_t *p);

static void pulse_progress(cl_animation_t *anim, float t, void *user)
{
    demo_pulse_t *p = user;

    (void)anim;
    p->phase = p->back ? 1.0f - t : t;
    cl_widget_invalidate(&p->base);
}

static void pulse_done(cl_animation_t *anim, bool finished, void *user)
{
    demo_pulse_t *p = user;

    (void)anim;
    p->anim = NULL;
    if (!finished)
        return; /* cancelled: the widget is going away */
    p->back = !p->back;
    pulse_start(p); /* chain the next half-cycle */
}

static void pulse_start(demo_pulse_t *p)
{
    p->anim = cl_animation_start(
        demo.app, &(cl_animation_desc_t){ CL_ANIMATION_DESC_INIT_FIELDS,
                                          .duration_ms = 900,
                                          .easing = CL_EASE_IN_OUT,
                                          .on_progress = pulse_progress,
                                          .on_done = pulse_done,
                                          .user = p });
}

static cl_widget_t *make_pulse(void)
{
    cl_widget_t *w = cl_widget_alloc(demo.app, &demo_pulse_class);

    if (!w)
        return NULL;
    cl_widget_set_tooltip(w, "A custom widget: transform + opacity driven "
                             "by a looping animation");
    pulse_start((demo_pulse_t *)w);
    return w;
}

/* --- theme cross-fade -------------------------------------------------------- */
/* Animate every colour role from the current palette to the other variant's
 * one - a smooth light <-> dark transition out of cl_color_lerp. */

static void theme_tick(cl_animation_t *anim, float t, void *user)
{
    cl_theme_t *th = cl_application_theme(demo.app);
    int i;

    (void)anim;
    (void)user;
    for (i = 0; i < CL_COLOR__COUNT; i++)
        cl_theme_set_color(th, (cl_color_role_t)i,
                           cl_color_lerp(demo.th_from[i], demo.th_to[i], t));
    cl_widget_invalidate(demo.root);
}

static void theme_done(cl_animation_t *anim, bool finished, void *user)
{
    (void)anim;
    (void)user;
    demo.theme_anim = NULL;
    if (finished) {
        /* snap to the exact built-in palette (and keep cl_theme_variant honest) */
        cl_theme_set_variant(cl_application_theme(demo.app), demo.th_target);
        cl_widget_invalidate(demo.root);
    }
}

static void theme_toggle(void)
{
    cl_theme_t *th = cl_application_theme(demo.app);
    int i;

    if (demo.theme_anim)
        cl_animation_cancel(demo.theme_anim); /* restart from the mid-blend */
    demo.th_target = cl_theme_variant(th) == CL_THEME_LIGHT ? CL_THEME_DARK
                                                            : CL_THEME_LIGHT;
    for (i = 0; i < CL_COLOR__COUNT; i++)
        demo.th_from[i] = cl_theme_color(th, (cl_color_role_t)i);
    cl_theme_set_variant(th, demo.th_target);
    for (i = 0; i < CL_COLOR__COUNT; i++)
        demo.th_to[i] = cl_theme_color(th, (cl_color_role_t)i);
    for (i = 0; i < CL_COLOR__COUNT; i++)
        cl_theme_set_color(th, (cl_color_role_t)i, demo.th_from[i]);

    demo.theme_anim = cl_animation_start(
        demo.app, &(cl_animation_desc_t){ CL_ANIMATION_DESC_INIT_FIELDS,
                                          .duration_ms = 350,
                                          .easing = CL_EASE_IN_OUT,
                                          .on_progress = theme_tick,
                                          .on_done = theme_done });
    if (!demo.theme_anim) /* no clock on this platform: switch instantly */
        cl_theme_set_variant(th, demo.th_target);
    cl_checkbox_set_checked(demo.dark_box, demo.th_target == CL_THEME_DARK);
    cl_widget_invalidate(demo.root);
    status_msg("theme: %s",
               demo.th_target == CL_THEME_DARK ? "dark" : "light");
}

/* --- quitting (close veto + confirmation) ------------------------------------ */

static void quit_confirmed(int index, void *user)
{
    (void)user;
    if (index == 0)
        cl_application_quit(demo.app, 0);
    else
        status_msg("quit cancelled");
}

static void request_quit(void)
{
    cl_messagebox_show(demo.win, "Quit", "Close the widget gallery?",
                       CL_MSGBOX_YES_NO, quit_confirmed, NULL);
}

/* The window close button is vetoed; the modal confirmation quits instead. */
static bool on_close_request(cl_window_t *win, void *user)
{
    (void)win;
    (void)user;
    request_quit();
    return false;
}

/* --- menubar ------------------------------------------------------------------ */

static void mi_new_note(cl_widget_t *menu, void *user)
{
    (void)menu;
    (void)user;
    cl_textbox_set_text(demo.notes, "");
    status_msg("notes cleared");
}

static void mi_export(cl_widget_t *menu, void *user)
{
    (void)menu;
    status_msg("exported notes as %s (pretend)", (const char *)user);
}

static void mi_quit(cl_widget_t *menu, void *user)
{
    (void)menu;
    (void)user;
    request_quit();
}

static void mi_theme(cl_widget_t *menu, void *user)
{
    (void)menu;
    (void)user;
    theme_toggle();
}

static void mi_hints(cl_widget_t *menu, void *user)
{
    bool show = !cl_widget_is_visible(demo.hints);

    (void)menu;
    (void)user;
    cl_widget_set_visible(demo.hints, show);
    status_msg("hints %s", show ? "shown" : "hidden");
}

static void close_dialog(cl_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    cl_window_close_popup(demo.win);
}

static void mi_about(cl_widget_t *menu, void *user)
{
    cl_widget_t *dlg = cl_panel_create(
        demo.app, &(cl_panel_desc_t){ CL_PANEL_DESC_INIT_FIELDS,
                                      .padding = { 24, 20, 24, 20 },
                                      .bordered = true });
    cl_widget_t *col = cl_vbox_create(
        demo.app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS, .spacing = 8,
                                     .align_cross = CL_ALIGN_CENTER });
    char ver[64];

    (void)menu;
    (void)user;
    snprintf(ver, sizeof(ver), "copal %s", cl_version_string());
    cl_widget_add_child(col, cl_imageview_create(
                                 demo.app, &(cl_imageview_desc_t){
                                               CL_IMAGEVIEW_DESC_INIT_FIELDS,
                                               .image = demo.logo }));
    cl_widget_add_child(col, label(ver));
    cl_widget_add_child(col, muted_label("a lightweight C GUI library"));
    cl_widget_add_child(col, muted_label("GPL-3.0-or-later"));
    cl_widget_add_child(col, button("OK", close_dialog, NULL));
    cl_widget_add_child(dlg, col);
    cl_window_open_modal(demo.win, dlg);
}

static cl_widget_t *make_menubar(void)
{
    cl_widget_t *bar = cl_menubar_create(
        demo.app, &(cl_menubar_desc_t){ CL_MENUBAR_DESC_INIT_FIELDS });
    cl_widget_t *file = cl_menu_create(
        demo.app, &(cl_menu_desc_t){ CL_MENU_DESC_INIT_FIELDS });
    cl_widget_t *export_menu = cl_menu_create(
        demo.app, &(cl_menu_desc_t){ CL_MENU_DESC_INIT_FIELDS });
    cl_widget_t *view = cl_menu_create(
        demo.app, &(cl_menu_desc_t){ CL_MENU_DESC_INIT_FIELDS });
    cl_widget_t *help = cl_menu_create(
        demo.app, &(cl_menu_desc_t){ CL_MENU_DESC_INIT_FIELDS });

    cl_menu_add_item(file, "New note", mi_new_note, NULL);
    cl_menu_add_item(export_menu, "Plain text", mi_export, (void *)"plain text");
    cl_menu_add_item(export_menu, "Markdown", mi_export, (void *)"Markdown");
    cl_menu_add_submenu(file, "Export", export_menu);
    cl_menu_add_item(file, "Quit", mi_quit, NULL);

    cl_menu_add_item(view, "Toggle dark theme", mi_theme, NULL);
    cl_menu_add_item(view, "Toggle hints", mi_hints, NULL);

    cl_menu_add_item(help, "About...", mi_about, NULL);

    cl_menubar_add_menu(bar, "File", file);
    cl_menubar_add_menu(bar, "View", view);
    cl_menubar_add_menu(bar, "Help", help);
    return bar;
}

/* --- header and status bar ----------------------------------------------------- */

static void on_uptime_tick(cl_timer_t *timer, void *user)
{
    char buf[32];

    (void)timer;
    (void)user;
    demo.seconds++;
    snprintf(buf, sizeof(buf), "uptime: %d s", demo.seconds);
    cl_label_set_text(demo.uptime, buf);
}

static cl_widget_t *make_header(void)
{
    cl_widget_t *row = cl_hbox_create(
        demo.app, &(cl_hbox_desc_t){ CL_HBOX_DESC_INIT_FIELDS, .spacing = 10,
                                     .padding = { 16, 10, 16, 6 },
                                     .align_cross = CL_ALIGN_CENTER });
    cl_widget_t *logo = cl_imageview_create(
        demo.app, &(cl_imageview_desc_t){ CL_IMAGEVIEW_DESC_INIT_FIELDS,
                                          .image = demo.logo });
    cl_widget_t *pulse = make_pulse();

    cl_widget_set_margin(logo, (cl_insets_t){ 0, 0, 2, 0 });
    cl_widget_add_child(row, logo);
    cl_widget_add_child(row, label("copal widget gallery"));
    cl_widget_add_child(row, muted_label("every widget on one page"));
    cl_widget_add_child(row, flex_spacer());
    if (pulse)
        cl_widget_add_child(row, pulse);
    return row;
}

static cl_widget_t *make_statusbar(void)
{
    cl_widget_t *panel = cl_panel_create(
        demo.app, &(cl_panel_desc_t){ CL_PANEL_DESC_INIT_FIELDS,
                                      .padding = { 12, 4, 12, 4 } });
    cl_widget_t *row = hbox(8);

    demo.status = label("ready - every control reports here");
    demo.uptime = muted_label("uptime: 0 s");
    cl_widget_add_child(row, demo.status);
    cl_widget_add_child(row, flex_spacer());
    cl_widget_add_child(row, demo.uptime);
    cl_widget_add_child(panel, row);
    cl_timer_create(demo.app, 1000, true, on_uptime_tick, NULL);
    return panel;
}

/* --- section: text input -------------------------------------------------------- */

static void on_text_changed(cl_widget_t *w, const char *utf8, void *user)
{
    (void)w;
    (void)user;
    status_msg("typing: \"%s\"", utf8);
}

static void on_title_submit(cl_widget_t *w, const char *utf8, void *user)
{
    char buf[96];

    (void)w;
    (void)user;
    snprintf(buf, sizeof(buf), "copal - %s",
             utf8 && utf8[0] ? utf8 : "gallery");
    cl_window_set_title(demo.win, buf);
    status_msg("window title set");
}

static cl_widget_t *make_text_section(void)
{
    cl_widget_t *col = vbox(8);
    cl_widget_t *line = cl_textbox_create(
        demo.app,
        &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS,
                              .placeholder =
                                  "type and press Enter to set the window "
                                  "title" });
    cl_widget_t *pass = cl_textbox_create(
        demo.app, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS,
                                        .placeholder = "password (max 12)",
                                        .password = true,
                                        .max_length = 12 });
    cl_widget_t *ro = cl_textbox_create(
        demo.app,
        &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS,
                              .text = "read-only: caret, selection and "
                                      "copying work, editing does not",
                              .readonly = true });

    demo.notes = cl_textbox_create(
        demo.app,
        &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS, .multiline = true,
                              .text = "Multi-line notes.\nEnter adds a line; "
                                      "long lines wrap to the width of the "
                                      "box; Up/Down move between lines and "
                                      "the wheel scrolls." });
    cl_widget_set_preferred_size(demo.notes, (cl_size_t){ 0, 84 });
    cl_textbox_set_on_changed(line, on_text_changed, NULL);
    cl_textbox_set_on_submit(line, on_title_submit, NULL);
    cl_widget_set_tooltip(line, "Undo/redo (Ctrl+Z/Y), clipboard "
                                "(Ctrl+C/X/V), word jumps (Ctrl+arrows)");
    cl_widget_set_tooltip(pass, "Masked input with a length limit");
    cl_widget_set_tooltip(demo.notes, "File > New note clears this box");

    cl_widget_add_child(col, line);
    cl_widget_add_child(col, pass);
    cl_widget_add_child(col, ro);
    cl_widget_add_child(col, demo.notes);
    demo.title_box = line; /* focused once the tree is attached (main) */
    return section("Text input", col);
}

/* --- section: buttons & choices --------------------------------------------------- */

static void on_plain_click(cl_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    status_msg("button clicked");
}

static void posted_task(void *user)
{
    (void)user;
    status_msg("posted task ran on the next loop iteration");
}

static void on_post_click(cl_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    cl_application_post(demo.app, posted_task, NULL);
    status_msg("task posted (cl_application_post)");
}

static void on_dark_toggle(cl_widget_t *w, bool checked, void *user)
{
    (void)w;
    (void)checked;
    (void)user;
    theme_toggle();
}

static void on_check_toggle(cl_widget_t *w, bool checked, void *user)
{
    (void)w;
    (void)user;
    status_msg("checkbox: %s", checked ? "on" : "off");
}

static void on_density(cl_widget_t *w, bool selected, void *user)
{
    (void)w;
    if (selected)
        status_msg("density: %s", (const char *)user);
}

static void on_size_change(cl_widget_t *group, int index, void *user)
{
    static const char *names[] = { "small", "medium", "large" };

    (void)group;
    (void)user;
    status_msg("size: %s", index >= 0 && index < 3 ? names[index] : "?");
}

static void on_fruit_change(cl_widget_t *combo, int index, void *user)
{
    (void)user;
    status_msg("fruit: %s", cl_combobox_item_text(combo, (size_t)index));
}

static cl_widget_t *make_choices_section(void)
{
    cl_widget_t *col = vbox(10);
    cl_widget_t *btn_row = hbox(8);
    cl_widget_t *check_row = hbox(16);
    cl_widget_t *radio_row = hbox(16);
    cl_widget_t *pick_row = hbox(16);
    cl_widget_t *disabled = button("Disabled", NULL, NULL);
    cl_widget_t *verbose;
    cl_widget_t *off;
    cl_widget_t *compact;
    cl_widget_t *comfy;
    cl_widget_t *sizes;
    cl_widget_t *combo;

    /* buttons: a plain action, a posted task, a disabled one */
    cl_widget_set_enabled(disabled, false);
    cl_widget_set_tooltip(disabled, "cl_widget_set_enabled(w, false)");
    cl_widget_add_child(btn_row, button("Click me", on_plain_click, NULL));
    cl_widget_add_child(btn_row, button("Post a task", on_post_click, NULL));
    cl_widget_add_child(btn_row, disabled);

    /* checkboxes, incl. the animated theme toggle and a disabled one */
    verbose = cl_checkbox_create(
        demo.app, &(cl_checkbox_desc_t){ CL_CHECKBOX_DESC_INIT_FIELDS,
                                         .text = "Verbose",
                                         .checked = true });
    cl_checkbox_set_on_toggle(verbose, on_check_toggle, NULL);
    demo.dark_box = cl_checkbox_create(
        demo.app, &(cl_checkbox_desc_t){ CL_CHECKBOX_DESC_INIT_FIELDS,
                                         .text = "Dark theme" });
    cl_checkbox_set_on_toggle(demo.dark_box, on_dark_toggle, NULL);
    cl_widget_set_tooltip(demo.dark_box,
                          "Animated cross-fade between the palettes");
    off = cl_checkbox_create(
        demo.app, &(cl_checkbox_desc_t){ CL_CHECKBOX_DESC_INIT_FIELDS,
                                         .text = "Unavailable" });
    cl_widget_set_enabled(off, false);
    cl_widget_add_child(check_row, verbose);
    cl_widget_add_child(check_row, demo.dark_box);
    cl_widget_add_child(check_row, off);

    /* stand-alone radio buttons sharing a hand-picked group id */
    compact = cl_radiobutton_create(
        demo.app, &(cl_radiobutton_desc_t){ CL_RADIOBUTTON_DESC_INIT_FIELDS,
                                            .text = "Compact", .group = 1,
                                            .selected = true });
    comfy = cl_radiobutton_create(
        demo.app, &(cl_radiobutton_desc_t){ CL_RADIOBUTTON_DESC_INIT_FIELDS,
                                            .text = "Comfortable",
                                            .group = 1 });
    cl_radiobutton_set_on_select(compact, on_density, (void *)"compact");
    cl_radiobutton_set_on_select(comfy, on_density, (void *)"comfortable");
    cl_widget_add_child(radio_row, label("Density:"));
    cl_widget_add_child(radio_row, compact);
    cl_widget_add_child(radio_row, comfy);

    /* a radio GROUP (auto-exclusive column) and a combobox */
    sizes = cl_radiogroup_create(
        demo.app, &(cl_radiogroup_desc_t){ CL_RADIOGROUP_DESC_INIT_FIELDS,
                                           .spacing = 4.0f });
    cl_radiogroup_add(sizes, "Small");
    cl_radiogroup_add(sizes, "Medium");
    cl_radiogroup_add(sizes, "Large");
    cl_radiogroup_set_selected(sizes, 1);
    cl_radiogroup_set_on_change(sizes, on_size_change, NULL);

    combo = cl_combobox_create(
        demo.app, &(cl_combobox_desc_t){ CL_COMBOBOX_DESC_INIT_FIELDS,
                                         .placeholder = "Choose a fruit" });
    cl_combobox_add_item(combo, "Apple");
    cl_combobox_add_item(combo, "Banana");
    cl_combobox_add_item(combo, "Cherry");
    cl_combobox_add_item(combo, "Quince");
    cl_combobox_set_on_change(combo, on_fruit_change, NULL);

    cl_widget_add_child(pick_row, sizes);
    cl_widget_add_child(pick_row, combo);

    cl_widget_add_child(col, btn_row);
    cl_widget_add_child(col, check_row);
    cl_widget_add_child(col, radio_row);
    cl_widget_add_child(col, pick_row);
    return section("Buttons & choices", col);
}

/* --- section: values & animation --------------------------------------------------- */

static void on_slider_change(cl_widget_t *w, float value, void *user)
{
    (void)w;
    (void)user;
    if (demo.progress_anim)
        cl_animation_cancel(demo.progress_anim); /* hand control back */
    cl_progressbar_set_value(demo.progress, value / 100.0f);
    status_msg("slider: %.0f", value);
}

static void progress_tick(cl_animation_t *anim, float t, void *user)
{
    (void)anim;
    (void)user;
    cl_progressbar_set_value(demo.progress, t); /* t is already eased */
}

static void progress_done(cl_animation_t *anim, bool finished, void *user)
{
    (void)anim;
    (void)user;
    demo.progress_anim = NULL;
    if (finished)
        status_msg("progress animation finished");
}

static void on_play_click(cl_widget_t *w, void *user)
{
    static const char *names[] = { "linear", "ease-in", "ease-out",
                                   "ease-in-out" };
    int e = cl_combobox_selected(demo.easing);

    (void)w;
    (void)user;
    if (e < 0)
        e = CL_EASE_IN_OUT;
    if (demo.progress_anim)
        cl_animation_cancel(demo.progress_anim);
    cl_progressbar_set_value(demo.progress, 0.0f);
    demo.progress_anim = cl_animation_start(
        demo.app, &(cl_animation_desc_t){ CL_ANIMATION_DESC_INIT_FIELDS,
                                          .duration_ms = 1400,
                                          .easing = (cl_easing_t)e,
                                          .on_progress = progress_tick,
                                          .on_done = progress_done });
    status_msg("animating the bar with the %s curve", names[e]);
}

static cl_widget_t *make_value_section(void)
{
    cl_widget_t *col = vbox(8);
    cl_widget_t *row = hbox(8);

    demo.slider = cl_slider_create(
        demo.app, &(cl_slider_desc_t){ CL_SLIDER_DESC_INIT_FIELDS, .min = 0,
                                       .max = 100, .value = 40, .step = 5 });
    demo.progress = cl_progressbar_create(
        demo.app, &(cl_progressbar_desc_t){ CL_PROGRESSBAR_DESC_INIT_FIELDS,
                                            .value = 0.4f });
    demo.easing = cl_combobox_create(
        demo.app, &(cl_combobox_desc_t){ CL_COMBOBOX_DESC_INIT_FIELDS,
                                         .placeholder = "easing" });
    cl_combobox_add_item(demo.easing, "Linear");
    cl_combobox_add_item(demo.easing, "Ease in");
    cl_combobox_add_item(demo.easing, "Ease out");
    cl_combobox_add_item(demo.easing, "Ease in-out");
    cl_combobox_set_selected(demo.easing, CL_EASE_IN_OUT);

    cl_slider_set_on_change(demo.slider, on_slider_change, NULL);
    cl_widget_set_tooltip(demo.slider,
                          "Drag, or use arrows/Home/End while focused");
    cl_widget_set_tooltip(demo.progress,
                          "Fed by the slider or by an animation");

    cl_widget_add_child(row, demo.easing);
    cl_widget_add_child(row, button("Play", on_play_click, NULL));
    cl_widget_add_child(row, flex_spacer());

    cl_widget_add_child(col, demo.slider);
    cl_widget_add_child(col, demo.progress);
    cl_widget_add_child(col, row);
    return section("Values & animation", col);
}

/* --- section: list ------------------------------------------------------------------ */

static void on_list_select(cl_widget_t *list, int index, void *user)
{
    (void)user;
    if (index >= 0)
        status_msg("list: selected \"%s\"",
                   cl_list_item_text(list, (size_t)index));
}

static void on_list_activate(cl_widget_t *list, int index, void *user)
{
    (void)user;
    status_msg("list: activated \"%s\" (double-click or Enter)",
               cl_list_item_text(list, (size_t)index));
}

static void on_list_add(cl_widget_t *w, void *user)
{
    char buf[32];

    (void)w;
    (void)user;
    snprintf(buf, sizeof(buf), "added item %d", ++demo.next_item);
    cl_list_add_item(demo.list, buf);
    cl_list_set_selected(demo.list, (int)cl_list_count(demo.list) - 1);
    status_msg("list: %zu item(s)", cl_list_count(demo.list));
}

static void on_list_remove(cl_widget_t *w, void *user)
{
    int sel = cl_list_selected(demo.list);

    (void)w;
    (void)user;
    if (sel < 0) {
        status_msg("list: nothing selected");
        return;
    }
    cl_list_remove(demo.list, (size_t)sel);
    status_msg("list: %zu item(s)", cl_list_count(demo.list));
}

static void on_list_clear(cl_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    cl_list_clear(demo.list);
    status_msg("list cleared");
}

static cl_widget_t *make_list_section(void)
{
    cl_widget_t *col = vbox(8);
    cl_widget_t *row = hbox(8);
    int i;

    demo.list = cl_list_create(demo.app,
                               &(cl_list_desc_t){ CL_LIST_DESC_INIT_FIELDS });
    for (i = 0; i < 5; i++) {
        char buf[24];

        snprintf(buf, sizeof(buf), "list item %d", i + 1);
        cl_list_add_item(demo.list, buf);
    }
    cl_widget_set_preferred_size(demo.list, (cl_size_t){ 0, 110 });
    cl_list_set_on_select(demo.list, on_list_select, NULL);
    cl_list_set_on_activate(demo.list, on_list_activate, NULL);
    cl_widget_set_tooltip(demo.list, "Arrows, Home/End, PageUp/Down; Enter "
                                     "or double-click activates");

    cl_widget_add_child(row, button("Add", on_list_add, NULL));
    cl_widget_add_child(row, button("Remove selected", on_list_remove, NULL));
    cl_widget_add_child(row, button("Clear", on_list_clear, NULL));
    cl_widget_add_child(row, flex_spacer());

    cl_widget_add_child(col, demo.list);
    cl_widget_add_child(col, row);
    return section("List", col);
}

/* --- section: images & cursors -------------------------------------------------------- */

static cl_widget_t *cursor_chip(const char *text, cl_cursor_t cursor,
                                const char *tip)
{
    cl_widget_t *chip = cl_panel_create(
        demo.app, &(cl_panel_desc_t){ CL_PANEL_DESC_INIT_FIELDS,
                                      .padding = { 10, 4, 10, 4 },
                                      .bordered = true });

    cl_widget_add_child(chip, label(text));
    cl_widget_set_cursor(chip, cursor);
    cl_widget_set_tooltip(chip, tip);
    return chip;
}

static cl_widget_t *make_media_section(void)
{
    cl_widget_t *col = vbox(8);
    cl_widget_t *row = hbox(8);
    cl_widget_t *banner = cl_imageview_create(
        demo.app, &(cl_imageview_desc_t){ CL_IMAGEVIEW_DESC_INIT_FIELDS,
                                          .image = demo.banner });

    cl_widget_set_tooltip(banner, "A procedurally generated cl_image_t");

    cl_widget_add_child(row, cursor_chip("hand", CL_CURSOR_HAND,
                                         "CL_CURSOR_HAND"));
    cl_widget_add_child(row, cursor_chip("crosshair", CL_CURSOR_CROSSHAIR,
                                         "CL_CURSOR_CROSSHAIR"));
    cl_widget_add_child(row, cursor_chip("resize -", CL_CURSOR_SIZE_H,
                                         "CL_CURSOR_SIZE_H"));
    cl_widget_add_child(row, cursor_chip("resize |", CL_CURSOR_SIZE_V,
                                         "CL_CURSOR_SIZE_V"));
    cl_widget_add_child(row, flex_spacer());

    cl_widget_add_child(col, banner);
    cl_widget_add_child(col, muted_label("hover the chips to change the "
                                         "mouse cursor:"));
    cl_widget_add_child(col, row);
    return section("Images & cursors", col);
}

/* --- section: dialogs ------------------------------------------------------------------ */

static void msg_result(cl_widget_t *dialog, int index, void *user)
{
    (void)dialog;
    (void)user;
    status_msg("message box: %s (index %d)",
               index == 0 ? "confirmed" : "dismissed", index);
}

static void on_msgbox_click(cl_widget_t *w, void *user)
{
    cl_msgbox_buttons_t kind = (cl_msgbox_buttons_t)(intptr_t)user;
    static const char *texts[] = {
        "A plain notification with a single button.",
        "Proceed with the operation?",
        "Save the changes before closing?",
    };

    (void)w;
    cl_messagebox_show(demo.win, "Gallery", texts[kind], kind, msg_result,
                       NULL);
}

static void form_done(bool ok)
{
    if (ok) {
        static const char *roles[] = { "guest", "editor", "admin" };
        const char *name = cl_textbox_text(demo.form_name);
        int role = cl_radiogroup_selected(demo.form_role);

        status_msg("form: \"%s\", role %s", name && name[0] ? name : "(anon)",
                   role >= 0 && role < 3 ? roles[role] : "?");
    } else {
        status_msg("form cancelled");
    }
    demo.form_name = NULL;
    demo.form_role = NULL;
    cl_window_close_popup(demo.win);
}

static void on_form_ok(cl_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    form_done(true);
}

static void on_form_cancel(cl_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    form_done(false);
}

/* A hand-built modal form: textbox + radio group inside cl_window_open_modal.
 * Clicking focuses, Tab cycles the dialog's fields, drags stay captured. */
static void on_form_open(cl_widget_t *w, void *user)
{
    cl_widget_t *dlg = cl_panel_create(
        demo.app, &(cl_panel_desc_t){ CL_PANEL_DESC_INIT_FIELDS,
                                      .padding = { 20, 16, 20, 16 },
                                      .bordered = true });
    cl_widget_t *col = vbox(8);
    cl_widget_t *btns = hbox(8);

    (void)w;
    (void)user;
    demo.form_name = cl_textbox_create(
        demo.app, &(cl_textbox_desc_t){ CL_TEXTBOX_DESC_INIT_FIELDS,
                                        .placeholder = "your name" });
    demo.form_role = cl_radiogroup_create(
        demo.app, &(cl_radiogroup_desc_t){ CL_RADIOGROUP_DESC_INIT_FIELDS,
                                           .spacing = 4.0f });
    cl_radiogroup_add(demo.form_role, "Guest");
    cl_radiogroup_add(demo.form_role, "Editor");
    cl_radiogroup_add(demo.form_role, "Admin");
    cl_radiogroup_set_selected(demo.form_role, 0);

    cl_widget_add_child(btns, flex_spacer());
    cl_widget_add_child(btns, button("Cancel", on_form_cancel, NULL));
    cl_widget_add_child(btns, button("OK", on_form_ok, NULL));

    cl_widget_add_child(col, label("Create a profile"));
    cl_widget_add_child(col, demo.form_name);
    cl_widget_add_child(col, label("Role:"));
    cl_widget_add_child(col, demo.form_role);
    cl_widget_add_child(col, btns);
    cl_widget_add_child(dlg, col);

    cl_window_open_modal(demo.win, dlg);
    cl_widget_focus(demo.form_name);
}

static cl_widget_t *make_dialog_section(void)
{
    cl_widget_t *row = hbox(8);

    cl_widget_add_child(row, button("Message", on_msgbox_click,
                                    (void *)(intptr_t)CL_MSGBOX_OK));
    cl_widget_add_child(row, button("OK / Cancel", on_msgbox_click,
                                    (void *)(intptr_t)CL_MSGBOX_OK_CANCEL));
    cl_widget_add_child(row, button("Yes / No", on_msgbox_click,
                                    (void *)(intptr_t)CL_MSGBOX_YES_NO));
    cl_widget_add_child(row, button("Form dialog...", on_form_open, NULL));
    cl_widget_add_child(row, flex_spacer());
    return section("Dialogs", row);
}

/* --- section: nested scrolling ----------------------------------------------------------- */

static cl_widget_t *make_scroll_section(void)
{
    cl_widget_t *col = vbox(8);
    cl_widget_t *vscroll = cl_scrollview_create(
        demo.app, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS,
                                           .smooth = true });
    cl_widget_t *vbody = vbox(4);
    cl_widget_t *hscroll = cl_scrollview_create(
        demo.app, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS,
                                           .horizontal = true });
    cl_widget_t *hbody = hbox(6);
    int i;

    cl_widget_set_preferred_size(vscroll, (cl_size_t){ 0, 96 });
    for (i = 0; i < 20; i++) {
        char buf[32];

        snprintf(buf, sizeof(buf), "scrollable row %d", i + 1);
        cl_widget_add_child(vbody, label(buf));
    }
    cl_scrollview_set_content(vscroll, vbody);

    cl_widget_set_preferred_size(hscroll, (cl_size_t){ 0, 46 });
    for (i = 0; i < 12; i++) {
        char buf[32];

        snprintf(buf, sizeof(buf), "column %d", i + 1);
        cl_widget_add_child(hbody, button(buf, NULL, NULL));
    }
    cl_scrollview_set_content(hscroll, hbody);

    cl_widget_add_child(col, vscroll);
    cl_widget_add_child(col, hscroll);
    return section("Scrolling (smooth vertical, horizontal)", col);
}

/* --- root ----------------------------------------------------------------------------------- */

static cl_widget_t *build_root(void)
{
    cl_widget_t *root = vbox(0);
    cl_widget_t *scroll = cl_scrollview_create(
        demo.app, &(cl_scrollview_desc_t){ CL_SCROLLVIEW_DESC_INIT_FIELDS,
                                           .smooth = true });
    cl_widget_t *content = cl_vbox_create(
        demo.app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS, .spacing = 12,
                                     .padding = { 16, 12, 16, 12 },
                                     .align_cross = CL_ALIGN_STRETCH });

    demo.hints = muted_label("Tab/Shift+Tab move focus - Ctrl+C/X/V use the "
                             "clipboard - Ctrl+Z/Y undo/redo - Escape closes "
                             "menus and dialogs");

    cl_widget_add_child(content, make_text_section());
    cl_widget_add_child(content, make_choices_section());
    cl_widget_add_child(content, make_value_section());
    cl_widget_add_child(content, make_list_section());
    cl_widget_add_child(content, make_media_section());
    cl_widget_add_child(content, make_dialog_section());
    cl_widget_add_child(content, make_scroll_section());
    cl_widget_add_child(content, demo.hints);
    cl_scrollview_set_content(scroll, content);
    cl_widget_set_flex(scroll, 1.0f); /* the gallery takes the leftover space */

    cl_widget_add_child(root, make_menubar());
    cl_widget_add_child(root, make_header());
    cl_widget_add_child(root, scroll);
    cl_widget_add_child(root, make_statusbar());
    return root;
}

int main(int argc, char **argv)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .title = "copal - gallery",
                            .width = 640, .height = 560,
                            .min_width = 480, .min_height = 360,
                            .resizable = true };
    cl_font_t *font;
    int rc;

    ad.render_backend = example_backend(argc, argv); /* --software / --gl */
    demo.app = cl_application_create(&ad);
    if (!demo.app) {
        fprintf(stderr, "cannot create application: %s\n",
                cl_result_string(cl_last_error()));
        return 1;
    }
    font = example_load_font(demo.app, 16.0f);
    if (font)
        cl_theme_set_font(cl_application_theme(demo.app), font);

    demo.win = cl_window_create(demo.app, &wd);
    if (!demo.win) {
        fprintf(stderr, "cannot create window: %s\n",
                cl_result_string(cl_last_error()));
        cl_application_destroy(demo.app);
        return 1;
    }

    demo.logo = make_logo();
    demo.banner = make_banner();
    demo.root = build_root();
    cl_window_set_content(demo.win, demo.root);
    cl_window_set_on_close(demo.win, on_close_request, NULL);
    cl_widget_focus(demo.title_box); /* start with the caret ready to type */
    cl_window_show(demo.win);

    rc = example_run(demo.app);

    cl_image_release(demo.logo);
    cl_image_release(demo.banner);
    if (font)
        cl_font_release(font);
    cl_application_destroy(demo.app);
    return rc;
}
