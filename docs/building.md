# Building copal

How to build copal, run its tests, add a real window backend, consume it from another project, and strip it down to a freestanding core. Requires CMake 3.16+ and a C11 compiler.

## Quick start (headless)

The default build pulls in **no** SDL2 or OpenGL. It compiles the core plus the mock backends and is exactly what you want for tests, CI, and pure logic:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

This build links cleanly, but there is no platform backend compiled in, so `cl_application_create()` returns `NULL` and sets `CL_ERROR_UNSUPPORTED`. That is the intended result: widgets, layout, theming, text, and the software rasterizer are all exercised by the white-box tests without ever opening a window. To get a window, either enable a backend below or inject your own via `cl_application_desc_t`.

The `test_version` example needs no backend and prints the library version:

```sh
./build/examples/test_version/test_version
```

## A real window

### SDL2 + OpenGL (native)

```sh
cmake -S . -B build-native -DCOPAL_ENABLE_SDL=ON -DCOPAL_ENABLE_OPENGL=ON
cmake --build build-native
```

Builds the `helloworld` and `calc` GUI examples with a hardware-accelerated OpenGL 3.3 core renderer. `COPAL_ENABLE_OPENGL` requires `COPAL_ENABLE_SDL`; enabling GL alone emits a warning and disables the GL renderer.

### SDL2 only (CPU software renderer)

```sh
cmake -S . -B build-sw -DCOPAL_ENABLE_SDL=ON -DCOPAL_ENABLE_OPENGL=OFF
cmake --build build-sw
```

`COPAL_ENABLE_SDL` alone yields a working windowed build that draws with the built-in CPU rasterizer. `libGL` is never linked, so there is no GL/driver dependency — useful over RDP, in CI, or on machines without a usable GL stack. With no GL renderer compiled in, software is selected automatically.

### Windows / MSVC

On Windows the easiest path is to let CMake fetch and build SDL2 for you:

```sh
cmake -S . -B build -DCOPAL_ENABLE_SDL=ON -DCOPAL_ENABLE_OPENGL=ON -DCOPAL_FETCH_SDL2=ON
cmake --build build
```

`COPAL_FETCH_SDL2=ON` downloads SDL2 (pinned to release-2.30.9 by commit) via `FetchContent`, builds it as a shared library, and — on Windows, CMake 3.21+ — the example build drops the SDL2 DLL next to each example executable so it runs in place. Note that a fetched SDL2 cannot be part of copal's install/export set, so this configuration turns install rules off.

## CMake options

All options and their defaults, straight from `CMakeLists.txt`:

| Option | Default | Meaning |
|---|---|---|
| `COPAL_BUILD_SHARED` | `OFF` | Build copal as a shared library instead of static. |
| `COPAL_BUILD_EXAMPLES` | ON when top-level | Build the example programs. |
| `COPAL_BUILD_TESTS` | ON when top-level | Build the test suites and register them with ctest. |
| `COPAL_ENABLE_SDL` | `OFF` | Compile the SDL2 platform backend (windowing + input). |
| `COPAL_ENABLE_OPENGL` | `OFF` | Compile the OpenGL renderer (requires `COPAL_ENABLE_SDL`). |
| `COPAL_FETCH_SDL2` | `OFF` | Download and build SDL2 via `FetchContent` instead of `find_package`. |
| `COPAL_ENABLE_SANITIZERS` | `OFF` | Instrument with ASan/UBSan. |
| `COPAL_ENABLE_COVERAGE` | `OFF` | Instrument for gcov/llvm-cov (not MSVC). |
| `COPAL_ENABLE_INSTALL` | ON when top-level | Emit install rules and the CMake/pkg-config package files. |
| `COPAL_BUILD_BENCHMARKS` | `OFF` | Build the headless benchmark harness (needs the static library). |
| `COPAL_HOSTED` | `ON` | Build the hosted (OS libc) paths; `OFF` = freestanding core. |

"Top-level" means copal is the top project; when embedded via `add_subdirectory`, examples/tests/install default to `OFF`. A top-level build with no `CMAKE_BUILD_TYPE` defaults to `Release`.

Notes on interactions:
- `COPAL_BUILD_SHARED=ON` disables the white-box tests and benchmarks (they reach internal symbols that need the static library); only the example smoke tests remain in ctest.
- `COPAL_FETCH_SDL2=ON` without `COPAL_ENABLE_SDL=ON` fetches nothing and warns.

