<p align="right"><b>English</b> | <a href="./ru/CODESTYLE.md">Русский</a></p>

# copal — Code Style

Status: **accepted** (review complete). Version: 1.0.
History: 0.1 — draft based on the kernel style; 0.2 — `cl_*`/`snake_case_t`
naming, spaces, `case` indentation, GPL-3.0, pre-commit hook; **1.0 — 4-space
indentation, `GPL-3.0-or-later`, internal types without a prefix — accepted.**

## 0. Foundation and scope

The code style of the copal project is the **Linux kernel coding style**
(`Documentation/process/coding-style.rst`), with the deviations strictly
enumerated in §2 and §18 that are required for a userspace library with a
public C API compatible with C++ and FFI.

Scope:
- **all C code** in `src/`, `include/`, `examples/`, `tests/`;
- platform files (SDL/GL/X11 and the like) — the same style; isolation lives
  in separate TUs/directories, not in `#ifdef` interleaved with logic (§12);
- third-party code in `third_party/` (for example `stb_truetype.h`) is **not
  formatted** to our style and is not touched for style — it is only vendored
  as is.

Priority on conflict: readability → conformance with the kernel style →
consistency. The arbiter for formatting is `.clang-format` (§16); anything it
decides unambiguously is not up for debate in code review.

---

## 1. Indentation and line width

- Indentation is **spaces only, 4 columns per level**. Tabs are **not** used for
  indentation (the arbiter is `.clang-format` with `UseTab: Never`).
- Deep nesting is undesirable: more than 3–4 levels is a signal to extract part
  of the logic into a separate function.
- `switch`/`case`: **`case` is one level deeper than `switch`**, the body one
  more; between groups of `case` there is a **blank line**:

```c
switch (suffix) {
    case 'G':
    case 'g':
        mem <<= 30;
        break;

    case 'M':
        mem <<= 20;
        break;

    default:
        break;
}
```

- **The line-length limit is 80 columns.** Longer lines are broken into
  meaningful parts, unless doing so *significantly* hurts readability (do not
  break string literals of messages just to fit 80).
- Do not leave trailing whitespace. A file ends with a single `\n`.
- One definition/statement per line.

---

## 2. Naming (accepted)

### 2.1 Types
- **All** type names are `snake_case`.
- Every public type gets a **typedef with a `_t` suffix**; the struct tag has no
  suffix:

```c
typedef struct cl_widget       cl_widget_t;
typedef struct cl_window_desc  cl_window_desc_t;
typedef enum   cl_result       cl_result_t;
```

- Public types carry the **`cl_`** prefix (short for `copal_`):
  `cl_widget_t`, `cl_window_desc_t`, `cl_paint_context_t`, `cl_result_t`,
  `cl_glyph_handle_t`, `cl_allocator_t`.
- **Internal** (non-exported) types are `snake_case` + `_t`, **without** the
  `cl_` prefix: `glyph_atlas_t`, `input_router_t`.

### 2.2 Functions
- The **`cl_`** prefix, `snake_case`, following the `cl_<module>_<action>`
  scheme: `cl_window_create()`, `cl_widget_add_child()`,
  `cl_button_set_on_click()`.
- Internal functions are `static`, `snake_case`, without a mandatory `cl_`.

### 2.3 Macros, enum constants, guards
- Public macros and enum constants are **`UPPER_SNAKE` with the `CL_` prefix**:
  `CL_OK`, `CL_ERROR_OUT_OF_MEMORY`, `CL_WIDGET_CAST`, `CL_API`, `CL_ASSERT`,
  `CL_WIDGET_RESERVED`, `COPAL_VERSION_MAJOR`.
- Include guard — `CL_<MODULE>_H` (§9).

### 2.4 Build system (outside the C API)
- Names in CMake stay in the project namespace: the `copal::copal` target, the
  `COPAL_BUILD_SHARED`, `COPAL_ENABLE_SDL` options. Compile-time flags passed
  into C are defined as `CL_*` (for example `CL_ENABLE_LOG`), even when the
  controlling CMake option is named `COPAL_ENABLE_LOG`.

