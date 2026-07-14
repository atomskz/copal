<p align="right"><b>English</b> | <a href="./ru/PERFORMANCE.md">Русский</a></p>

# Performance

copal ships a headless suite of micro-benchmarks ([benchmarks/bench.c](../benchmarks/bench.c))
so that performance is measured rather than argued about. It runs on the mock and
software backends (no window, no GPU), so it reproduces anywhere and in CI as a
separate informational job.

## How to run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCOPAL_BUILD_BENCHMARKS=ON
cmake --build build
COPAL_FONT=/path/to/font.ttf ./build/benchmarks/copal_bench
```

Each case picks its own iteration count (~0.1 s of wall-clock time) and prints
microseconds per operation and operations per second. The text cases need a
system font (`COPAL_FONT`); otherwise they are skipped.

## What is measured

| Case | Backend | What it stresses |
|---|---|---|
| `empty_frame` | software | Clearing an 800×600 frame + present. |
| `fullscreen_frame` | software | A fullscreen fill with alpha blending — the cost of **per-pixel** rasterization. |
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

A single run on x86-64 Linux (gcc, `-O2`); absolute numbers depend on the machine,
so look at the **orders of magnitude** and at before/after optimizations. CI prints
fresh numbers on every build.

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

### Observations

- **Frame cost is dominated by per-pixel rasterization.** Fullscreen software
  blending (`fullscreen_frame`, ~1 ms for 800×600) is two to three orders of
  magnitude more expensive than generating commands for the widget tree
  (`many_widgets`, ~5 µs). Hence the value of damage regions and pacing: don't
  repaint what hasn't changed.
- **Tree traversal is cheap.** Layout, the paint walk, hit-testing and input all
  fit within single-digit microseconds even at 300 widgets — the bottleneck in a
  real UI is pixels, not the CPU-side walk.
- **The CPU cost of a small update is now proportional to the change**, not to
  the size of the tree (see damage cull below).

## Effect of optimizations (before/after)

Numbers from the same machine, the same benchmark.

| Optimization | Metric | Before | After |
|---|---|---:|---:|
| **Advance cache** for non-Latin/Cyrillic (CJK/symbols/emoji) | `text_measure_mixed` | 0.327 us/op | 0.222 us/op (**−32%**) |
| **Damage cull** of the paint walk | `damage: one widget` (300-widget tree) | 5.87 us/op | 1.29 us/op |
| | speedup vs full redraw | ~1.0x | **~4–6x** |

Before the damage cull, repainting a single widget walked and drew the entire tree
and cost as much as a full frame (≈1.0x). Now `cl_widget_do_paint` culls widgets
and clipping subtrees outside the damage region, and the cost of a small update
drops to a few times cheaper (growing with tree size) — on the per-pixel software
path the win is even larger, because pixels outside the region are not blended.

## What the measurements leave out

`empty_frame`/`fullscreen_frame` isolate the per-pixel software cost, but coverage
is limited: the GL path is not benchmarked (it needs a GPU/llvmpipe in CI), and the
absolute numbers come from a single machine. Performance regressions are caught by
eye from the CI output; there is no automatic threshold yet.
