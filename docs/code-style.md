# Code style

copal follows the **Linux kernel coding style** with a few deliberate deviations for a userspace library with a public C API. `.clang-format` at the repo root is the arbiter for formatting; anything it decides unambiguously is not up for debate. Code under `third_party/` (e.g. `stb_truetype.h`) is vendored as-is and never reformatted.

## Naming

| Kind | Rule | Example |
|------|------|---------|
| Public type | `snake_case` + `_t` typedef, `cl_` prefix; struct tag has no suffix | `cl_widget_t`, `cl_window_desc_t`, `cl_result_t` |
| Internal type | `snake_case` + `_t`, **no** `cl_` prefix | `glyph_atlas_t`, `input_router_t` |
| Function | `cl_` prefix, `cl_<module>_<action>` | `cl_window_create()`, `cl_button_set_on_click()` |
| Internal function | `static`, `snake_case`, no `cl_` required | `utf8_next()` |
| Macro / enum constant / guard | `UPPER_SNAKE`, `CL_` prefix | `CL_OK`, `CL_ERROR_OUT_OF_MEMORY`, `CL_WIDGET_H` |
| Local variable | short `snake_case` | `i`, `n`, `len`, `ret`, `self` |

No Hungarian notation; abbreviations are treated as words (`utf8_next`, `glyph_id`).

CMake names stay in the project namespace (`copal::copal`, `COPAL_BUILD_SHARED`). Compile-time flags passed into C are `CL_*` even when the controlling option is `COPAL_ENABLE_*`.

## Formatting

- **4-space indent, spaces only** (`UseTab: Never`). Deep nesting (>3–4 levels) is a signal to extract a function.
- **80-column limit.** Break long lines into meaningful parts; don't break message string literals just to fit.
- **K&R braces**, but functions get the opening brace on its **own line**.
- `case` one level deeper than `switch`, with a **blank line between case groups** (maintained by hand; clang-format does not insert it).
- No trailing whitespace; file ends with a single `\n`. One statement per line.
- Space after `if switch case for do while`; no space with `sizeof`/function calls. `*` binds to the name: `void *p`, `cl_widget_t *cl_widget_parent(cl_widget_t *w)`.

```c
switch (suffix) {
    case 'M':
        mem <<= 20;
        break;

    default:
        break;
}

cl_widget_t *cl_button_create(cl_application_t *app,
                              const cl_button_desc_t *desc)
{
    ...
}
```

## Error handling

| Function kind | Returns |
|---------------|---------|
| Action (can fail) | `cl_result_t` (`CL_OK == 0`) |
| Constructor | pointer (`NULL` + last-error), or `cl_result_t` + out-param — consistent within a module |
| Predicate | `bool` (`cl_widget_is_visible()`) |
| Simple setter | `void` |

Use **centralized `goto` cleanup** for resource release; labels are named after the action and come in reverse order of acquisition. If there is nothing to clean up, `return` directly.

```c
    t = cl_alloc(desc->alloc, sizeof(*t));
    if (!t)
        return CL_ERROR_OUT_OF_MEMORY;

    r = cl_thing_init(t, desc);
    if (r != CL_OK)
        goto err_free;

    *out = t;
    return CL_OK;

err_free:
    cl_free(desc->alloc, t);
    return r;
```

Check `if (!p)` / `if (r != CL_OK)` right after the operation. Use `NULL` for pointers; `bool`/`true`/`false` from `<stdbool.h>`.

## Headers

- Include guard is `#ifndef CL_<MODULE>_H` — **not** `#pragma once`.
- Public headers are C++-compatible via `extern "C"` and self-contained (include everything they use).
- Include order in a `.c` file, blank line between groups: **module header → public project headers → internal headers → system/third-party**.
- Public headers expose only public types; internal structs stay hidden.

## Memory

- Allocate through the application allocator (`cl_allocator_t`), not `malloc` directly, in library code.
- Take the size from the pointer, not the type name; don't cast the `void *` result without need:

```c
p = cl_alloc(alloc, sizeof(*p));        /* not sizeof(struct cl_foo) */
```

Every `alloc` has a matching `free` on all exit paths (see the `goto` cleanup above).

## Symbol export

Public functions are marked `CL_API` (from `include/copal/export.h`): `__declspec(dllexport/dllimport)` on Windows, `__attribute__((visibility("default")))` on GCC/Clang. The default is `-fvisibility=hidden`, so internal functions are `static` or simply lack `CL_API` and are not visible outside the `.so`/`.dll`.

## License header

The project is **GPL-3.0-or-later**. The first line of every `.c`/`.h` file (and scripts) is the SPDX identifier:

```c
/* SPDX-License-Identifier: GPL-3.0-or-later */
```

## Tooling

- **`.clang-format`** (repo root) — the formatting arbiter: `IndentWidth: 4`, `UseTab: Never`, `ColumnLimit: 80`, `IndentCaseLabels: true`, `BreakBeforeBraces: Linux`, `PointerAlignment: Right`, `SpaceAfterCStyleCast: false`, `SortIncludes: false`.
- **`.editorconfig`** — space indent, size 4, UTF-8, LF, trim trailing whitespace, final newline, `max_line_length = 80`.
- **`.githooks/pre-commit`** (mandatory) — runs `clang-format --dry-run --Werror` on changed `.c`/`.h` files (excluding `third_party/`) and blocks the commit on any discrepancy. Activate once per clone:

```sh
git config core.hooksPath .githooks
```

## Deviations from pure kernel style

| Kernel rule | copal decision |
|-------------|----------------|
| Tab indent ×8; `case` at `switch` level | 4-space indent; `case` one level deeper with blank line between groups |
| `struct foo`, no typedef; `u8`/`u32` types | `snake_case` + `_t` typedefs, `cl_` prefix; `<stdint.h>` types for the FFI-safe public API |
| No `extern "C"`; checkpatch line ~100 | Public headers wrapped in `extern "C"`; strictly 80 columns |

Everything else (K&R braces, `goto` cleanup, spaces around operators, `sizeof(*p)`, self-contained headers) follows the kernel style strictly.

---
*See also: [Extending](./extending.md) · [Architecture](./architecture.md) · [Building](./building.md)*
