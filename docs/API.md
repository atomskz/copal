<p align="right"><b>English</b> | <a href="./ru/API.md">Русский</a></p>

# copal — Public API

Status: **current** (matches the public headers `include/copal/*` as of Stage 7).
Version: 1.0. The current library version is **0.3.0**.
Notation is canonical (`cl_*` / `cl_*_t` / `CL_*`); see [CODESTYLE §2](CODESTYLE.md).
Builds on [ARCHITECTURE.md](ARCHITECTURE.md).

Every signature below is given **as it appears in the headers** and assumes
`CL_API` (the export macro is omitted for brevity). Units in the public API are
**logical pixels** (`float`); the conversion to physical pixels happens in the
renderer (ARCHITECTURE §8.3). All strings are **UTF-8**.

Part 1 (§1–§10): conventions, types, handles, enums, events, callbacks, ownership,
errors, versioning, minimal example.
Part 2 (§11–§22): signatures by module.

---

## 1. Naming conventions

| Category | Rule | Example |
|----------|------|---------|
| Type | `snake_case` + `_t` suffix, `cl_` prefix | `cl_widget_t`, `cl_window_desc_t` |
| Function | `cl_<module>_<action>()` | `cl_window_create()`, `cl_widget_add_child()` |
| Macro / enum constant | `CL_*` (UPPER_SNAKE) | `CL_OK`, `CL_ALIGN_CENTER` |
| Callback type | `cl_<event>_fn` | `cl_action_fn`, `cl_value_fn` |
| Desc struct | `cl_<object>_desc_t` | `cl_button_desc_t` |
| Getter (predicate) | `bool cl_<object>_is_<property>()` | `cl_widget_is_visible()` |
| Setter | `void cl_<object>_set_<property>()` | `cl_widget_set_visible()` |

---

## 2. Opaque handles

Declared as `typedef struct cl_x cl_x_t;`, with the body hidden; access is only
through functions.

| Handle | Created by | Destroyed by | Owner |
|--------|-----------|--------------|-------|
| `cl_application_t` | `cl_application_create()` | `cl_application_destroy()` | user |
| `cl_window_t` | `cl_window_create()` | `cl_window_destroy()` / destroying the app | application |
| `cl_timer_t` | `cl_timer_create()` | `cl_timer_cancel()` / destroying the app | application |
| `cl_theme_t` | with the application (built in) | with the application | application |
| `cl_font_t` | `cl_font_load_*()` | `cl_font_release()` | user |
| `cl_paint_context_t` | the library (in `paint`) | the library | the library (do not keep!) |
| `cl_platform_t` / `cl_renderer_t` | DI or the built-in bootstrap | with the application | application |

`cl_paint_context_t` is valid only inside a `paint` call; storing it or using it
outside paint is forbidden.

`cl_widget_t` is **semi-opaque**: to the application (`<copal/copal.h>`) it is
opaque and used through `cl_widget_*` and the concrete constructors
(`cl_button_create()`, etc.); for widget authors the base is exposed through
`<copal/widget_impl.h>` (§13).

## 3. Public value types (`types.h`)

```c
typedef struct cl_point  { float x, y; }                     cl_point_t;
typedef struct cl_size   { float w, h; }                     cl_size_t;
typedef struct cl_rect   { float x, y, w, h; }               cl_rect_t;
typedef struct cl_insets { float left, top, right, bottom; } cl_insets_t;
typedef struct cl_color  { uint8_t r, g, b, a; }             cl_color_t; /* NOT premultiplied */

typedef struct cl_constraints { cl_size_t min; cl_size_t max; } cl_constraints_t;

typedef enum cl_align { CL_ALIGN_START, CL_ALIGN_CENTER, CL_ALIGN_END,
                        CL_ALIGN_STRETCH,
                        CL_ALIGN_AUTO /* per-child: defer to the container */ } cl_align_t;
typedef enum cl_orientation { CL_ORIENT_HORIZONTAL, CL_ORIENT_VERTICAL }
                            cl_orientation_t;

#define CL_UNBOUNDED (3.4e38f)   /* marker for an unbounded constraint in measure */

static inline cl_color_t cl_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
```

### 3.1 Desc structs and the ABI handshake

Every `*_desc_t` begins with two service fields (`abi_version`, `struct_size`),
stamped by an initializer macro (ARCHITECTURE §9, ADR-005). The main idiom is
`CL_<OBJ>_DESC_INIT_FIELDS` (the service fields for a designated initializer /
compound literal), available on **all** descs:

```c
cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                        .title = "Example", .width = 800, .height = 600 };
cl_button_create(app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS,
                                           .text = "Close" });
```

Every desc has both initializers: `CL_<OBJ>_DESC_INIT_FIELDS` stamps only the
service fields (`abi_version`/`struct_size`) for use inside a compound literal
with designated initializers, while the full `CL_<OBJ>_DESC_INIT`
(= `{ CL_<OBJ>_DESC_INIT_FIELDS }`) yields a whole default desc — the remaining
fields zeroed.

`cl_*_create()` checks the desc's ABI header, and the handshake is
**evolvable**: the same major version is compatible as long as the size is at
least that of the service header (`abi_version`+`struct_size`). The library
normalizes the desc into its own full structure — an unfilled (shorter, from an
older header) tail is treated as default values, and any excess (from a newer
header) is ignored. So within a single major you can **append fields to the end**
of a desc without breaking already-compiled consumers. A different major, or a
size smaller than the header → `NULL` + `CL_ERROR_ABI_MISMATCH`. Backend ops
tables must carry the whole base set (a missing operation cannot be called), so a
shorter table is rejected while a longer one (a newer backend) is accepted.

## 4. Enums

