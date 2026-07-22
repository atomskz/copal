# Extending copal

copal has three extension points: custom widgets, custom platform/renderer backends, and injected services (allocator and theme). All three plug in through public installed headers.

## Custom widgets

Widget authors include `<copal/widget_impl.h>` (not part of the `<copal/copal.h>` umbrella — application code never needs it). A custom widget:

1. Embeds `cl_widget_t` as the **first** member of its struct, so a `MyWidget*` and its `cl_widget_t*` base share an address.
2. Fills a static `cl_widget_class_t` (RTTI + layout descriptor) and a static `cl_widget_vtable_t` (behaviour).
3. Allocates instances with `cl_widget_alloc()`, which zeroes `instance_size` bytes and initialises the base.
4. Downcasts inside vtable slots with `CL_WIDGET_CAST`.

### The class descriptor

```c
struct cl_widget_class {
    const char *name;
    const cl_widget_class_t *base;   /* superclass, or NULL */
    uint32_t type_id;
    size_t instance_size;            /* sizeof your struct */
    const cl_widget_vtable_t *vtable;
    size_t vtable_size;              /* REQUIRED when vtable != NULL */
};
```

`vtable_size` is the ABI handshake: set it to `sizeof(cl_widget_vtable_t)`. `cl_widget_alloc()` rejects a non-NULL `vtable` whose `vtable_size` does not match the library's own `sizeof` — it records `CL_ERROR_ABI_MISMATCH` and returns `NULL`. This stops a library that added vtable slots from reading past a vtable compiled against an older header. A `NULL` vtable pointer is allowed and needs no `vtable_size`.

### The vtable

Every slot takes `cl_widget_t*` first. A **NULL slot means default behaviour** — `hit_test` defaults to a rect test, `on_event` fans out to the `mouse_*` / `key_*` slots. Fill only what you need. Key slots:

| Slot | Purpose |
|------|---------|
| `destroy` | free owned resources (not the widget itself) |
| `measure` | return desired size for given `cl_constraints_t` |
| `arrange` | position children (containers) |
| `paint` | draw via the `cl_paint_context_t` |
| `hit_test` | refine hit region for non-rectangular shapes |
| `on_event` | intercept the raw event stream (bypasses `mouse_*`) |
| `mouse_down/up/move/wheel` | pointer buttons and wheel |
| `mouse_enter/leave` | hover, delivered to the hovered widget only |
| `key_down/up`, `text_input`, `text_edit` | keyboard and IME (focused widget) |
| `focus_gained/lost` | focus transitions |
| `clip_rect`, `reveal` | clipping and scroll-into-view for containers |

See `widget_impl.h` for the full list and per-slot contracts. Growing the vtable is a source-compatible library change but requires recompiling consumers.

### Minimal example: a clickable counter

```c
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/theme.h>
#include <copal/font.h>
#include <stdio.h>

typedef struct counter {
    cl_widget_t base;   /* MUST be first */
    int count;
} counter_t;

static cl_size_t counter_measure(cl_widget_t *w, cl_constraints_t c)
{
    (void)w; (void)c;
    return (cl_size_t){ 80.0f, 32.0f };
}

static void counter_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    counter_t *self = CL_WIDGET_CAST(counter, w);
    cl_font_t *font = cl_theme_font(cl_paint_theme(ctx));
    char buf[32];

    cl_paint_fill_round_rect(ctx, w->rect, 6.0f,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    snprintf(buf, sizeof buf, "%d", self->count);
    if (font)
        cl_paint_draw_text(ctx, font, buf,
                           (cl_point_t){ w->rect.x + 8.0f, w->rect.y + 6.0f },
                           cl_paint_theme_color(ctx, CL_COLOR_TEXT));
}

static bool counter_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    if (ev->data.mouse.button != CL_MOUSE_LEFT)
        return false;
    CL_WIDGET_CAST(counter, w)->count++;
    cl_widget_invalidate(w);
    return true;
}

static const cl_widget_vtable_t counter_vtable = {
    .measure = counter_measure,
    .paint = counter_paint,
    .mouse_down = counter_mouse_down,
};

static const cl_widget_class_t counter_class = {
    .name = "counter",
    .base = NULL,
    .type_id = 0x636e7472u, /* 'cntr' */
    .instance_size = sizeof(counter_t),
    .vtable = &counter_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t), /* required with a vtable */
};

cl_widget_t *counter_create(cl_application_t *app)
{
    return cl_widget_alloc(app, &counter_class); /* zeroed + base initialised */
}
```

