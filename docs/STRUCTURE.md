<p align="right"><b>English</b> | <a href="./ru/STRUCTURE.md">Русский</a></p>

# copal — Source tree structure

Status: **current** (matches the code as of Stage 7). Version: 1.0.
Builds on [ARCHITECTURE.md](ARCHITECTURE.md), [CODESTYLE.md](CODESTYLE.md),
[API.md](API.md).

This document describes the **actual** repository tree and each file's
responsibility — not a sketch. Sections that started as design drafts
(ARCHITECTURE §15, early versions of this file) have been replaced with what is
actually built and tested.

## 1. Directory tree (actual)

```text
copal/
├── CMakeLists.txt                 root; target copal (alias copal::copal), options, install
├── README.md CHANGELOG.md         project description; version history
├── COPYING                        full GPL-3.0 text
├── .clang-format .editorconfig .gitattributes   style/attributes (LF, UTF-8, 4 spaces, 80 columns)
├── .githooks/pre-commit           clang-format check on commit
├── .github/workflows/ci.yml       CI: Linux gcc/clang × mock|SDL+GL|SDL-soft (ASan/UBSan), shared, Windows MSVC
├── cmake/
│   ├── CompilerWarnings.cmake      warnings (-Wall -Wextra -Wpedantic -Wshadow …) + sanitizers
│   ├── copalConfig.cmake.in        package config for find_package(copal); find_dependency(Threads)
│   └── copal.pc.in                 pkg-config template (installed as lib/pkgconfig/copal.pc)
├── tools/
│   ├── setup-hooks.sh              activate .githooks (core.hooksPath)
│   └── lsan.supp                   suppression list for LeakSanitizer
├── include/copal/                 PUBLIC headers (installed)
│   ├── backend/                   backend SPI: platform.h, renderer.h (ARCHITECTURE §13)
│   ├── copal.h                    umbrella header (everything except widget_impl.h)
│   ├── export.h                   CL_API (symbol export/visibility)
│   ├── version.h                  compile/runtime version + check
│   ├── types.h                    geometry, color, constraints, align/orientation, glyph handle
│   ├── error.h                    cl_result_t, log level, last-error, log callback
│   ├── allocator.h                cl_allocator_t + cl_alloc/realloc/free wrappers
│   ├── event.h                    event types, keys, modifiers, cl_event_t, callback types
│   ├── font.h                     font loading, metrics, text measurement
│   ├── image.h                    cl_image_t: RGBA8 resource for draw_image/imageview
│   ├── theme.h                    color roles, light/dark, cl_text_style_t, theme
│   ├── render.h                   cl_paint_context_t (drawing inside paint)
│   ├── widget.h                   base widget (public part for the application)
│   ├── widget_impl.h              widget base for widget AUTHORS (vtable, class, cast)
│   ├── layout.h                   vbox/hbox containers
│   ├── application.h              application, event loop, post
│   ├── window.h                   window, content, overlay popup, tooltip
│   ├── timer.h                    application-loop timers
│   ├── animation.h                time-based animations: easing, cancellation, lerp helpers
│   └── widgets/                   label button checkbox radiobutton slider imageview
│                                  menu menubar combobox textbox scrollview list
│                                  progressbar messagebox panel spacer radiogroup
├── src/
│   ├── core/foundation/           foundation (depends on nothing)
│   │   ├── foundation_internal.h  internal declarations (cl_set_last_error)
│   │   ├── version.c              runtime version
│   │   ├── error.c                thread-local last-error, error strings, log
│   │   ├── allocator.c            default malloc allocator, wrappers, OOM accounting
│   │   ├── abi.c                  desc/ops ABI handshake (cl_abi_ok, cl_desc_fill)
│   │   ├── utf8.c                 UTF-8 decoder/code-point iteration
│   │   ├── mutex.c                opaque cross-platform mutex (pthread/CRITICAL_SECTION)
│   │   └── mutex_internal.h       mutex interface (for the application task queue)
│   ├── widget/
│   │   ├── widget.c               base, vtable dispatcher, RTTI/cast, tree, invalidation,
│   │   │                          clip-aware paint/hit-test, reveal, tooltip, event dispatch
│   │   ├── widget_internal.h      internal declarations for the widget layer
│   │   └── widget_host.h          widget host interface (implemented by the window; acyclic §2)
│   ├── layout/
│   │   ├── vbox.c  hbox.c         container measure/arrange (flex, padding, spacing, align)
│   │   └── scrollview.c           ScrollView: two axes, clip_rect, reveal, smooth animation
│   ├── theme/
│   │   ├── theme.c                built-in light/dark themes, color roles, radius, font
│   │   └── theme_internal.h
│   ├── render/
│   │   ├── renderer.h             wrapper over the public copal/backend/renderer.h
│   │   ├── paint_context.c/.h     public cl_paint_context_t on top of renderer
│   │   ├── image.c image_internal.h   cl_image_t: RGBA8 resource (evict in renderers)
│   │   ├── gl/                    OpenGL 3.3 core backend (renderer_gl, gl_loader — glad-like)
│   │   ├── soft/renderer_soft.c   software/CPU rasterizer (SDF+AA, glyph blit, clip; no GL; always built)
│   │   └── mock/                  record renderer for headless tests (draw-command list)
│   ├── text/
│   │   ├── font.c                 font/metrics/measurement on top of stb_truetype
│   │   ├── font_internal.h
│   │   └── stb_impl.c             the single TU with the stb_truetype implementation (STB_*_IMPLEMENTATION)
│   ├── platform/
│   │   ├── platform.h             wrapper over the public copal/backend/platform.h
│   │   ├── sdl/                   SDL2 backend: GL window (cl_platform_sdl_create) AND software window
│   │   │                          without GL (cl_platform_sdl_soft_create, surface + UpdateWindowSurface)
│   │   └── mock/                  headless backend (scripted events, controllable clock)
│   ├── widgets/                   widget implementations + internal tooltip bubble
│   │   ├── button.c label.c checkbox.c radiobutton.c slider.c imageview.c
│   │   ├── combobox.c menu.c menubar.c textbox.c list.c
│   │   ├── progressbar.c messagebox.c panel.c spacer.c radiogroup.c
│   │   └── tooltip.c tooltip_internal.h   (internal hover-hint widget)
│   └── app/
│       ├── application.c          create/loop, timers, task queue, IME rect, event dispatcher
│       ├── window.c               window, content, overlay/tooltip layer, focus/reveal, render
│       ├── timer.c                application timer list (FIFO, monotonic clock)
│       ├── animation.c            animations on the shared ticker (progress from now_ms, easing)
│       └── app_internal.h         private declarations for app/window/timer/task
├── third_party/
│   ├── stb/stb_truetype.h         vendored glyph rasterizer
│   ├── GL/glcorearb.h             OpenGL core header (for the in-house loader)
│   └── KHR/khrplatform.h          Khronos platform types (glcorearb.h dependency)
├── examples/                      one directory per example (build/examples/<name>/<name>)
│   ├── CMakeLists.txt              shared util lib + copal_add_gui_example helper
│   ├── common/example_util.{h,c}  font loading, headless run (COPAL_MAX_FRAMES), LSan suppressions
│   ├── test_version/main.c        smoke test: prints the version (no backends)
│   ├── helloworld/main.c          gallery of the whole API: all widgets, menus/dialogs,
│   │                              animations (easing, theme switch), images,
│   │                              cursors, a custom widget, timers, post
│   └── calc/                      calculator: calc_engine (model) + calc_widgets
│                                  (calc_key/calc_display — custom widgets) + main.c
├── tests/
│   ├── CMakeLists.txt             registers the CTest suites (copal_add_test)
│   ├── test_foundation.c          version/errors/allocator/UTF-8/types
│   ├── test_abi.c                 desc/ops ABI evolution (ADR-005): tail-tolerant handshake
│   ├── test_soft.c                golden pixels of the software renderer (stub fb, no SDL)
│   ├── test_gl_golden.c           GL render vs the software reference (SDL+GL only; skips without GL)
│   ├── test_image.c               cl_image_create: input validation, refuse a size overflow
│   ├── test_theme.c               color roles, light/dark, radius, font
│   ├── test_widgets.c             base widget, tree, cast, layout, basic widgets
│   ├── test_textbox.c             single-line editing/navigation/selection
│   ├── test_multiline.c           wrapping by width, up/down navigation, vertical scroll
│   ├── test_ime.c                 IME composition (preedit, commit, cancel)
│   ├── test_scrollview.c          two axes, reveal, scroll-to-widget
│   ├── test_combobox.c            combobox: items, selection, popup
│   ├── test_popup.c               overlay popup/menu, light dismiss
│   ├── test_overlay.c             overlay-stack cap (CL_WINDOW_MAX_OVERLAYS): refused past the cap
│   ├── test_tooltip.c             hover hint (dwell timer, cancel)
│   ├── test_timer.c               one-shot/repeat, cancel/restart, coalescing
│   ├── test_animation.c           animations: time progress, easing, cancellation, chains
│   ├── test_damage.c              damage regions: union of invalidations, full frames
│   ├── test_post.c                cross-thread cl_application_post
│   ├── test_oom.c                 fail-after-N allocator: create-path error handling
│   ├── test_fuzz.c                robustness of cl_font_load_memory/cl_image_create (fixed seed, under ASan/UBSan)
│   ├── test_layout.c              flex/align/margin, nested boxes
│   ├── test_lifecycle.c           window/focus/hover/cursor, destroy from a callback
│   ├── test_recursion.c           tree-depth cap (CL_WIDGET_MAX_DEPTH): no stack overflow
│   └── test_gui.c                 integration scenario on the mock backends
├── benchmarks/                    headless benchmarks (COPAL_BUILD_BENCHMARKS, OFF by default)
│   ├── CMakeLists.txt             copal_bench target (links against copal::mocks)
│   └── bench.c                    drives render/layout through white-box entry points
└── docs/                          ARCHITECTURE.md CODESTYLE.md API.md STRUCTURE.md
```

