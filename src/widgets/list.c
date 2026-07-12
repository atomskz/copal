/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/list.h>
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <string.h>

#include "widget/widget_internal.h"
#include "core/foundation/foundation_internal.h"
#include "theme/theme_internal.h"

#define LIST_HPAD 10.0f
#define LIST_VPAD 4.0f   /* list top/bottom padding */
#define ROW_VPAD 4.0f    /* per-row vertical padding */
#define LIST_MIN_W 160.0f
#define FALLBACK_LH 16.0f
#define PAGE_ROWS 10     /* PageUp/PageDown step (no viewport knowledge) */

typedef struct cl_list {
    cl_widget_t base;
    char **items;
    size_t count;
    size_t cap;
    int selected; /* -1 = none */
    int hovered;  /* -1 = none */
    cl_list_fn on_select;
    void *on_select_user;
    cl_list_fn on_activate;
    void *on_activate_user;
} cl_list_t;

static cl_size_t list_measure(cl_widget_t *w, cl_constraints_t c);
static void list_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool list_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool list_mouse_move(cl_widget_t *w, const cl_event_t *ev);
static void list_mouse_leave(cl_widget_t *w);
static bool list_key_down(cl_widget_t *w, const cl_event_t *ev);
static void list_destroy(cl_widget_t *w);

static const cl_widget_vtable_t list_vtable = {
    .destroy = list_destroy,
    .measure = list_measure,
    .paint = list_paint,
    .mouse_down = list_mouse_down,
    .mouse_move = list_mouse_move,
    .mouse_leave = list_mouse_leave,
    .key_down = list_key_down,
};

static const cl_widget_class_t cl_list_class = {
    .name = "cl_list",
    .base = NULL,
    .type_id = 0x6c697374u, /* 'list' */
    .instance_size = sizeof(cl_list_t),
    .vtable = &list_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static cl_font_t *list_font(cl_widget_t *w)
{
    return cl_theme_font(cl_application_theme(w->app));
}

static float row_height(cl_widget_t *w)
{
    cl_font_t *font = list_font(w);
    float lh = font ? cl_font_metrics(font).line_height : FALLBACK_LH;

    return lh + 2.0f * ROW_VPAD;
}

/* Row index at window position p, or -1. */
static int row_at(cl_list_t *l, cl_point_t p)
{
    float top = l->base.rect.y + LIST_VPAD;
    float rh = row_height(&l->base);
    int idx;

    if (!cl_rect_contains(l->base.rect, p) || rh <= 0.0f || p.y < top)
        return -1;
    idx = (int)((p.y - top) / rh);
    if (idx < 0 || (size_t)idx >= l->count)
        return -1;
    return idx;
}

static void select_index(cl_list_t *l, int idx, bool notify)
{
    if (idx < -1 || (idx >= 0 && (size_t)idx >= l->count))
        return;
    if (l->selected == idx)
        return;
    l->selected = idx;
    cl_widget_invalidate(&l->base);
    if (notify && l->on_select)
        l->on_select(&l->base, idx, l->on_select_user); /* last: may destroy */
}

static cl_size_t list_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, w);
    cl_font_t *font = list_font(w);
    float maxw = 0.0f;
    size_t i;
    cl_size_t out;

    (void)c;
    if (font) {
        for (i = 0; i < l->count; i++) {
            float tw = cl_text_measure(font, l->items[i], CL_UNBOUNDED).w;

            if (tw > maxw)
                maxw = tw;
        }
    }
    out.w = maxw + 2.0f * LIST_HPAD;
    if (out.w < LIST_MIN_W)
        out.w = LIST_MIN_W;
    out.h = (float)l->count * row_height(w) + 2.0f * LIST_VPAD;
    return out;
}

static void list_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, w);
    cl_font_t *font = cl_theme_font(cl_paint_theme(ctx));
    float rh = row_height(w);
    cl_rect_t r = w->rect;
    size_t i;

    cl_paint_fill_rect(ctx, r, cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    for (i = 0; i < l->count; i++) {
        float ry = r.y + LIST_VPAD + (float)i * rh;
        cl_rect_t row = { r.x, ry, r.w, rh };

        if ((int)i == l->selected)
            cl_paint_fill_rect(ctx, row,
                               cl_paint_theme_color(ctx, CL_COLOR_SELECTION));
        else if ((int)i == l->hovered)
            cl_paint_fill_rect(
                ctx, row, cl_paint_theme_color(ctx, CL_COLOR_SURFACE_HOVER));
        if (font)
            cl_paint_draw_text(ctx, font, l->items[i],
                               (cl_point_t){ r.x + LIST_HPAD, ry + ROW_VPAD },
                               cl_paint_theme_color(ctx, CL_COLOR_TEXT));
    }
    if (cl_widget_has_focus(w))
        cl_paint_stroke_round_rect(
            ctx, r, 0.0f, 1.0f,
            cl_paint_theme_color(ctx, CL_COLOR_FOCUS_RING));
}

static bool list_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, w);
    int idx;

    if (ev->data.mouse.button != CL_MOUSE_LEFT)
        return false;
    idx = row_at(l, ev->data.mouse.pos);
    if (idx < 0)
        return true; /* padding area: consume, keep selection */
    if (ev->data.mouse.clicks >= 2 && idx == l->selected) {
        if (l->on_activate)
            l->on_activate(w, idx, l->on_activate_user); /* may destroy */
        return true;
    }
    select_index(l, idx, true);
    return true;
}