`CL_WIDGET_CAST(counter, w)` expands to a checked downcast against `counter_class`; it returns `NULL` on a type mismatch (and records `CL_ERROR_INVALID_ARGUMENT` for a live widget of the wrong class). `cl_widget_alloc` zeroes the whole instance, so `count` starts at 0 without extra initialisation. Add the widget to a window's tree with `cl_widget_add_child`; the framework owns it from then on and calls `destroy` (if set) when the subtree is torn down.

### Flags and cast helpers

`w->flags` carries the base state bits. Read them directly; change them through the `cl_widget_set_*` API in [api.md](./api.md), not by writing the bits.

```c
enum cl_widget_flags {          /* <copal/widget_impl.h> */
    CL_WF_VISIBLE   = 1u << 0,
    CL_WF_ENABLED   = 1u << 1,
    CL_WF_FOCUSABLE = 1u << 2,
    CL_WF_CLIP      = 1u << 5,   /* clip children to this widget's rect when painting */
    /* CL_WF_DEAD is internal (the deferred-free marker) — never set or clear it. */
};
```

Beyond `cl_widget_alloc` and `CL_WIDGET_CAST`, the author surface includes:

```c
void  cl_widget_init_base(cl_widget_t *w, cl_application_t *app,
                          const cl_widget_class_t *cls); /* init a base you allocated yourself */
void *cl_widget_check_cast(cl_widget_t *w, const cl_widget_class_t *cls); /* CL_WIDGET_CAST wraps this */
bool  cl_widget_is_a(cl_widget_t *w, const cl_widget_class_t *cls);       /* silent type probe, records no error */
```

`CL_WIDGET_CAST_UNCHECKED(name, w)` is a raw downcast with no type check — use it only where the type is already certain.

### Writing a container

A container overrides `measure` and `arrange`. Lay out children through the plumbing wrappers rather than calling a child's vtable directly — they apply the NULL-slot defaults, honour the child's preferred size, and write `child->measured` / `child->rect`:

```c
cl_size_t cl_widget_do_measure(cl_widget_t *child, cl_constraints_t c);
void      cl_widget_do_arrange(cl_widget_t *child, cl_rect_t rect);
void      cl_widget_reveal(cl_widget_t *w);  /* scroll scrollable ancestors until w is visible */
```

## Custom backends

The backend SPI lives in `<copal/backend/platform.h>` and `<copal/backend/renderer.h>` — public **installed** headers, but deliberately outside the `<copal/copal.h>` umbrella. Backends replace how copal talks to the OS (windows, input, present) and how it rasterises.

Each backend:

1. Allocates its own struct with `cl_platform_t` / `cl_renderer_t` as the **first** member.
2. Points that base's `ops` at a static ops table whose **first two fields form the ABI handshake**: `struct_size = sizeof(...ops_t)` and `abi_version = COPAL_VERSION` of the headers it was built against.
3. Injects the base via `cl_application_desc_t.platform` / `.renderer`.

