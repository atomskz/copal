<p align="right"><b>English</b> | <a href="./ru/ARCHITECTURE.md">Русский</a></p>

# copal — Architecture

Status: **current** (matches the code as of Stage 7). Version: 1.0.
Project: **copal** · CMake target `copal` (alias `copal::copal`) · public function
prefix `cl_`, types `snake_case`+`_t` (`cl_widget_t`), macros/enums `CL_*` (CODESTYLE
§2, ADR-012) · license **GPL-3.0-or-later**.

This document describes the architecture **as implemented**. The early versions
(0.1–0.2) were a design draft in the `Gui*`/`GUI_*` notation and carried
groundwork, part of which was implemented differently or deferred; such places are
called out explicitly (“**not implemented in the MVP**”, “**simpler than the design
sketch**”). The actual file tree is in [STRUCTURE.md](STRUCTURE.md); the public
signatures are in [API.md](API.md).

## 0. Settled decisions (ADR summary)

| Axis | Decision | ADR | Status |
|-----|---------|-----|--------|
| Platform | SDL2 behind a `platform` interface | ADR-001 | implemented (+ mock) |
| Renderer | OpenGL 3.3 core behind an interface, SDF primitives + glyph atlas | ADR-002 | implemented (+ mock) |
| Renderer (lightweight) | Selectable software/CPU backend with no GL context | ADR-015 | implemented (`render/soft`) |
| Look & feel | Own rendering + themes, no native controls | ADR-003 | implemented (light/dark) |
| Text | stb_truetype, Latin/Cyrillic, UTF-8, no shaping/bidi | ADR-004 | implemented |
| Object model | Public base as the first field + reserve + runtime `size/version` check | ADR-005 | implemented |
| Events | Hybrid: `on_event` fans out into convenience methods by default | ADR-006 | implemented |
| Ownership | Hierarchy + weak focus/hover/popup; no refcount | ADR-007 | implemented (simplified, §5) |
| Errors | `cl_result_t` + thread-local last-error + `void` setters | ADR-008 | implemented |
| Threads | Single GUI thread + thread-safe `post`/`wakeup` | ADR-009 | implemented |
| Testability | Mock renderer + mock platform (headless) | ADR-010 | implemented (no mock font, §3.4) |
| Window | One OS window + overlay stack (menu/submenu/menubar/combobox/dialog/tooltip) | ADR-011 | implemented |
| DPI | Logical px in the API/layout; rounding when render commands are generated | ADR-013 | implemented |
| Text data | Glyph-cache key by (font, codepoint) | ADR-014 | atlas in the GL renderer; key by glyph_id — groundwork for shaping |

## 1. Layers and dependency direction

Rule: an upper layer depends on a lower one; nothing below depends on anything
above. Core (widget/layout/theme) depends at compile time only on Foundation and
on the renderer/platform **interfaces** — never on concrete SDL/GL/stb types. The
concrete backends are selected by CMake options and linked into `src/app`.

```text
┌─────────────────────────────────────────────────────────┐
│  App / Window            (public facade, event loop,     │
│                           timers, task queue)            │
├─────────────────────────────────────────────────────────┤
│  Widgets library    (Label Button CheckBox RadioButton   │
│                      Slider ComboBox Menu TextBox …)      │
├─────────────────────────────────────────────────────────┤
│  Widget system │ Layout (vbox/hbox/scrollview) │ Theme   │  ← CORE (no platform/GL)
├───────────────┴──────────────────────────┬──────────────┤
│  Renderer (iface)     Platform (iface)    │  Text/Font   │  ← abstractions
│   ├ GL 3.3 (+atlas)    ├ SDL2/GL          │  (stb_truetype)
│   ├ Software/CPU       ├ SDL2/software    │              │
│   └ Mock/record        └ Mock/headless    │              │  ← implementations (chosen via CMake/DI)
├─────────────────────────────────────────────────────────┤
│  Foundation: allocator · error · utf8 · mutex · version  │  ← depends on nothing
└─────────────────────────────────────────────────────────┘
      external: SDL2, OpenGL (own loader), stb_truetype (vendored)
```

## 2. Dependency graph (modules) — acyclic

An arrow `A ──► B` means “A depends on B”. Compile-time edges point at interfaces;
the concrete backends are wired in only by the composition point (`src/app`) at
link time.

```text
                         foundation            ◄── (no outgoing edges)
                          ▲   ▲   ▲
        platform-if ──────┘   │   └────── theme
        renderer-if ──────────┘
             ▲   ▲            ▲   ▲
   sdl ──────┘   │     gl ────┘   │      (backend ──► its own iface + foundation)
   mock-plat ────┘     mock-rend ─┘

        widget-system ──► platform-if, renderer-if, theme, text   [interfaces, compile-time]
        widget-system ──► widget_host-if  [narrow host interface (widget_host.h,
                             owned by the widget layer): dirty/focus/popup/clipboard/
                             IME; the window IMPLEMENTS it by embedding host as the
                             first field of cl_window — the cycle is gone, see §18]
             ▲
        { layout, scrollview }
             ▲
        widgets-lib
             ▲
        app/{application,window,timer} ──► widget-system, {interfaces}   [compile-time]
                                       --► sdl|mock-plat, gl|mock-rend    [LINK-TIME, composition point]
```

Core does not link SDL/GL: the SDL/GL symbols are present only in the `src/app` TUs
(bootstrap factories under `COPAL_ENABLE_SDL/OPENGL`) or are injected by the caller
through `cl_application_desc_t` (§3.9). There are no cycles: the widget layer sees
the window only through `cl_widget_host_t` — an interface declared in the widget
layer itself.

## 3. Modules and responsibilities

### 3.1 Foundation (`src/core/foundation`, public parts in `include/copal/`)
- `cl_allocator_t` (userdata + alloc/realloc/free); the default is a malloc wrapper
  (`allocator.c`); the `cl_alloc/realloc/free` wrappers record OOM in last-error.