## 2. Responsibilities of the key files

| File | Responsibility | Public? |
|------|----------------|---------|
| `include/copal/export.h` | `CL_API`: dllexport/import, visibility | yes |
| `include/copal/version.h` | compile/runtime version, `COPAL_VERSION_ENCODE` | yes |
| `include/copal/types.h` | `cl_point/size/rect/insets/color/constraints/glyph_handle`, `cl_align/orientation`, `cl_rgba` | yes |
| `include/copal/error.h` | `cl_result_t`, `cl_log_*`, `cl_last_error`, `cl_result_string` | yes |
| `include/copal/allocator.h` | `cl_allocator_t`, `cl_alloc/realloc/free`, default | yes |
| `include/copal/event.h` | event/key/modifier types, `cl_event_t`, callback types | yes |
| `include/copal/widget.h` | tree/ownership, geometry, focus, invalidation, tooltip | yes |
| `include/copal/widget_impl.h` | widget base for authors: vtable, class, flags, cast | yes (for extension) |
| `include/copal/application.h` | `cl_application_*`, event loop, `cl_application_post` | yes |
| `include/copal/window.h` | window, content, overlay popup, tooltip | yes |
| `include/copal/timer.h` | application-loop timers | yes |
| `include/copal/animation.h` | time-based animations (easing, cancellation, lerp helpers) | yes |
| `src/core/foundation/abi.c` | desc/ops ABI handshake: `cl_abi_ok`, `cl_desc_fill` | no |
| `src/core/foundation/mutex.c` | opaque mutex behind the thread-safe task queue | no |
| `src/widget/widget.c` | vtable dispatcher, RTTI/cast, tree, clip/hit/reveal, event dispatch | no |
| `src/app/application.c` | loop, timers, task queue, dispatch of neutral events | no |
| `src/app/window.c` | content + overlay/tooltip layer, focus/reveal, frame render | no |
| `src/render/paint_context.c` | public `cl_paint_context_t` on top of the internal renderer | no |
| `src/render/soft/renderer_soft.c` | software/CPU rasterizer (SDF+AA, glyph blit); no GL | no |
| `src/text/stb_impl.c` | the single TU that instantiates stb_truetype | no |

