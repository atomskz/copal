# copal documentation

copal is a lightweight cross-platform **C11 GUI library** for Windows and Linux. Windows and input go through SDL2; rendering runs on OpenGL 3.3 or a built-in CPU software rasterizer. This is the documentation index — start with the page that matches what you want to do.

New to copal? The [project README](../README.md) has the elevator pitch and a minimal example; then come back here.

## Using copal

| Page | What's in it |
|------|--------------|
| [Building](./building.md) | Build and test, add a window backend, use copal as a dependency, and the freestanding/UEFI core. |
| [API reference](./api.md) | The core framework: application, window, timer, animation, the widget base, layout, theme, font, and the paint context — plus conventions, types, events, ownership, and the error model. |
| [Widgets](./widgets.md) | Every built-in widget: label, button, checkbox, radio, slider, textbox, list, combobox, menu, scrollview, dialogs, and more. |
| [Extending](./extending.md) | Write your own widgets, plug in a custom platform/renderer backend, or inject an allocator/theme. |

## Understanding copal

| Page | What's in it |
|------|--------------|
| [Architecture](./architecture.md) | The layer stack, dependency rules, ownership and lifetimes, threading, rendering/DPI, text, and the design decisions behind them. |
| [Performance](./performance.md) | The benchmark suite, indicative numbers, and what dominates frame cost. |

## Contributing

| Page | What's in it |
|------|--------------|
| [Code style](./code-style.md) | Naming, formatting, error handling, headers, and the tooling (`.clang-format`, the pre-commit hook). |

---

Release notes are in the [changelog](../CHANGELOG.md). The documentation is English-only; the [README is also available in Russian](./ru/README.md).