- Errors (`error.c`): `cl_result_t`, `CL_ERROR_ABI_MISMATCH` and others, a
  **thread-local** last-error, a log callback, and `CL_ASSERT` in debug.
- Geometry/types (`types.h`): `cl_point/size/rect/insets/color` (RGBA8, not
  premultiplied), `cl_constraints_t`. Coordinates are `float`.
- UTF-8 (`utf8.c`): decoding/iteration over code points; rejects overlong forms,
  surrogates, `>U+10FFFF`, and truncated tails (substituting `U+FFFD`).
- Mutex (`mutex.c`, **new**): an opaque cross-platform mutex
  (pthread / CRITICAL_SECTION) — backing the application's thread-safe task queue.
- Version (`version.c`): `CL_VERSION_*`, `cl_version_runtime/string`.
- No dependencies; tested in isolation.

### 3.2 Platform interface (`copal/backend/platform.h`, `src/platform/`)
The `cl_platform_ops_t` operations table (ops pointers; a backend inherits
`cl_platform_t` as its first field; the SPI is public — an installed header with an
ABI handshake `struct_size`/`abi_version`, §13):
- window: `create_window` (returns an opaque `cl_platform_window_t*`),
  `destroy_window` (rolls back the native window when `cl_window_create` fails;
  optional), `set_title`, `drawable_size`, `scale` — window operations take a window
  handle, and events carry a `window_id` (groundwork for multi-window: the SPI won't
  have to be broken a second time; single-window backends may ignore the parameter
  and must accept NULL as “the sole window”);
- events: `poll`/`wait` → `cl_platform_event_t` (a neutral type, including
  `CL_PEV_TEXT_EDIT` for IME and `CL_PEV_EXPOSE` on surface damage; mouse events
  carry modifiers and a click counter); `present`;
- **`wakeup()`** — a thread-safe, coalescible way out of the nearest `wait`;
- text input/IME: `start_text_input`, `set_ime_rect` (position of the composition
  window);
