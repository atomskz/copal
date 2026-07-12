/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/menubar.h>
#include <copal/widgets/menu.h>
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <string.h>

#include "widget/widget_internal.h"
#include "widget/widget_host.h"
#include "core/foundation/foundation_internal.h"
#include "theme/theme_internal.h"

#define BAR_HPAD 10.0f /* title left/right padding */
#define BAR_VPAD 5.0f
#define FALLBACK_LH 16.0f

typedef struct bar_item {
    char *title;
    cl_widget_t *menu; /* owned; pushed as a non-owned overlay when open */
} bar_item_t;

typedef struct cl_menubar {
    cl_widget_t base;
    bar_item_t *items;
    size_t count;
    size_t cap;
    int hovered; /* hovered title index, or -1 */
} cl_menubar_t;

static cl_size_t menubar_measure(cl_widget_t *w, cl_constraints_t c);
static void menubar_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool menubar_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool menubar_mouse_move(cl_widget_t *w, const cl_event_t *ev);
static void menubar_mouse_leave(cl_widget_t *w);
static void menubar_destroy(cl_widget_t *w);

static const cl_widget_vtable_t menubar_vtable = {
    .destroy = menubar_destroy,
    .measure = menubar_measure,
    .paint = menubar_paint,
    .mouse_down = menubar_mouse_down,
    .mouse_move = menubar_mouse_move,
    .mouse_leave = menubar_mouse_leave,
};

static const cl_widget_class_t cl_menubar_class = {
    .name = "cl_menubar",
    .base = NULL,
    .type_id = 0x6d626172u, /* 'mbar' */
    .instance_size = sizeof(cl_menubar_t),
    .vtable = &menubar_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static cl_font_t *bar_font(cl_widget_t *w)
{
    return cl_theme_font(cl_application_theme(w->app));
}

static float title_width(cl_widget_t *w, const char *title)
{
    cl_font_t *font = bar_font(w);
    float tw = font && title ? cl_text_measure(font, title, CL_UNBOUNDED).w
                             : 0.0f;

    return tw + 2.0f * BAR_HPAD;
}

/* The x extent of title idx within the bar. */
static void title_span(cl_menubar_t *b, int idx, float *x0, float *x1)
{
    float x = b->base.rect.x;
    int i;

    for (i = 0; i < idx; i++)
        x += title_width(&b->base, b->items[i].title);
    *x0 = x;
    *x1 = x + title_width(&b->base, b->items[idx].title);
}

/* Title index at window position p, or -1. */
static int title_at(cl_menubar_t *b, cl_point_t p)
{
    float x = b->base.rect.x;
    size_t i;

    if (!cl_rect_contains(b->base.rect, p))
        return -1;
    for (i = 0; i < b->count; i++) {
        float w = title_width(&b->base, b->items[i].title);

        if (p.x >= x && p.x < x + w)
            return (int)i;
        x += w;
    }
    return -1;
}

static cl_size_t menubar_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_menubar_t *b = CL_WIDGET_CAST(cl_menubar, w);
    cl_font_t *font = bar_font(w);
    float lh = font ? cl_font_metrics(font).line_height : FALLBACK_LH;
    cl_size_t out = { 0.0f, lh + 2.0f * BAR_VPAD };
    size_t i;

    (void)c;
    for (i = 0; i < b->count; i++)
        out.w += title_width(w, b->items[i].title);
    return out;
}

static void menubar_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_menubar_t *b = CL_WIDGET_CAST(cl_menubar, w);
    cl_font_t *font = cl_theme_font(cl_paint_theme(ctx));
    size_t i;
    float x = w->rect.x;

    cl_paint_fill_rect(ctx, w->rect,
                       cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    for (i = 0; i < b->count; i++) {
        float tw = title_width(w, b->items[i].title);
        bool open = b->items[i].menu &&
                    cl_widget_window(b->items[i].menu) != NULL;

        if (open || (int)i == b->hovered) {
            cl_rect_t hl = { x, w->rect.y, tw, w->rect.h };

            cl_paint_fill_rect(
                ctx, hl,
                cl_paint_theme_color(ctx, open ? CL_COLOR_SURFACE_ACTIVE
                                               : CL_COLOR_SURFACE_HOVER));
        }
        if (font && b->items[i].title) {
            cl_point_t p = { x + BAR_HPAD, w->rect.y + BAR_VPAD };

            cl_paint_draw_text(ctx, font, b->items[i].title, p,
                               cl_paint_theme_color(ctx, CL_COLOR_TEXT));
        }
        x += tw;
    }
}