```c
/* error.h */
typedef enum cl_result {
    CL_OK = 0,
    CL_ERROR_INVALID_ARGUMENT, CL_ERROR_OUT_OF_MEMORY, CL_ERROR_PLATFORM,
    CL_ERROR_RENDERER, CL_ERROR_FONT, CL_ERROR_UNSUPPORTED, CL_ERROR_ABI_MISMATCH
} cl_result_t;

typedef enum cl_log_level { CL_LOG_DEBUG, CL_LOG_INFO, CL_LOG_WARN, CL_LOG_ERROR }
                          cl_log_level_t;

/* event.h */
typedef enum cl_event_type {
    CL_EVENT_MOUSE_DOWN, CL_EVENT_MOUSE_UP, CL_EVENT_MOUSE_MOVE,
    CL_EVENT_MOUSE_WHEEL, CL_EVENT_MOUSE_ENTER, CL_EVENT_MOUSE_LEAVE,
    CL_EVENT_KEY_DOWN, CL_EVENT_KEY_UP, CL_EVENT_TEXT_INPUT,
    CL_EVENT_TEXT_EDIT,          /* IME pre-edit (composition) */
    CL_EVENT_FOCUS_GAINED, CL_EVENT_FOCUS_LOST
} cl_event_type_t;

typedef enum cl_mouse_button { CL_MOUSE_LEFT, CL_MOUSE_MIDDLE, CL_MOUSE_RIGHT }
                             cl_mouse_button_t;

typedef enum cl_key_mods {   /* bitmask */
    CL_MOD_NONE = 0, CL_MOD_SHIFT = 1<<0, CL_MOD_CTRL = 1<<1,
    CL_MOD_ALT = 1<<2, CL_MOD_SUPER = 1<<3
} cl_key_mods_t;

typedef enum cl_key {
    CL_KEY_UNKNOWN = 0,
    CL_KEY_LEFT, CL_KEY_RIGHT, CL_KEY_UP, CL_KEY_DOWN,
    CL_KEY_HOME, CL_KEY_END, CL_KEY_BACKSPACE, CL_KEY_DELETE,
    CL_KEY_ENTER, CL_KEY_TAB, CL_KEY_ESCAPE, CL_KEY_SPACE,
    CL_KEY_PAGE_UP, CL_KEY_PAGE_DOWN,
    CL_KEY_A, CL_KEY_B, /* ... */ CL_KEY_Z,  /* letters — for modifier combos */
    CL_KEY_0, /* ... */ CL_KEY_9,            /* number row */
    CL_KEY_F1, /* ... */ CL_KEY_F12
} cl_key_t;

/* theme.h */
typedef enum cl_color_role {
    CL_COLOR_BACKGROUND, CL_COLOR_SURFACE, CL_COLOR_SURFACE_HOVER,
    CL_COLOR_SURFACE_ACTIVE, CL_COLOR_SURFACE_RAISED, CL_COLOR_TEXT,
    CL_COLOR_TEXT_MUTED, CL_COLOR_ACCENT, CL_COLOR_BORDER, CL_COLOR_FOCUS_RING,
    CL_COLOR_SELECTION, CL_COLOR_SHADOW, CL_COLOR__COUNT
} cl_color_role_t;

typedef enum cl_theme_variant { CL_THEME_LIGHT, CL_THEME_DARK } cl_theme_variant_t;
```

## 5. Event (`cl_event_t`)

```c
typedef struct cl_event {
    cl_event_type_t type;
    cl_key_mods_t   mods;
    union {
        struct { cl_point_t pos; cl_mouse_button_t button;
                 int clicks; /* 1 = single, 2 = double, ... */ } mouse;
        struct { cl_point_t pos; float dx, dy; }             wheel;
        struct { cl_key_t key; bool repeat; }                key;
        struct { const char *utf8; }                         text;  /* NUL-terminated */
        struct { const char *utf8; int cursor; }             edit;  /* IME: string + caret (in code points) */
    } data;
} cl_event_t;
```

## 6. Callbacks (function pointer + `void *user`)

```c
/* allocator.h */
typedef struct cl_allocator {
    void *userdata;
    void *(*alloc)(void *userdata, size_t size);
    void *(*realloc)(void *userdata, void *ptr, size_t size);
    void  (*free)(void *userdata, void *ptr);
} cl_allocator_t;

/* allocator.h — allocator functions. NULL anywhere selects the built-in malloc
 * default; on allocation failure the wrappers record CL_ERROR_OUT_OF_MEMORY (cl_last_error). */
const cl_allocator_t *cl_allocator_default(void);
void *cl_alloc(const cl_allocator_t *a, size_t size);
void *cl_realloc(const cl_allocator_t *a, void *ptr, size_t size);
void  cl_free(const cl_allocator_t *a, void *ptr);

/* event.h — widget events */
typedef void (*cl_action_fn)(cl_widget_t *w, void *user);                 /* click/action */
typedef void (*cl_text_changed_fn)(cl_widget_t *w, const char *utf8, void *user);
typedef void (*cl_toggled_fn)(cl_widget_t *w, bool checked, void *user);  /* Checkbox/Radio */
typedef void (*cl_value_fn)(cl_widget_t *w, float value, void *user);     /* Slider */
typedef void (*cl_selection_fn)(cl_widget_t *w, int index, void *user);   /* ComboBox */

/* application.h / window.h / timer.h / error.h */
typedef void (*cl_task_fn)(void *user);                                   /* posted from another thread */
typedef bool (*cl_window_close_fn)(cl_window_t *win, void *user);         /* false = veto */
typedef void (*cl_timer_fn)(cl_timer_t *timer, void *user);
typedef void (*cl_log_fn)(cl_log_level_t level, const char *msg, void *user);
```

Rules: `cl_widget_destroy()` **detaches at once, frees deferred** (the DEAD
queue, ARCHITECTURE §5): the subtree disappears immediately from the tree, from
hit-testing and from events, while the memory of widgets attached to a window is
freed at the end of the current loop iteration. Destroying any widget from any
callback is therefore safe; a repeat destroy is a no-op. A tree that was never
attached to a window is freed immediately. `void *user` is "weak" — the library
does not own it.

## 7. Ownership rules

- Hierarchy: application → window → root widget → children (recursively).
- `cl_widget_add_child(parent, child)` — ownership **transfers** to `parent`.
- `cl_widget_remove_child(parent, child)` — ownership **returns** to the caller
  (who must later `cl_widget_destroy()` or re-attach it).
- `cl_widget_destroy(w)` — detaches the subtree immediately, frees it at the end
  of the loop iteration (safe from any callback — see §6).
- `cl_window_set_content(win, root)`, `cl_scrollview_set_content(sv, content)`,
  `cl_window_open_popup(win, popup, at)` — the receiving side **owns** the subtree.
- Strings returned by the library (`cl_textbox_text`, `cl_combobox_selected_text`,
  `cl_widget_tooltip`, `cl_textbox_preedit`) are **non-owning**, valid until the
  next mutation of the object.
- Live widgets exist only on the heap (the `cl_*_create` constructors).

## 8. Error model

- Fallible operations (`create`, `load`, `add_*`) → a pointer (`NULL` on failure)
  **or** a `cl_result_t`.
- Last-error diagnostics are thread-local:
  ```c
  cl_result_t cl_last_error(void);
  const char *cl_result_string(cl_result_t result);
  void        cl_set_log_callback(cl_log_fn fn, void *user);
  void        cl_set_assert_handler(cl_assert_fn fn); /* (expr, file, line) */
  ```
- The log callback is the **only** logging mechanism (process-wide); it receives
  library diagnostics (backend failures, GL shader errors, rejected fonts).
  Without a callback, WARN/ERROR go to stderr (hosted only).
- `cl_set_assert_handler` installs the handler for a failed `CL_ASSERT` (debug
  builds). The hosted default logs and aborts; a freestanding embedder installs
  one to report and halt (there is no libc assert). Assertions are compiled out
  in release (NDEBUG).
