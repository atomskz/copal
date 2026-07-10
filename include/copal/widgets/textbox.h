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
 * Single-line text box (MVP). UTF-8, codepoint-aware cursor and selection,
 * insertion/deletion, arrow/Home/End navigation, mouse positioning, password
 * masking, read-only mode, and a codepoint length cap.
 *
 * Not yet implemented (documented limitations): clipboard, undo/redo, IME
 * composition, multi-line, and clipping of overflowing text to the box.
 */
typedef struct cl_textbox_desc {
    uint32_t abi_version;
    size_t struct_size;
    const char *text;        /* initial text (UTF-8); may be NULL */
    const char *placeholder; /* shown when empty and unfocused; may be NULL */
    bool password;           /* mask characters */
    bool readonly;           /* navigation allowed, editing blocked */
    size_t max_length;       /* max codepoints; 0 = unlimited */
} cl_textbox_desc_t;

#define CL_TEXTBOX_DESC_INIT_FIELDS \
    .abi_version = CL_VERSION, .struct_size = sizeof(cl_textbox_desc_t)

CL_API cl_widget_t *cl_textbox_create(cl_application_t *app,
                                      const cl_textbox_desc_t *desc);

/** cl_textbox_set_text() - replace the text. Does NOT fire on_changed. */
CL_API void cl_textbox_set_text(cl_widget_t *tb, const char *utf8);

/** cl_textbox_text() - current text (NUL-terminated UTF-8); valid until edited. */
CL_API const char *cl_textbox_text(cl_widget_t *tb);

/** cl_textbox_set_on_changed() - called only on user edits that change text. */
CL_API void cl_textbox_set_on_changed(cl_widget_t *tb, cl_text_changed_fn fn,
                                      void *user);

/** cl_textbox_set_on_submit() - called only when Enter is pressed. */
CL_API void cl_textbox_set_on_submit(cl_widget_t *tb, cl_text_changed_fn fn,
                                     void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_TEXTBOX_H */
