/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_EVENT_H
#define CL_EVENT_H

#include <stdbool.h>

#include <copal/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_widget cl_widget_t;

typedef enum cl_event_type {
    CL_EVENT_MOUSE_DOWN,
    CL_EVENT_MOUSE_UP,
    CL_EVENT_MOUSE_MOVE,
    CL_EVENT_MOUSE_WHEEL,
    CL_EVENT_MOUSE_ENTER,
    CL_EVENT_MOUSE_LEAVE,
    CL_EVENT_KEY_DOWN,
    CL_EVENT_KEY_UP,
    CL_EVENT_TEXT_INPUT,
    CL_EVENT_FOCUS_GAINED,
    CL_EVENT_FOCUS_LOST
} cl_event_type_t;

typedef enum cl_mouse_button {
    CL_MOUSE_LEFT,
    CL_MOUSE_MIDDLE,
    CL_MOUSE_RIGHT
} cl_mouse_button_t;

typedef enum cl_key_mods {
    CL_MOD_NONE = 0,
    CL_MOD_SHIFT = 1 << 0,
    CL_MOD_CTRL = 1 << 1,
    CL_MOD_ALT = 1 << 2,
    CL_MOD_SUPER = 1 << 3
} cl_key_mods_t;

/* Platform-neutral key codes (MVP subset; extended over time). */
typedef enum cl_key {
    CL_KEY_UNKNOWN = 0,
    CL_KEY_LEFT,
    CL_KEY_RIGHT,
    CL_KEY_UP,
    CL_KEY_DOWN,
    CL_KEY_HOME,
    CL_KEY_END,
    CL_KEY_BACKSPACE,
    CL_KEY_DELETE,
    CL_KEY_ENTER,
    CL_KEY_TAB,
    CL_KEY_ESCAPE
} cl_key_t;

typedef struct cl_event {
    cl_event_type_t type;
    cl_key_mods_t mods;
    union {
        struct {
            cl_point_t pos;
            cl_mouse_button_t button;
        } mouse;
        struct {
            cl_point_t pos;
            float dx, dy;
        } wheel;
        struct {
            cl_key_t key;
            bool repeat;
        } key;
        struct {
            const char *utf8; /* NUL-terminated */
        } text;
    } data;
} cl_event_t;

/* Widget action (button click, checkbox toggle, ...). */
typedef void (*cl_action_fn)(cl_widget_t *w, void *user);

/* Event listener; returns true if the event was handled (stops bubbling). */
typedef bool (*cl_event_fn)(cl_widget_t *w, const cl_event_t *ev, void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_EVENT_H */