- Simple setters are `void` and validate/clamp their input.
- Codes: `CL_ERROR_INVALID_ARGUMENT`, `CL_ERROR_OUT_OF_MEMORY`, `CL_ERROR_PLATFORM`,
  `CL_ERROR_RENDERER`, `CL_ERROR_FONT`, `CL_ERROR_UNSUPPORTED`, `CL_ERROR_ABI_MISMATCH`.

## 9. Versioning (`version.h`)

```c
#define COPAL_VERSION_MAJOR 0
#define COPAL_VERSION_MINOR 3
#define COPAL_VERSION_PATCH 0
#define COPAL_VERSION_ENCODE(ma, mi, pa) (((ma) << 16) | ((mi) << 8) | (pa))
#define COPAL_VERSION COPAL_VERSION_ENCODE(COPAL_VERSION_MAJOR, COPAL_VERSION_MINOR, COPAL_VERSION_PATCH)

uint32_t    cl_version_runtime(void);
const char *cl_version_string(void);   /* "0.3.0" */
```

A mismatch between the headers and the `.so`/`.dll` is caught by the ABI
handshake in `*_desc_t` (§3.1) and by `cl_version_runtime()`.

**ABI policy:** within a single major, the public desc/ops structures only gain
fields at the end — this is binary-compatible thanks to the tail-tolerant
handshake (§3.1); breaking changes (reordering/removing/repurposing fields,
growing the widget base or the vtable) bump the major and `SOVERSION`. Before 1.0
the ABI is not frozen yet: the major stays 0, so the handshake will accept any
0.x — rebuild consumers for each 0.x version.

## 10. Minimal example

```c
#include <copal/copal.h>

static void on_close(cl_widget_t *w, void *user) { (void)w;
    cl_application_quit((cl_application_t *)user, 0);
}

int main(void)
{
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_application_t *app = cl_application_create(&ad);
    if (!app) /* e.g. the default headless build has no backend */
        return 1;

    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
    wd.title = "Example"; wd.width = 800; wd.height = 600;
    cl_window_t *win = cl_window_create(app, &wd);

    cl_widget_t *root = cl_vbox_create(app, &(cl_vbox_desc_t){
        CL_VBOX_DESC_INIT_FIELDS, .spacing = 8, .padding = { 12, 12, 12, 12 } });
    cl_widget_t *label = cl_label_create(app, &(cl_label_desc_t){
        CL_LABEL_DESC_INIT_FIELDS, .text = "Hello" });
    cl_widget_t *button = cl_button_create(app, &(cl_button_desc_t){
        CL_BUTTON_DESC_INIT_FIELDS, .text = "Close" });

    cl_button_set_on_click(button, on_close, app);
    cl_widget_add_child(root, label);
    cl_widget_add_child(root, button);
    cl_window_set_content(win, root);
    cl_window_show(win);

    int rc = cl_application_run(app);
    cl_application_destroy(app);   /* destroys the window and the widget tree */
    return rc;
}
```

Every widget constructor takes `cl_application_t *app` as its first argument: the
app provides the allocator, access to the theme/fonts, and predictable memory
ownership with no global state. A widget is bound to a window later — through
`add_child`/`set_content`.

---

# Part 2 — signatures by module

## 11. Application (`application.h`)

```c
/* Built-in render backend when platform/renderer are not injected. */
typedef enum cl_render_backend {
    CL_RENDER_AUTO = 0,  /* OpenGL if compiled in, otherwise software */
    CL_RENDER_GL,        /* OpenGL renderer */
    CL_RENDER_SOFTWARE   /* CPU rasterizer, no GPU context */
} cl_render_backend_t;

/* Injectable mutex for the cross-thread task queue (cl_application_post).
 * NULL uses the hosted default (pthread / critical section); a freestanding
 * build has no default and must inject one (on UEFI: RaiseTPL/RestoreTPL). */
typedef struct cl_mutex_iface {
    void *(*create)(void *user);
    void  (*destroy)(void *user, void *handle);
    void  (*lock)(void *user, void *handle);
    void  (*unlock)(void *user, void *handle);
    void  *user;
} cl_mutex_iface_t;

typedef struct cl_application_desc {
    uint32_t abi_version; size_t struct_size;
    const cl_allocator_t *allocator;      /* NULL -> built-in malloc (none freestanding) */
    cl_platform_t        *platform;       /* backend DI; ownership → app */
    cl_renderer_t        *renderer;       /* backend DI; ownership → app */
    cl_render_backend_t   render_backend; /* built-in backend choice (0 = AUTO) */
    const cl_mutex_iface_t *mutex;        /* NULL -> hosted default (see above) */
} cl_application_desc_t;
#define CL_APPLICATION_DESC_INIT \
    { .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_application_desc_t) }

cl_application_t *cl_application_create(const cl_application_desc_t *desc);
void              cl_application_destroy(cl_application_t *app);   /* destroys the window and trees */

int  cl_application_run(cl_application_t *app);            /* blocking loop; exit code */
bool cl_application_step(cl_application_t *app, bool wait); /* one step; false = time to quit */
void cl_application_quit(cl_application_t *app, int exit_code);

/* Queue fn(user) on the GUI thread. Thread-safe (from any thread); FIFO; runs
 * inside run()/step(); a task may post more tasks; tasks not drained by destroy
 * are dropped. Requires a thread-safe allocator (the default one is). Return:
 * CL_OK / INVALID_ARGUMENT / OUT_OF_MEMORY. */
cl_result_t cl_application_post(cl_application_t *app, cl_task_fn fn, void *user);

cl_theme_t           *cl_application_theme(cl_application_t *app);
const cl_allocator_t *cl_application_allocator(cl_application_t *app);
```

If `platform`/`renderer` are not set (NULL), the built-in SDL2 backend is used
(when built with `COPAL_ENABLE_SDL`); otherwise `create` returns `NULL` +
`CL_ERROR_UNSUPPORTED`. `render_backend` selects the renderer: **GL** (the
default under AUTO when OpenGL is compiled in) or **software** (a CPU rasterizer
with no GL context — less memory and a fast start, works over RDP / in CI; see
ADR-015). Under AUTO, software can be turned on with the runtime variable
**`COPAL_RENDER=software`**; if the GL window fails to come up (headless/RDP/old
driver), AUTO **falls back** to software on its own (WARN to the log); an explicit
`CL_RENDER_GL` does not fall back — it fails loudly. Building `COPAL_ENABLE_SDL`
**without** `OPENGL` yields a software backend without linking libGL. `run` is
"own the loop" (returns the exit_code from `quit`); `step(wait=true)` is one step:
the wait is bounded by the nearest timer (or a ~100 ms slice with no timers) and
always returns to the caller, so an idle `while (step(app, true))` loop does not
burn a core.