- mouse cursor: `set_cursor` (system shapes `cl_cursor_t`; the window applies the
  hovered widget's shape, `cl_widget_set_cursor`);
- clipboard: `clipboard_get`/`clipboard_set`;
- GL: `gl_get_proc` (procedure address for the loader; NULL on non-GL backends);
- **`now_ms()`** — monotonic milliseconds (backing timers, §7);
- software: `lock_framebuffer`/`unlock_framebuffer` — hand out the window's CPU
  buffer (`cl_pixmap`: pixels/size/pitch + channel masks) for software rendering;
  NULL on the GPU path;
- `destroy`.
Implementations: **SDL2/GL** (`platform/sdl`, `cl_platform_sdl_create` — a window
with a GL context), **SDL2/software** (`cl_platform_sdl_soft_create` — a window
**without** GL: `lock_framebuffer` = `SDL_GetWindowSurface`, `present` =
`SDL_UpdateWindowSurface`; **needs no OpenGL**), and **Mock/headless**
(`platform/mock`, a scripted queue `cl_platform_mock_push`, a controllable clock
`cl_platform_mock_advance`, no window).
`wait` blocks via `SDL_WaitEvent(NULL)` (waits but does not dequeue the event —
`process_events` drains it); otherwise, under a steady event stream, the loop would
spin at 100% of one core instead of sleeping.

### 3.3 Renderer interface (`src/render/`)
Split into two parts so that GPU details do not leak to widget authors:
1. **The public `cl_paint_context_t`** (`render.h`, `paint_context.c`) — passed
   into `paint`. Drawing only: `fill_rect`, `fill_round_rect(r, radius)`,
   `stroke_round_rect(r, radius, width)`, `draw_text(font, utf8, pos, color)`,
   `draw_image(img, dst)` (an RGBA8 resource `cl_image_t`, image.h),
   `push_clip`/`pop_clip`, `push_transform(offset, scale)`/`pop_transform`
   (translate + uniform scale, also applied to the clip rects), and
   `push_opacity(alpha)`/`pop_opacity` (group alpha multiplication, with no
   intermediate buffer — overlaps within the group show through); plus read access
   to the theme (`theme`, `theme_color`). The device/frame/GPU resources are not
   reachable.
2. **The device/frame interface** (`copal/backend/renderer.h`; the SPI is public,
   with an ABI handshake `struct_size`/`abi_version`, §13) — owned by App/Window:
   `begin_frame(size, scale)`/`end_frame`, GPU-resource management, uploading glyphs
   into the atlas. Still not reachable by widget authors.
Implementations: **GL** (`render/gl`: GL 3.3 core, a custom loader `gl_loader.c` on
top of `third_party/GL`+`KHR`, an SDF shader for rounding/AA, a glyph atlas);
**Software/CPU** (`render/soft/renderer_soft.c`: the same 9 operations on the CPU —
the same rounding SDF+AA, ported per-pixel; text is done by blitting stb_truetype
coverage bitmaps out of a CPU glyph cache; a clip stack; the pixel buffer is
obtained from the platform via `lock/unlock_framebuffer`. It is platform-neutral,
built **always**, and **creates no GL context** → a fast flat startup and an order
of magnitude less memory, and it works over RDP and in CI); **Mock/record**
(`render/mock`: a list of draw commands `cl_mock_command_t` for deterministic
headless tests, no GL).

### 3.4 Text/Font (`src/text/`, `font.h`)
**Simpler than the design sketch.** Implemented:
- `font.c` on top of **stb_truetype** (`stb_impl.c` — the single TU with
  `STB_TRUETYPE_IMPLEMENTATION`): loading `.ttf/.otf` from file/memory, metrics
  (`cl_font_metrics_t`), and measuring via `cl_text_measure` and
  `cl_text_measure_bytes` (by byte length — for caret positioning) **without
  rasterization**;
- glyph rasterization and glyph caches live in the renderers (GL: the atlas; soft:
  coverage bitmaps); the cache key is a (font, **codepoint**) pair, because without
  shaping “1 code point = 1 glyph” holds (§12); a key by glyph_id is groundwork for
  the future (ADR-014). Lookup uses an open hash table; on overflow of the table
  (512 slots) or of the GL atlas the cache is **reset** and the string is
  re-rendered (before the reset GL flushes the accumulated batch — its quads still
  reference the old texels); an unrasterizable glyph is skipped without breaking the
  string. `cl_font_release` invalidates the caches through the `evict_font`
  operation (reuse of a font's address by the allocator no longer produces false
  hits).
The design's `GuiFontProvider`/`GuiShaper`/`GuiTextEngine`/`GuiGlyphCache` as
separate public entities **are not carved out**; their role is folded into `font.c`
+ the renderer's atlas. The extension points for FreeType/HarfBuzz are kept
conceptually (ADR-004/014). Limitations — §12.

### 3.5 Widget system (`src/widget/`, `widget.h`, `widget_impl.h`)
- `cl_widget_t` — the public base as the first field + a reserve
  (`CL_WIDGET_RESERVED = 20`); `cl_widget_vtable_t`; `cl_widget_class_t` (name,
  parent class, informational `type_id`, instance size, vtable) for RTTI and checked
  casts.
- Tree: `first_child`/`last_child`/`next_sibling`, `parent` (weak), `window` (weak
  back-ref), `app` (weak); add/remove/destroy.
- State: `rect` (absolute), `measured`/`pref_size`, `margin`/`align`/`flex`, flags
  (`VISIBLE`/`ENABLED`/`FOCUSABLE`/`DEAD`/`CLIP`; bit 3 is a reserve, the former
  `DIRTY`), `cursor`, `userdata`, `tooltip` (owned UTF-8).
- The event dispatcher (`widget.c`): `on_event` fans out into convenience methods by
  default (§6); clip-aware `paint`/hit-test (under `CL_WF_CLIP` children are clipped
  to `clip_rect`); `cl_widget_reveal` — a walk up the ancestors with a `reveal` hook.
- Invalidation: `cl_widget_invalidate` (paint → `window.dirty`),
  `cl_widget_invalidate_layout` (measure/arrange).

### 3.6 Layout (`src/layout/`, `layout.h`)
- Two measure/arrange passes in logical float px; a constraints model (min/max,
  `CL_UNBOUNDED`).
- Containers: `vbox`/`hbox` (spacing, padding, cross-align, flex weights);
  **ScrollView** (`scrollview.c`, `widgets/scrollview.h`) — two axes (opt-in
  `horizontal`), implementing the vtable hooks `clip_rect` (clipping the content) and
  `reveal`/scroll-to-view, with an opt-in `smooth` wheel animation (via a timer).
- Per-child attributes are set on the child (`cl_widget_set_flex/margin/align/…`).

### 3.7 Theme/Style (`src/theme/`, `theme.h`)
- `cl_theme_t`: color roles (`cl_color_role_t`: BACKGROUND, SURFACE(+HOVER/
  ACTIVE/RAISED), TEXT(+MUTED), ACCENT, BORDER, FOCUS_RING, SELECTION, SHADOW),
  built-in **light/dark** schemes (`cl_theme_set_variant`), corner radius, and a
  default font. `cl_text_style_t` (font/color/align) for text widgets.
- Widgets request a color by role in `paint` through `cl_paint_context_t`.

### 3.8 Widgets library (`include/copal/widgets/`, `src/widgets/`)
Implemented: **Label, Button, CheckBox, RadioButton** (mutual exclusion by a numeric
`group` id rather than a separate container), **Slider, ComboBox, Menu** (a popup via
an overlay), **TextBox** (single-/multi-line, password, readonly, `max_length`,
selection/clipboard, IME composition), **ScrollView**. The internal **tooltip**
bubble (`src/widgets/tooltip.c`) is not a public widget but an element of the
window's hover layer. Each public widget is a `*_desc_t` + `*_create` (§API).
Also implemented are Panel (a grouping surface), Spacer, RadioGroup (automatic mutual
exclusion), ImageView, List, ProgressBar, Menubar, and the modal MessageBox.

### 3.9 App & Window (`src/app/`, `application.h`, `window.h`, `timer.h`,
`animation.h`)
- `cl_application_t`: owns the platform, renderer, theme, allocator; the event loop
  (`run`/`step`/`quit`); **timers** (`timer.c`, §7); **animations**
  (`animation.c`, §7) — a shared ~60 Hz ticker on top of the timers, progress from
  `now_ms() - start` (not from a tick count), easing curves, cancellation with
  `on_done`, composition/chaining; a **thread-safe task queue** `cl_application_post`
  (mutex + FIFO, §7); the IME rect. The backends are DI from `cl_application_desc_t`
  **or** the bootstrap-TU built-in factories.
  **Backend selection:** `cl_application_desc.render_backend`
  (`CL_RENDER_AUTO`/`GL`/`SOFTWARE`) + a runtime override `COPAL_RENDER=software`
  for AUTO; in a `COPAL_ENABLE_SDL` build **without** `COPAL_ENABLE_OPENGL` only
  software is available (the GL renderer is not compiled, libGL is not linked —
  ADR-015). A built-in renderer is not bound to an injected platform that cannot
  serve it (software requires `lock_framebuffer`, GL requires `gl_get_proc`;
  otherwise `cl_application_create` → `CL_ERROR_UNSUPPORTED`). Under AUTO with the
  built-in backends, a failure to create a GL window falls back once to the software
  pair (`cl_app_software_fallback`); explicit GL and DI do not fall back. A failure
  of the lazy gl_init (shaders/function loading) is recorded in `cl_last_error`
  (CL_ERROR_RENDERER) and in the log.