## 3. Dependency direction (no cycles)

Rule (ARCHITECTURE §1/§2): an arrow `A → B` means "A depends on B"; there are no cycles.

```text
Public headers (compile-time):
  copal.h → {export, version, types, error, allocator, event, font, theme,
             render, widget, layout, application, window, timer, widgets/*}
  types.h → (only <stdint.h>/<stdbool.h>);  export.h — leaf
  widget_impl.h → widget.h, event.h, render.h        (for widget authors)

Implementation:
  foundation (alloc/error/utf8/version/mutex/abi) → own public .h + foundation_internal.h
  widget/layout/theme/text → foundation + interfaces (platform/renderer)
  render/gl · render/soft · render/mock → internal renderer.h
      (gl: + third_party/GL,KHR; soft: platform-neutral, only platform.h + font)
  platform/sdl · platform/mock → platform.h
  app/{application,window,timer} → everything above; the backend link point:
      SDL2 when COPAL_ENABLE_SDL (software path), OpenGL — extra, only when
      COPAL_ENABLE_OPENGL; or DI from the desc
```

At compile time the core (widget/layout/theme/text) depends only on Foundation
and on the platform/renderer **interfaces** — never on concrete SDL/GL/stb types.
The concrete backends are selected by CMake options and linked into `src/app`.