## 12. Window + overlay/popup + tooltip (`window.h`)

```c
typedef bool (*cl_window_close_fn)(cl_window_t *win, void *user);  /* false = veto */

typedef struct cl_window_desc {
    uint32_t abi_version; size_t struct_size;
    const char *title;                 /* UTF-8; NULL allowed */
    int32_t     width, height;
    int32_t     min_width, min_height; /* 0 -> no constraint */
    bool        resizable;
} cl_window_desc_t;
#define CL_WINDOW_DESC_INIT \
    { .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_window_desc_t) }

/* MVP allows a single window; a second one → NULL + CL_ERROR_UNSUPPORTED (ADR-011). */
cl_window_t *cl_window_create(cl_application_t *app, const cl_window_desc_t *desc);
void         cl_window_destroy(cl_window_t *win);

void         cl_window_show(cl_window_t *win);
void         cl_window_set_content(cl_window_t *win, cl_widget_t *root); /* the window owns root;
                                       replacing destroys the previous subtree (NULL = clear) */
cl_widget_t *cl_window_content(cl_window_t *win);
cl_application_t *cl_window_application(cl_window_t *win);
void         cl_window_set_title(cl_window_t *win, const char *utf8);
cl_size_t    cl_window_size(cl_window_t *win);
void         cl_window_set_on_close(cl_window_t *win, cl_window_close_fn fn, void *user);

/* Overlay stack on top of the content (popups, submenus, dialogs). open_popup
 * shows `popup` at window position `at` (clamped on-screen) and takes ownership
 * of it; popups stack (a menu pushes its submenu on top), and a click into a
 * lower entry collapses the ones above it. open_modal centres the dialog and
 * makes it a barrier: clicks outside the modal are swallowed, and light-dismiss
 * and replacement only work ABOVE it. A click outside the chain, or close_popup(),
 * closes it; closing is deferred (a popup's own handler may request the close).
 * The stack is bounded (8 levels); a request past the cap is ignored (WARN,
 * CL_ERROR_UNSUPPORTED), and the popup is destroyed rather than leaked. */
void         cl_window_open_popup(cl_window_t *win, cl_widget_t *popup, cl_point_t at);
void         cl_window_open_modal(cl_window_t *win, cl_widget_t *dialog);
void         cl_window_close_popup(cl_window_t *win);
cl_widget_t *cl_window_popup(cl_window_t *win);

/* The tooltip bubble currently shown by the hover layer, or NULL (introspection/tests; owned by the window). */
cl_widget_t *cl_window_tooltip(cl_window_t *win);
```

ComboBox and Menu (§18) use the overlay mechanism. A popup is clipped to the
window's bounds (ADR-011).

Input while overlays are open: menus get the keyboard at their root (navigation,
Escape) and the mouse without a capture (drag-to-select works across the whole
chain). Modal dialogs behave like content: a click inside moves focus to the
nearest focusable widget and captures the pointer for the drag; when focus is
inside the topmost overlay, keys and text input go to it (bubbling up to the
dialog root — Enter/Escape), an unhandled Tab cycles the dialog's focusable
widgets; mouse motion over overlays updates hover and the cursor.

## 13. Timer (`timer.h`)

```c
typedef void (*cl_timer_fn)(cl_timer_t *timer, void *user);

/* Timers fire from the application loop (run/step), on the same thread, between
 * dispatch and rendering. Timing is best-effort: a firing may be late (never
 * early), and a repeating timer that falls behind coalesces missed ticks into
 * one; the firing order of timers due at the same time is unspecified. A timer
 * is owned by the application. interval_ms == 0 for a one-shot fires on the next
 * poll; a repeating one is floored to 1 ms. NULL on OOM / no clock. */
cl_timer_t *cl_timer_create(cl_application_t *app, uint32_t interval_ms,
                            bool repeat, cl_timer_fn fn, void *user);
void        cl_timer_cancel(cl_timer_t *timer);   /* stop + free; safe from its own callback */
void        cl_timer_restart(cl_timer_t *timer);  /* re-arm from "now" */
```

The handle stays valid after a one-shot fires (it can be `restart`ed); it becomes
invalid only after `cl_timer_cancel`.

## 13a. Animation (`animation.h`)

```c
typedef enum cl_easing { CL_EASE_LINEAR, CL_EASE_IN, CL_EASE_OUT, CL_EASE_IN_OUT } cl_easing_t;

typedef void (*cl_animation_fn)(cl_animation_t *anim, float t, void *user);      /* t ∈ [0,1], eased */
typedef void (*cl_animation_done_fn)(cl_animation_t *anim, bool finished, void *user);

typedef struct cl_animation_desc {
    uint32_t abi_version;  size_t struct_size;   /* CL_ANIMATION_DESC_INIT_FIELDS */
    uint32_t duration_ms;                        /* 0 — completes on the first tick */
    cl_easing_t easing;
    cl_animation_fn on_progress;                 /* required */
    cl_animation_done_fn on_done;                /* finished=false on cancel */
    void *user;
} cl_animation_desc_t;

cl_animation_t *cl_animation_start(cl_application_t *app, const cl_animation_desc_t *desc);
void            cl_animation_cancel(cl_animation_t *anim);  /* on_done(false) + free */

/* Interpolation of "animatable values". */
float      cl_ease(cl_easing_t easing, float t);
float      cl_lerp(float from, float to, float t);
cl_color_t cl_color_lerp(cl_color_t from, cl_color_t to, float t);
cl_rect_t  cl_rect_lerp(cl_rect_t from, cl_rect_t to, float t);
```

All animations share a single ~60 Hz ticker on top of timers; progress is
computed from elapsed time (`now_ms() - start`), not from the tick count — a loop
that falls behind "skips ahead" rather than slowing the animation down. The final
`on_progress` call always receives exactly `1.0`. Animations compose: any number
may run at once, and a chain is built by starting the next one from `on_done`.

Ownership differs from a timer: an animation frees itself on completion or
cancellation — after `on_done` the handle is invalid (NULL it in `on_done` if you
keep it for `cancel`). Animations still alive when the application is destroyed
are freed without callbacks. `NULL` on OOM, when the platform has no clock, on a
missing `on_progress`, or on an ABI-mismatched desc.

## 14. Widget — base (`widget.h`)

