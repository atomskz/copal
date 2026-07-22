<p align="right"><b>English</b> | <a href="./docs/ru/README.md">Русский</a></p>

# copal

[![CI](https://github.com/atomskz/copal/actions/workflows/ci.yml/badge.svg)](https://github.com/atomskz/copal/actions/workflows/ci.yml)

A lightweight cross-platform C11 GUI library for Windows and Linux.
Windows and input go through SDL2; rendering runs on OpenGL 3.3 core **or** a
built-in CPU rasterizer (software), chosen at build time and at runtime.

- **Small public C API** with an ABI handshake (desc structs carrying
  `struct_size`/`abi_version`), no global state.
- **Two render backends**: OpenGL (glyph atlas, HiDPI) and software (SDF
  rasterization on the CPU — works over RDP, in CI, and without a GPU);
  `CL_RENDER_AUTO` falls back to software on its own when a GL window fails to
  come up.
- **Dependency injection**: the platform, renderer, and allocator are swapped in
  through `cl_application_desc_t` — the library is fully testable headless on
  mock backends.
- **Widgets**: label, button, checkbox, radiobutton, slider, textbox
  (single-line/multi-line, password, undo/redo, mouse selection, IME),
  combobox, list, progressbar, imageview, menu with submenus, menubar,
  message box/modal dialogs, tooltip, scrollview; vbox/hbox containers with
  flex weights, alignment, and padding.
- **Text**: UTF-8, TrueType via vendored stb_truetype, system-font lookup
  (`cl_font_load_system`, the `COPAL_FONT` variable).
- **Themes**: light/dark palette, switchable at runtime.
- Custom widgets and containers — through the installed `widget_impl.h`.

Limitations: a single window, no shaping/bidi (1 code point = 1 glyph); see
[docs/architecture.md](docs/architecture.md) for the design and its rationale.

## Build

The headless build (mock backends, no SDL/GL) is the default:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Native build with a window (SDL2 + OpenGL):

```sh
cmake -S . -B build-native -DCOPAL_ENABLE_SDL=ON -DCOPAL_ENABLE_OPENGL=ON
cmake --build build-native
./build-native/examples/helloworld/helloworld
```

Software build without OpenGL (SDL2 only, libGL is not linked):

```sh
cmake -S . -B build-sw -DCOPAL_ENABLE_SDL=ON
cmake --build build-sw
./build-sw/examples/calc/calc
```

Windows (MSVC): `COPAL_FETCH_SDL2=ON` downloads and builds SDL2 for you, and the
DLL is placed next to the examples:

```bat
cmake -S . -B build -DCOPAL_ENABLE_SDL=ON -DCOPAL_ENABLE_OPENGL=ON -DCOPAL_FETCH_SDL2=ON
cmake --build build --config Release
build\examples\helloworld\Release\helloworld.exe
```

Useful options: `-DCOPAL_ENABLE_SANITIZERS=ON` (ASan/UBSan),
`-DCOPAL_ENABLE_COVERAGE=ON` (gcov/llvm-cov; not MSVC),
`-DCOPAL_BUILD_SHARED=ON`, `-DCOPAL_BUILD_EXAMPLES=OFF`,
`-DCOPAL_BUILD_TESTS=OFF`, `-DCOPAL_ENABLE_INSTALL=OFF`.

Runtime variables: `COPAL_RENDER=software` (CPU rendering instead of GL),
`COPAL_FONT=/path/to/font.ttf` (explicit font), `COPAL_GL_DEBUG=1`
(GL version at startup).

## Minimal example

> **Important:** the default (headless) build links no platform backend, so the
> example below compiles and links, but `cl_application_create` returns `NULL`
> (`CL_ERROR_UNSUPPORTED`). To open a real window, build the library with
> `-DCOPAL_ENABLE_SDL=ON` (add `-DCOPAL_ENABLE_OPENGL=ON` for GL) or inject your
> own backend through `cl_application_desc_t`.

```c
#include <copal/copal.h>

static void on_close(cl_widget_t *w, void *user)
{
    (void)w;
    cl_application_quit((cl_application_t *)user, 0);
}

int main(void)
{
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_application_t *app = cl_application_create(&ad);
    if (!app) /* e.g. the default headless build has no backend */
        return 1;

    cl_font_t *font = cl_font_load_system(app, 16.0f);
    cl_theme_set_font(cl_application_theme(app), font);

    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
    wd.title = "Example";
    wd.width = 800;
    wd.height = 600;
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
    cl_font_release(font);
    cl_application_destroy(app); /* destroys the window and the widget tree */
    return rc;
}
```

More examples are in [examples/](examples/): `helloworld` — a gallery demo of
the whole API (every widget, a menubar with submenus, modal dialogs and forms,
animations with easing and a smooth theme switch, images, cursors, lists,
timers, a custom widget) and `calc` (a calculator). Both accept
`--software`/`--gl`.

## Using as a dependency

```cmake
find_package(copal CONFIG REQUIRED)
target_link_libraries(app PRIVATE copal::copal)
```

or `add_subdirectory(copal)` (examples/tests/install are then disabled
automatically), or pkg-config: `pkg-config --cflags --libs copal`.

> The default package is built with no platform backend (the mock backend is
> for tests only and is not installed), so `cl_application_create` returns
> `NULL` in it. To open windows, build/link copal with `-DCOPAL_ENABLE_SDL=ON`
> (and `-DCOPAL_ENABLE_OPENGL=ON` if needed), or supply your own backend
> through `cl_application_desc_t`.

## Documentation

Full documentation is in **[docs/](docs/README.md)**:

- [Building](docs/building.md) — build, install, and use copal as a dependency.
- [API reference](docs/api.md) — the core framework API.
- [Widgets](docs/widgets.md) — the built-in widget catalog.
- [Architecture](docs/architecture.md) — how it works, and the design decisions.
- [Extending](docs/extending.md) — custom widgets and backends.
- [Performance](docs/performance.md) — benchmarks.
- [Code style](docs/code-style.md) — for contributors.

## License

GPL-3.0-or-later, see [COPYING](COPYING). Vendored third-party files
(stb_truetype, Khronos headers) are under their own licenses, see
[third_party/README.md](third_party/README.md).
