/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_TEXTBOX_H
#define CL_WIDGETS_TEXTBOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <copal/export.h>
#include <copal/version.h>
#include <copal/widget.h>
#include <copal/event.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_application cl_application_t;

/*
 * Text box. UTF-8, codepoint-aware cursor and selection, insertion/deletion,
 * arrow/Home/End and Ctrl+arrow word navigation, mouse positioning and
 * selection (drag,
 * Shift+click extends, double click selects the word), password masking,
 * read-only mode, a codepoint length cap, clipboard cut/copy/paste
 * (Ctrl+X/C/V), and undo/redo (Ctrl+Z / Ctrl+Y or Ctrl+Shift+Z).
 *
 * With `multiline` set the box wraps text to its width and keeps explicit line
 * breaks: Enter inserts a newline (not submit), Up/Down move between lines,
 * Home/End act per visual line (Ctrl+Home/End jump to the document ends), the
 * content scrolls vertically (wheel or to follow the caret), and pasted line
 * breaks are kept. password + multiline is rejected by
 * cl_textbox_create (CL_ERROR_INVALID_ARGUMENT): the multiline paint has no
 * masking and would show the secret in plain text.
 *
 * IME composition is supported: a pre-edit (composition) string is shown
 * underlined at the caret without entering the buffer until the input method
 * commits it. cl_textbox_preedit() exposes the current composition.
 */
typedef struct cl_textbox_desc {
    uint32_t abi_version;
    size_t struct_size;
    const char *text;        /* initial text (UTF-8); may be NULL */
    const char *placeholder; /* shown when empty and unfocused; may be NULL */
    bool password;           /* mask characters (single-line only) */
    bool readonly;           /* navigation allowed, editing blocked */
    bool multiline;          /* wrap to width, keep newlines, scroll vertically */
    size_t max_length;       /* max codepoints; 0 = unlimited */
} cl_textbox_desc_t;

#define CL_TEXTBOX_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_textbox_desc_t)
#define CL_TEXTBOX_DESC_INIT { CL_TEXTBOX_DESC_INIT_FIELDS }

CL_API cl_widget_t *cl_textbox_create(cl_application_t *app,
                                      const cl_textbox_desc_t *desc);

/** cl_textbox_set_text() - replace the text. Does NOT fire on_changed. */
CL_API void cl_textbox_set_text(cl_widget_t *tb, const char *utf8);

/** cl_textbox_text() - current text (NUL-terminated UTF-8); valid until edited. */
CL_API const char *cl_textbox_text(cl_widget_t *tb);

/** cl_textbox_set_on_changed() - called only on user edits that change text. */
CL_API void cl_textbox_set_on_changed(cl_widget_t *tb, cl_text_changed_fn fn,
                                      void *user);

/** cl_textbox_set_on_submit() - called only when Enter is pressed (single-line;
 *  in multiline mode Enter inserts a newline and never submits). */
CL_API void cl_textbox_set_on_submit(cl_widget_t *tb, cl_text_changed_fn fn,
                                     void *user);

/** cl_textbox_line_count() - number of wrapped visual lines (multiline); the
 *  layout is recomputed against the current width. Returns 1 for a single-line
 *  box. */
CL_API size_t cl_textbox_line_count(cl_widget_t *tb);

/** cl_textbox_cursor_line() - visual line index the caret is on (multiline),
 *  or 0 for a single-line box. */
CL_API size_t cl_textbox_cursor_line(cl_widget_t *tb);

/** cl_textbox_preedit() - the current IME composition string shown at the caret
 *  (not yet in the text), or NULL when not composing. */
CL_API const char *cl_textbox_preedit(cl_widget_t *tb);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_TEXTBOX_H */
