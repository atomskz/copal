/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_BACKEND_PLATFORM_H
#define CL_BACKEND_PLATFORM_H

/*
 * Platform backend SPI - for AUTHORS of platform backends. Application code
 * never needs this header (it is not part of <copal/copal.h>).
 *
 * A backend allocates its own struct with cl_platform_t as the FIRST member,
 * points `ops` at a static cl_platform_ops_t whose struct_size/abi_version
 * are filled in, and hands the cl_platform_t* to the application through
 * cl_application_desc_t.platform. cl_application_create() rejects an ops
 * table built against different headers with CL_ERROR_ABI_MISMATCH.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <copal/types.h>
#include <copal/error.h>
#include <copal/event.h>
#include <copal/allocator.h>
#include <copal/window.h>
#include <copal/version.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_platform cl_platform_t;

/*
 * Backend-defined native window handle, returned by create_window and passed
 * back to every window-scoped op. Single-window backends may ignore the
 * parameter; they must also accept NULL as "the (only) window" - the built-in
 * software renderer locks the framebuffer without knowing the handle.
 */
typedef struct cl_platform_window cl_platform_window_t;

typedef enum cl_platform_event_kind {
    CL_PEV_NONE,
    CL_PEV_QUIT,
    CL_PEV_RESIZE,
    CL_PEV_EXPOSE, /* surface contents lost/damaged: repaint */
    CL_PEV_MOUSE_DOWN,
    CL_PEV_MOUSE_UP,
    CL_PEV_MOUSE_MOVE,
    CL_PEV_MOUSE_WHEEL,
    CL_PEV_KEY_DOWN,
    CL_PEV_KEY_UP,
    CL_PEV_TEXT_INPUT,
    CL_PEV_TEXT_EDIT
} cl_platform_event_kind_t;

typedef struct cl_platform_event {
    cl_platform_event_kind_t kind;
    /*
     * Backend id of the source window (0 when unknown or for process-wide
     * events such as CL_PEV_QUIT). Single-window applications may ignore it;
     * it exists so a multi-window 0.3 will not have to reshape the SPI.
     */
    uint32_t window_id;
    cl_size_t size;           /* CL_PEV_RESIZE (logical px) */
    cl_point_t pos;           /* mouse events (logical px) */
    cl_mouse_button_t button; /* mouse button events */
    int clicks;               /* mouse button events: consecutive presses */
    float wheel_x, wheel_y;   /* CL_PEV_MOUSE_WHEEL (lines; +y = up/away) */
    cl_key_t key;             /* key events */
    cl_key_mods_t mods;       /* key and mouse events */
    char text[32];            /* CL_PEV_TEXT_INPUT / _EDIT (NUL-term UTF-8) */
    int edit_cursor;          /* CL_PEV_TEXT_EDIT: caret pos (codepoints) */
    bool repeat;              /* CL_PEV_KEY_DOWN/_UP: synthetic auto-repeat */
} cl_platform_event_t;

/*
 * A lockable CPU framebuffer exposed by software platform backends. Pixels are
 * 32-bit; `pitch` is bytes per row. The channel masks describe the byte layout
 * so a software renderer can pack colours without knowing the native format.
 */
typedef struct cl_pixmap {
    void *pixels;
    int w, h;   /* framebuffer size in physical pixels */
    int pitch;  /* bytes per row */
    uint32_t r_mask, g_mask, b_mask, a_mask; /* a_mask == 0 -> opaque surface */
} cl_pixmap_t;

typedef struct cl_platform_ops {
    /*
     * ABI handshake: set to sizeof(cl_platform_ops_t) and COPAL_VERSION of
     * the headers the backend was compiled against. Checked when the backend
     * is injected; a mismatch fails cl_application_create with
     * CL_ERROR_ABI_MISMATCH instead of calling through a reshaped table.
     */
    size_t struct_size;
    uint32_t abi_version;

    /* Create the native window and store its handle in *out. */
    cl_result_t (*create_window)(cl_platform_t *p, const cl_window_desc_t *desc,
                                 cl_platform_window_t **out);
    /*
     * Tear down a native window created by create_window, so a failed
     * cl_window_create can be rolled back and the slot reused. Optional
     * (NULL when the backend keeps no per-window state); must tolerate
     * being called with no window.
     */
    void (*destroy_window)(cl_platform_t *p, cl_platform_window_t *win);
    void (*set_title)(cl_platform_t *p, cl_platform_window_t *win,
                      const char *utf8);
    cl_size_t (*drawable_size)(cl_platform_t *p, cl_platform_window_t *win);
    float (*scale)(cl_platform_t *p, cl_platform_window_t *win);
    bool (*poll)(cl_platform_t *p, cl_platform_event_t *out);
    void (*wait)(cl_platform_t *p, int timeout_ms);
    void (*present)(cl_platform_t *p, cl_platform_window_t *win);
    /*
     * Optional: like present, but only `rect` (logical px) is guaranteed to
     * reach the screen - the rest may stay as previously presented. Paired
     * with partial redraws (renderer set_damage); NULL when unsupported,
     * in which case the application falls back to present.
     */
    void (*present_region)(cl_platform_t *p, cl_platform_window_t *win,
                           cl_rect_t rect);
    void (*wakeup)(cl_platform_t *p);
    void (*start_text_input)(cl_platform_t *p, cl_platform_window_t *win,
                             bool enable);
    /* Show a system cursor shape (process-wide, follows the pointer).
     * Optional: NULL when the backend has no cursors. */
    void (*set_cursor)(cl_platform_t *p, cl_cursor_t cursor);
    /* Position the IME candidate window near the caret (logical px). NULL if
     * the backend has no IME. */
    void (*set_ime_rect)(cl_platform_t *p, cl_platform_window_t *win,
                         cl_rect_t rect);
    /*
     * Clipboard. clipboard_get returns a NUL-terminated UTF-8 copy allocated
     * with a (caller frees with a), or NULL if empty/unavailable. clipboard_set
     * copies utf8 into the system clipboard.
     */
    char *(*clipboard_get)(cl_platform_t *p, const cl_allocator_t *a);
    void (*clipboard_set)(cl_platform_t *p, const char *utf8);
    void (*destroy)(cl_platform_t *p);
    /* GL proc loader for the OpenGL renderer; NULL for non-GL backends. */
    void *(*gl_get_proc)(cl_platform_t *p, const char *name);
    /*
     * Monotonic clock in milliseconds. Only the relative difference between
     * two readings is meaningful. NULL if the backend has no clock, in which
     * case timers are unavailable (cl_timer_create returns NULL).
     */
    uint64_t (*now_ms)(cl_platform_t *p);
    /*
     * Software backends only: lock the window's pixel buffer for CPU drawing
     * (fills *out; returns false if unavailable) and unlock it; present() then
     * blits it to the screen. NULL for GPU backends.
     */
    bool (*lock_framebuffer)(cl_platform_t *p, cl_platform_window_t *win,
                             cl_pixmap_t *out);
    void (*unlock_framebuffer)(cl_platform_t *p, cl_platform_window_t *win);
} cl_platform_ops_t;

/* Concrete backends embed this as their first member. */
struct cl_platform {
    const cl_platform_ops_t *ops;
};

#ifdef __cplusplus
}
#endif

#endif /* CL_BACKEND_PLATFORM_H */