- `cl_window_t`: the native window + GL context, the root widget (content) **and an
  overlay stack** (menus/submenus/dropdowns/modal dialogs; modal entries are
  barriers for light-dismiss) plus a separate **hover-tooltip** layer;
  focus/mouse-target/reveal; input in modal dialogs has content semantics (focus on
  click, pointer capture, hover/cursor, a Tab cycle, key bubbling up to the dialog
  root); a dirty flag + a **damage region** (the union bounding rect of the
  invalidations, §8.3). It owns the content, the overlays (except detachable menu
  entries), and the tooltip; it keeps a weak back-ref in the widgets. In the MVP —
  one window (ADR-011; a second one → `CL_ERROR_UNSUPPORTED`).

## 4. Ownership model

```text
cl_application_t
 ├─ owns platform, renderer, theme, allocator
 ├─ owns cl_window_t (one in MVP)
 │    ├─ owns native surface + GL context
 │    ├─ owns content: cl_widget_t → owns children (recursively)
 │    ├─ owns overlay popup (menu/combobox), if open
 │    └─ owns hover tooltip, if shown
 ├─ owns cl_timer_t[]        (FIFO list)
 ├─ owns cl_animation_t[]    (shared ticker — one of the timers)
 └─ owns posted-task queue   (from other threads)

Weak (raw pointers, nulled when the target is destroyed):
  widget.parent / widget.window / widget.app
  window.focus / mouse_target → widget
```

Rules:
- **A parent owns its children.** `cl_widget_add_child` transfers ownership;
  `cl_widget_remove_child` returns ownership to the caller; `cl_widget_destroy`
  destroys the subtree.
- **Weak references** do not own; when a widget is destroyed, its window-weak
  references (focus/mouse_target/popup-owner/tooltip-target) are nulled (§5).
- **No refcounting** (ADR-007).
- All allocations go through the application's `cl_allocator_t`; widgets are
  allocated zeroed (`cl_widget_alloc`, contract §9).
- **Live widgets are heap-only** (created by `cl_*_create`).

## 5. Destruction and callback safety (as implemented)

`cl_widget_destroy(w)` **detaches immediately, frees deferred** through the
application's DEAD queue. The order (`widget.c`, `application.c`):

- **No-op on repeat.** If `w` is `NULL` or already marked `CL_WF_DEAD`, the call
  does nothing: a repeated `destroy` is safe.
- **Detach + mark dead.** Save the `host` (before detach, while the window back-ref
  is still alive) → if there is a parent, `cl_widget_remove_child` (unlinks
  immediately, driving `focus_lost` through the detach) → `widget_mark_dead(w)`
  recursively over the subtree: null the window-weak references via
  `host->ops->widget_gone` (`focus`/`mouse_target`/hover/popup-owner/tooltip-target),
  clear `window`, and set `CL_WF_DEAD`. A dead node is invisible to hit-testing,
  events, and a repeated `add_child`.
- **Deferred free of an attached subtree.** If the node was in a window,
  `host->ops->defer_destroy` puts the subtree into the DEAD queue (`app->dead`); the
  memory is freed **at the end of the current loop iteration** — `cl_app_reap_dead`
  is called after `reap_overlay` and before rendering (the order in
  `cl_application_run`/`_step`, §7) and on application teardown. The reap drains the
  queue **to empty**, so destroying another detached tree **from** a destroy callback
  is safe. An already-detached subtree (no window) has no references from the loop
  and is freed immediately (`cl_widget_free_subtree`).
- **Callback-safety guarantee.** Handles stay valid until the end of the iteration,
  so destroying **any** widget from any callback (event, timer, animation) is safe;
  the free walk itself is bottom-up (`cl_widget_free_subtree`: children →
  `vtable->destroy` → `tooltip` → the node).
- **Weak nulling on detach.** `remove_child` calls `cl_widget_set_window(child,
  NULL)`, so the clearing of window-weak references is also duplicated in the detach
  branch of `cl_widget_set_window` — otherwise focus/mouse_target/popup/tooltip could
  point at an unlinked node.
- **Deferral of overlays/timers/animations:**
  - **Overlay/popup** (menu/combobox): closing is deferred by the `overlay_closing`
    flag and performed in `cl_window_reap_overlay` **after** dispatch, before
    rendering. So a menu-item/selection handler can safely request the closing of its
    own popup.
  - **Timers/animations**: freeing during the fire pass is deferred and performed by
    a `reap` after the pass; reentrancy (a nested `step`/`run`) is protected — the
    reap only runs on the outermost pass (§7).
- **The callback is the last action.** The built-in widgets (button, checkbox,
  slider, radio, combobox, textbox, menu) do not touch their own state after invoking
  the user callback — destroying the widget from its callback is safe provided the
  handler then returns `true` (see the `cl_widget_destroy` contract in widget.h and
  API §6).
- **Flags.** `CL_WF_DEAD` (bit 4) is implemented and used as above; bit 3 (the former
  `DIRTY`) is reserved and not implemented; the `generation` field from the 0.2 sketch
  was removed — there is no weak-handle validation (ADR-007, no refcount).

## 6. Error model and events

- `cl_result_t` for fallible operations (create app/window/timer, load font,
  add-item, ABI mismatch). Constructors on failure → `NULL` + thread-local
  last-error.
- Setters are `void`: they validate/clamp; in debug — `CL_ASSERT` on gross errors.
- `cl_last_error()`, `cl_result_string()`, `cl_set_log_callback()`. The log callback
  is process-wide and unique (the per-app `log_fn` was removed); internal diagnostic
  points go through `cl_log()` (foundation), with a WARN/ERROR fallback to stderr.

