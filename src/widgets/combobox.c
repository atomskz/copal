/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/combobox.h>
#include <copal/widgets/menu.h>
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/allocator.h>
#include <copal/window.h>

#include <stdint.h>
#include <string.h>

#include "widget/widget_internal.h"
#include "app/app_internal.h"
#include "theme/theme_internal.h"

#define CB_PAD_X 10.0f
#define CB_PAD_Y 5.0f
#define CB_ARROW_W 20.0f
#define CB_MIN_W 120.0f
#define CB_RADIUS 4.0f
#define FALLBACK_LH 16.0f
#define CB_ARROW "\xE2\x96\xBE" /* U+25BE BLACK DOWN-POINTING SMALL TRIANGLE */

typedef struct cl_combobox {
    cl_widget_t base;
    char **items;
    size_t count;
    size_t cap;
    int selected;
    char *placeholder;
    cl_selection_fn on_change;
    void *user;
} cl_combobox_t;

static cl_size_t combo_measure(cl_widget_t *w, cl_constraints_t c);
static void combo_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool combo_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool combo_mouse_up(cl_widget_t *w, const cl_event_t *ev);
static bool combo_key_down(cl_widget_t *w, const cl_event_t *ev);
static void combo_focus_changed(cl_widget_t *w);
static void combo_destroy(cl_widget_t *w);

static const cl_widget_vtable_t combo_vtable = {
    .destroy = combo_destroy,
    .measure = combo_measure,
    .paint = combo_paint,
    .mouse_down = combo_mouse_down,
    .mouse_up = combo_mouse_up,
    .key_down = combo_key_down,
    .focus_gained = combo_focus_changed,
    .focus_lost = combo_focus_changed,
};

static const cl_widget_class_t cl_combobox_class = {
    .name = "cl_combobox",
    .base = NULL,
    .type_id = 0x636f6d62u, /* 'comb' */
    .instance_size = sizeof(cl_combobox_t),
    .vtable = &combo_vtable,
};

static char *dup_str(const cl_allocator_t *a, const char *s)
{
    size_t n;
    char *p;

    if (!s)
        return NULL;
    n = strlen(s) + 1;
    p = cl_alloc(a, n);
    if (p)
        memcpy(p, s, n);
    return p;
}

static const char *display_text(cl_combobox_t *cb)
{
    if (cb->selected >= 0 && (size_t)cb->selected < cb->count)
        return cb->items[cb->selected];
    return cb->placeholder;
}

static void select_index(cl_widget_t *w, int index, bool notify)
{
    cl_combobox_t *cb = CL_WIDGET_CAST(cl_combobox, w);

    if (index < -1 || index >= (int)cb->count || index == cb->selected)
        return;
    cb->selected = index;
    cl_widget_invalidate(w);
    if (notify && cb->on_change)
        cb->on_change(w, index, cb->user);
}

/*
 * Menu item callback: the owning combo is the menu's userdata and the item's
 * index is encoded in `user`, so no pointer into the combo's (growable) storage
 * is ever handed out.
 */
static void combo_item_chosen(cl_widget_t *menu_w, void *user)
{
    cl_widget_t *combo = cl_widget_userdata(menu_w);

    if (combo)
        select_index(combo, (int)(intptr_t)user, true);
}

static void open_dropdown(cl_widget_t *w)
{
    cl_combobox_t *cb = CL_WIDGET_CAST(cl_combobox, w);
    cl_window_t *win = cl_widget_window(w);
    cl_widget_t *menu;
    size_t i;

    if (!win || cb->count == 0)
        return;
    menu = cl_menu_create(w->app);
    if (!menu)
        return;
    cl_widget_set_userdata(menu, w); /* combo back-reference for the callback */
    for (i = 0; i < cb->count; i++)
        cl_menu_add_item(menu, cb->items[i], combo_item_chosen,
                         (void *)(intptr_t)i);
    cl_window_open_popup(win, menu,
                         (cl_point_t){ w->rect.x, w->rect.y + w->rect.h });
    /* Tie the popup's lifetime to this combo: if the combo is destroyed while
     * the dropdown is open, the window tears the popup down (no dangling refs). */
    cl_window_set_overlay_owner(win, w);
}

static cl_size_t combo_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_combobox_t *cb = CL_WIDGET_CAST(cl_combobox, w);
    cl_font_t *font = cl_theme_font(cl_application_theme(w->app));
    float lh = font ? cl_font_metrics(font).line_height : FALLBACK_LH;
    float maxw = 0.0f;
    size_t i;
    cl_size_t out;

    (void)c;
    if (font) {
        if (cb->placeholder)
            maxw = cl_text_measure(font, cb->placeholder, CL_UNBOUNDED).w;
        for (i = 0; i < cb->count; i++) {
            float tw = cl_text_measure(font, cb->items[i], CL_UNBOUNDED).w;

            if (tw > maxw)
                maxw = tw;
        }
    }
    out.w = maxw + 2.0f * CB_PAD_X + CB_ARROW_W;
    if (out.w < CB_MIN_W)
        out.w = CB_MIN_W;
    out.h = lh + 2.0f * CB_PAD_Y;
    return out;
}

