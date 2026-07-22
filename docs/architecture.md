# Architecture

How copal is put together: the layer stack, who owns what, how the single GUI thread drives the loop, and the settled design decisions behind it all.

## Overview

copal is a small stack of layers with a strict downward dependency rule: each layer uses the one below it and nothing points back up. The core (widgets, layout, theme, text) is written against platform and renderer *interfaces* only — concrete SDL, OpenGL, and stb types never leak into it. Backends are wired in at the composition point (`src/app`) or injected by the caller.

```text
┌──────────────────────────────────────────────────────────┐
│  App / Window      event loop · timers · animations ·     │
│                    task queue · overlay & tooltip layers  │
├──────────────────────────────────────────────────────────┤
│  Widgets           label button checkbox slider combobox  │
│                    menu textbox scrollview …              │
├──────────────────────────────────────────────────────────┤
│  Widget system │ Layout (vbox/hbox/scroll) │ Theme        │ ← core, no platform/GL
├────────────────┴───────────────────────────┬─────────────┤
│  Renderer iface        Platform iface       │  Text/Font  │ ← abstractions
│   ├ GL 3.3              ├ SDL2/GL            │ stb_truetype│
│   ├ Software/CPU        ├ SDL2/software      │             │
│   └ Mock/record         └ Mock/headless      │             │ ← impls (CMake / DI)
├──────────────────────────────────────────────────────────┤
│  Foundation   allocator · error · utf8 · mutex · version  │ ← depends on nothing
└──────────────────────────────────────────────────────────┘
   external: SDL2 · OpenGL (own loader) · stb_truetype (vendored)
```

## Dependency direction

The module graph is acyclic. An upper layer depends on a lower one; nothing below depends on anything above.

- The core (widget / layout / theme / text) depends at compile time only on Foundation and on the platform/renderer **interfaces** (`copal/backend/platform.h`, `copal/backend/renderer.h`) — never on concrete SDL/GL/stb types.
- Concrete backends are chosen by CMake options and linked into `src/app`, or injected by the caller through `cl_application_desc_t`. The SDL/GL symbols live only in the `src/app` bootstrap TUs, so the core never links SDL or libGL.
- The widget layer sees the window only through a narrow **host interface** (`src/widget/widget_host.h`): dirty/focus/popup/clipboard/IME. The window *implements* it by embedding `cl_widget_host_t` as its first field. That inversion is what keeps the graph free of the widget↔window cycle.

## Module map

| Area | Responsibility |
|------|----------------|
| `src/core/foundation` | Allocator, `cl_result_t` + thread-local last-error, geometry/color types, UTF-8, opaque mutex, ABI handshake, libc-free str/math/format helpers, version. Depends on nothing. |
| `src/widget` | `cl_widget_t` base, vtable dispatch, RTTI/checked casts, tree, invalidation, clip-aware paint/hit-test, event fan-out. Plus `widget_host.h`. |
| `src/layout` | `vbox`/`hbox` measure/arrange (flex, padding, spacing, align) and `scrollview` (two axes, clip, reveal). |
| `src/theme` | Built-in light/dark schemes, color roles, corner radius, default font, `cl_text_style_t`. |
| `src/render` | Public `cl_paint_context_t` + internal device/frame interface, `cl_image_t`, and three backends: `gl` (GL 3.3 + SDF + glyph atlas), `soft` (CPU rasterizer, no GL, always built), `mock` (draw-command recorder). |
| `src/text` | Font loading, metrics, and measurement on top of `stb_truetype`; single implementation TU. |
| `src/platform` | Interface wrapper plus `sdl` (GL window and software window) and `mock` (headless, scripted events, controllable clock). |
| `src/widgets` | Concrete widget implementations plus the internal tooltip bubble. |
| `src/app` | Composition point: application, event loop, window + overlay/tooltip stack, timers, animations, task queue. Binds the concrete backends. |
| `include/copal` | Public headers; `copal.h` umbrella, `widget_impl.h` and `backend/*` as opt-in extension headers. |

## Ownership and lifetimes

- **A parent owns its children.** `cl_widget_add_child` transfers ownership; `cl_widget_remove_child` returns it to the caller; `cl_widget_destroy` destroys the subtree. The application owns the window; the window owns the content root, open overlays, and the tooltip. Live widgets are heap-only, allocated zeroed through the application's `cl_allocator_t`.
- **Everything else is a weak reference** — raw pointers, no refcount. `parent`, `window`, `app` back-refs and the window's `focus` / `mouse_target` / popup-owner / tooltip-target are nulled the moment their target is destroyed.
- **`cl_widget_destroy` detaches immediately, frees deferred.** It unlinks the subtree from the tree and marks it `CL_WF_DEAD` at once (nulling every weak ref that pointed into it, driving `focus_lost` on the way out), then queues the memory on the app's DEAD queue. The queue is reaped at the end of the loop iteration, drained to empty — so destroying *any* widget from *any* callback (event, timer, animation) is safe, including destroying a second tree from a destroy callback.
- **Repeat destroy is a no-op.** A `NULL` or already-dead widget is ignored. A dead node is invisible to hit-testing, events, and re-`add_child`.