**Event hybrid (ADR-006):** the vtable holds `on_event` + the concrete slots. The
dispatcher by default fans a `cl_event_t` out into `mouse_down/up/move/wheel`,
`key_down/up`, `text_input`, **`text_edit`** (IME pre-edit), `focus_gained/lost`. A
widget author overrides **either** `on_event` **or** the individual methods. An
`on_event` that returned `true` stops further propagation.

## 7. Threading model, timers, tasks

- A single GUI thread owns app/window/widgets/renderer.
- **`cl_application_post(app, fn, user)`** is thread-safe: it puts the task into the
  queue under a mutex and wakes the loop via `platform.wakeup()`; the queue is
  **detached wholesale under the lock and executed without the lock** (a task may post
  a new one — it is drained on the next pass, with no deadlock); FIFO; tasks not
  drained by destroy are discarded. The mutex is injectable (`cl_mutex_iface_t` in the
  app desc): the hosted build defaults to pthread / a critical section; a freestanding
  build injects one (on UEFI, `RaiseTPL`/`RestoreTPL` — §19). The node is allocated
  outside the locked region, so the lock may run at a raised TPL.
- **Animations** (`animation.c`, `animation.h`): all of the application's live
  animations share one ~60 Hz repeat timer (created with the first one, dropped when
  the list empties — the idle loop keeps sleeping). Progress is from elapsed time, not
  from a tick count: coalescing of the ticker “jumps” an animation forward, and the
  final call is always at t = 1.0. Reentrancy follows the timers' pattern
  (`anim_firing` + a deferred reap); an animation frees itself on completion/cancel
  (after `on_done` the handle is invalid), with the remainder freed on application
  teardown without callbacks.
- **Timers** (`timer.c`): an app-owned FIFO list; they fire on the GUI thread between
  dispatch and rendering; `wait` blocks until the nearest deadline
  (`cl_app_timers_timeout`), polled by `cl_app_timers_poll`. Time is monotonic
  `platform.now_ms`. A one-shot with `interval_ms==0` fires on the next poll; a repeat
  is floored to 1 ms and coalesces missed ticks. Freeing during the fire pass is
  deferred (§5); the window is destroyed **before** `cl_app_timers_free_all` so that
  widgets can cancel their timers against a live list.
- GPU/font resources — GUI thread only; from another thread only `post` is allowed.

## 8. Data flows

### 8.1 Event flow
```text
OS event → SDL2 → platform backend → cl_platform_event_t → app loop → window
  → hit-test (overlay/tooltip on top → then content; focus; mouse-target;
     light-dismiss popup on a click outside / Esc)
  → target: on_event(cl_event_t*)  [default → mouse_*/key_*/text_input/text_edit/focus_*]
  → the widget updates its state → invalidate(paint|layout)
  → run tasks → poll timers → reap overlay → (layout if needed) → paint → present
```

### 8.2 Layout flow
```text
invalidate_layout(w) → measure-dirty on w and its ancestors up to the root
  next frame: measure(constraints) [2 passes] → arrange(final rect) → paint
```

### 8.3 Rendering flow + DPI rounding (ADR-013)
```text
window.dirty → [damage region?] → begin_frame(size, scale)
  → content.paint(cl_paint_context_t): fill/stroke_round_rect (SDF), draw_text
    (quads from the glyph atlas), push/pop_clip; then overlay popup and tooltip on top
  → end_frame → present / present_region     (idle with no dirty → no frame is drawn)
```
**Rounding contract:** layout stays entirely in logical float px without rounding;
rounding happens only when render commands are generated, translating logical edges
to physical ones (pixel snapping by absolute edges, so that shared boundaries of
adjacent widgets line up); a hairline is ≥ 1 physical pixel.

**Damage regions (software path).** `cl_widget_invalidate` reports the widget's
rectangle (+1 px for AA) to the window via the `damage` host operation; the window
accumulates the union bounding rect. If all of the frame's invalidations came with
rects and the renderer supports `set_damage` (software: its surface persists between
frames), only the region is cleared and drawn, and SDL blits it with
`SDL_UpdateWindowSurfaceRects` (the `present_region` platform operation). **The paint
walk culls to the region:** `cl_widget_do_paint` skips a widget's own drawing when
its rect does not touch the damage, and skips a clipping container
(`CL_WF_CLIP` — its subtree is bounded by the clip) as a whole subtree; a
non-clipping container still walks its children (a child may extend beyond its parent
and gets culled itself). A full redraw remains for the first frame, layout changes,
overlays, and `cl_window_mark_dirty`; GL always draws the whole frame (double
buffering does not preserve the back buffer).

**Software-path pacing.** `SDL_UpdateWindowSurface` is a blit with no vsync, so the
presents (full and partial) are throttled by a frame limiter on `now_ms` at the
display's refresh rate (queried at window creation, with a 60 Hz fallback). This is a
deliberate choice over SDL_Renderer+PRESENTVSYNC: the damage regions rely on the
persistence of the window surface (SDL_LockTexture does not preserve the pixels), and
a real vsync would cost a second present pipeline. Slight tearing is a known, accepted
artifact of the software path; the GL path synchronizes via
SDL_GL_SetSwapInterval(1).

### 8.4 Text rendering flow
```text
draw_text — a single line (wrapping by width / `\n` is implemented only inside
TextBox, which splits the text into lines itself and calls draw_text per-line) →
  for each code point: lookup in the renderer cache by (font, codepoint);
  miss → stb rasterization → GL: upload to atlas / soft: coverage bitmap →
  quad/blit.
Measurement — by metrics/advance, without rasterization; max_width in
cl_text_measure* is reserved and still ignored.
```

## 9. The C object model (details)

The application sees `cl_widget_t` opaquely (`copal/copal.h` does not include
`widget_impl.h`); widget authors include `copal/widget_impl.h` and inherit by
embedding the base **as the first field**.

