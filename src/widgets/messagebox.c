/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/messagebox.h>
#include <copal/widgets/label.h>
#include <copal/widgets/button.h>
#include <copal/window.h>
#include <copal/layout.h>
#include <copal/widget_impl.h>
#include <copal/application.h>

#include "widget/widget_internal.h"
#include "widget/widget_host.h"
#include "core/foundation/foundation_internal.h"
#include "theme/theme_internal.h"

#define MSG_PAD 16.0f
#define MSG_SPACING 12.0f

/*
 * The dialog is its own widget class: a one-child frame that paints a raised
 * panel (menu-style) behind a vbox holding the labels and the button row.
 */
typedef struct cl_msgbox {
    cl_widget_t base;
    cl_msgbox_fn fn;
    void *user;
    cl_widget_t *btn[2]; /* [0]=OK/Yes, [1]=Cancel/No or NULL */
    bool done;           /* the callback fired; ignore further activations */
} cl_msgbox_t;

static cl_size_t msgbox_measure(cl_widget_t *w, cl_constraints_t c);
static void msgbox_arrange(cl_widget_t *w, cl_rect_t rect);
static void msgbox_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool msgbox_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool msgbox_key_down(cl_widget_t *w, const cl_event_t *ev);

static const cl_widget_vtable_t msgbox_vtable = {
    .measure = msgbox_measure,
    .arrange = msgbox_arrange,
    .paint = msgbox_paint,
    .mouse_down = msgbox_mouse_down,
    .key_down = msgbox_key_down,
};

static const cl_widget_class_t cl_msgbox_class = {
    .name = "cl_msgbox",
    .base = NULL,
    .type_id = 0x6d736762u, /* 'msgb' */
    .instance_size = sizeof(cl_msgbox_t),
    .vtable = &msgbox_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static void msg_finish(cl_msgbox_t *mb, int index)
{
    cl_widget_host_t *h = cl_widget_host(&mb->base);
    cl_msgbox_fn fn = mb->fn;
    void *user = mb->user;

    if (mb->done)
        return;
    mb->done = true;
    if (h)
        h->ops->close_popup(h); /* deferred; the dialog outlives the call */
    if (fn)
        fn(index, user); /* last: may open another dialog */
}

static cl_size_t msgbox_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_size_t sz = { 0, 0 };

    if (w->first_child)
        sz = cl_widget_do_measure(w->first_child, c);
    sz.w += 2.0f * MSG_PAD;
    sz.h += 2.0f * MSG_PAD;
    return sz;
}

static void msgbox_arrange(cl_widget_t *w, cl_rect_t rect)
{
    cl_rect_t inner = { rect.x + MSG_PAD, rect.y + MSG_PAD,
                        rect.w - 2.0f * MSG_PAD, rect.h - 2.0f * MSG_PAD };

    if (w->first_child)
        cl_widget_do_arrange(w->first_child, inner);
}

static void msgbox_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    float radius = cl_theme_radius(cl_paint_theme(ctx));
    cl_rect_t r = w->rect;
    cl_rect_t shadow = { r.x + 1.0f, r.y + 3.0f, r.w, r.h };

    cl_paint_fill_round_rect(ctx, shadow, radius,
                             cl_paint_theme_color(ctx, CL_COLOR_SHADOW));
    cl_paint_fill_round_rect(
        ctx, r, radius, cl_paint_theme_color(ctx, CL_COLOR_SURFACE_RAISED));
    cl_paint_stroke_round_rect(ctx, r, radius, 1.0f,
                               cl_paint_theme_color(ctx, CL_COLOR_BORDER));
}

static bool msgbox_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    (void)w;
    (void)ev;
    return true; /* consume presses on the panel itself */
}

static bool msgbox_key_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_msgbox_t *mb = CL_WIDGET_CAST(cl_msgbox, w);

    switch (ev->data.key.key) {
        case CL_KEY_ENTER:
            msg_finish(mb, 0);
            return true;

        case CL_KEY_ESCAPE:
            /* Escape means "the dismissive choice": Cancel/No when present,
             * otherwise OK. */
            msg_finish(mb, mb->btn[1] ? 1 : 0);
            return true;

        default:
            return true; /* modal: swallow the keyboard */
    }
}

static void msg_button_clicked(cl_widget_t *btn, void *user)
{
    cl_msgbox_t *mb = user;

    msg_finish(mb, btn == mb->btn[1] ? 1 : 0);
}

static cl_widget_t *msg_button(cl_application_t *app, const char *text)
{
    return cl_button_create(
        app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS, .text = text });
}

cl_widget_t *cl_messagebox_show(cl_window_t *win, const char *title,
                                const char *text,
                                cl_msgbox_buttons_t buttons, cl_msgbox_fn fn,
                                void *user)
{
    static const char *const names[][2] = {
        { "OK", NULL },       /* CL_MSGBOX_OK */
        { "OK", "Cancel" },   /* CL_MSGBOX_OK_CANCEL */
        { "Yes", "No" },      /* CL_MSGBOX_YES_NO */
    };
    cl_application_t *app;
    cl_widget_t *w;
    cl_msgbox_t *mb;
    cl_widget_t *col;
    cl_widget_t *row;
    cl_widget_t *spacer;
    int b;

    if (!win || !text || (int)buttons < 0 || buttons > CL_MSGBOX_YES_NO) {
        cl_set_last_error(CL_ERROR_INVALID_ARGUMENT);
        return NULL;
    }
    app = cl_window_application(win);
    w = cl_widget_alloc(app, &cl_msgbox_class);
    if (!w)
        return NULL;
    mb = CL_WIDGET_CAST(cl_msgbox, w);
    mb->fn = fn;
    mb->user = user;

    col = cl_vbox_create(
        app, &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS,
                                .spacing = MSG_SPACING,
                                .align_cross = CL_ALIGN_STRETCH });
    if (title && title[0])
        cl_widget_add_child(
            col, cl_label_create(
                     app, &(cl_label_desc_t){ CL_LABEL_DESC_INIT_FIELDS,
                                              .text = title }));
    cl_widget_add_child(
        col, cl_label_create(app, &(cl_label_desc_t){
                                      CL_LABEL_DESC_INIT_FIELDS,
                                      .text = text }));

    row = cl_hbox_create(app, &(cl_hbox_desc_t){ CL_HBOX_DESC_INIT_FIELDS,
                                                 .spacing = 8.0f });
    spacer = cl_vbox_create(app,
                            &(cl_vbox_desc_t){ CL_VBOX_DESC_INIT_FIELDS });
    cl_widget_set_flex(spacer, 1.0f); /* right-align the buttons */
    cl_widget_add_child(row, spacer);
    for (b = 0; b < 2 && names[buttons][b]; b++) {
        mb->btn[b] = msg_button(app, names[buttons][b]);
        cl_button_set_on_click(mb->btn[b], msg_button_clicked, mb);
        cl_widget_add_child(row, mb->btn[b]);
    }
    cl_widget_add_child(col, row);
    cl_widget_add_child(w, col);

    cl_window_open_modal(win, w);
    return w;
}