static bool menubar_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_menubar_t *b = CL_WIDGET_CAST(cl_menubar, w);
    cl_widget_host_t *h = cl_widget_host(w);
    int idx = title_at(b, ev->data.mouse.pos);
    float x0, x1;

    if (ev->data.mouse.button != CL_MOUSE_LEFT || idx < 0 || !h)
        return false;
    if (!b->items[idx].menu ||
        cl_widget_window(b->items[idx].menu) != NULL)
        return true; /* no menu, or already open (outside-press closes it) */
    title_span(b, idx, &x0, &x1);
    h->ops->push_popup(h, w, b->items[idx].menu,
                       (cl_point_t){ x0, w->rect.y + w->rect.h });
    cl_widget_invalidate(w);
    return true;
}

static bool menubar_mouse_move(cl_widget_t *w, const cl_event_t *ev)
{
    cl_menubar_t *b = CL_WIDGET_CAST(cl_menubar, w);
    int idx = title_at(b, ev->data.mouse.pos);

    if (idx != b->hovered) {
        b->hovered = idx;
        cl_widget_invalidate(w);
    }
    return true;
}

static void menubar_mouse_leave(cl_widget_t *w)
{
    cl_menubar_t *b = CL_WIDGET_CAST_UNCHECKED(cl_menubar, w);

    if (b->hovered != -1) {
        b->hovered = -1;
        cl_widget_invalidate(w);
    }
}

static void menubar_destroy(cl_widget_t *w)
{
    cl_menubar_t *b = CL_WIDGET_CAST(cl_menubar, w);
    const cl_allocator_t *a = cl_application_allocator(w->app);
    size_t i;

    for (i = 0; i < b->count; i++) {
        cl_free(a, b->items[i].title);
        cl_widget_destroy(b->items[i].menu); /* detached when not open */
    }
    cl_free(a, b->items);
}

cl_widget_t *cl_menubar_create(cl_application_t *app,
                               const cl_menubar_desc_t *desc)
{
    cl_widget_t *w;
    cl_menubar_t *b;

    if (!CL_DESC_ABI_OK(desc, cl_menubar_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_menubar_class);
    if (!w)
        return NULL;
    b = CL_WIDGET_CAST(cl_menubar, w);
    b->hovered = -1;
    return w;
}

cl_result_t cl_menubar_add_menu(cl_widget_t *bar, const char *title,
                                cl_widget_t *menu)
{
    cl_menubar_t *b = CL_WIDGET_CAST(cl_menubar, bar);
    const cl_allocator_t *a;
    char *dup;

    if (!b || !title || !menu)
        return CL_ERROR_INVALID_ARGUMENT;
    a = cl_application_allocator(bar->app);

    if (b->count == b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4;
        bar_item_t *ni = cl_realloc(a, b->items, nc * sizeof(*ni));

        if (!ni)
            return CL_ERROR_OUT_OF_MEMORY;
        b->items = ni;
        b->cap = nc;
    }
    dup = cl_strdup(a, title);
    if (!dup)
        return CL_ERROR_OUT_OF_MEMORY;
    b->items[b->count].title = dup;
    b->items[b->count].menu = menu;
    b->count++;
    cl_widget_invalidate_layout(bar);
    return CL_OK;
}

size_t cl_menubar_count(cl_widget_t *bar)
{
    cl_menubar_t *b = CL_WIDGET_CAST(cl_menubar, bar);

    return b ? b->count : 0;
}