```text
cl_widget_class_t {              // one per type (static, const)
  const char *name;
  const cl_widget_class_t *base; // chain of ancestors; NULL for leaf classes
  uint32_t type_id;              // INFORMATIONAL (FourCC) — not needed for the cast
  size_t instance_size;
  const cl_widget_vtable_t *vtable;
}
struct cl_widget {               // public base, first field of the derived struct
  const cl_widget_class_t *cls;  // basis of the cast (class pointer identity)
  cl_application_t *app;         // weak
  cl_window_t *window;           // weak back-ref
  cl_widget_t *parent;           // weak
  cl_widget_t *first_child, *last_child, *next_sibling;
  cl_rect_t rect; cl_size_t measured, pref_size;
  cl_insets_t margin; cl_align_t align_h, align_v; float flex;
  uint32_t flags;                // VISIBLE|ENABLED|FOCUSABLE|DEAD|CLIP (bit 3 — reserved, former DIRTY)
  uint32_t cursor;               // cl_cursor_t — cursor shape on hover
  void *userdata;
  char *tooltip;                 // owned UTF-8 or NULL
  unsigned char reserved[CL_WIDGET_RESERVED /* 20 */];
}
```

**vtable slots** (all with a first parameter `cl_widget_t*`; NULL = default
behavior):
`destroy`, `measure`, `arrange`, `paint`, `clip_rect`, `reveal`, `hit_test` (NULL =
rect), `on_event` (NULL = fan-out), `mouse_down/up/move/wheel`, `key_down/up`,
`text_input`, `text_edit`, `focus_gained/lost`. `clip_rect`, `reveal`, `mouse_wheel`,
`text_edit` were added in Stages 6/7 for ScrollView, scroll-to-view, the wheel, and
IME; the other widgets do not set them (NULL-compatible).

**Checked cast.** `CL_WIDGET_CAST(name, w)` is checked, **always NULL on a mismatch**
(a walk over `cls->base`); `CL_WIDGET_CAST_UNCHECKED` is the fast path, UB on the
wrong type. Casts rely on **class-pointer identity** (`&name##_class`), so `type_id`
is not needed for correctness and creates no risk of collisions between libraries.

**The `cl_widget_init_base(w, app, cls)` contract.** Sets `cls`/`app`, the flags
`VISIBLE|ENABLED`, and the default aligns. `cl_widget_alloc` allocates `instance_size`
zeroed and calls `init_base`; user initialization follows.

**ABI handshake (ADR-005).** Each `*_desc_t` carries `abi_version`+`struct_size`,
stamped by macros: `CL_*_DESC_INIT_FIELDS` — for a compound literal
(`{ ..._FIELDS, .field = ... }`), and the full `CL_*_DESC_INIT` (= `{ ..._FIELDS }`)
for a default desc; both are defined for every `*_desc_t`. `cl_*_create` checks the
header tail-tolerantly: the same major version and a size no smaller than the service
header → the desc is normalized into the full structure (a missing tail = defaults, an
extra one is ignored); otherwise `NULL` + `CL_ERROR_ABI_MISMATCH`. Ops tables must
carry the entire base set (a shorter one is rejected). Growing the base/vtable is a
breaking change (bumps the major/`SOVERSION`) and requires recompilation.

**The C++ authoring path.** C++ slot functions must have C linkage; they are declared
**exactly** with `cl_widget_t*` as the first parameter, with the downcast inside the
body (otherwise UB on an incompatible function-pointer type).

## 10. Virtual methods (rationale)

| Method | Virtual? | Why |
|-------|-------------|--------|
| `destroy`, `measure`, `arrange`, `paint` | yes | resources/size/layout/drawing are type-specific |
| `clip_rect`, `reveal` | yes (opt-in) | ScrollView: clipping children and scroll-to-view |
| `hit_test` | yes (default = rect) | non-standard shapes |
| `on_event` | yes | input extension point (hybrid) |
| `mouse_down/up/move/wheel`, `key_down/up` | yes (convenience) | selective overriding |
| `text_input`, `text_edit` | yes (convenience) | text input and IME composition |
| `focus_gained/lost` | yes | reacting to focus |

## 11. Invalidation

- Paint invalidation → `window.dirty`; coalesced per frame.
- Layout invalidation → measure-dirty on the widget and its ancestors; before the
  next paint — a layout pass. Invalidations during a pass are deferred to the next
  frame.

## 12. Text limitations (honestly)

stb_truetype rasterizes glyphs but does no shaping, bidi, mark-positioning, or auto
fallback. It is correct for **NFC-precomposed** Latin/Cyrillic under “1 code point →
1 glyph”. It is incorrect for combining marks/NFD (a diacritic as a separate glyph),
Arabic/Indic scripts (reordering/ligatures), mixed LTR/RTL (bidi), and glyphs missing
from the font (“tofu”, no auto fallback). The caret/selection are indexed by code
point. The extension paths (FreeType/HarfBuzz) are laid in conceptually
(ADR-004/014).

## 13. Extension points

- A custom widget: include `widget_impl.h`, embed the base as the first field, fill in
  the static `cl_widget_class_t` + `cl_widget_vtable_t` (C and C++).
- A custom renderer / platform: the SPI is published in the installed headers
  `copal/backend/platform.h` and `copal/backend/renderer.h` (they are not part of the
  umbrella `copal.h`). A backend embeds `cl_platform_t` / `cl_renderer_t` as the first
  field of its structure and fills in a static ops table — its first fields
  `struct_size`/`abi_version` form the ABI handshake: `cl_application_create()`
  rejects a table built against different headers with `CL_ERROR_ABI_MISMATCH` — and
  injects the object through `cl_application_desc_t`. Ownership passes to the
  application only on a successful create (see application.h).
- Allocator, theme.

## 14. Lifecycles