Overlays, timers, and animations follow the same defer-then-reap pattern: closing a popup or freeing a timer/animation from within its own callback is deferred to a reap pass after dispatch, so it is always safe.

## Threading

- A **single GUI thread** owns the application, window, widgets, and renderer. Layout, paint, GPU and font resources are that thread only.
- The **only thread-safe entry point** is `cl_application_post(app, fn, user)`: it enqueues a task under a mutex and wakes the loop through the platform's `wakeup()`. The queue is detached wholesale under the lock and run without it (FIFO; a task may post another, drained next pass). The mutex is injectable (`cl_mutex_iface_t`) for freestanding builds; hosted builds default to pthread / a critical section.
- From any other thread, `post` is the whole contract — nothing else may touch app state.

## Rendering and DPI

Three interchangeable renderers implement the same interface:

| Renderer | Notes |
|----------|-------|
| **GL** | OpenGL 3.3 core, in-house loader, SDF shader for rounded corners + AA, glyph atlas. Draws the whole frame each time (back buffer is not preserved); vsync via swap interval. |
| **Software / CPU** | The same primitives on the CPU, per-pixel SDF + AA, glyphs blitted from a coverage-bitmap cache. Creates **no GL context**, links no libGL, and is **built always** — fast flat startup, far less memory, works over RDP and in CI. |
| **Mock / record** | Records draw commands for deterministic headless tests. No GL. |

Backend choice: `cl_application_desc.render_backend` (`CL_RENDER_AUTO` / `CL_RENDER_GL` / `CL_RENDER_SOFTWARE`) plus a runtime `COPAL_RENDER=software` override for AUTO. In an SDL build without OpenGL only software exists. Under AUTO with the built-in backends, a failed GL window falls back once to the software pair; explicit GL and injected backends do not.

**DPI rounding.** The API and layout work entirely in logical float pixels — no rounding. Rounding happens only when render commands are generated, snapping logical edges to physical ones by *absolute* edge so adjacent widgets share a boundary; a hairline is at least one physical pixel.

**Damage regions (software path).** Each `cl_widget_invalidate` reports its rectangle to the window, which accumulates the union bounding rect. When every invalidation of a frame carried a rect, the software renderer clears and redraws only that region (its surface persists between frames) and blits just the region. The paint walk culls to the damage: a widget skips its own drawing when its rect does not touch the region, and a clipping (`CL_WF_CLIP`) container is skipped as a whole subtree. Full redraws remain for the first frame, layout changes, overlays, and explicit dirtying. GL always redraws the whole frame.

## Text

Text runs on **stb_truetype** with UTF-8 input under a strict **"1 code point = 1 glyph"** model. Fonts load from file or memory; measurement is metrics-only (no rasterization); glyph rasterization and caches live in the renderers (GL atlas, software coverage bitmaps), keyed by (font, codepoint). Caret and selection are indexed by code point.

The limitation is real and stated plainly: there is **no shaping, no bidi, no mark positioning, and no font fallback.** This is correct for NFC-precomposed Latin/Cyrillic; it is wrong for combining marks/NFD, Arabic/Indic scripts, mixed LTR/RTL, and glyphs missing from the font (no auto-fallback "tofu"). FreeType/HarfBuzz extension points are kept conceptually but not built.

## Design decisions

| Axis | Decision |
|------|----------|
| Platform | SDL2 behind a `platform` interface (plus a mock). |
| Renderer | OpenGL 3.3 core behind an interface: SDF primitives + glyph atlas. |
| Lightweight renderer | Selectable software/CPU backend, no GL context, always built. |
| Freestanding | `COPAL_HOSTED=OFF` core for UEFI/bare-metal; libc/libm-free, injectable allocator/mutex/platform, software renderer into a GOP framebuffer. |
| Look & feel | Own rendering + light/dark themes; no native controls. |
| Text | stb_truetype, UTF-8, no shaping/bidi/fallback. |
| Object model | Public base as the first field + reserve + runtime ABI (`abi_version`/`struct_size`) check. |
| Events | Hybrid: `on_event` fans out into convenience slots by default. |
| Ownership | Hierarchy + weak focus/hover/popup refs; no refcount; deferred destroy. |
| Errors | `cl_result_t` + thread-local last-error + `void` setters. |
| Threads | Single GUI thread + thread-safe `post` / `wakeup`. |
| Testability | Mock renderer + mock platform for headless tests. |
| Window | One OS window + an overlay stack (menu/combobox/dialog/tooltip). |
| DPI | Logical float px in the API/layout; rounding only when render commands are generated. |
| Glyph cache | Keyed by (font, codepoint) — groundwork for a glyph_id switch under future shaping. |

---
*See also: [Extending](./extending.md) · [Building](./building.md) · [API](./api.md) · [Widgets](./widgets.md)*
