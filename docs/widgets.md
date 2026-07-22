# Widgets

The catalog of every built-in widget: how each is created, the desc fields that matter, and its exact functions. For the widget base type, layout boxes (`vbox`/`hbox`), and theming, see [api.md](./api.md).

## Conventions

Every widget is created the same way:

```c
cl_widget_t *w = cl_button_create(app, &(cl_button_desc_t){
    CL_BUTTON_DESC_INIT_FIELDS,
    .text = "Click me",
});
```

- `cl_<w>_create(app, &desc)` takes a filled desc; `desc` may be `NULL` for defaults where the type has one.
- `CL_<W>_DESC_INIT_FIELDS` seeds the mandatory `abi_version`/`struct_size` header — always list it first in the compound literal, then set the fields you care about. (`CL_<W>_DESC_INIT` is the same as a full braced initializer.) The `abi_version`/`struct_size` boilerplate is omitted from the field tables below.
- **Programmatic setters do not fire the change callback.** `cl_*_set_value`, `set_checked`, `set_selected`, `set_text`, etc. update state silently; callbacks fire only on genuine user interaction.

Callback typedefs (from `event.h`):

| Typedef | Signature |
|---|---|
| `cl_action_fn` | `void (*)(cl_widget_t *w, void *user)` |
| `cl_toggled_fn` | `void (*)(cl_widget_t *w, bool checked, void *user)` |
| `cl_value_fn` | `void (*)(cl_widget_t *w, float value, void *user)` |
| `cl_selection_fn` | `void (*)(cl_widget_t *w, int index, void *user)` |
| `cl_text_changed_fn` | `void (*)(cl_widget_t *w, const char *utf8, void *user)` |

## Basic controls

### Label

Single-line text. Measures to its natural text width; no wrapping or ellipsis — a label given less width is clipped to its own rect.

| Field | Type | Notes |
|---|---|---|
| `text` | `const char *` | UTF-8; may be NULL |
| `style` | `const cl_text_style_t *` | NULL → theme defaults |

```c
cl_widget_t *cl_label_create(cl_application_t *app, const cl_label_desc_t *desc);
void         cl_label_set_text(cl_widget_t *label, const char *utf8);
const char  *cl_label_text(cl_widget_t *label);   /* borrowed; valid until next set_text */
```

### Button

Clickable push button.

| Field | Type | Notes |
|---|---|---|
| `text` | `const char *` | UTF-8; may be NULL |

```c
cl_widget_t *cl_button_create(cl_application_t *app, const cl_button_desc_t *desc);
void         cl_button_set_text(cl_widget_t *button, const char *utf8);
const char  *cl_button_text(cl_widget_t *button);   /* borrowed */
void         cl_button_set_on_click(cl_widget_t *button, cl_action_fn fn, void *user);
```

### Checkbox

Two-state box with an optional inline label. Toggles on left click or Space while focused.

| Field | Type | Notes |
|---|---|---|
| `text` | `const char *` | label (UTF-8); may be NULL |
| `checked` | `bool` | initial state |

```c
cl_widget_t *cl_checkbox_create(cl_application_t *app, const cl_checkbox_desc_t *desc);
void         cl_checkbox_set_checked(cl_widget_t *cb, bool checked);   /* no on_toggle */
bool         cl_checkbox_is_checked(cl_widget_t *cb);
void         cl_checkbox_set_text(cl_widget_t *cb, const char *utf8);
const char  *cl_checkbox_text(cl_widget_t *cb);   /* borrowed */
void         cl_checkbox_set_on_toggle(cl_widget_t *cb, cl_toggled_fn fn, void *user);
```

### RadioButton

Radio with an optional inline label. Selecting one deselects every other radio sharing the same **positive** `group` id in the same widget tree; a non-positive `group` (default 0) is ungrouped and independent. Selects on left click or Space; an already-selected radio does not deselect on click.

| Field | Type | Notes |
|---|---|---|
| `text` | `const char *` | label (UTF-8); may be NULL |
| `group` | `int` | mutual-exclusion group id; `<= 0` means ungrouped |
| `selected` | `bool` | initial state |