### 2.5 Note on the `_t` suffix
POSIX reserves identifiers ending in `_t`. The risk of colliding with system
typedefs is low (this is what `cairo_t` and others do), but new names are
checked for conflicts against system headers. The deviation is a deliberate,
accepted choice.

---

## 3. Naming (details)

- Names are essentially free of Hungarian notation; the type is not encoded in
  the name.
- **Local** variables are short, `snake_case`: `i`, `n`, `tmp`, `len`, `ret`,
  `p`, `self`, `w`.
- **Globals** (avoid where possible) and **functions** are descriptive.
- Abbreviations are treated as words: `utf8_next`, `glyph_id` (not `Utf8`, not
  `GlId`).

---

## 4. Braces and spaces (K&R, kernel variant)

- **The opening brace goes at the end** of the statement line; the closing brace
  on its own line:

```c
if (x == y) {
    ...
} else if (x > y) {
    ...
} else {
    ...
}

do {
    ...
} while (condition);
```

- **Functions are the exception**: the opening brace goes on a **separate** line:

```c
cl_widget_t *cl_button_create(const cl_button_desc_t *desc)
{
    ...
}
```

- `else`/`while` (in a `do-while`) go on the closing-brace line: `} else {`,
  `} while (cond);`.
- Do **not** put braces around a single statement:

```c
if (condition)
    action();
```

  but if at least one branch of an `if/else` is multi-line, use braces on
  **both**.
- Spaces:
  - **after** the keywords `if switch case for do while` — a space: `if (x)`;
  - with `sizeof typeof alignof __attribute__` and function names — **no** space:
    `sizeof(*p)`, `foo(x)`; no spaces inside the parentheses: not `sizeof( x )`;
  - around binary/ternary operators — one space each: `a + b`, `x ? y : z`;
  - after unary operators (`& * + - ~ !`) and around `.`/`->` — **no** space:
    `*p`, `&x`, `p->field`;
  - `++`/`--` with no space to the operand: `i++`, `--n`.
- For pointers, `*` binds to the **name**, not the type: `void *p`, `char **argv`,
  `cl_widget_t *cl_widget_parent(cl_widget_t *w)`.
- A cast has no space after the parenthesis: `(cl_widget_t *)p`.

---

## 5. Functions

- Short, and they do **one** thing; the guideline is 1–2 screens. More than
  5–10 local variables is a signal to split.
- Between functions — **one** blank line.
- Give **parameter names** in prototypes:
  `cl_result_t cl_font_load_file(cl_font_provider_t *fp, const char *path);`.
- Do **not** write `extern` on function prototypes. Public functions are marked
  with `CL_API` (§15).
- Parameter order: "object/context" → inputs → outputs (out-pointers last,
  documented, §10).

---

## 6. Returns and error handling

- **Centralized exit via `goto`** for resource cleanup. If there is nothing to
  clean up, `return` directly.
- Labels are named after the action: `out_free_buffer:`, `err_destroy_window:`;
  they come in the reverse order of resource acquisition.

```c
cl_result_t cl_thing_create(const cl_thing_desc_t *desc, cl_thing_t **out)
{
    cl_thing_t *t;
    cl_result_t r;

    t = cl_alloc(desc->alloc, sizeof(*t));
    if (!t)
        return CL_ERROR_OUT_OF_MEMORY;

    r = cl_thing_init(t, desc);
    if (r != CL_OK)
        goto err_free;

    r = cl_thing_attach(t);
    if (r != CL_OK)
        goto err_deinit;

    *out = t;
    return CL_OK;

err_deinit:
    cl_thing_deinit(t);
err_free:
    cl_free(desc->alloc, t);
    return r;
}
```

- Return-code convention:
  - **action** functions that can fail return `cl_result_t` (`CL_OK == 0`);
  - **constructors** return either a pointer (`NULL` + last-error) or
    `cl_result_t` + an out-parameter — consistently within a module;
  - **predicates** return `bool` (`cl_widget_is_visible()`);
  - simple **setters** return `void`.
- Check `if (!p)` / `if (r != CL_OK)` right after the operation.
- Use `NULL` for pointers (not `0`); `bool`/`true`/`false` from `<stdbool.h>`.

---

## 7. Comments

- Style is C, `/* ... */`. `//` is allowed only for the SPDX header line.
- Multi-line comments use the kernel format (an asterisk in the column):

