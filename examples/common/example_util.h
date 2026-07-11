/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef COPAL_EXAMPLE_UTIL_H
#define COPAL_EXAMPLE_UTIL_H

/*
 * Shared helpers for the copal examples: font loading and a run helper that
 * supports headless verification. Kept out of each example's main() so the
 * examples themselves stay focused on the UI they demonstrate.
 */
#include <copal/copal.h>

/*
 * example_load_font() - load a UI font at size_px: tries $COPAL_FONT, then a
 * list of common system fonts. Returns NULL (after a stderr warning) if none
 * load; the caller may still run, just without text.
 */
cl_font_t *example_load_font(cl_application_t *app, float size_px);

/*
 * example_run() - run the application to completion and return its exit code.
 * If COPAL_MAX_FRAMES=N is set, renders N frames then returns 0 (used for
 * headless smoke tests); otherwise runs the normal blocking event loop.
 */
int example_run(cl_application_t *app);

#endif /* COPAL_EXAMPLE_UTIL_H */
