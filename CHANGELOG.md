<p align="right"><b>English</b> | <a href="./docs/ru/CHANGELOG.md">Русский</a></p>

# Changelog

Format — [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); version
numbering — [SemVer](https://semver.org/). ABI policy: within a single major,
public desc/ops structs only gain fields at the end — the tail-tolerant ABI
handshake accepts a consumer built against a different minor of the same major
and rejects an incompatible major. Before 1.0 the ABI is not frozen yet: the
major stays at 0, so the handshake will skip any 0.x — rebuild consumers for
every 0.x version.

## [Unreleased]

### Added

- Read-back getters to pair existing setters: `cl_button_text`,
  `cl_label_text`, `cl_checkbox_text`, `cl_radiobutton_text`,
  `cl_radiobutton_set_text`, `cl_window_title`, `cl_slider_min`/`_max`/`_step`,
  and `cl_theme_radius` (declared; it already existed).
- `cl_textbox_set_on_change` as an alias of `cl_textbox_set_on_changed`, for a
  consistent change-callback spelling across widgets.
- CI now installs copal and consumes it through `find_package` and pkg-config
  (static and shared), guarding the packaging surface.

### Changed

- **Breaking:** `cl_msgbox_fn` now leads with the dialog widget
  (`void (*)(cl_widget_t *dialog, int index, void *user)`), matching the other
  widget callbacks.
- `cl_list_set_selected` no longer fires `on_select` (now consistent with every
  other widget's programmatic setter).
- `cl_window_open_modal` returns `bool` (false when the overlay stack is full);
  `cl_messagebox_show` returns NULL in that case instead of a dead handle.
- Pre-1.0 packaging: the shared library's SONAME and the CMake package version
  now encode the minor (each 0.x is a distinct, non-interchangeable ABI).

### Fixed

- Double-free when a focused widget is destroyed from its own `focus_lost`;
  events no longer dispatch to widgets destroyed mid-bubble.
- `cl_application_post`/`_quit` no longer race the software-fallback platform
  swap.
- Non-finite input is rejected/clamped for the slider, progressbar and font
  size; a password field never renders cleartext on an allocation failure.
- The software renderer no longer caches a blank glyph after a transient
  allocation failure; window resize and `cl_window_show` repaint in full on the
  partial-redraw path; mouse side buttons are ignored rather than left-clicks.
- Numerous smaller correctness, portability and consistency fixes across the
  widgets, layout, text, backends and documentation.

## [0.2.0] — 2026-07-15

### Added

- Backend SPI published: installable headers `copal/backend/platform.h` and
  `copal/backend/renderer.h`; ops tables begin with the `struct_size`/
  `abi_version` ABI handshake, and an injected backend with a foreign table is
  rejected with `CL_ERROR_ABI_MISMATCH`.
- Platform SPI made multi-window-ready: window operations take an opaque
  `cl_platform_window_t*` (create_window returns a handle), and events carry a
  `window_id`.
- The widget ↔ app/window cycle is broken: the widget layer declares a narrow
  host interface (invalidate/focus/popup/clipboard/IME) that the window
  implements; along the way, a dangling `content` on direct destroy of the root
  widget is fixed.
- Hover events: the window delivers `CL_EVENT_MOUSE_ENTER`/
  `CL_EVENT_MOUSE_LEAVE` (new vtable slots `mouse_enter`/`mouse_leave`, no
  bubbling); a button is highlighted with the `CL_COLOR_SURFACE_HOVER` role.

- Images: the `cl_image_t` resource (image.h, raw RGBA8), `cl_paint_draw_image`,
  and the SPI operations `draw_image`/`evict_image` in all three renderers (GL —
  a texture cache, software — a scalable blit with blending, mock — recording
  the command) plus the `cl_imageview` widget.
- Widgets: `cl_list` (mouse/keyboard selection, PageUp/Down, activation by
  double-click/Enter, remove/clear), `cl_progressbar`, and `cl_messagebox_show`
  (an OK/Cancel/Yes-No modal dialog with Enter/Escape).
- Overlay stack: submenus (`cl_menu_add_submenu`; click/Enter/Right open them,
  Escape/Left close only the submenu), the `cl_menubar` widget, and modal
  dialogs `cl_window_open_modal` (a click outside does not close them).
- Mouse cursors: `cl_cursor_t` (default/ibeam/hand/crosshair/size),
  `cl_widget_set_cursor` (the shape is inherited from the nearest ancestor), and
  the `set_cursor` SPI operation; the textbox shows an I-beam by default.
- `cl_key_t` extended: the digit row `CL_KEY_0..9`, `CL_KEY_F1..F12`, and
  `CL_KEY_PAGE_UP`/`CL_KEY_PAGE_DOWN` (mapped in the SDL backend).

- Layout widgets: `cl_panel` (a grouping surface with padding and a border),
  `cl_spacer` (a fixed/flexible gap), and `cl_radiogroup` (a column of mutually
  exclusive radio buttons with an automatic group-id and a selection callback).
- API symmetry: `cl_combobox_item_text/remove/clear`,
  `cl_menu_item_text/remove/clear`, and the getters
  `cl_widget_preferred_size/margin/align_h/align_v/flex/is_focusable`.

- Animations (`animation.h`): `cl_animation_start`/`cl_animation_cancel` —
  callback animations on a shared ~60 Hz ticker with progress driven by elapsed
  time (a lagging loop jumps ahead rather than slowing down), linear/in/out/
  in-out easing, `on_done` with an outcome (finished/cancelled), chaining and
  parallel composition; helpers `cl_ease`, `cl_lerp`, `cl_color_lerp`,
  `cl_rect_lerp`.
- Render primitives: `cl_paint_push_transform`/`pop_transform` (translate + scale
  for a subtree, also affecting clips) and `cl_paint_push_opacity`/`pop_opacity`
  (group alpha multiplication) in all three renderers; new SPI operations
  `push_transform`/`push_opacity` with matching pops.
- Damage regions: `cl_widget_invalidate` accumulates the bounding rect of
  invalidations; the software path clears, draws and presents only that region
  (the renderer's `set_damage` SPI operation, the platform's `present_region` —
  `SDL_UpdateWindowSurfaceRects`). GL and full invalidations redraw the whole
  frame as before.
- Software-path pacing: presents are throttled to the display refresh rate by a
  frame limiter keyed on `now_ms` (60 Hz fallback) — animation no longer
  presents at loop speed.
- Input in modal dialogs: a click focuses the nearest focusable widget and
  captures the pointer for the duration of the drag; keys and text go to the
  focus inside the top overlay (bubbling up to the dialog root — Enter/Escape),
  Tab cycles the dialog's focusable widgets, and hover and the cursor work over
  overlays.
- Mock renderer: commands carry a pointer to the drawn image; the last frame's
  `set_damage` region is available to tests; push/pop transform/opacity are
  recorded, and draw-command geometry is already transformed.

- Freestanding core (`COPAL_HOSTED=OFF`, a new CMake option, default ON): the core
  (foundation + software renderer + widgets + layout + text) builds for
  `-ffreestanding`/UEFI with no hosted C runtime and no libc/libm — copal
  namespaces the str/math/format helpers it needs (`cl_strlen`/`cl_strcmp`, an
  `fmath` set, a minimal `cl_vsnprintf`), and a CI job pins the residual external
  surface to `memcpy`/`memmove`/`memset`. See ARCHITECTURE §19 / ADR-016.
- Injectable task-queue mutex: `cl_mutex_iface_t` and `cl_application_desc_t.mutex`
  (a tail-appended, ABI-compatible field). NULL keeps the hosted default (pthread /
  critical section); a freestanding embedder injects one (on UEFI,
  `RaiseTPL`/`RestoreTPL`).
- Injectable assertion handler: `cl_set_assert_handler` (a failed `CL_ASSERT` is
  routed to it; compiled out under `NDEBUG`). The hosted default logs and aborts.

### Changed

- `cl_menu_create` takes a `cl_menu_desc_t` (the last widget without a desc);
  existing calls are updated by adding
  `&(cl_menu_desc_t){ CL_MENU_DESC_INIT_FIELDS }`.

- `cl_widget_destroy`: the subtree is detached immediately, but the memory of
  the attached widgets is freed at the end of the current loop iteration (the
  DEAD queue) — destroying any widget from any callback is safe, and a repeated
  destroy is a no-op.

- The hosted build no longer links libm: the software renderer and stb_truetype
  route their math through the `cl_*` helpers, and the `sqrt`/`abs` builtins lower
  to hardware ops under `-fno-math-errno`.
- With `COPAL_HOSTED=OFF` the hosted-only paths compile out and fail closed:
  `cl_allocator_default()` returns NULL (inject an allocator via the app desc),
  `cl_font_load_file`/`cl_font_load_system` return `CL_ERROR_UNSUPPORTED` (use
  `cl_font_load_memory`), the stderr log fallback is gone (install a
  `cl_set_log_callback` sink), and there is no built-in task-queue mutex.

### Fixed

- Rounded-rect borders: the software renderer paints the outer edge and AA of a
  thin border instead of clipping it to the fill rect, matching the OpenGL path
  pixel-for-pixel.
- Text crispness: glyph origins snap to the device pixel grid on both renderers,
  and a CI golden pixel-verifies the OpenGL renderer against the software
  reference (strokes and text).

## [0.1.0] — 2026-07-12

First release.

### Added

- Application core: `cl_application_*` (run/step/quit, tasks from other threads
  via `cl_application_post`, timers), a single `cl_window_*` window with a
  close veto callback.
- Widgets: label, button, checkbox, radiobutton, slider, textbox (single-line/
  multi-line, password, undo/redo, mouse and keyboard selection, word-wise
  navigation, IME), combobox, menu, tooltip, scrollview; vbox/hbox containers
  (flex weights, per-child alignment, margin/padding).
- Renderers: OpenGL 3.3 core (glyph atlas, HiDPI) and software (CPU
  rasterization into the window buffer); `CL_RENDER_AUTO` with a runtime
  GL → software fallback and a `COPAL_RENDER=software` override.
- Platform: SDL2 (GL and software paths), events with modifiers and a click
  count, clipboard, IME rectangle, EXPOSE.
- Text: UTF-8 (a validating decoder), TrueType via stb_truetype,
  `cl_font_load_system()` with `COPAL_FONT`, an advance cache for Latin-1 and
  Cyrillic, and glyph-cache invalidation on `cl_font_release`.
- Themes: light/dark, switchable at runtime.
- Extensibility: injection of platform/renderer/allocator via desc; custom
  widgets and containers via `widget_impl.h` (a vtable versioned with
  `vtable_size`).
- Build: CMake ≥ 3.16, SDL/OpenGL/shared/sanitizer/coverage options,
  `find_package(copal)`, the `copal.pc` pkg-config, and pinned vendoring of SDL2
  (`COPAL_FETCH_SDL2`).
- Tests: 17 headless suites on mock backends (layout, lifecycle, OOM sweep,
  software-renderer golden tests, the UTF-8 table) plus smoke runs of the
  examples.

### Known limitations

- A single window; menus without submenus; no image primitive, mouse cursors,
  hover events, or drag&drop.
- No text shaping/bidi: 1 code point = 1 glyph.
- Fonts are loaded from a trusted source (stb_truetype does not check truncated
  data against the buffer length).
