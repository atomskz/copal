/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CALC_WIDGETS_H
#define CALC_WIDGETS_H

/*
 * Two small custom widgets built entirely on copal's public widget-author API
 * (<copal/widget_impl.h>): a calculator key and the display. Factoring them out
 * keeps the calculator's assembly code (main.c) short and declarative.
 */
#include <copal/copal.h>

/* Fixed metrics so main() can size the window to match the grid. */
enum {
    CALC_KEY_W     = 64,
    CALC_KEY_H     = 52,
    CALC_DISPLAY_H = 64,
    CALC_GAP       = 8,
    CALC_PAD       = 16,
    CALC_ROW_GAP   = 12
};

/* Visual role of a key, which drives its colour. */
typedef enum calc_role {
    CALC_ROLE_DIGIT, /* 0-9 and '.'  */
    CALC_ROLE_OP,    /* + - * / =    */
    CALC_ROLE_FUNC   /* C, +/-, %, del */
} calc_role_t;

/* Invoked when a key is clicked; `code` is the key's calc_engine code. */
typedef void (*calc_key_fn)(cl_widget_t *key, int code, void *user);

/** calc_key_create() - a clickable calculator key with a label and a code. */
cl_widget_t *calc_key_create(cl_application_t *app, const char *label,
                             int code, calc_role_t role);

/** calc_key_set_handler() - set the click handler for a key. */
void calc_key_set_handler(cl_widget_t *key, calc_key_fn fn, void *user);

/** calc_display_create() - the calculator screen (right-aligned readout). */
cl_widget_t *calc_display_create(cl_application_t *app);

/** calc_display_set_text() - replace the shown text (copied). */
void calc_display_set_text(cl_widget_t *display, const char *utf8);

#endif /* CALC_WIDGETS_H */