`cl_application_create()` validates the handshake and fails with `CL_ERROR_ABI_MISMATCH` rather than calling through a reshaped table. **Ownership transfers to the application only on success** — if `create()` returns `NULL`, injected backends stay with the caller to free or retry. On success they are destroyed by `cl_application_destroy` (via each table's `destroy` op).

### `cl_platform_ops_t` — OS integration

| Area | Ops |
|------|-----|
| Windows | `create_window`, `destroy_window`, `set_title`, `drawable_size`, `scale` |
| Events | `poll`, `wait`, `wakeup` (thread-safe unblock) |
| Present | `present`, `present_region` (optional partial) |
| Text/IME | `start_text_input`, `set_ime_rect`, `set_cursor` |
| Clipboard | `clipboard_get`, `clipboard_set` |
| Clock | `now_ms` (monotonic ms; NULL disables timers) |
| GL | `gl_get_proc` (NULL for non-GL backends) |
| Software FB | `lock_framebuffer` / `unlock_framebuffer` (NULL for GPU backends) |

Optional ops are documented as NULL-able in the header; e.g. a single-window backend may ignore the `cl_platform_window_t*` handle and accept `NULL` as "the only window".

`poll`/`wait` produce a **`cl_platform_event_t`** — a neutral event the app layer turns into widget events:

```c
typedef enum cl_platform_event_kind {
    CL_PEV_NONE, CL_PEV_QUIT, CL_PEV_RESIZE, CL_PEV_EXPOSE,
    CL_PEV_MOUSE_DOWN, CL_PEV_MOUSE_UP, CL_PEV_MOUSE_MOVE, CL_PEV_MOUSE_WHEEL,
    CL_PEV_KEY_DOWN, CL_PEV_KEY_UP, CL_PEV_TEXT_INPUT, CL_PEV_TEXT_EDIT
} cl_platform_event_kind_t;

typedef struct cl_platform_event {
    cl_platform_event_kind_t kind;
    uint32_t      window_id;
    cl_size_t     size;             /* RESIZE */
    cl_point_t    pos;              /* mouse events */
    cl_mouse_button_t button;
    int           clicks;           /* consecutive presses */
    float         wheel_x, wheel_y; /* MOUSE_WHEEL */
    cl_key_t      key;
    cl_key_mods_t mods;
    char          text[32];         /* TEXT_INPUT / _EDIT (NUL-terminated UTF-8) */
    int           edit_cursor;      /* TEXT_EDIT caret, in codepoints */
    bool          repeat;           /* KEY_DOWN/_UP synthetic auto-repeat */
} cl_platform_event_t;
```

A software platform also fills a **`cl_pixmap_t`** in `lock_framebuffer`, describing the CPU buffer so the software renderer can pack colours without knowing the native format:

```c
typedef struct cl_pixmap {
    void    *pixels;
    int      w, h;      /* framebuffer size in physical pixels */
    int      pitch;     /* bytes per row */
    uint32_t r_mask, g_mask, b_mask, a_mask;  /* a_mask == 0 -> opaque surface */
} cl_pixmap_t;
```

### `cl_renderer_ops_t` — drawing

| Area | Ops |
|------|-----|
| Frame | `begin_frame` (clears to a colour), `end_frame` |
| Paint | `fill_rect`, `fill_round_rect`, `stroke_round_rect`, `draw_text`, `draw_image` |
| State stacks | `push_clip`/`pop_clip`; optional `push_transform`/`pop_transform`, `push_opacity`/`pop_opacity` |
| Caches | `evict_font`, `evict_image` (NULL = no cache); glyph/image upload happens lazily inside `draw_text`/`draw_image` |
| Damage | `set_damage` (optional partial-redraw region for the next frame) |
| Teardown | `destroy` |

See both headers for the full field list and the exact contract of each optional/NULL-able op (transform composition, opacity multiplication, damage semantics).

The in-tree mock backend (`src/render/mock` and `src/platform/mock`, built into the internal `copal::mocks` test library) is a complete worked example of both tables.

## Custom allocator and theme

Two lighter injection points, both through the application descriptor and existing API — no SPI header needed.

**Allocator.** Set `cl_application_desc_t.allocator` to a `cl_allocator_t` (from `<copal/allocator.h>`) with all three of `alloc` / `realloc` / `free` filled — the library calls them without NULL checks, and they follow malloc-family semantics. A `NULL` allocator selects the built-in malloc-based default. The whole library, including every widget, allocates through this. For a thread-safe `cl_application_post`, the allocator must be thread-safe (the default is).

**Theme.** Retrieve the active theme with `cl_application_theme()` and adjust colours, corner radius, and default font through the `cl_theme_*` setters. Widgets read the current theme every frame (through `cl_paint_theme` / `cl_paint_theme_color`), so changes apply on the next paint. See [api.md](./api.md) for the full theme API and colour roles.

---
*See also: [architecture.md](./architecture.md) · [api.md](./api.md) · [widgets.md](./widgets.md)*