The mock backends (`render/mock`, `platform/mock`) are built into a separate
static library, **`copal_mocks`** (target `copal::mocks`, only when
`COPAL_BUILD_TESTS`) — the tests link against it, and the mock code never lands
in the installed `libcopal` artifact.

## 4. Public vs internal headers

- **Public** (`include/copal/*`) — installed, C++-compatible (`extern "C"`),
  use `CL_API`, guard `CL_*_H`. `copal.h` is the single umbrella.
- **Internal** (`src/**/*_internal.h` and the like) — not installed, no
  `CL_API`, reachable only inside the library build.
- The exceptions are the "for extension" headers: they are installed but do NOT
  belong to the `copal.h` umbrella — `widget_impl.h` (the widget base for
  third-party widget authors, ARCHITECTURE §9) and `backend/platform.h` +
  `backend/renderer.h` (the backend SPI with an ABI handshake in the ops table,
  ARCHITECTURE §13; the internal `src/platform/platform.h` and
  `src/render/renderer.h` are thin wrappers over them).

## 5. How to build and check

Headless build (mock backends, no SDL/GL) — the default:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure     # 24 suites + smoke runs
./build/examples/test_version/test_version
```

Native windowed build (SDL2 + OpenGL) — builds the helloworld and calc GUI examples:

```sh
cmake -S . -B build-native -DCOPAL_ENABLE_SDL=ON -DCOPAL_ENABLE_OPENGL=ON
cmake --build build-native
SDL_VIDEODRIVER=offscreen COPAL_MAX_FRAMES=3 ./build-native/examples/calc/calc
SDL_VIDEODRIVER=offscreen COPAL_MAX_FRAMES=3 ./build-native/examples/helloworld/helloworld
```

Software build **without OpenGL** (SDL2 only; libGL is not linked — a lightweight backend):

```sh
cmake -S . -B build-sw -DCOPAL_ENABLE_SDL=ON -DCOPAL_ENABLE_OPENGL=OFF
cmake --build build-sw
# software is chosen automatically (GL is not built); or explicitly COPAL_RENDER=software
SDL_VIDEODRIVER=dummy COPAL_MAX_FRAMES=3 ./build-sw/examples/calc/calc
```

Backend selection at runtime with both options enabled: `COPAL_RENDER=software`
(or `cl_application_desc.render_backend = CL_RENDER_SOFTWARE`) — CPU rendering
without a GL context (less memory, RDP/CI); GL by default. The calc/helloworld
examples accept the flags **`--software`** / **`--gl`** (e.g. `./calc --software`).

Options: `-DCOPAL_BUILD_SHARED=ON` (shared; the white-box tests require a static
library and are disabled — only the example smoke runs remain in ctest),
`-DCOPAL_ENABLE_SANITIZERS=ON`
(ASan/UBSan), `-DCOPAL_ENABLE_COVERAGE=ON` (gcov/llvm-cov; not MSVC),
`-DCOPAL_FETCH_SDL2=ON` (download and build SDL2),
`-DCOPAL_BUILD_BENCHMARKS=ON` (headless benchmarks; require a static library),
`-DCOPAL_BUILD_EXAMPLES=OFF`, `-DCOPAL_BUILD_TESTS=OFF`,
`-DCOPAL_ENABLE_INSTALL=OFF`.

Using as a dependency: `find_package(copal CONFIG REQUIRED)` +
`target_link_libraries(app PRIVATE copal::copal)`, or `add_subdirectory(copal)`.