```c
cl_widget_t *cl_radiobutton_create(cl_application_t *app, const cl_radiobutton_desc_t *desc);
void         cl_radiobutton_set_selected(cl_widget_t *rb, bool selected);   /* no on_select */
bool         cl_radiobutton_is_selected(cl_widget_t *rb);
void         cl_radiobutton_set_text(cl_widget_t *rb, const char *utf8);
const char  *cl_radiobutton_text(cl_widget_t *rb);   /* borrowed */
void         cl_radiobutton_set_on_select(cl_widget_t *rb, cl_toggled_fn fn, void *user);
```

### RadioGroup

A vertical column of mutually exclusive radios the group creates and owns. The options share an auto-assigned exclusion group; do not override the radios' own `on_select` — the group uses it and reports through its own callback.

| Field | Type | Notes |
|---|---|---|
| `spacing` | `float` | vertical gap between the radios |

Callback: `typedef void (*cl_radiogroup_fn)(cl_widget_t *group, int index, void *user);` — `index` is the newly selected option.

```c
cl_widget_t *cl_radiogroup_create(cl_application_t *app, const cl_radiogroup_desc_t *desc);
cl_widget_t *cl_radiogroup_add(cl_widget_t *group, const char *text);   /* returns the radio */
size_t       cl_radiogroup_count(cl_widget_t *group);
int          cl_radiogroup_selected(cl_widget_t *group);   /* -1 = none */
void         cl_radiogroup_set_selected(cl_widget_t *group, int index);   /* -1 clears; no callback */
void         cl_radiogroup_set_on_change(cl_widget_t *group, cl_radiogroup_fn fn, void *user);
```

### Slider

Horizontal draggable thumb selecting a value in `[min, max]`. Drag with the left button, or Left/Right/Up/Down (by step) and Home/End (min/max) while focused. If `max <= min` the range defaults to `[0, 1]`; if `step <= 0` it defaults to `(max - min) / 20`.

| Field | Type | Notes |
|---|---|---|
| `min` / `max` | `float` | range |
| `value` | `float` | initial value (clamped) |
| `step` | `float` | keyboard increment; `0` = `(max - min) / 20` |

```c
cl_widget_t *cl_slider_create(cl_application_t *app, const cl_slider_desc_t *desc);
void         cl_slider_set_value(cl_widget_t *slider, float value);   /* clamped; no on_change */
float        cl_slider_value(cl_widget_t *slider);
float        cl_slider_min(cl_widget_t *slider);
float        cl_slider_max(cl_widget_t *slider);
float        cl_slider_step(cl_widget_t *slider);   /* the effective (resolved) step */
void         cl_slider_set_range(cl_widget_t *slider, float min, float max);   /* re-clamps value; no on_change */
void         cl_slider_set_on_change(cl_widget_t *slider, cl_value_fn fn, void *user);
```

### ProgressBar

Horizontal determinate progress bar.

| Field | Type | Notes |
|---|---|---|
| `value` | `float` | `0..1`, clamped |

```c
cl_widget_t *cl_progressbar_create(cl_application_t *app, const cl_progressbar_desc_t *desc);
void         cl_progressbar_set_value(cl_widget_t *w, float value);   /* 0..1, clamped, repaints */
float        cl_progressbar_value(cl_widget_t *w);
```

## Text input

### TextBox

UTF-8 text entry with codepoint-aware cursor and selection, word navigation (Ctrl+arrow), mouse positioning/selection (drag, Shift+click extends, double-click selects a word), clipboard cut/copy/paste (Ctrl+X/C/V), and undo/redo (Ctrl+Z / Ctrl+Y or Ctrl+Shift+Z).