```c
/*
 * A short explanation of WHAT and, most importantly, WHY.
 * Do not describe line by line HOW — that is visible from the code.
 */
```

- A comment explains intent/invariant/pitfall, not trivialities.
- **Public functions** are documented in the kernel-doc style:

```c
/**
 * cl_widget_add_child() - add a child widget and transfer ownership.
 * @parent: the parent container (not NULL).
 * @child:  the widget to add (not NULL); ownership passes to @parent.
 *
 * Return: CL_OK, or CL_ERROR_INVALID_ARGUMENT on NULL / a cyclic link.
 */
```

- Each file starts with the SPDX line (§17) and a brief statement of its purpose.

---

## 8. Macros, enums, constants

- Constants and enum labels are **`CL_...`** (§2.3); for a set of related
  constants prefer an `enum` over a series of `#define`s.
- Multi-statement macros go in `do { } while (0)`.
- Macro arguments go in parentheses; watch out for operator precedence:
  `#define CL_MIN(a, b) ((a) < (b) ? (a) : (b))`.
- Prefer `static inline` functions to macros where possible.
- Do not hide control flow in macros (except for documented contracts such as
  `CL_WIDGET_CAST`).

---

## 9. Header files

- **Include guard — `#ifndef`/`#define`/`#endif`** (not `#pragma once`):

```c
#ifndef CL_WIDGET_H
#define CL_WIDGET_H
...
#endif /* CL_WIDGET_H */
```

- Public headers are **C++-compatible** via `extern "C"`:

```c
#ifdef __cplusplus
extern "C" {
#endif
...
#ifdef __cplusplus
}
#endif
```

- A header is self-contained: it includes everything it uses.
- `#include` order in a `.c` file: the module header → public project headers →
  internal headers → system/third-party; a blank line between groups.
- Public headers expose only public types; internal structs are not revealed
  (except `widget_impl.h` for widget authors, architecture §9).

---

## 10. Types

- In the **public API**, use the standard types from `<stdint.h>`/`<stddef.h>`:
  `uint32_t`, `int32_t`, `size_t`, `uint8_t`.
- `size_t` is for memory sizes and element counts.
- Fixed-width types where width matters (formats, color `uint8_t`); otherwise
  plain `int`/`unsigned`.
- `bool`/`true`/`false` come from `<stdbool.h>`.
- Mind alignment and strict aliasing (architecture §9/§18): access an object
  only through its real type after a checked cast.
- `const`-correctness is mandatory: `const cl_button_desc_t *desc`.

---

## 11. Memory allocation

- Go through the application's allocator (`cl_allocator_t`), not `malloc`
  directly, in library code. Take the size from `sizeof(*p)`, not the type name:

```c
p = cl_alloc(alloc, sizeof(*p));        /* not sizeof(struct cl_foo) */
```

- Do not cast the `void *` result without need.
- Widgets are zero-allocated (the `cl_widget_init_base` contract, architecture
  §9).
- Every `alloc` has a matching `free` on all exit paths (see the `goto` cleanup
  in §6).

---

## 12. Conditional compilation and platform isolation

- Minimize `#ifdef` in logic `.c` files. Platform differences are pushed behind
  interfaces (`platform`/`renderer`) and into separate TUs (`src/platform/sdl`,
  `src/render/gl`), not scattered through the core code.
- Avoid `#ifdef` in the middle of functions; branch at the level of build files
  (CMake picks the right TU).
- Annotate the closing `#endif` of a long block with its condition:
  `#endif /* CL_ENABLE_OPENGL */`.

---

## 13. Miscellaneous

- Do not leave commented-out code.
- `switch` over an enum: by default add a `default:` (or `/* fallthrough */`
  for a deliberate fall-through).
- Ternaries only for simple cases; do complex logic with `if`.
- Use C99 **designated initializers**:
  `(cl_window_desc_t){ .title = "X", .width = 800 }`.

---

## 14. Before/after example

Bad:
```c
cl_widget_t* cl_label_create( const cl_label_desc_t *d ){
    if(d==NULL){return NULL;}
    cl_label_t * l=(cl_label_t*)malloc(sizeof(cl_label_t));
    l->text=d->text; return (cl_widget_t*)l;
}
```