```c
/* Tree / ownership. */
cl_result_t  cl_widget_add_child(cl_widget_t *parent, cl_widget_t *child);    /* ownership → parent */
cl_result_t  cl_widget_remove_child(cl_widget_t *parent, cl_widget_t *child); /* ownership → caller */
void         cl_widget_destroy(cl_widget_t *w);            /* destroys the subtree */
cl_widget_t *cl_widget_parent(cl_widget_t *w);
cl_window_t *cl_widget_window(cl_widget_t *w);

/* Geometry / state. */
cl_rect_t cl_widget_rect(cl_widget_t *w);                  /* assigned rect (absolute) */
void      cl_widget_set_visible(cl_widget_t *w, bool v);
bool      cl_widget_is_visible(cl_widget_t *w);
void      cl_widget_set_enabled(cl_widget_t *w, bool e);
void      cl_widget_set_cursor(cl_widget_t *w, cl_cursor_t cursor); /* shape while hovered */
cl_cursor_t cl_widget_cursor(cl_widget_t *w);
bool      cl_widget_is_enabled(cl_widget_t *w);

/* Child layout attributes (read by box containers):
 * preferred_size overrides the widget's own measure on axes where > 0;
 * flex > 0 gets that share of the main-axis remainder (growth only; 0 = fixed);
 * align overrides the container's align_cross on the cross axis
 * (CL_ALIGN_AUTO — the default — takes the container's alignment; the main-axis
 * component is ignored by boxes). */
void cl_widget_set_preferred_size(cl_widget_t *w, cl_size_t s);
void cl_widget_set_margin(cl_widget_t *w, cl_insets_t m);
void cl_widget_set_align(cl_widget_t *w, cl_align_t h, cl_align_t v);
void cl_widget_set_flex(cl_widget_t *w, float weight);
cl_size_t   cl_widget_preferred_size(cl_widget_t *w);     /* getters for the attributes above */
cl_insets_t cl_widget_margin(cl_widget_t *w);
cl_align_t  cl_widget_align_h(cl_widget_t *w);
cl_align_t  cl_widget_align_v(cl_widget_t *w);
float       cl_widget_flex(cl_widget_t *w);

/* Focus. */
void cl_widget_set_focusable(cl_widget_t *w, bool focusable);
bool cl_widget_is_focusable(cl_widget_t *w);
bool cl_widget_focus(cl_widget_t *w);                      /* request focus; false if not allowed */
bool cl_widget_has_focus(cl_widget_t *w);

/* Invalidation. */
void cl_widget_invalidate(cl_widget_t *w);                 /* repaint */
void cl_widget_invalidate_layout(cl_widget_t *w);          /* recompute layout */

/* Userdata. */
void  cl_widget_set_userdata(cl_widget_t *w, void *user);
void *cl_widget_userdata(cl_widget_t *w);

/* Hover tooltip (text is copied; NULL or "" clears it). */
void        cl_widget_set_tooltip(cl_widget_t *w, const char *utf8);
const char *cl_widget_tooltip(cl_widget_t *w);
```

## 15. Widget — base for authors (`widget_impl.h`)

```c
#define CL_WIDGET_RESERVED 20

enum cl_widget_flags {
    CL_WF_VISIBLE = 1u<<0, CL_WF_ENABLED = 1u<<1, CL_WF_FOCUSABLE = 1u<<2,
    /* bit 3 reserved (was DIRTY; never implemented) */
    CL_WF_DEAD = 1u<<4,  /* internal: subtree awaiting the deferred free (do not touch) */
    CL_WF_CLIP = 1u<<5   /* clip children to this widget's rect while painting */
};

typedef struct cl_widget_vtable {
    void      (*destroy)(cl_widget_t *w);
    cl_size_t (*measure)(cl_widget_t *w, cl_constraints_t c);
    void      (*arrange)(cl_widget_t *w, cl_rect_t rect);
    void      (*paint)(cl_widget_t *w, cl_paint_context_t *ctx);
    cl_rect_t (*clip_rect)(cl_widget_t *w);                 /* NULL -> whole rect (when CL_WF_CLIP) */
    void      (*reveal)(cl_widget_t *w, cl_rect_t target);  /* scroll a descendant's target into view */
    bool      (*hit_test)(cl_widget_t *w, cl_point_t p);    /* NULL -> rect */
    bool      (*on_event)(cl_widget_t *w, const cl_event_t *ev); /* NULL -> fan out to the convenience methods */
    bool (*mouse_down)(cl_widget_t *w, const cl_event_t *ev);
    bool (*mouse_up)(cl_widget_t *w, const cl_event_t *ev);
    bool (*mouse_move)(cl_widget_t *w, const cl_event_t *ev);
    bool (*mouse_wheel)(cl_widget_t *w, const cl_event_t *ev);
    void (*mouse_enter)(cl_widget_t *w);  /* hover: no bubbling */
    void (*mouse_leave)(cl_widget_t *w);
    bool (*key_down)(cl_widget_t *w, const cl_event_t *ev);
    bool (*key_up)(cl_widget_t *w, const cl_event_t *ev);
    bool (*text_input)(cl_widget_t *w, const cl_event_t *ev);
    bool (*text_edit)(cl_widget_t *w, const cl_event_t *ev); /* IME pre-edit */
    void (*focus_gained)(cl_widget_t *w);
    void (*focus_lost)(cl_widget_t *w);
} cl_widget_vtable_t;

struct cl_widget_class {
    const char               *name;
    const cl_widget_class_t   *base;          /* ancestor chain for RTTI */
    uint32_t                  type_id;        /* informational/debug */
    size_t                    instance_size;
    const cl_widget_vtable_t *vtable;
    size_t                    vtable_size;    /* = sizeof(cl_widget_vtable_t);
                                     cl_widget_alloc checks it (vtable ABI) */
};

/* The public base is the FIRST field of a derived struct (inheritance by embedding). */
struct cl_widget {
    const cl_widget_class_t *cls;
    cl_application_t *app;      /* weak */
    cl_window_t      *window;   /* weak back-ref (O(1) clearing of focus/hover) */
    cl_widget_t      *parent;   /* weak */
    cl_widget_t      *first_child, *last_child, *next_sibling;
    cl_rect_t         rect;     /* absolute, assigned by arrange */
    cl_size_t         measured;
    cl_size_t         pref_size;
    cl_insets_t       margin;
    cl_align_t        align_h, align_v;
    float             flex;
    uint32_t          flags;
    uint32_t          cursor;   /* cl_cursor_t shown while hovered (widget.h) */
    void             *userdata;
    char             *tooltip;  /* owned UTF-8, or NULL */
    unsigned char     reserved[CL_WIDGET_RESERVED];
};

cl_widget_t *cl_widget_alloc(cl_application_t *app, const cl_widget_class_t *cls); /* zero-alloc + init_base */
void         cl_widget_init_base(cl_widget_t *w, cl_application_t *app,
                                 const cl_widget_class_t *cls);
void        *cl_widget_check_cast(cl_widget_t *w, const cl_widget_class_t *cls);   /* NULL on mismatch */
bool         cl_widget_is_a(cl_widget_t *w, const cl_widget_class_t *cls);

#define CL_WIDGET_CAST(name, w) \
    ((name##_t *)cl_widget_check_cast((w), &name##_class))   /* checked; NULL on mismatch */
#define CL_WIDGET_CAST_UNCHECKED(name, w) ((name##_t *)(w))  /* fast path; UB on the wrong type */

/* Plumbing for CONTAINERS: measure/arrange a child from your own
 * measure/arrange (applies the NULL defaults, honours pref_size, writes
 * child->measured and child->rect — do not call the child's vtable directly). */
cl_size_t cl_widget_do_measure(cl_widget_t *child, cl_constraints_t c);
void      cl_widget_do_arrange(cl_widget_t *child, cl_rect_t rect);
void      cl_widget_reveal(cl_widget_t *w); /* scroll ancestors until w is visible */
```