- **`multiline`**: wraps to width and keeps explicit line breaks. Enter inserts a newline (never submits), Up/Down move between lines, Home/End act per visual line (Ctrl+Home/End jump to document ends), content scrolls vertically.
- **`password`**: masks characters (single-line only). `password + multiline` is rejected by `cl_textbox_create` with `CL_ERROR_INVALID_ARGUMENT` — the multiline paint has no masking.
- **IME**: a pre-edit composition string is shown underlined at the caret without entering the buffer until committed; `cl_textbox_preedit()` exposes it.

| Field | Type | Notes |
|---|---|---|
| `text` | `const char *` | initial text (UTF-8); may be NULL |
| `placeholder` | `const char *` | shown when empty and unfocused; may be NULL |
| `password` | `bool` | mask characters (single-line only) |
| `readonly` | `bool` | navigation allowed, editing blocked |
| `multiline` | `bool` | wrap to width, keep newlines, scroll vertically |
| `max_length` | `size_t` | max codepoints; `0` = unlimited |

```c
cl_widget_t *cl_textbox_create(cl_application_t *app, const cl_textbox_desc_t *desc);
void         cl_textbox_set_text(cl_widget_t *tb, const char *utf8);   /* no on_changed */
const char  *cl_textbox_text(cl_widget_t *tb);   /* NUL-terminated UTF-8; valid until edited */
void         cl_textbox_set_on_changed(cl_widget_t *tb, cl_text_changed_fn fn, void *user);
void         cl_textbox_set_on_change(cl_widget_t *tb, cl_text_changed_fn fn, void *user);   /* alias of set_on_changed */
void         cl_textbox_set_on_submit(cl_widget_t *tb, cl_text_changed_fn fn, void *user);   /* single-line Enter only */
size_t       cl_textbox_line_count(cl_widget_t *tb);    /* wrapped visual lines; 1 if single-line */
size_t       cl_textbox_cursor_line(cl_widget_t *tb);   /* caret's visual line; 0 if single-line */
const char  *cl_textbox_preedit(cl_widget_t *tb);       /* IME composition, or NULL */
```

`cl_textbox_set_on_change` is an alias of `cl_textbox_set_on_changed`, spelled to match the other widgets' `set_on_change` setters.

## Selection & lists

### List