static void combo_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_combobox_t *cb = CL_WIDGET_CAST(cl_combobox, w);
    cl_font_t *font = cl_theme_font(cl_paint_theme(ctx));
    bool focused = cl_widget_has_focus(w);
    const char *text = display_text(cb);
    bool has_sel = cb->selected >= 0 && (size_t)cb->selected < cb->count;
    float lh = font ? cl_font_metrics(font).line_height : FALLBACK_LH;
    float ty = w->rect.y + (w->rect.h - lh) * 0.5f;

    cl_paint_fill_round_rect(ctx, w->rect, CB_RADIUS,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    cl_paint_stroke_round_rect(
        ctx, w->rect, CB_RADIUS, focused ? 2.0f : 1.0f,
        cl_paint_theme_color(ctx, focused ? CL_COLOR_FOCUS_RING
                                          : CL_COLOR_BORDER));

    if (font && text && text[0])
        cl_paint_draw_text(
            ctx, font, text, (cl_point_t){ w->rect.x + CB_PAD_X, ty },
            cl_paint_theme_color(ctx, has_sel ? CL_COLOR_TEXT
                                              : CL_COLOR_TEXT_MUTED));

    if (font) {
        cl_size_t as = cl_text_measure(font, CB_ARROW, CL_UNBOUNDED);
        cl_point_t ap = { w->rect.x + w->rect.w - CB_ARROW_W +
                              (CB_ARROW_W - as.w) * 0.5f,
                          ty };

        cl_paint_draw_text(ctx, font, CB_ARROW, ap,
                           cl_paint_theme_color(ctx, CL_COLOR_TEXT_MUTED));
    }
}

static bool combo_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    (void)w;
    /* Consume the press so the release routes back here; open on release so the
     * click that opens the dropdown is fully spent before the popup captures. */
    return ev->data.mouse.button == CL_MOUSE_LEFT;
}

static bool combo_mouse_up(cl_widget_t *w, const cl_event_t *ev)
{
    if (ev->data.mouse.button != CL_MOUSE_LEFT)
        return false;
    open_dropdown(w);
    return true;
}

static bool combo_key_down(cl_widget_t *w, const cl_event_t *ev)
{
    switch (ev->data.key.key) {
        case CL_KEY_SPACE:
        case CL_KEY_ENTER:
        case CL_KEY_DOWN:
            open_dropdown(w);
            return true;

        default:
            return false;
    }
}

static void combo_focus_changed(cl_widget_t *w)
{
    cl_widget_invalidate(w);
}

static void combo_destroy(cl_widget_t *w)
{
    cl_combobox_t *cb = CL_WIDGET_CAST(cl_combobox, w);
    const cl_allocator_t *a = cl_application_allocator(w->app);
    size_t i;

    for (i = 0; i < cb->count; i++)
        cl_free(a, cb->items[i]);
    cl_free(a, cb->items);
    cl_free(a, cb->placeholder);
}

cl_widget_t *cl_combobox_create(cl_application_t *app,
                                const cl_combobox_desc_t *desc)
{
    cl_widget_t *w;
    cl_combobox_t *cb;

    if (!CL_DESC_ABI_OK(desc, cl_combobox_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_combobox_class);
    if (!w)
        return NULL;
    cb = CL_WIDGET_CAST(cl_combobox, w);
    w->flags |= CL_WF_FOCUSABLE;
    cb->selected = -1;
    if (desc)
        cb->placeholder = dup_str(cl_application_allocator(app),
                                  desc->placeholder);
    return w;
}

cl_result_t cl_combobox_add_item(cl_widget_t *combo, const char *text)
{
    cl_combobox_t *cb = CL_WIDGET_CAST(cl_combobox, combo);
    const cl_allocator_t *a;
    char *dup;

    if (!cb || !text)
        return CL_ERROR_INVALID_ARGUMENT;
    a = cl_application_allocator(combo->app);

    if (cb->count == cb->cap) {
        size_t nc = cb->cap ? cb->cap * 2 : 4;
        char **ni = cl_realloc(a, cb->items, nc * sizeof(*ni));

        if (!ni)
            return CL_ERROR_OUT_OF_MEMORY;
        cb->items = ni;
        cb->cap = nc;
    }

    dup = dup_str(a, text);
    if (!dup)
        return CL_ERROR_OUT_OF_MEMORY;
    cb->items[cb->count] = dup;
    cb->count++;
    cl_widget_invalidate_layout(combo);
    return CL_OK;
}

size_t cl_combobox_count(cl_widget_t *combo)
{
    cl_combobox_t *cb = CL_WIDGET_CAST(cl_combobox, combo);

    return cb ? cb->count : 0;
}

void cl_combobox_set_selected(cl_widget_t *combo, int index)
{
    select_index(combo, index, false);
}

int cl_combobox_selected(cl_widget_t *combo)
{
    cl_combobox_t *cb = CL_WIDGET_CAST(cl_combobox, combo);

    return cb ? cb->selected : -1;
}

const char *cl_combobox_selected_text(cl_widget_t *combo)
{
    cl_combobox_t *cb = CL_WIDGET_CAST(cl_combobox, combo);

    if (cb && cb->selected >= 0 && (size_t)cb->selected < cb->count)
        return cb->items[cb->selected];
    return NULL;
}

void cl_combobox_set_on_change(cl_widget_t *combo, cl_selection_fn fn,
                               void *user)
{
    cl_combobox_t *cb = CL_WIDGET_CAST(cl_combobox, combo);

    if (!cb)
        return;
    cb->on_change = fn;
    cb->user = user;
}