A NULL vtable slot means the default behaviour (`hit_test` = rect test,
`on_event` = fan out to `mouse_*`/`key_*`/`text_*`/`focus_*`). The `clip_rect`,
`reveal`, `mouse_wheel`, and `text_edit` slots are the hooks for the ScrollView
clip, scroll-to-view, the wheel, and the IME respectively; other widgets leave
them unset.

## 16. Containers / Layout (`layout.h`)

```c
typedef struct cl_vbox_desc {
    uint32_t abi_version; size_t struct_size;
    float spacing; cl_insets_t padding;
    cl_align_t align_cross;   /* alignment of children on the cross (horizontal) axis */
} cl_vbox_desc_t;
#define CL_VBOX_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_vbox_desc_t)
cl_widget_t *cl_vbox_create(cl_application_t *app, const cl_vbox_desc_t *desc);

/* cl_hbox_desc_t — the same (align_cross is vertical); CL_HBOX_DESC_INIT_FIELDS. */
cl_widget_t *cl_hbox_create(cl_application_t *app, const cl_hbox_desc_t *desc);
```

Per-child attributes (`flex`, `margin`, `align`, `preferred_size`) are set on the
child itself through `cl_widget_set_*` (§14). The main-axis remainder (a vbox's
height / an hbox's width beyond the sum of the measured sizes) is distributed
among children with `flex > 0` in proportion to their weights; when space is
short, children are not shrunk below their measured size.

## 17. Text and control widgets (`widgets/*.h`)

```c
/* Label */
typedef struct cl_label_desc { uint32_t abi_version; size_t struct_size;
    const char *text; const cl_text_style_t *style; } cl_label_desc_t;  /* style NULL -> theme */
#define CL_LABEL_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_label_desc_t)
cl_widget_t *cl_label_create(cl_application_t *app, const cl_label_desc_t *desc);
void         cl_label_set_text(cl_widget_t *label, const char *utf8);

/* Button */
typedef struct cl_button_desc { uint32_t abi_version; size_t struct_size;
    const char *text; } cl_button_desc_t;
#define CL_BUTTON_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_button_desc_t)
cl_widget_t *cl_button_create(cl_application_t *app, const cl_button_desc_t *desc);
void         cl_button_set_text(cl_widget_t *button, const char *utf8);
void         cl_button_set_on_click(cl_widget_t *button, cl_action_fn fn, void *user);

/* CheckBox */
typedef struct cl_checkbox_desc { uint32_t abi_version; size_t struct_size;
    const char *text; bool checked; } cl_checkbox_desc_t;
#define CL_CHECKBOX_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_checkbox_desc_t)
cl_widget_t *cl_checkbox_create(cl_application_t *app, const cl_checkbox_desc_t *desc);
void  cl_checkbox_set_checked(cl_widget_t *cb, bool checked);   /* does NOT fire on_toggle */
bool  cl_checkbox_is_checked(cl_widget_t *cb);
void  cl_checkbox_set_text(cl_widget_t *cb, const char *utf8);
void  cl_checkbox_set_on_toggle(cl_widget_t *cb, cl_toggled_fn fn, void *user); /* user changes only */

/* RadioButton (mutual exclusion by numeric group id, not via a separate container) */
typedef struct cl_radiobutton_desc { uint32_t abi_version; size_t struct_size;
    const char *text; int group; bool selected; } cl_radiobutton_desc_t; /* group <= 0 = ungrouped */
#define CL_RADIOBUTTON_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_radiobutton_desc_t)
cl_widget_t *cl_radiobutton_create(cl_application_t *app, const cl_radiobutton_desc_t *desc);
void  cl_radiobutton_set_selected(cl_widget_t *rb, bool selected); /* selecting deselects the group */
bool  cl_radiobutton_is_selected(cl_widget_t *rb);
void  cl_radiobutton_set_on_select(cl_widget_t *rb, cl_toggled_fn fn, void *user);

/* Slider */
typedef struct cl_slider_desc { uint32_t abi_version; size_t struct_size;
    float min, max, value, step; } cl_slider_desc_t;   /* step 0 -> (max-min)/20 */
#define CL_SLIDER_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_slider_desc_t)
cl_widget_t *cl_slider_create(cl_application_t *app, const cl_slider_desc_t *desc);
void  cl_slider_set_value(cl_widget_t *slider, float value);       /* clamped; does NOT fire on_change */
float cl_slider_value(cl_widget_t *slider);
void  cl_slider_set_range(cl_widget_t *slider, float min, float max);
void  cl_slider_set_on_change(cl_widget_t *slider, cl_value_fn fn, void *user);

/* TextBox (single-/multi-line; password/readonly/multiline/max_length;
 * IME composition; mouse selection: drag, Shift+click, double click = word) */
typedef struct cl_textbox_desc { uint32_t abi_version; size_t struct_size;
    const char *text; const char *placeholder;
    bool password;   /* mask; password+multiline is rejected (INVALID_ARGUMENT) */
    bool readonly;   /* navigation allowed, input not */
    bool multiline;  /* width wrapping + \n + vertical scroll */
    size_t max_length; /* max code points; 0 = no limit */ } cl_textbox_desc_t;
#define CL_TEXTBOX_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_textbox_desc_t)
cl_widget_t *cl_textbox_create(cl_application_t *app, const cl_textbox_desc_t *desc);
void         cl_textbox_set_text(cl_widget_t *tb, const char *utf8);   /* does NOT fire on_changed */
const char  *cl_textbox_text(cl_widget_t *tb);          /* non-owning; valid until edited */
void         cl_textbox_set_on_changed(cl_widget_t *tb, cl_text_changed_fn fn, void *user);
void         cl_textbox_set_on_submit(cl_widget_t *tb, cl_text_changed_fn fn, void *user); /* Enter, single-line */
size_t       cl_textbox_line_count(cl_widget_t *tb);    /* visual lines (multiline) */
size_t       cl_textbox_cursor_line(cl_widget_t *tb);   /* caret line (multiline) */
const char  *cl_textbox_preedit(cl_widget_t *tb);       /* IME composition string at the caret, or NULL */
```

## 17a. 0.2 widgets: list, progressbar, imageview, panel, spacer, radiogroup, menubar, messagebox

```c
/* widgets/list.h — row selection, full keyboard, activation */
cl_widget_t *cl_list_create(cl_application_t *app, const cl_list_desc_t *desc);
cl_result_t  cl_list_add_item(cl_widget_t *l, const char *text);
cl_result_t  cl_list_remove(cl_widget_t *l, size_t index);
void         cl_list_clear(cl_widget_t *l);
size_t       cl_list_count(cl_widget_t *l);
const char  *cl_list_item_text(cl_widget_t *l, size_t index);
int          cl_list_selected(cl_widget_t *l);            /* -1 = none */
void         cl_list_set_selected(cl_widget_t *l, int index);
void         cl_list_set_on_select(cl_widget_t *l, cl_list_fn fn, void *user);
void         cl_list_set_on_activate(cl_widget_t *l, cl_list_fn fn, void *user);

/* widgets/progressbar.h */
cl_widget_t *cl_progressbar_create(cl_application_t *app, const cl_progressbar_desc_t *desc);
void         cl_progressbar_set_value(cl_widget_t *pb, float v); /* 0..1, clamped */
float        cl_progressbar_value(cl_widget_t *pb);

/* widgets/imageview.h + image.h (resource: raw RGBA8, files are not decoded) */
cl_image_t  *cl_image_create(cl_application_t *app, int w, int h, const void *rgba);
void         cl_image_release(cl_image_t *img);           /* invalidates the render caches */
cl_size_t    cl_image_size(const cl_image_t *img);
const void  *cl_image_pixels(const cl_image_t *img);      /* RGBA8, for your own backends */
cl_widget_t *cl_imageview_create(cl_application_t *app, const cl_imageview_desc_t *desc);
void         cl_imageview_set_image(cl_widget_t *iv, cl_image_t *img); /* borrowed */
cl_image_t  *cl_imageview_image(cl_widget_t *iv);

/* widgets/panel.h, widgets/spacer.h */
cl_widget_t *cl_panel_create(cl_application_t *app, const cl_panel_desc_t *desc);
cl_widget_t *cl_spacer_create(cl_application_t *app, const cl_spacer_desc_t *desc);

/* widgets/radiogroup.h — a column of mutually exclusive radios */
cl_widget_t *cl_radiogroup_create(cl_application_t *app, const cl_radiogroup_desc_t *desc);
cl_widget_t *cl_radiogroup_add(cl_widget_t *g, const char *text);
size_t       cl_radiogroup_count(cl_widget_t *g);
int          cl_radiogroup_selected(cl_widget_t *g);
void         cl_radiogroup_set_selected(cl_widget_t *g, int index); /* no callback */
void         cl_radiogroup_set_on_change(cl_widget_t *g, cl_radiogroup_fn fn, void *user);

/* widgets/menubar.h — a bar of menu titles (owns the menus, reuses them) */
cl_widget_t *cl_menubar_create(cl_application_t *app, const cl_menubar_desc_t *desc);
cl_result_t  cl_menubar_add_menu(cl_widget_t *bar, const char *title, cl_widget_t *menu);
size_t       cl_menubar_count(cl_widget_t *bar);

/* widgets/messagebox.h — a modal dialog over the content */
cl_widget_t *cl_messagebox_show(cl_window_t *win, const char *title, const char *text,
                                cl_msgbox_buttons_t buttons, cl_msgbox_fn fn, void *user);
```

## 18. Popup widgets: ComboBox and Menu (`widgets/combobox.h`, `widgets/menu.h`)

Both use the window's overlay layer (§12).

```c
/* ComboBox */
typedef struct cl_combobox_desc { uint32_t abi_version; size_t struct_size;
    const char *placeholder; } cl_combobox_desc_t;
#define CL_COMBOBOX_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_combobox_desc_t)
cl_widget_t *cl_combobox_create(cl_application_t *app, const cl_combobox_desc_t *desc);
cl_result_t  cl_combobox_add_item(cl_widget_t *combo, const char *text);
const char  *cl_combobox_item_text(cl_widget_t *combo, size_t index);
cl_result_t  cl_combobox_remove(cl_widget_t *combo, size_t index);
void         cl_combobox_clear(cl_widget_t *combo);
size_t       cl_combobox_count(cl_widget_t *combo);
void         cl_combobox_set_selected(cl_widget_t *combo, int index);  /* -1 = nothing; does NOT fire on_change */
int          cl_combobox_selected(cl_widget_t *combo);                 /* index or -1 */
const char  *cl_combobox_selected_text(cl_widget_t *combo);            /* text or NULL */
void         cl_combobox_set_on_change(cl_widget_t *combo, cl_selection_fn fn, void *user);

/* Menu (popup menu; build it, then cl_window_open_popup, which takes ownership) */
cl_widget_t *cl_menu_create(cl_application_t *app, const cl_menu_desc_t *desc);
cl_result_t  cl_menu_add_item(cl_widget_t *menu, const char *text, cl_action_fn fn, void *user);
cl_result_t  cl_menu_add_submenu(cl_widget_t *menu, const char *text, cl_widget_t *submenu);
const char  *cl_menu_item_text(cl_widget_t *menu, size_t index);
cl_result_t  cl_menu_remove(cl_widget_t *menu, size_t index);
void         cl_menu_clear(cl_widget_t *menu);
size_t       cl_menu_count(cl_widget_t *menu);
```

Submenus are supported: a submenu item (`cl_menu_add_submenu`) opens a nested
menu beside it on click, Enter or Right, and popups stack (§12). Not yet
supported: opening a new popup from an item's callback (the menu is destroyed
after the callback returns).