Good:
```c
cl_widget_t *cl_label_create(const cl_label_desc_t *desc)
{
    cl_label_t *l;

    if (!desc)
        return NULL;

    l = cl_alloc(desc->alloc, sizeof(*l));
    if (!l)
        return NULL;

    l->text = desc->text;
    return &l->base;
}
```

---

## 15. Symbol export

- Public functions are marked with **`CL_API`** (a generated export macro):
  `__declspec(dllexport/dllimport)` on Windows,
  `__attribute__((visibility("default")))` on GCC/Clang. The default is
  `-fvisibility=hidden`.
- Internal functions are `static` or have no `CL_API`; they are not visible
  outside the `.so`/`.dll`.

---

## 16. Tooling

### 16.1 `.clang-format` (repository root) — the formatting arbiter

```yaml
# key fields
BasedOnStyle: LLVM
IndentWidth: 4
UseTab: Never
ColumnLimit: 80
IndentCaseLabels: true          # case one level deeper than switch (§1)
BreakBeforeBraces: Linux
AllowShortIfStatementsOnASingleLine: false
AllowShortFunctionsOnASingleLine: None
PointerAlignment: Right
SpaceAfterCStyleCast: false
SortIncludes: false
```

`clang-format` does not insert the blank line between groups of `case` (§1) —
that convention is maintained by hand and checked in review.

### 16.2 `.editorconfig`

```ini
root = true
[*.{c,h}]
indent_style = space
indent_size = 4
charset = utf-8
trim_trailing_whitespace = true
insert_final_newline = true
max_line_length = 80
```

### 16.3 Pre-commit hook (mandatory)

`.githooks/pre-commit` checks the formatting of changed `.c/.h` files
(excluding `third_party/`) and blocks the commit on any discrepancy:

```sh
#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
files=$(git diff --cached --name-only --diff-filter=ACM \
        | grep -E '\.(c|h)$' | grep -v '^third_party/')
[ -z "$files" ] && exit 0

status=0
for f in $files; do
    clang-format --dry-run --Werror "$f" || status=1
done

if [ "$status" -ne 0 ]; then
    echo "pre-commit: formatting violations detected." >&2
    echo "Fix them: clang-format -i <files>" >&2
fi
exit $status
```

Activation (once per clone): `git config core.hooksPath .githooks`.
The hook is installed by the instructions in the README/the
`tools/setup-hooks.sh` script.

### 16.4 Miscellaneous
- Files are UTF-8, with LF line endings (including on Windows), enforced via
  `.gitattributes`.
- A `checkpatch.pl`-style tool can be added later as an optional CI check.

---

## 17. License and file header

- The project license is **GPL-3.0-or-later**. The full text is in the `COPYING`
  file at the repository root.
- The first line of every `.c/.h` file (and scripts) is the SPDX identifier:

```c
/* SPDX-License-Identifier: GPL-3.0-or-later */
```

- Note: GPL-3.0 is strong copyleft; applications that link against copal must be
  GPL-compatible. This is an architectural consequence (see ADR-012).

---

## 18. Summary of deviations from pure kernel style

| # | Kernel rule | Our decision | Reason |
|---|----------------|--------------|---------|
| D1 | `struct foo` without a typedef, lowercase | typedef allowed; types `snake_case` + `_t` suffix, public `cl_` prefix | the norm for C libraries (`cairo_t`); FFI/hiding |
| D2 | `u8/u16/u32` types | `<stdint.h>`: `uint32_t` etc. | public userspace/FFI API |
| D3 | tab indentation ×8 | **spaces only, 4 columns** | review decision |
| D4 | `case` at the `switch` level | `case` one level deeper, blank line between groups | review decision |
| D5 | no `extern "C"` | public headers in `extern "C"` | C++ compatibility (spec) |
| D6 | checkpatch sometimes 100 | strictly 80 | predictability |
| D7 | kernel-doc for exported symbols | applied to the public API | spec: doc comments on public functions |

Everything else (K&R braces, `goto` cleanup, spaces around operators,
`sizeof(*p)`, macros, comments, self-contained headers) follows the **kernel
style strictly**.
