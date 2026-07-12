/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGETS_MESSAGEBOX_H
#define CL_WIDGETS_MESSAGEBOX_H

#include <copal/widget.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_window cl_window_t;

typedef enum cl_msgbox_buttons {
    CL_MSGBOX_OK = 0,   /* [OK] */
    CL_MSGBOX_OK_CANCEL, /* [OK] [Cancel] */
    CL_MSGBOX_YES_NO     /* [Yes] [No] */
} cl_msgbox_buttons_t;

/* index: 0 = OK/Yes, 1 = Cancel/No (also Escape; Enter picks 0). */
typedef void (*cl_msgbox_fn)(int index, void *user);

/**
 * cl_messagebox_show() - a modal message box over the window's content.
 *
 * Shows `title` (optional, may be NULL) and `text` centred in the window
 * with the requested buttons; outside clicks are swallowed (modal). The
 * callback fires once with the chosen button index and the dialog closes.
 * The window owns the dialog. Returns the dialog widget (rarely needed;
 * e.g. to restyle it or close it early via cl_window_close_popup).
 */
CL_API cl_widget_t *cl_messagebox_show(cl_window_t *win, const char *title,
                                       const char *text,
                                       cl_msgbox_buttons_t buttons,
                                       cl_msgbox_fn fn, void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGETS_MESSAGEBOX_H */