```text
Application:
  cl_application_create(&desc) → allocator, platform, renderer, theme, task-mutex;
    backends — DI from desc or built-in factories
  → run() | loop: wait(until the nearest timer) → process_events → run_tasks →
     poll_timers → reap_overlay → (layout) → paint(dirty) → present
  → quit() sets the exit flag
  → destroy() → the window first (widgets cancel their timers) → timers → tasks →
     theme/renderer/platform

Window:
  cl_window_create(app, &desc) → platform.create_window + GL context
  → set_content(root) → the window owns root; back-ref window on the subtree
  → events/resize/paint; overlay popup and tooltip on top of content

Widget:
  alloc(zero) → init_base(app, cls) → user-defined initialization
  → add_child (ownership → parent; back-ref window)
  → measure/arrange/paint; events via on_event
  → destroy (bottom-up; nulling of weak refs) → vtable->destroy → free
```

## 15. Directory structure and CMake

The actual file tree and responsibilities are in [STRUCTURE.md](STRUCTURE.md). A
brief take on the build:
- Target-based, the `copal` target (alias `copal::copal`), static/shared, generation
  of an export header (`CL_API`).
- Options: `COPAL_BUILD_SHARED/EXAMPLES/TESTS`, `COPAL_ENABLE_SDL/OPENGL`,
  `COPAL_FETCH_SDL2`, `COPAL_ENABLE_SANITIZERS/INSTALL` (see STRUCTURE §5).
- `find_package(Threads REQUIRED)` (backing the mutex/`cl_application_post`);
  SDL2+OpenGL are linked only when both `COPAL_ENABLE_SDL`+`COPAL_ENABLE_OPENGL` are
  set, in the `src/app` TUs.
- `find_package(copal CONFIG REQUIRED)` + `target_link_libraries(app PRIVATE
  copal::copal)`; `add_subdirectory` is supported.

## 16. ADR registry

- **ADR-001** SDL2 behind a `platform` interface (replaceable; there is a mock).
- **ADR-002** OpenGL 3.3 core behind an interface; SDF primitives + a glyph atlas.
- **ADR-003** Own rendering + themes (light/dark); no native controls.
- **ADR-004** stb_truetype; no shaping/bidi/mark-positioning; interfaces for
  FreeType/HarfBuzz reserved conceptually.
- **ADR-005** A public base + a reserve + an ABI handshake `abi_version`/`struct_size`
  in `*_desc_t`, checked in `cl_*_create`. Growing the base/vtable → recompilation.
- **ADR-006** Hybrid event dispatch (`on_event` + convenience slots).
- **ADR-007** Hierarchical ownership + weak focus/mouse-target/popup/tooltip; no
  refcount. Widget destruction is deferred: `destroy` detaches and marks `CL_WF_DEAD`
  immediately, the memory is freed from the DEAD queue at the end of the loop
  iteration; overlays/timers/animations are deferred separately (§5).
- **ADR-008** `cl_result_t` + thread-local last-error + `void` setters;
  `CL_ERROR_ABI_MISMATCH`.
- **ADR-009** A single GUI thread + a thread-safe `cl_application_post` (mutex+FIFO)
  and a platform-neutral `wakeup()`; timers on the GUI thread.
- **ADR-010** A mock renderer + a mock platform for headless tests. **The mock font is
  not implemented**: text is measured with the real stb_truetype (the tests load the
  system DejaVu optionally; the render checks are under `if (font)`).
- **ADR-011** One OS window in the MVP; menu/combobox/tooltip are an overlay layer
  inside the window (clipped to the window bounds, light-dismiss). The seams for
  multi-window are preserved (`cl_window_t` is separated from the app; resources are at
  the app level). A second window → `CL_ERROR_UNSUPPORTED`.
- **ADR-012** Names: types `cl_*_t`, functions `cl_*`, macros/enums `CL_*`; CMake —
  `copal::copal`/`COPAL_*`. License — **GPL-3.0-or-later**.
- **ADR-013** Logical px in the API/layout; rounding by absolute edges when render
  commands are generated.
- **ADR-014** The renderers' glyph caches are keyed by (font, **codepoint**) —
  equivalent to glyph_id while “1 code point = 1 glyph” holds (§12); the switch to
  glyph_id comes together with future shaping (HarfBuzz).
- **ADR-015** **The software/CPU renderer as a selectable backend** (`render/soft`).
  It creates no GL context and **does not link libGL in a `COPAL_ENABLE_SDL` build
  without `OPENGL`** → truly “lightweight”: a fast flat startup, an order of magnitude
  less memory (measured: calc software 18.5 MB on Windows / 8 MB on a Linux dummy
  against ~70–83 MB for GL), works over RDP and in CI with no GPU driver. The cost is a
  speed ceiling on a heavy/animated UI. GL remains the default for AUTO when present;
  software is selected via `render_backend`/`COPAL_RENDER`.
- **ADR-016** **Freestanding core (`COPAL_HOSTED=OFF`)** for UEFI/bare-metal (§19).
  The core carries copal-namespaced str/math/format helpers (no libc/libm), gates
  the hosted paths (default allocator, file font loaders, stderr log), and makes the
  task-queue mutex and the assert handler injectable. The residual external surface
  is `memcpy/memmove/memset`, enforced by a CI job.

## 17. What was added in Stages 6–7 (delta to the MVP design)

- **Foundation**: an opaque mutex (`mutex.c`).
- **Platform**: `now_ms` (a monotonic clock), `set_ime_rect`, the `CL_PEV_TEXT_EDIT`
  event; a controllable clock and queue in the mock.
- **Application**: timers (`timer.c`), a thread-safe task queue
  (`cl_application_post`).
