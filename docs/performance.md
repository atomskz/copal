# Performance

copal ships a headless suite of micro-benchmarks in [benchmarks/bench.c](../benchmarks/bench.c). They run on the mock and software backends (no window, no GPU), so they reproduce anywhere and in CI.

## Running the suite

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCOPAL_BUILD_BENCHMARKS=ON
cmake --build build
COPAL_FONT=/path/to/font.ttf ./build/benchmarks/copal_bench
```

Each case picks its own iteration count (~0.1 s of wall-clock) and prints microseconds per operation and operations per second. The text cases need a system font via `COPAL_FONT`; without it they are skipped.

## What is measured

| Case | Backend | Stresses |
|---|---|---|
| `empty_frame` | software | Clearing an 800×600 frame + present. |
| `fullscreen_frame` | software | Fullscreen fill with alpha blending — **per-pixel** rasterization. |
| `many_widgets` | mock | Paint walk of a 300-widget tree (command generation). |
| `deep_layout` | mock | measure+arrange of a tree 60 levels deep. |
| `resize` | mock | Resize → relayout → paint (150 widgets). |
| `input mouse-move` | mock | A single input event through the loop (hit-test/hover/dispatch). |
| `scroll` | mock | Wheel → scrolling content of 300 widgets. |
| `damage: full / one` | mock | Full redraw versus invalidating a single widget — **damage cull**. |
| `resource_churn` | mock | 100 widget create+destroy (allocator). |
| `text_measure_mixed` | — | Measuring a Latin/Cyrillic/CJK/symbols string — **advance cache**. |
| `text_edit` | mock | Typing/backspace in a textbox through the loop. |

## Results (indicative)

A single run on x86-64 Linux (gcc, `-O2`). Absolute numbers depend on the machine — look at the orders of magnitude, not the exact figures. CI prints fresh numbers on every build.

```
pixel (software renderer):
  empty_frame (800x600 clear)                24.7 us/op
  fullscreen_frame (800x600 blend)         1046.4 us/op
widget / layout / input (mock renderer):
  many_widgets (300, paint)                   5.2 us/op
  deep_layout (60 nested, relayout)           1.4 us/op
  resize + relayout (150)                     3.1 us/op
  input mouse-move (300 widgets)              1.4 us/op
  scroll (wheel, 300-tall content)            4.9 us/op
  damage: full redraw (300)                   5.2 us/op
  damage: one widget (300)                    1.3 us/op   (~4-6x faster than full)
  resource_churn (100 create+destroy)         2.3 us/op
text:
  text_measure_mixed (Latin/Cyr/CJK/sym)      0.22 us/op
  text_edit (type/backspace)                 39.3 us/op
```

## Observations

- **Frame cost is dominated by per-pixel rasterization.** Fullscreen software blending (`fullscreen_frame`, ~1 ms for 800×600) is two to three orders of magnitude more expensive than generating commands for the whole widget tree (`many_widgets`, ~5 µs). That is why damage regions and frame pacing matter: don't repaint what hasn't changed.
- **Tree traversal is cheap.** Layout, the paint walk, hit-testing and input all fit within single-digit microseconds even at 300 widgets. The bottleneck in a real UI is pixels, not the CPU-side walk.
- **A small update costs proportional to the change, not the tree.** `cl_widget_do_paint` culls widgets and clipped subtrees outside the damage region, so invalidating one widget in a 300-widget tree is ~4–6x cheaper than a full redraw — and even cheaper on the software path, where pixels outside the region are never blended. The advance cache similarly cuts non-Latin/Cyrillic text measurement (`text_measure_mixed`, −32%).

## Limits

The `*_frame` cases isolate per-pixel software cost, but coverage is partial: the GL path is **not** benchmarked (it needs a GPU/llvmpipe in CI), and every number here comes from a single machine. Regressions are spotted by eye from CI output — there is no automatic threshold yet.

---
*See also: [Architecture](./architecture.md) · [Building](./building.md)*