## 19. ScrollView (`widgets/scrollview.h`)

```c
typedef struct cl_scrollview_desc { uint32_t abi_version; size_t struct_size;
    bool horizontal;  /* allow horizontal overflow and scrolling */
    bool smooth;      /* animate wheel scrolling instead of jumping */ } cl_scrollview_desc_t;
#define CL_SCROLLVIEW_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_scrollview_desc_t)
cl_widget_t *cl_scrollview_create(cl_application_t *app, const cl_scrollview_desc_t *desc);
void         cl_scrollview_set_content(cl_widget_t *sv, cl_widget_t *content); /* owns content */
cl_widget_t *cl_scrollview_content(cl_widget_t *sv);
void         cl_scrollview_scroll_to(cl_widget_t *sv, float y);    /* vertical offset (clamped) */
void         cl_scrollview_scroll_to_x(cl_widget_t *sv, float x);  /* horizontal offset (clamped) */
float        cl_scrollview_scroll_y(cl_widget_t *sv);
float        cl_scrollview_scroll_x(cl_widget_t *sv);
void         cl_scrollview_scroll_to_widget(cl_widget_t *sv, cl_widget_t *descendant); /* minimal scroll into view */
```

ScrollView implements the `clip_rect` (content clipping) and `reveal`
(scroll-to-view; used when focus moves, §14 `cl_widget_focus`) vtable hooks.