static bool list_mouse_move(cl_widget_t *w, const cl_event_t *ev)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, w);
    int idx = row_at(l, ev->data.mouse.pos);

    if (idx != l->hovered) {
        l->hovered = idx;
        cl_widget_invalidate(w);
    }
    return false; /* let ancestors (scrollview) see moves too */
}

static void list_mouse_leave(cl_widget_t *w)
{
    cl_list_t *l = CL_WIDGET_CAST_UNCHECKED(cl_list, w);

    if (l->hovered != -1) {
        l->hovered = -1;
        cl_widget_invalidate(w);
    }
}

static bool list_key_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, w);
    int n = (int)l->count;
    int idx = l->selected;

    if (n == 0)
        return false;
    switch (ev->data.key.key) {
        case CL_KEY_DOWN:
            select_index(l, idx < 0 ? 0 : (idx + 1 < n ? idx + 1 : n - 1),
                         true);
            return true;

        case CL_KEY_UP:
            select_index(l, idx <= 0 ? 0 : idx - 1, true);
            return true;

        case CL_KEY_HOME:
            select_index(l, 0, true);
            return true;

        case CL_KEY_END:
            select_index(l, n - 1, true);
            return true;

        case CL_KEY_PAGE_DOWN:
            select_index(l, idx + PAGE_ROWS < n ? idx + PAGE_ROWS : n - 1,
                         true);
            return true;

        case CL_KEY_PAGE_UP:
            select_index(l, idx > PAGE_ROWS ? idx - PAGE_ROWS : 0, true);
            return true;

        case CL_KEY_ENTER:
            if (l->selected >= 0 && l->on_activate)
                l->on_activate(w, l->selected,
                               l->on_activate_user); /* may destroy */
            return true;

        default:
            return false;
    }
}

static void list_destroy(cl_widget_t *w)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, w);
    const cl_allocator_t *a = cl_application_allocator(w->app);
    size_t i;

    for (i = 0; i < l->count; i++)
        cl_free(a, l->items[i]);
    cl_free(a, l->items);
}

cl_widget_t *cl_list_create(cl_application_t *app, const cl_list_desc_t *desc)
{
    cl_widget_t *w;
    cl_list_t *l;

    if (!CL_DESC_ABI_OK(desc, cl_list_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_list_class);
    if (!w)
        return NULL;
    w->flags |= CL_WF_FOCUSABLE;
    l = CL_WIDGET_CAST(cl_list, w);
    l->selected = -1;
    l->hovered = -1;
    return w;
}

cl_result_t cl_list_add_item(cl_widget_t *list, const char *text)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, list);
    const cl_allocator_t *a;
    char *dup;

    if (!l || !text)
        return CL_ERROR_INVALID_ARGUMENT;
    a = cl_application_allocator(list->app);
    if (l->count == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 8;
        char **ni = cl_realloc(a, l->items, nc * sizeof(*ni));

        if (!ni)
            return CL_ERROR_OUT_OF_MEMORY;
        l->items = ni;
        l->cap = nc;
    }
    dup = cl_strdup(a, text);
    if (!dup)
        return CL_ERROR_OUT_OF_MEMORY;
    l->items[l->count++] = dup;
    cl_widget_invalidate_layout(list);
    return CL_OK;
}

cl_result_t cl_list_remove(cl_widget_t *list, size_t index)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, list);
    size_t i;

    if (!l || index >= l->count)
        return CL_ERROR_INVALID_ARGUMENT;
    cl_free(cl_application_allocator(list->app), l->items[index]);
    for (i = index; i + 1 < l->count; i++)
        l->items[i] = l->items[i + 1];
    l->count--;
    if (l->selected >= 0) {
        if ((size_t)l->selected == index)
            l->selected = -1; /* the selected row is gone */
        else if ((size_t)l->selected > index)
            l->selected--; /* shifted up */
    }
    if (l->hovered >= 0 && (size_t)l->hovered >= l->count)
        l->hovered = -1;
    cl_widget_invalidate_layout(list);
    return CL_OK;
}

void cl_list_clear(cl_widget_t *list)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, list);
    const cl_allocator_t *a;
    size_t i;

    if (!l)
        return;
    a = cl_application_allocator(list->app);
    for (i = 0; i < l->count; i++)
        cl_free(a, l->items[i]);
    l->count = 0;
    l->selected = -1;
    l->hovered = -1;
    cl_widget_invalidate_layout(list);
}

size_t cl_list_count(cl_widget_t *list)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, list);

    return l ? l->count : 0;
}

const char *cl_list_item_text(cl_widget_t *list, size_t index)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, list);

    if (!l || index >= l->count) {
        cl_set_last_error(CL_ERROR_INVALID_ARGUMENT);
        return NULL;
    }
    return l->items[index];
}

int cl_list_selected(cl_widget_t *list)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, list);

    return l ? l->selected : -1;
}

void cl_list_set_selected(cl_widget_t *list, int index)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, list);

    if (l)
        select_index(l, index, true);
}

void cl_list_set_on_select(cl_widget_t *list, cl_list_fn fn, void *user)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, list);

    if (l) {
        l->on_select = fn;
        l->on_select_user = user;
    }
}

void cl_list_set_on_activate(cl_widget_t *list, cl_list_fn fn, void *user)
{
    cl_list_t *l = CL_WIDGET_CAST(cl_list, list);

    if (l) {
        l->on_activate = fn;
        l->on_activate_user = user;
    }
}
