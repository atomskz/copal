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
    CL_EVENT_TEXT_EDIT, /* IME pre-edit (composition) update */
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
    CL_KEY_ESCAPE,
    CL_KEY_SPACE,
    /* Letters (used with modifiers for shortcuts). */
    CL_KEY_A, CL_KEY_B, CL_KEY_C, CL_KEY_D, CL_KEY_E, CL_KEY_F, CL_KEY_G,
    CL_KEY_H, CL_KEY_I, CL_KEY_J, CL_KEY_K, CL_KEY_L, CL_KEY_M, CL_KEY_N,
    CL_KEY_O, CL_KEY_P, CL_KEY_Q, CL_KEY_R, CL_KEY_S, CL_KEY_T, CL_KEY_U,
    CL_KEY_V, CL_KEY_W, CL_KEY_X, CL_KEY_Y, CL_KEY_Z
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
        struct {
            const char *utf8; /* NUL-terminated composition (may be empty) */
            int cursor;       /* caret position within it, in codepoints */
        } edit;
    } data;
} cl_event_t;

/* Widget action (button click, checkbox toggle, ...). */
typedef void (*cl_action_fn)(cl_widget_t *w, void *user);

/* Text changed / submitted (TextBox); utf8 is the current text. */
typedef void (*cl_text_changed_fn)(cl_widget_t *w, const char *utf8,
                                   void *user);

/* Toggle state changed (Checkbox / RadioButton); state is the new value. */
typedef void (*cl_toggled_fn)(cl_widget_t *w, bool checked, void *user);

/* Continuous value changed (Slider); value is the new value. */
typedef void (*cl_value_fn)(cl_widget_t *w, float value, void *user);

/* Discrete selection changed (ComboBox); index is the new selection. */
typedef void (*cl_selection_fn)(cl_widget_t *w, int index, void *user);

#ifdef __cplusplus
}
#endif

#endif /* CL_EVENT_H */