- **Window**: an overlay stack (menus + submenus + modal dialogs; opaque ownership:
  entries own their popups, except submenus/menubar menus — those are detached to the
  owner for reuse); a hover-tooltip layer above the content and popups; scroll-to-focus
  (reveal when focus is moved); hover tracking (`CL_EVENT_MOUSE_ENTER/LEAVE` —
  delivered to the hovered widget without bubbling; under a drag capture hover is
  frozen, and a popup resets it).
- **Widget/vtable**: the slots `clip_rect`, `reveal`, `mouse_wheel`, `text_edit`,
  `mouse_enter`/`mouse_leave`; the `CL_WF_CLIP` flag; the `tooltip` field; the `app`
  back-ref; `last_child`.
- **Layout**: ScrollView (two axes, clip/reveal, an opt-in `smooth` animation).
- **TextBox**: a multi-line mode (word wrap, up/down navigation, vertical scroll), IME
  composition (preedit at the caret), `cl_text_measure_bytes` for the caret.
- **Theme**: the dark scheme (`cl_theme_set_variant`), the `SURFACE_RAISED`/`SHADOW`
  roles, the radius.
- **Renderer (optimization stage)**: the **software/CPU backend** (`render/soft`,
  ADR-015) selectable via `render_backend`/`COPAL_RENDER`; the SDL platform and its
  selection decoupled from OpenGL (a `COPAL_ENABLE_SDL` build without `OPENGL` =
  software with no libGL). In the GL renderer: hoisting of per-frame state
  (projection/program/atlas once per frame), reuse of the text VBO; in the font — an
  advance cache over Latin-1. In the SDL platform: `wait` via `SDL_WaitEvent(NULL)` —
  the loop's busy-spin at idle is eliminated.

## 18. Self-check

- The graph is acyclic (§2): the widget layer depends on its own `widget_host.h`
  (dirty/focus/popup/clipboard/IME), and the window implements this interface by
  embedding `cl_widget_host_t` as the first field of `cl_window` — no file under
  `src/widget`, `src/widgets`, `src/layout` includes `app_internal.h`. The backends
  come from `src/app` at link time.
- Ownership and lifetimes are defined (§4/§5): weak back-refs, nulling on detach,
  deferred widget freeing (the DEAD queue: destroy detaches immediately, the memory
  lives until the end of the loop iteration), and the deferral of overlays/timers.
- Core is testable without a window (§3.2/§3.3): a mock platform + renderer; text
  measurement is renderer-free.
- The public API does not depend on platform types (§1); widget authors get
  `cl_paint_context_t`, not the device (§3.3).
- A custom widget with no private structures (§9, `widget_impl.h`).
- UTF-8 is correct (§3.1); text goes by `glyph_id` (ADR-014); the limitations are
  honest (§12).
- Renderer/platform are replaceable (DI, §3.9); DPI rounding is specified (§8.3).
- Casts with no UB on function-pointer types (§9); the ABI handshake in `*_desc_t`.
- Builds as C; the headers are C++-compatible (`extern "C"`); export via `CL_API`.

## 19. Freestanding / UEFI

copal's core (foundation + software renderer + widgets + layout + text) builds in a
**freestanding** environment (`-ffreestanding`, no hosted C runtime) for targets
like UEFI, where the software renderer draws into a linear framebuffer (GOP) - the
only viable model before an OS and its GL driver exist. The GL and SDL backends are
hosted-only and stay out of such a build.

**The `COPAL_HOSTED` axis.** The CMake option `COPAL_HOSTED` (default ON) defines
`CL_HOSTED`. With it OFF the paths that need a hosted runtime are compiled out:
- the default malloc allocator - `cl_allocator_default()` returns NULL, so the
  embedder must inject one via `cl_application_desc_t.allocator`;
- the file/system font loaders - `cl_font_load_file`/`cl_font_load_system` return
  `CL_ERROR_UNSUPPORTED`; load fonts from memory with `cl_font_load_memory`;
- the stderr log fallback - diagnostics go only to a `cl_set_log_callback` sink;
- the built-in task-queue mutex - inject one via `cl_application_desc_t.mutex`.

**No libc/libm dependency.** The core carries copal-namespaced helpers so it
references no libc str*/math symbol: `cl_strlen`/`cl_strcmp` (`cstr.c`), the whole
math surface the renderer and stb_truetype use (`fmath.c`: sqrt/floor/ceil/fabs and
the SDF-path fmod/cos/acos/pow), and a minimal `cl_vsnprintf` (`format.c`) for the
log. libm is not linked at all; the sqrt/abs builtins lower to hardware ops under
`-fno-math-errno`.

**The shim contract.** After the gates the freestanding core references only three
external symbols - `memcpy`, `memmove`, `memset` - which the compiler emits
implicitly (struct copies, initialization) and every UEFI toolchain provides (EDK2
`BaseMemoryLib`, gnu-efi). A CI job (`scripts/check-freestanding-symbols.sh`) builds
`COPAL_HOSTED=OFF` and fails if any symbol escapes that set, so a new hosted
dependency cannot slip in.

**Backend responsibilities (out of tree).** The UEFI backend is the app's, the way
the SDL backend is in-tree. It injects: an allocator (AllocatePool/FreePool); a
mutex (`cl_mutex_iface_t` over `RaiseTPL`/`RestoreTPL` - a TPL notify callback can
post into the loop, so the queue still needs mutual exclusion, and
`cl_application_post` allocates outside the locked region so the lock may run at
raised TPL); a platform over GOP plus the input/timer protocols; an embedded font;
and optionally an assert handler (`cl_set_assert_handler`). The canonical
freestanding flag set is the one the CI check applies
(`scripts/check-freestanding-symbols.sh`: `-ffreestanding -fno-math-errno
-D_FORTIFY_SOURCE=0 -fno-stack-protector`, plus function/data sections) - treat
the script as the source of truth; a UEFI target adds the ABI set on top
(`-mno-red-zone`, PE/COFF, MS x64).
