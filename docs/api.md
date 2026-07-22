# API reference

The core copal framework API: application, window, timer, animation, the widget base, layout, theme, font, and the paint context. Individual widgets have their own catalog in [widgets](./widgets.md).

## Conventions

copal is plain C11. One header per module under `include/copal/`; `<copal/copal.h>` pulls in the whole public surface.

| Kind | Pattern | Example |
| --- | --- | --- |
| Type | `cl_*_t` | `cl_window_t`, `cl_rect_t` |
| Function | `cl_<module>_<action>` | `cl_window_set_content` |
| Macro / enum constant | `CL_*` | `CL_OK`, `CL_ALIGN_CENTER` |
| Event callback | `cl_<event>_fn` | `cl_action_fn`, `cl_timer_fn` |
| Descriptor struct | `cl_<obj>_desc_t` | `cl_window_desc_t` |
| Predicate | `cl_<x>_is_*` | `cl_widget_is_visible` |
| Setter | `cl_<x>_set_*` | `cl_widget_set_enabled` |

- **Units** are logical pixels (`float`); the renderer converts to physical pixels. Colours are 8-bit-per-channel, non-premultiplied RGBA.
- **Strings** are UTF-8. Returned strings are borrowed (see [Ownership](#ownership)).
- `CL_OK` is guaranteed to be `0`.

## Handles

Opaque handles are forward-declared structs you only ever hold as a pointer.

| Handle | Created by | Destroyed by | Owner |
| --- | --- | --- | --- |
| `cl_application_t` | `cl_application_create` | `cl_application_destroy` | you (root object) |
| `cl_window_t` | `cl_window_create` | `cl_window_destroy`, or app destroy | application |
| `cl_timer_t` | `cl_timer_create` | `cl_timer_cancel`, or app destroy | application |
| `cl_animation_t` | `cl_animation_start` | self, on completion/cancel; or app destroy | application |
| `cl_theme_t` | the application (one per app) | app destroy | application |
| `cl_font_t` | `cl_font_load_*` | `cl_font_release` | you (borrows app allocator) |
| `cl_paint_context_t` | the framework, per paint | the framework | framework |
| `cl_platform_t` | built-in, or injected | app destroy (on create success) | application |
| `cl_renderer_t` | built-in, or injected | app destroy (on create success) | application |

- `cl_paint_context_t` is valid **only inside** a widget's `paint()` call; never store it.
- `cl_widget_t` is **semi-opaque**: opaque to applications (manipulated only through `cl_widget_*` and per-widget APIs), but its vtable/layout internals are exposed to widget *authors* via `widget_impl.h`. See [extending](./extending.md).

## Value types

From `<copal/types.h>`. Plain structs, passed and returned by value.

```c
typedef struct cl_point  { float x, y; }                     cl_point_t;
typedef struct cl_size   { float w, h; }                     cl_size_t;
typedef struct cl_rect   { float x, y, w, h; }               cl_rect_t;
typedef struct cl_insets { float left, top, right, bottom; } cl_insets_t;
typedef struct cl_color  { uint8_t r, g, b, a; }             cl_color_t; /* NOT premultiplied */

typedef struct cl_constraints { cl_size_t min; cl_size_t max; } cl_constraints_t;

static inline cl_color_t cl_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

#define CL_UNBOUNDED (3.4e38f) /* an infinite max constraint component */
```

```c
typedef enum cl_align {
    CL_ALIGN_START, CL_ALIGN_CENTER, CL_ALIGN_END, CL_ALIGN_STRETCH,
    CL_ALIGN_AUTO  /* per-child: defer to the container's alignment */
} cl_align_t;

typedef enum cl_cursor {
    CL_CURSOR_DEFAULT = 0, CL_CURSOR_IBEAM, CL_CURSOR_HAND, CL_CURSOR_CROSSHAIR,
    CL_CURSOR_SIZE_H, CL_CURSOR_SIZE_V, CL_CURSOR__COUNT
} cl_cursor_t;

/* Reserved. No public API consumes an orientation yet; do not infer that an
 * orientation knob exists (boxes are separate cl_vbox/cl_hbox creators). */
typedef enum cl_orientation { CL_ORIENT_HORIZONTAL, CL_ORIENT_VERTICAL } cl_orientation_t;
```

## ABI handshake

Every descriptor (`cl_*_desc_t`) begins with two service fields:

```c
uint32_t abi_version;   /* COPAL_VERSION at your compile time */
size_t   struct_size;   /* sizeof(the desc) */
```

Stamp them with the module's INIT macros, then set the fields you care about — the rest default to zero:

```c
cl_window_desc_t d = {
    CL_WINDOW_DESC_INIT_FIELDS,     /* stamps abi_version + struct_size */
    .title = "Hello", .width = 640, .height = 480, .resizable = true,
};
cl_window_t *win = cl_window_create(app, &d);
```

`CL_<OBJ>_DESC_INIT_FIELDS` stamps just the two service fields (for use inside a compound literal); `CL_<OBJ>_DESC_INIT` is the full default initializer (`{ CL_<OBJ>_DESC_INIT_FIELDS }`). The handshake is **tail-tolerant** within a major version: a newer library reading an older, shorter desc uses `struct_size` to know where your struct ends and defaults the rest. A `cl_*_create` call with a mismatched `abi_version` is rejected with `CL_ERROR_ABI_MISMATCH`.

## Enums

```c
/* <copal/error.h> */
typedef enum cl_result {
    CL_OK = 0,
    CL_ERROR_INVALID_ARGUMENT,
    CL_ERROR_OUT_OF_MEMORY,
    CL_ERROR_PLATFORM,
    CL_ERROR_RENDERER,
    CL_ERROR_FONT,
    CL_ERROR_UNSUPPORTED,
    CL_ERROR_ABI_MISMATCH
} cl_result_t;

typedef enum cl_log_level { CL_LOG_DEBUG, CL_LOG_INFO, CL_LOG_WARN, CL_LOG_ERROR } cl_log_level_t;
```

```c
/* <copal/event.h> */
typedef enum cl_event_type {
    CL_EVENT_MOUSE_DOWN, CL_EVENT_MOUSE_UP, CL_EVENT_MOUSE_MOVE, CL_EVENT_MOUSE_WHEEL,
    CL_EVENT_MOUSE_ENTER, CL_EVENT_MOUSE_LEAVE,
    CL_EVENT_KEY_DOWN, CL_EVENT_KEY_UP,
    CL_EVENT_TEXT_INPUT,
    CL_EVENT_TEXT_EDIT,   /* IME pre-edit (composition) update */
    CL_EVENT_FOCUS_GAINED, CL_EVENT_FOCUS_LOST
} cl_event_type_t;

typedef enum cl_mouse_button { CL_MOUSE_LEFT, CL_MOUSE_MIDDLE, CL_MOUSE_RIGHT } cl_mouse_button_t;

typedef enum cl_key_mods {   /* bit flags */
    CL_MOD_NONE = 0, CL_MOD_SHIFT = 1<<0, CL_MOD_CTRL = 1<<1, CL_MOD_ALT = 1<<2, CL_MOD_SUPER = 1<<3
} cl_key_mods_t;

typedef enum cl_key {        /* platform-neutral subset */
    CL_KEY_UNKNOWN = 0,
    CL_KEY_LEFT, CL_KEY_RIGHT, CL_KEY_UP, CL_KEY_DOWN, CL_KEY_HOME, CL_KEY_END,
    CL_KEY_BACKSPACE, CL_KEY_DELETE, CL_KEY_ENTER, CL_KEY_TAB, CL_KEY_ESCAPE,
    CL_KEY_SPACE, CL_KEY_PAGE_UP, CL_KEY_PAGE_DOWN,
    CL_KEY_A .. CL_KEY_Z,     /* letters */
    CL_KEY_0 .. CL_KEY_9,     /* digit row (not keypad) */
    CL_KEY_F1 .. CL_KEY_F12   /* function row */
} cl_key_t;
```

```c
/* <copal/theme.h> */
typedef enum cl_color_role {
    CL_COLOR_BACKGROUND,     /* window backdrop */
    CL_COLOR_SURFACE,        /* base widget surface */
    CL_COLOR_SURFACE_HOVER,  /* surface under the pointer */
    CL_COLOR_SURFACE_ACTIVE, /* surface while pressed */
    CL_COLOR_SURFACE_RAISED, /* elevated surface (popups, menus) */
    CL_COLOR_TEXT,
    CL_COLOR_TEXT_MUTED,
    CL_COLOR_ACCENT,
    CL_COLOR_BORDER,
    CL_COLOR_FOCUS_RING,
    CL_COLOR_SELECTION,
    CL_COLOR_SHADOW,         /* drop-shadow colour (semi-transparent) */
    CL_COLOR__COUNT
} cl_color_role_t;

typedef enum cl_theme_variant { CL_THEME_LIGHT, CL_THEME_DARK } cl_theme_variant_t;
```

## Events and callbacks

Events are delivered to widgets as a `cl_event_t`; widget authors handle them (see [extending](./extending.md)). The tagged union:

```c
typedef struct cl_event {
    cl_event_type_t type;
    cl_key_mods_t   mods;
    union {
        struct { cl_point_t pos; cl_mouse_button_t button; int clicks; } mouse; /* clicks: 1=single, 2=double */
        struct { cl_point_t pos; float dx, dy; } wheel;
        struct { cl_key_t key; bool repeat; } key;
        struct { const char *utf8; } text;               /* NUL-terminated */
        struct { const char *utf8; int cursor; } edit;   /* IME composition + caret (codepoints) */
    } data;
} cl_event_t;
```

Application-facing callback typedefs:

| Typedef | Signature | Fired by |
| --- | --- | --- |
| `cl_action_fn` | `(cl_widget_t *w, void *user)` | button click, generic action |
| `cl_text_changed_fn` | `(cl_widget_t *w, const char *utf8, void *user)` | textbox change/submit |
| `cl_toggled_fn` | `(cl_widget_t *w, bool checked, void *user)` | checkbox / radiobutton |
| `cl_value_fn` | `(cl_widget_t *w, float value, void *user)` | slider |
| `cl_selection_fn` | `(cl_widget_t *w, int index, void *user)` | combobox |
| `cl_list_fn` | `(cl_widget_t *list, int index, void *user)` | list select / activate |
| `cl_radiogroup_fn` | `(cl_widget_t *group, int index, void *user)` | radio group change |
| `cl_msgbox_fn` | `(cl_widget_t *dialog, int index, void *user)` | message-box button |
| `cl_task_fn` | `(void *user)` | `cl_application_post` |
| `cl_window_close_fn` | `bool (cl_window_t *win, void *user)` | window close (return `false` to veto) |
| `cl_timer_fn` | `(cl_timer_t *timer, void *user)` | timer firing |
| `cl_animation_fn` | `(cl_animation_t *anim, float t, void *user)` | animation progress (eased `t` in `[0,1]`) |
| `cl_animation_done_fn` | `(cl_animation_t *anim, bool finished, void *user)` | animation end (`finished=false` if cancelled) |
| `cl_log_fn` | `(cl_log_level_t level, const char *msg, void *user)` | library diagnostics |
| `cl_assert_fn` | `(const char *expr, const char *file, int line)` | failed `CL_ASSERT` (debug builds) |

## Ownership

- Widgets form a single-parent tree. `cl_widget_add_child` appends a child (which must be parentless); `cl_widget_remove_child` detaches it back to you.
- `cl_widget_destroy` destroys a widget and its whole subtree. For widgets attached to a window the subtree is detached immediately but freed at the end of the current loop iteration, so **destroying any widget from any callback is safe**; a second destroy is a no-op. An unattached tree is freed at once.
- Ownership-transferring calls consume the widget you pass: `cl_window_set_content`, `cl_window_open_popup`, `cl_window_open_modal`. Setting new content destroys the previous subtree.
- Returned strings (`cl_window_title`, `cl_widget_tooltip`, ...) are **borrowed** and valid only until the next mutation of that object.

## Error model

Most functions that can fail return a `cl_result_t` or a handle (`NULL` on failure). Every failure also records a **thread-local last error**.

```c
cl_result_t  cl_last_error(void);                 /* last error on this thread */
const char  *cl_result_string(cl_result_t r);     /* static human-readable text */
void         cl_set_log_callback(cl_log_fn fn, void *user);   /* NULL removes; else stderr */
void         cl_set_assert_handler(cl_assert_fn fn);          /* NULL removes */
```

`void` setters do not report errors: they clamp or validate their input silently. Install the log callback before the first copal call and before spawning threads (the sink is not synchronised). Without one, `WARN`/`ERROR` go to stderr. Assertions are compiled out under `NDEBUG`.

## Versioning

From `<copal/version.h>`. Current version **0.3.1**.

```c
#define COPAL_VERSION_MAJOR 0
#define COPAL_VERSION_MINOR 3
#define COPAL_VERSION_PATCH 1
#define COPAL_VERSION_ENCODE(major, minor, patch)  /* -> 0x00MMmmpp */
#define COPAL_VERSION  COPAL_VERSION_ENCODE(0, 3, 1)

uint32_t     cl_version_runtime(void);  /* linked library version; compare to COPAL_VERSION */
const char  *cl_version_string(void);   /* e.g. "0.3.1" */
```

Pre-1.0 the ABI is not frozen: a minor bump may break it, and `abi_version` in every desc is the guard (`CL_ERROR_ABI_MISMATCH`). Compare `cl_version_runtime()` against the compile-time `COPAL_VERSION` to catch a header/binary skew.

## Allocator

Every allocation in the library goes through a `cl_allocator_t`, set once on the application descriptor (`NULL` selects a built-in malloc-based default). From `<copal/allocator.h>`.

```c
typedef struct cl_allocator {
    void *userdata;
    void *(*alloc)(void *userdata, size_t size);
    void *(*realloc)(void *userdata, void *ptr, size_t size);
    void  (*free)(void *userdata, void *ptr);
} cl_allocator_t;

const cl_allocator_t *cl_allocator_default(void);  /* the built-in malloc allocator */

/* Thin wrappers over an allocator; a NULL allocator uses the default. A zero
 * size is normalized to 1, so NULL always means out of memory (recorded as
 * CL_ERROR_OUT_OF_MEMORY). */
void *cl_alloc(const cl_allocator_t *a, size_t size);
void *cl_realloc(const cl_allocator_t *a, void *ptr, size_t size);
void  cl_free(const cl_allocator_t *a, void *ptr);
```

All three function pointers are required (the library calls them without NULL checks) and follow malloc-family semantics: `realloc(userdata, NULL, size)` acts like `alloc`, and `free(userdata, NULL)` is a no-op. Reach the application's active allocator with `cl_application_allocator()`. For a thread-safe `cl_application_post`, the allocator must be thread-safe (the default is). Injecting a custom allocator is covered in [extending.md](./extending.md).

## Application

The root object and event loop. From `<copal/application.h>`.

```c
typedef enum cl_render_backend {
    CL_RENDER_AUTO = 0,  /* GL if compiled in and the GL window comes up, else software;
                          * COPAL_RENDER=software forces the CPU path */
    CL_RENDER_GL,        /* OpenGL renderer */
    CL_RENDER_SOFTWARE   /* CPU rasterizer, no GPU context */
} cl_render_backend_t;

/* Injectable mutex for the cross-thread task queue. NULL -> hosted default
 * (freestanding builds have no default). create() returns an opaque handle. */
typedef struct cl_mutex_iface {
    void *(*create)(void *user);
    void  (*destroy)(void *user, void *handle);
    void  (*lock)(void *user, void *handle);
    void  (*unlock)(void *user, void *handle);
    void *user;
} cl_mutex_iface_t;

typedef struct cl_application_desc {
    uint32_t abi_version;
    size_t   struct_size;
    const cl_allocator_t *allocator;    /* NULL -> built-in malloc */
    cl_platform_t        *platform;     /* injected backend, or NULL for built-in */
    cl_renderer_t        *renderer;     /* injected backend, or NULL for built-in */
    cl_render_backend_t   render_backend; /* built-in backend choice (0 = AUTO) */
    const cl_mutex_iface_t *mutex;      /* NULL -> hosted default */
} cl_application_desc_t;

#define CL_APPLICATION_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_application_desc_t)
#define CL_APPLICATION_DESC_INIT { CL_APPLICATION_DESC_INIT_FIELDS }

cl_application_t *cl_application_create(const cl_application_desc_t *desc);
void              cl_application_destroy(cl_application_t *app);

int   cl_application_run(cl_application_t *app);              /* loop until quit; returns exit code */
bool  cl_application_step(cl_application_t *app, bool wait);  /* one iteration; false once quit requested */
void  cl_application_quit(cl_application_t *app, int exit_code);        /* thread-safe */
cl_result_t cl_application_post(cl_application_t *app, cl_task_fn fn, void *user); /* run fn on UI thread; thread-safe */

cl_theme_t           *cl_application_theme(cl_application_t *app);
const cl_allocator_t *cl_application_allocator(cl_application_t *app);
```

Injected backends transfer ownership to the application only when `create` succeeds; on failure they stay with the caller. `cl_application_run` blocks between events, waking for due timers and posted tasks.

## Window

One top-level window (a second `create` returns `NULL` + `CL_ERROR_UNSUPPORTED`). From `<copal/window.h>`.

```c
typedef bool (*cl_window_close_fn)(cl_window_t *win, void *user); /* false vetoes the close */

typedef struct cl_window_desc {
    uint32_t abi_version;
    size_t   struct_size;
    const char *title;              /* UTF-8; may be NULL */
    int32_t  width, height;
    int32_t  min_width, min_height;
    bool     resizable;
} cl_window_desc_t;

#define CL_WINDOW_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_window_desc_t)
#define CL_WINDOW_DESC_INIT { CL_WINDOW_DESC_INIT_FIELDS }

cl_window_t *cl_window_create(cl_application_t *app, const cl_window_desc_t *desc);
void         cl_window_destroy(cl_window_t *win);

void            cl_window_show(cl_window_t *win);
void            cl_window_set_content(cl_window_t *win, cl_widget_t *root); /* takes ownership; NULL clears */
cl_widget_t    *cl_window_content(cl_window_t *win);
void            cl_window_set_title(cl_window_t *win, const char *utf8);
const char     *cl_window_title(cl_window_t *win);        /* borrowed; valid until next set_title */
cl_application_t *cl_window_application(cl_window_t *win);
cl_size_t       cl_window_size(cl_window_t *win);
void            cl_window_set_on_close(cl_window_t *win, cl_window_close_fn fn, void *user);

/* Overlay popups: take ownership; stack up to 8 deep. */
void         cl_window_open_popup(cl_window_t *win, cl_widget_t *popup, cl_point_t at);
bool         cl_window_open_modal(cl_window_t *win, cl_widget_t *dialog); /* centred; false if stack full */
void         cl_window_close_popup(cl_window_t *win);
cl_widget_t *cl_window_popup(cl_window_t *win);       /* topmost open popup, or NULL */

cl_widget_t *cl_window_tooltip(cl_window_t *win);     /* current tooltip bubble, or NULL (introspection) */
```

`open_popup` dismisses on an outside click; `open_modal` swallows outside clicks (close it explicitly). Closing is deferred to a safe point, so a popup's own handler may request it. A push past the 8-deep cap is ignored (`WARN` logged, `CL_ERROR_UNSUPPORTED`) and the would-be-owned widget is destroyed rather than leaked.

## Timer

Best-effort timers fired on the loop thread between event dispatch and rendering. From `<copal/timer.h>`.

```c
typedef void (*cl_timer_fn)(cl_timer_t *timer, void *user);

cl_timer_t *cl_timer_create(cl_application_t *app, uint32_t interval_ms,
                            bool repeat, cl_timer_fn fn, void *user);
void        cl_timer_cancel(cl_timer_t *timer);   /* stop + free; safe from own callback */
void        cl_timer_restart(cl_timer_t *timer);  /* re-arm interval_ms from now */
```

A firing may be late but never early; a repeating timer that falls behind coalesces missed ticks into one. A one-shot handle stays valid after firing (re-arm with `restart`); only a cancelled handle is invalid. A repeating interval floors at 1 ms.

## Animation

Time-based animations on one shared ~60 Hz ticker. From `<copal/animation.h>`.

```c
typedef enum cl_easing {
    CL_EASE_LINEAR = 0, CL_EASE_IN, CL_EASE_OUT, CL_EASE_IN_OUT
} cl_easing_t;

typedef void (*cl_animation_fn)(cl_animation_t *anim, float t, void *user);       /* eased t in [0,1] */
typedef void (*cl_animation_done_fn)(cl_animation_t *anim, bool finished, void *user);

typedef struct cl_animation_desc {
    uint32_t abi_version;
    size_t   struct_size;
    uint32_t duration_ms;          /* 0 completes on the first tick */
    cl_easing_t easing;
    cl_animation_fn      on_progress; /* required */
    cl_animation_done_fn on_done;     /* optional */
    void *user;
} cl_animation_desc_t;

#define CL_ANIMATION_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_animation_desc_t)
#define CL_ANIMATION_DESC_INIT { CL_ANIMATION_DESC_INIT_FIELDS }

cl_animation_t *cl_animation_start(cl_application_t *app, const cl_animation_desc_t *desc);
void            cl_animation_cancel(cl_animation_t *anim); /* fires on_done(false); then invalid */

/* Interpolation helpers */
float      cl_ease(cl_easing_t easing, float t);            /* map linear t (clamped) through a curve */
float      cl_lerp(float from, float to, float t);         /* from + (to-from)*t */
cl_color_t cl_color_lerp(cl_color_t from, cl_color_t to, float t);
cl_rect_t  cl_rect_lerp(cl_rect_t from, cl_rect_t to, float t);
```

An animation frees itself once complete or cancelled — the handle is invalid after `on_done` (or, with no `on_done`, after `on_progress` sees `t == 1.0`). Keep the handle only if you may cancel, and `NULL` it from `on_done`. The final progress call always receives exactly `1.0`.

## Widget base

Common tree, geometry, layout, focus, and state operations shared by all widgets. From `<copal/widget.h>`.

```c
/* Tree / ownership */
cl_result_t   cl_widget_add_child(cl_widget_t *parent, cl_widget_t *child);
cl_result_t   cl_widget_remove_child(cl_widget_t *parent, cl_widget_t *child);
void          cl_widget_destroy(cl_widget_t *w);          /* deferred; safe from callbacks */
cl_widget_t  *cl_widget_parent(cl_widget_t *w);
cl_window_t  *cl_widget_window(cl_widget_t *w);

/* Geometry / state */
cl_rect_t     cl_widget_rect(cl_widget_t *w);
void          cl_widget_set_visible(cl_widget_t *w, bool v);
bool          cl_widget_is_visible(cl_widget_t *w);
void          cl_widget_set_enabled(cl_widget_t *w, bool e);
bool          cl_widget_is_enabled(cl_widget_t *w);
void          cl_widget_set_cursor(cl_widget_t *w, cl_cursor_t cursor); /* DEFAULT defers to ancestor */
cl_cursor_t   cl_widget_cursor(cl_widget_t *w);

/* Per-child layout attributes (consumed by vbox/hbox — set on the CHILD) */
void          cl_widget_set_preferred_size(cl_widget_t *w, cl_size_t s); /* overrides measure per axis > 0 */
cl_size_t     cl_widget_preferred_size(cl_widget_t *w);
void          cl_widget_set_margin(cl_widget_t *w, cl_insets_t m);
cl_insets_t   cl_widget_margin(cl_widget_t *w);
void          cl_widget_set_align(cl_widget_t *w, cl_align_t h, cl_align_t v);
cl_align_t    cl_widget_align_h(cl_widget_t *w);
cl_align_t    cl_widget_align_v(cl_widget_t *w);
void          cl_widget_set_flex(cl_widget_t *w, float weight); /* grow share of leftover space; 0 = fixed */
float         cl_widget_flex(cl_widget_t *w);

/* Focus */
void          cl_widget_set_focusable(cl_widget_t *w, bool focusable);
bool          cl_widget_is_focusable(cl_widget_t *w);
bool          cl_widget_focus(cl_widget_t *w);
bool          cl_widget_has_focus(cl_widget_t *w);

/* Invalidation */
void          cl_widget_invalidate(cl_widget_t *w);         /* repaint */
void          cl_widget_invalidate_layout(cl_widget_t *w);  /* re-measure + re-layout */

/* User data */
void          cl_widget_set_userdata(cl_widget_t *w, void *user);
void         *cl_widget_userdata(cl_widget_t *w);

/* Hover tooltip (text is copied; NULL or "" clears) */
void          cl_widget_set_tooltip(cl_widget_t *w, const char *utf8);
const char   *cl_widget_tooltip(cl_widget_t *w);
```

`add_child` rejects a NULL/destroyed widget, an already-parented child, a cycle, or a tree deeper than the internal depth limit (a few hundred levels), all with `CL_ERROR_INVALID_ARGUMENT`.

## Layout

Box containers stack children along one axis. From `<copal/layout.h>`. Per-child attributes (`flex`, `preferred_size`, `align`, `margin`) are set on each child through the [widget base](#widget-base).

```c
typedef struct cl_vbox_desc {
    uint32_t abi_version;
    size_t   struct_size;
    float       spacing;
    cl_insets_t padding;
    cl_align_t  align_cross;  /* cross-axis (horizontal) alignment of children */
} cl_vbox_desc_t;

#define CL_VBOX_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_vbox_desc_t)
#define CL_VBOX_DESC_INIT { CL_VBOX_DESC_INIT_FIELDS }

cl_widget_t *cl_vbox_create(cl_application_t *app, const cl_vbox_desc_t *desc);

typedef struct cl_hbox_desc {
    uint32_t abi_version;
    size_t   struct_size;
    float       spacing;
    cl_insets_t padding;
    cl_align_t  align_cross;  /* cross-axis (vertical) alignment of children */
} cl_hbox_desc_t;

#define CL_HBOX_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_hbox_desc_t)
#define CL_HBOX_DESC_INIT { CL_HBOX_DESC_INIT_FIELDS }

cl_widget_t *cl_hbox_create(cl_application_t *app, const cl_hbox_desc_t *desc);
```

## Theme

Semantic colour roles, a corner radius, and a default font, resolved per application. From `<copal/theme.h>`. Roles and variants are listed under [Enums](#enums).

```c
/* Reusable text style */
typedef struct cl_text_style {
    cl_font_t  *font;   /* NULL falls back to the theme font */
    cl_color_t  color;  /* a zero-alpha (unset) colour falls back to CL_COLOR_TEXT */
    cl_align_t  align;  /* plain value; CL_ALIGN_START by default */
} cl_text_style_t;

cl_color_t         cl_theme_color(cl_theme_t *theme, cl_color_role_t role);
void               cl_theme_set_color(cl_theme_t *theme, cl_color_role_t role, cl_color_t color);
void               cl_theme_set_variant(cl_theme_t *theme, cl_theme_variant_t variant); /* replaces all roles */
cl_theme_variant_t cl_theme_variant(cl_theme_t *theme);
void               cl_theme_set_radius(cl_theme_t *theme, float radius);
float              cl_theme_radius(cl_theme_t *theme);
cl_font_t         *cl_theme_font(cl_theme_t *theme);           /* may be NULL until set */
void               cl_theme_set_font(cl_theme_t *theme, cl_font_t *font); /* borrowed, not owned */
```

`set_variant` replaces every colour role but preserves the font and radius.

## Font

TrueType/OpenType loading and text measurement (no rasterization here). From `<copal/font.h>`.

```c
typedef struct cl_font_metrics {
    float ascent;      /* px above baseline (positive) */
    float descent;     /* px below baseline (positive) */
    float line_height; /* recommended line advance */
} cl_font_metrics_t;

cl_font_t *cl_font_load_file(cl_application_t *app, const char *path, float size_px);
cl_font_t *cl_font_load_memory(cl_application_t *app, const void *data, size_t len, float size_px);
cl_font_t *cl_font_load_system(cl_application_t *app, float size_px); /* honours COPAL_FONT, then probes */
void       cl_font_release(cl_font_t *font);

cl_font_metrics_t cl_font_metrics(cl_font_t *font);
cl_size_t  cl_text_measure(cl_font_t *font, const char *utf8, float max_width);
cl_size_t  cl_text_measure_bytes(cl_font_t *font, const char *utf8, size_t len, float max_width);
```

Fonts must come from a **trusted source**: the stb_truetype parser rejects non-font data but does not bounds-check a truncated real font. There is **no shaping** — measurement and rendering are per-glyph. `max_width` is reserved for future wrapping and currently ignored (pass `CL_UNBOUNDED`; every value measures a single line). `measure_bytes` stops at an embedded NUL, mirroring `draw_text`. Release every font before destroying the application that loaded it.

## Paint context

The drawing surface handed to a widget's `paint()` method. From `<copal/render.h>`. Coordinates are absolute logical pixels within the window.

```c
void cl_paint_fill_rect(cl_paint_context_t *ctx, cl_rect_t r, cl_color_t color);
void cl_paint_fill_round_rect(cl_paint_context_t *ctx, cl_rect_t r, float radius, cl_color_t color);
void cl_paint_stroke_round_rect(cl_paint_context_t *ctx, cl_rect_t r, float radius,
                                float width, cl_color_t color);
void cl_paint_draw_text(cl_paint_context_t *ctx, cl_font_t *font, const char *utf8,
                        cl_point_t pos, cl_color_t color);
void cl_paint_draw_image(cl_paint_context_t *ctx, cl_image_t *img, cl_rect_t dst); /* blend, scale to dst */

/* Stacks — pushes and pops must balance within one paint() call */
void cl_paint_push_clip(cl_paint_context_t *ctx, cl_rect_t r); /* intersect with current clip */
void cl_paint_pop_clip(cl_paint_context_t *ctx);
void cl_paint_push_transform(cl_paint_context_t *ctx, cl_point_t offset, float scale); /* translate + uniform scale */
void cl_paint_pop_transform(cl_paint_context_t *ctx);
void cl_paint_push_opacity(cl_paint_context_t *ctx, float alpha);  /* alpha in [0,1]; nested multiply */
void cl_paint_pop_opacity(cl_paint_context_t *ctx);

cl_theme_t *cl_paint_theme(cl_paint_context_t *ctx);
cl_color_t  cl_paint_theme_color(cl_paint_context_t *ctx, cl_color_role_t role);
```

The context is valid **only for the duration** of the `paint()` call and must never be stored. Transform and opacity are no-ops on a renderer without support (all built-in renderers support both). See [extending](./extending.md) for writing a `paint()` method.

---
*See also: [Widgets](./widgets.md) · [Architecture](./architecture.md) · [Extending](./extending.md)*