## Runtime environment variables

| Variable | Effect |
|---|---|
| `COPAL_RENDER=software` | With both backends built, forces the CPU renderer instead of GL (honoured only for `CL_RENDER_AUTO`). |
| `COPAL_FONT=/path/to/font.ttf` | Font tried first by `cl_font_load_system()` before the built-in per-OS candidate list. |
| `COPAL_GL_DEBUG=1` | GL renderer logs `GL_VERSION`/`GL_RENDERER` and related context info at startup. |
| `COPAL_MAX_FRAMES=N` | Example helper only: renders N frames then exits 0 (drives the headless smoke tests); unset runs the normal blocking loop. |

The example smoke tests run with `COPAL_MAX_FRAMES=3` against `SDL_VIDEODRIVER=dummy`.

## Using copal as a dependency

**Installed package (CMake):**

```cmake
find_package(copal CONFIG REQUIRED)
target_link_libraries(app PRIVATE copal::copal)
```

Pre-1.0, each 0.x minor is treated as incompatible: `find_package(copal 0.2)` will not accept an installed 0.3.

**Vendored subdirectory:**

```cmake
add_subdirectory(copal)
target_link_libraries(app PRIVATE copal::copal)
```

Embedded this way, copal's own examples/tests/install default off; set `COPAL_ENABLE_SDL`/`COPAL_ENABLE_OPENGL` in your cache to pick a backend.

**Non-CMake (pkg-config):**

```sh
pkg-config --cflags --libs copal
```

The private entries matter because copal is a static library by default: consumers pull in `sdl2`, `-pthread` (hosted, non-Windows), and `-lGL` (SDL+GL) transitively.

Whichever route you use, the default package carries **no backend**, so `cl_application_create()` returns `NULL` until you enable SDL/GL or inject your own platform + renderer through `cl_application_desc_t`.

**ABI policy.** The ABI is not frozen before 1.0. Each 0.x minor gets its own SONAME and its package rejects other minors — rebuild every consumer for each 0.x bump.

## Freestanding / UEFI

`-DCOPAL_HOSTED=OFF` builds the core (foundation + software renderer + widgets + layout + text) with no hosted C runtime, for targets like UEFI where the software renderer draws into a linear GOP framebuffer. The SDL and GL backends are hosted-only and stay out of such a build.

With `CL_HOSTED` undefined, these paths compile out and the embedder must supply the replacement:

| Compiled out | Embedder injects |
|---|---|
| Default malloc allocator (`cl_allocator_default()` returns `NULL`) | An allocator via `cl_application_desc_t.allocator` |
| File/system font loaders (`cl_font_load_file`/`cl_font_load_system` return `CL_ERROR_UNSUPPORTED`) | A font from memory via `cl_font_load_memory` |
| stderr log / `abort()` diagnostics | A log sink via `cl_set_log_callback` and an assert handler via `cl_set_assert_handler` |
| Built-in task-queue mutex | A mutex via `cl_application_desc_t.mutex` (e.g. UEFI `RaiseTPL`/`RestoreTPL`) |
| SDL platform / GL renderer | A platform via `cl_application_desc_t.platform` (out of tree, over GOP + input/timer protocols) |

The **software renderer is not injected** — it is built in and always compiled. Ask for it with `cl_application_desc_t.render_backend = CL_RENDER_SOFTWARE`; `cl_application_create` builds it against your platform's `lock_framebuffer` op (the GOP linear framebuffer), so your platform must implement `lock_framebuffer`/`unlock_framebuffer` returning a `cl_pixmap_t`. See [extending.md](./extending.md) for the platform SPI.

Two behaviours to know freestanding: the last error is a plain global (single-threaded — no TLS runtime is needed), and a failed `CL_ASSERT` with no handler installed halts in a spin loop, so install `cl_set_assert_handler` for debug builds (assertions are compiled out under `NDEBUG`). The core links no libm and references only `memcpy`/`memmove`/`memset`, which every UEFI toolchain supplies; see [architecture.md](./architecture.md) for the rationale and the shim contract.

---
*See also: [architecture.md](./architecture.md) · [api.md](./api.md) · [performance.md](./performance.md)*