Selectable vertical list of text items. Click selects and focuses; Up/Down/Home/End/PageUp/PageDown move the selection; double-click or Enter activates the selected item. The list measures to its full content — put it in a [ScrollView](#scrollview) for long content. The desc has no meaningful fields.

Callback: `typedef void (*cl_list_fn)(cl_widget_t *list, int index, void *user);` — `index` is the affected item, or `-1` when the selection was cleared.

```c
cl_widget_t *cl_list_create(cl_application_t *app, const cl_list_desc_t *desc);
cl_result_t  cl_list_add_item(cl_widget_t *list, const char *text);   /* text copied */
cl_result_t  cl_list_remove(cl_widget_t *list, size_t index);
void         cl_list_clear(cl_widget_t *list);
size_t       cl_list_count(cl_widget_t *list);
const char  *cl_list_item_text(cl_widget_t *list, size_t index);   /* borrowed; NULL for bad index */
int          cl_list_selected(cl_widget_t *list);   /* -1 = none */
void         cl_list_set_selected(cl_widget_t *list, int index);   /* -1 clears; no on_select */
void         cl_list_set_on_select(cl_widget_t *list, cl_list_fn fn, void *user);
void         cl_list_set_on_activate(cl_widget_t *list, cl_list_fn fn, void *user);   /* double-click / Enter */
```

### ComboBox

Drop-down selector: shows the selected item's text with a caret; clicking it (or Space/Enter/Down while focused) opens a popup list in the window overlay layer.

| Field | Type | Notes |
|---|---|---|
| `placeholder` | `const char *` | shown when nothing is selected; may be NULL |

Change callback uses `cl_selection_fn`.

```c
cl_widget_t *cl_combobox_create(cl_application_t *app, const cl_combobox_desc_t *desc);
cl_result_t  cl_combobox_add_item(cl_widget_t *combo, const char *text);
size_t       cl_combobox_count(cl_widget_t *combo);
const char  *cl_combobox_item_text(cl_widget_t *combo, size_t index);   /* borrowed; NULL for bad index */
cl_result_t  cl_combobox_remove(cl_widget_t *combo, size_t index);   /* no on_change */
void         cl_combobox_clear(cl_widget_t *combo);
void         cl_combobox_set_selected(cl_widget_t *combo, int index);   /* -1 = none; no on_change */
int          cl_combobox_selected(cl_widget_t *combo);   /* -1 = none */
const char  *cl_combobox_selected_text(cl_widget_t *combo);   /* or NULL */
void         cl_combobox_set_on_change(cl_widget_t *combo, cl_selection_fn fn, void *user);
```

## Containers

The layout boxes `vbox`/`hbox` are documented in [api.md](./api.md); [ScrollView](#scrollview) is below.

### Panel

A framed grouping surface. Paints a rounded themed surface (optionally bordered) and arranges every child to fill its padded content box — put a `vbox`/`hbox` inside for real layout.

| Field | Type | Notes |
|---|---|---|
| `padding` | `cl_insets_t` | content-box insets |
| `bordered` | `bool` | stroke a border around the surface |

```c
cl_widget_t *cl_panel_create(cl_application_t *app, const cl_panel_desc_t *desc);
```

### Spacer

Empty space inside a box: a fixed gap or a flexible one.

| Field | Type | Notes |
|---|---|---|
| `width` / `height` | `float` | fixed size; `0` = none |
| `flex` | `float` | `> 0`: grab that share of the box's leftover space |

```c
cl_widget_t *cl_spacer_create(cl_application_t *app, const cl_spacer_desc_t *desc);
```

## Popups & menus

### Menu

Popup menu: a vertical list of items shown in the window overlay layer. Hover highlights; left click or Enter activates and closes the chain; Escape dismisses the topmost menu; a click outside dismisses the chain. Build it, then hand it to `cl_window_open_popup`, which takes ownership. The desc has no meaningful fields.

```c
cl_widget_t *cl_menu_create(cl_application_t *app, const cl_menu_desc_t *desc);
cl_result_t  cl_menu_add_item(cl_widget_t *menu, const char *text, cl_action_fn fn, void *user);
cl_result_t  cl_menu_add_submenu(cl_widget_t *menu, const char *text, cl_widget_t *submenu);   /* takes ownership of submenu */
size_t       cl_menu_count(cl_widget_t *menu);
const char  *cl_menu_item_text(cl_widget_t *menu, size_t index);   /* borrowed; NULL for bad index */
cl_result_t  cl_menu_remove(cl_widget_t *menu, size_t index);   /* not allowed while open */
void         cl_menu_clear(cl_widget_t *menu);   /* not allowed while open */
```

A submenu item opens `submenu` (another menu widget) beside it on click, Enter or Right; the parent takes ownership and reuses the widget across opens.

### MenuBar

A horizontal bar of menu titles. Clicking a title opens its menu right below it; clicking again (or anywhere outside) dismisses the chain. Typically the first child of the window's root vbox, stretched across the top. The desc has no meaningful fields.

```c
cl_widget_t *cl_menubar_create(cl_application_t *app, const cl_menubar_desc_t *desc);
cl_result_t  cl_menubar_add_menu(cl_widget_t *bar, const char *title, cl_widget_t *menu);   /* takes ownership of menu */
size_t       cl_menubar_count(cl_widget_t *bar);
```

## Images

### ImageView

Draws a `cl_image_t`. Measures to the image's pixel size (override per axis with `cl_widget_set_preferred_size`); the image is stretched to the widget's rect when layout assigns a different size. The image is **borrowed** — keep it alive while the widget uses it, release it after the widget is gone.

| Field | Type | Notes |
|---|---|---|
| `image` | `cl_image_t *` | borrowed, not owned; may be NULL (paints nothing) |

```c
cl_widget_t *cl_imageview_create(cl_application_t *app, const cl_imageview_desc_t *desc);
void         cl_imageview_set_image(cl_widget_t *w, cl_image_t *image);   /* borrowed; NULL ok */
cl_image_t  *cl_imageview_image(cl_widget_t *w);   /* or NULL */
```

### cl_image_t resource

An image is a raw RGBA8 pixel buffer (`image.h`). The library does **not** decode image files — decode with your own codec (or embed pixel arrays for icons) and hand the raw pixels here. Every image must be released **before** the application that created it is destroyed, and a released handle must never be used again.

```c
cl_image_t  *cl_image_create(cl_application_t *app, int w, int h, const void *rgba);
/* w,h > 0; rgba is w*h*4 bytes, row-major, straight (non-premultiplied) alpha; copied.
   Sizes whose byte total overflows size_t are rejected (CL_ERROR_INVALID_ARGUMENT). */
void         cl_image_release(cl_image_t *img);
cl_size_t    cl_image_size(const cl_image_t *img);      /* pixels; drawn 1:1 in logical px */
const void  *cl_image_pixels(const cl_image_t *img);    /* RGBA8 data (for custom backends) */
```

## Dialogs

### MessageBox

A modal message box over the window's content. Shows an optional `title` and `text` centred in the window with the requested buttons; outside clicks are swallowed. The callback fires once with the chosen button index and the dialog closes; the window owns the dialog.

Buttons (`cl_msgbox_buttons_t`): `CL_MSGBOX_OK`, `CL_MSGBOX_OK_CANCEL`, `CL_MSGBOX_YES_NO`.

Callback: `typedef void (*cl_msgbox_fn)(cl_widget_t *dialog, int index, void *user);` — `index` is `0` for OK/Yes (also Enter) and `1` for Cancel/No (also Escape). `dialog` is valid for the duration of the callback.

```c
cl_widget_t *cl_messagebox_show(cl_window_t *win, const char *title, const char *text,
                                cl_msgbox_buttons_t buttons, cl_msgbox_fn fn, void *user);
```

Returns the dialog widget (rarely needed — e.g. to restyle it or close it early via `cl_window_close_popup`), or **NULL if the overlay stack is full**.

## ScrollView

Scroll container holding a single content widget, clipping it to the viewport and scrolling via wheel or draggable scrollbars. By default only vertical scrolling is enabled (content is laid out at viewport width, so wrapping content reflows); `horizontal` also allows sideways overflow. A descendant that gains keyboard focus is scrolled into view automatically.

| Field | Type | Notes |
|---|---|---|
| `horizontal` | `bool` | allow horizontal overflow and scrolling |
| `smooth` | `bool` | ease wheel scrolling over a few frames instead of jumping (needs a platform clock; falls back to instant) |

```c
cl_widget_t *cl_scrollview_create(cl_application_t *app, const cl_scrollview_desc_t *desc);
void         cl_scrollview_set_content(cl_widget_t *sv, cl_widget_t *content);   /* takes ownership; destroys previous, resets offset */
cl_widget_t *cl_scrollview_content(cl_widget_t *sv);   /* or NULL */
void         cl_scrollview_scroll_to(cl_widget_t *sv, float y);       /* vertical offset (clamped) */
void         cl_scrollview_scroll_to_x(cl_widget_t *sv, float x);     /* horizontal; needs horizontal enabled */
float        cl_scrollview_scroll_y(cl_widget_t *sv);   /* current vertical offset (px) */
float        cl_scrollview_scroll_x(cl_widget_t *sv);   /* current horizontal offset (px) */
void         cl_scrollview_scroll_to_widget(cl_widget_t *sv, cl_widget_t *descendant);   /* scroll minimally to reveal descendant */
```

**Programmatic scrolling needs a completed layout.** `scroll_to`, `scroll_to_x` and `scroll_to_widget` are computed from the laid-out geometry, which is known only after the first layout: call them after the first frame. Before that, offsets clamp to 0 and `scroll_to_widget` is a no-op. (Automatic scroll-to-focus does not need this — it defers itself until after layout.)

---
*See also: [API Reference](./api.md) · [Extending](./extending.md) · [Architecture](./architecture.md)*