## 20. Theme / Style (`theme.h`)

```c
typedef struct cl_text_style {
    cl_font_t *font;    /* NULL -> theme font */
    cl_color_t color;
    cl_align_t align;   /* horizontal alignment */
} cl_text_style_t;

cl_color_t cl_theme_color(cl_theme_t *theme, cl_color_role_t role);
void       cl_theme_set_color(cl_theme_t *theme, cl_color_role_t role, cl_color_t color);
void       cl_theme_set_variant(cl_theme_t *theme, cl_theme_variant_t variant); /* light/dark; replaces all colors, font/metrics kept */
cl_theme_variant_t cl_theme_variant(cl_theme_t *theme);
void       cl_theme_set_radius(cl_theme_t *theme, float radius);   /* default corner radius */
cl_font_t *cl_theme_font(cl_theme_t *theme);                       /* may be NULL before set */
void       cl_theme_set_font(cl_theme_t *theme, cl_font_t *font);  /* borrowed, not owned */
```

The application's theme is `cl_application_theme(app)`. The color roles are listed
in §4.

## 21. Font / Text (`font.h`)

```c
typedef struct cl_font_metrics { float ascent, descent, line_height; } cl_font_metrics_t;

cl_font_t        *cl_font_load_file(cl_application_t *app, const char *path, float size_px);
cl_font_t        *cl_font_load_memory(cl_application_t *app, const void *data, size_t len, float size_px);
cl_font_t        *cl_font_load_system(cl_application_t *app, float size_px);
                  /* COPAL_FONT=/path, then the known system paths
                   * (Segoe UI/Arial, DejaVu/Liberation/Noto); NULL + WARN */
void              cl_font_release(cl_font_t *font);
cl_font_metrics_t cl_font_metrics(cl_font_t *font);

/* Measurement without rasterization. max_width is RESERVED for future wrapping
 * and is ignored for now: any value measures a single line (pass CL_UNBOUNDED);
 * width wrapping currently exists only inside textbox. */
cl_size_t cl_text_measure(cl_font_t *font, const char *utf8, float max_width);
/* Exactly len bytes (not NUL-terminated) — for caret positioning. */
cl_size_t cl_text_measure_bytes(cl_font_t *font, const char *utf8, size_t len, float max_width);
```

Data that is not a font is rejected (`NULL` + `CL_ERROR_FONT`), but the parser
(stb_truetype) does not check a *truncated* real font against the buffer boundary
— fonts must come from a trusted source.

Text limitations (no shaping/bidi/mark-positioning) — ARCHITECTURE §12.

## 22. Renderer / PaintContext (for `paint`) (`render.h`)

```c
void cl_paint_fill_rect(cl_paint_context_t *ctx, cl_rect_t r, cl_color_t color);
void cl_paint_fill_round_rect(cl_paint_context_t *ctx, cl_rect_t r, float radius, cl_color_t color);
void cl_paint_stroke_round_rect(cl_paint_context_t *ctx, cl_rect_t r, float radius,
                                float width, cl_color_t color);
void cl_paint_draw_text(cl_paint_context_t *ctx, cl_font_t *font, const char *utf8,
                        cl_point_t pos, cl_color_t color);
void cl_paint_draw_image(cl_paint_context_t *ctx, cl_image_t *img, cl_rect_t dst);
void cl_paint_push_clip(cl_paint_context_t *ctx, cl_rect_t r);
void cl_paint_pop_clip(cl_paint_context_t *ctx);

/* A transform (translate + uniform scale) for everything drawn until the
 * matching pop; nested pushes compose. Clip rects are transformed the same way. */
void cl_paint_push_transform(cl_paint_context_t *ctx, cl_point_t offset, float scale);
void cl_paint_pop_transform(cl_paint_context_t *ctx);

/* Group opacity: the alpha of everything drawn until the pop is multiplied by
 * alpha ∈ [0,1]; nested pushes multiply together. The multiplier is applied to
 * each primitive individually (no intermediate buffer) — overlapping elements
 * inside a "fading" group show through one another. */
void cl_paint_push_opacity(cl_paint_context_t *ctx, float alpha);
void cl_paint_pop_opacity(cl_paint_context_t *ctx);

cl_theme_t *cl_paint_theme(cl_paint_context_t *ctx);
cl_color_t  cl_paint_theme_color(cl_paint_context_t *ctx, cl_color_role_t role);
```

`cl_paint_context_t` is given only in `paint`; it must not be stored. Rounded
corners/borders and text are rasterized by the renderer (GL: SDF primitives +
glyph atlas; mock: a list of draw commands). Push/pop (clip, transform, opacity)
must be balanced within a single `paint` call; glyphs under a scale transform are
stretched as bitmaps (the quality is good enough for transitional animations).

## 23. Custom widget — minimal example

```c
#include <copal/widget_impl.h>

typedef struct cl_mycounter { cl_widget_t base; int value; } cl_mycounter_t;

static void mycounter_paint(cl_widget_t *w, cl_paint_context_t *ctx) {
    cl_mycounter_t *self = CL_WIDGET_CAST(cl_mycounter, w);   /* checked */
    cl_paint_fill_round_rect(ctx, w->rect, 6.0f,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    (void)self; /* draw self->value ... */
}
static bool mycounter_mouse_down(cl_widget_t *w, const cl_event_t *ev) {
    cl_mycounter_t *self = CL_WIDGET_CAST(cl_mycounter, w);
    (void)ev; self->value++; cl_widget_invalidate(w); return true;
}
static const cl_widget_vtable_t cl_mycounter_vtable = {
    .paint = mycounter_paint, .mouse_down = mycounter_mouse_down,
};
static const cl_widget_class_t cl_mycounter_class = {
    .name = "cl_mycounter", .base = NULL, .type_id = 0,
    .instance_size = sizeof(cl_mycounter_t), .vtable = &cl_mycounter_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t), /* required when vtable is set */
};
cl_widget_t *cl_mycounter_create(cl_application_t *app) {
    cl_widget_t *w = cl_widget_alloc(app, &cl_mycounter_class); /* zero + init_base */
    return w;   /* self->value is already 0 (zero-alloc) */
}
```

`cl_widget_alloc` allocates `instance_size` zeroed and calls `cl_widget_init_base`;
user initialization comes after. Casts rely on class-pointer identity
(`&cl_mycounter_class`); `CL_WIDGET_CAST` always returns NULL on a mismatch.
Growing the vtable (adding a slot) requires recompiling the application (a
consequence of ADR-005).
