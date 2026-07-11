/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/menu.h>
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/allocator.h>
#include <copal/window.h>

#include <string.h>

#include "widget/widget_internal.h"
#include "theme/theme_internal.h"

#define MENU_HPAD 14.0f  /* item text left/right padding */
#define MENU_VPAD 5.0f   /* menu top/bottom padding */
#define ITEM_VPAD 5.0f   /* per-item vertical padding */
#define MENU_MIN_W 140.0f
#define HL_INSET 3.0f    /* highlight inset from the menu edge */
#define FALLBACK_LH 16.0f

typedef struct menu_item {
    char *text;
    cl_action_fn fn;
    void *user;
} menu_item_t;

typedef struct cl_menu {
    cl_widget_t base;
    menu_item_t *items;
    size_t count;
    size_t cap;
    int hovered; /* hovered item index, or -1 */
} cl_menu_t;

static cl_size_t menu_measure(cl_widget_t *w, cl_constraints_t c);
static void menu_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool menu_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool menu_mouse_up(cl_widget_t *w, const cl_event_t *ev);
static bool menu_mouse_move(cl_widget_t *w, const cl_event_t *ev);
static bool menu_key_down(cl_widget_t *w, const cl_event_t *ev);
static void menu_destroy(cl_widget_t *w);

static const cl_widget_vtable_t menu_vtable = {
    .destroy = menu_destroy,
    .measure = menu_measure,
    .paint = menu_paint,
    .mouse_down = menu_mouse_down,
    .mouse_up = menu_mouse_up,
    .mouse_move = menu_mouse_move,
    .key_down = menu_key_down,
};

static const cl_widget_class_t cl_menu_class = {
    .name = "cl_menu",
    .base = NULL,
    .type_id = 0x6d656e75u, /* 'menu' */
    .instance_size = sizeof(cl_menu_t),
    .vtable = &menu_vtable,
};

static cl_font_t *menu_font(cl_widget_t *w)
{
    return cl_theme_font(cl_application_theme(w->app));
}

static float item_height(cl_widget_t *w)
{
    cl_font_t *font = menu_font(w);
    float lh = font ? cl_font_metrics(font).line_height : FALLBACK_LH;

    return lh + 2.0f * ITEM_VPAD;
}

/* Item index at window-y, or -1 if outside the item rows. */
static int item_at(cl_menu_t *m, float y)
{
    float top = m->base.rect.y + MENU_VPAD;
    float ih = item_height(&m->base);
    int idx;

    if (ih <= 0.0f || y < top)
        return -1;
    idx = (int)((y - top) / ih);
    if (idx < 0 || (size_t)idx >= m->count)
        return -1;
    return idx;
}

static void activate(cl_menu_t *m, int idx)
{
    cl_action_fn fn;
    void *user;
    cl_window_t *win;

    if (idx < 0 || (size_t)idx >= m->count)
        return;
    fn = m->items[idx].fn;
    user = m->items[idx].user;
    win = cl_widget_window(&m->base);
    /*
     * Request a deferred close BEFORE running the callback, then invoke it. The
     * menu is destroyed only after event dispatch unwinds (window reap), so the
     * callback safely runs with the menu still alive.
     */
    if (win)
        cl_window_close_popup(win);
    if (fn)
        fn(&m->base, user);
}

static cl_size_t menu_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_menu_t *m = CL_WIDGET_CAST(cl_menu, w);
    cl_font_t *font = menu_font(w);
    float maxw = 0.0f;
    size_t i;
    cl_size_t out;

    (void)c;
    if (font) {
        for (i = 0; i < m->count; i++) {
            if (m->items[i].text && m->items[i].text[0]) {
                float tw = cl_text_measure(font, m->items[i].text,
                                           CL_UNBOUNDED).w;

                if (tw > maxw)
                    maxw = tw;
            }
        }
    }
    out.w = maxw + 2.0f * MENU_HPAD;
    if (out.w < MENU_MIN_W)
        out.w = MENU_MIN_W;
    out.h = (float)m->count * item_height(w) + 2.0f * MENU_VPAD;
    return out;
}

static void menu_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_menu_t *m = CL_WIDGET_CAST(cl_menu, w);
    cl_font_t *font = cl_theme_font(cl_paint_theme(ctx));
    float radius = cl_theme_radius(cl_paint_theme(ctx));
    float ih = item_height(w);
    cl_rect_t r = w->rect;
    cl_rect_t shadow = { r.x + 1.0f, r.y + 3.0f, r.w, r.h };
    size_t i;

    cl_paint_fill_round_rect(ctx, shadow, radius,
                             cl_paint_theme_color(ctx, CL_COLOR_SHADOW));
    cl_paint_fill_round_rect(ctx, r, radius,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE_RAISED));
    cl_paint_stroke_round_rect(ctx, r, radius, 1.0f,
                               cl_paint_theme_color(ctx, CL_COLOR_BORDER));

    for (i = 0; i < m->count; i++) {
        float iy = r.y + MENU_VPAD + (float)i * ih;

        if ((int)i == m->hovered) {
            cl_rect_t hl = { r.x + HL_INSET, iy, r.w - 2.0f * HL_INSET, ih };

            cl_paint_fill_round_rect(
                ctx, hl, radius * 0.5f,
                cl_paint_theme_color(ctx, CL_COLOR_SURFACE_HOVER));
        }
        if (font && m->items[i].text) {
            cl_point_t p = { r.x + MENU_HPAD, iy + ITEM_VPAD };

            cl_paint_draw_text(ctx, font, m->items[i].text, p,
                               cl_paint_theme_color(ctx, CL_COLOR_TEXT));
        }
    }
}

static bool menu_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    (void)w;
    (void)ev;
    return true; /* consume so the press is not treated as an outside dismiss */
}

static bool menu_mouse_up(cl_widget_t *w, const cl_event_t *ev)
{
    cl_menu_t *m = CL_WIDGET_CAST(cl_menu, w);

    activate(m, item_at(m, ev->data.mouse.pos.y));
    return true;
}

static bool menu_mouse_move(cl_widget_t *w, const cl_event_t *ev)
{
    cl_menu_t *m = CL_WIDGET_CAST(cl_menu, w);
    int idx = item_at(m, ev->data.mouse.pos.y);

    if (idx != m->hovered) {
        m->hovered = idx;
        cl_widget_invalidate(w);
    }
    return true;
}

static bool menu_key_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_menu_t *m = CL_WIDGET_CAST(cl_menu, w);
    int n = (int)m->count;

    switch (ev->data.key.key) {
        case CL_KEY_DOWN:
            if (n > 0)
                m->hovered = m->hovered < 0 ? 0 : (m->hovered + 1) % n;
            cl_widget_invalidate(w);
            return true;

        case CL_KEY_UP:
            if (n > 0)
                m->hovered = m->hovered <= 0 ? n - 1 : m->hovered - 1;
            cl_widget_invalidate(w);
            return true;

        case CL_KEY_ENTER:
            activate(m, m->hovered);
            return true;

        case CL_KEY_ESCAPE: {
            cl_window_t *win = cl_widget_window(w);

            if (win)
                cl_window_close_popup(win);
            return true;
        }

        default:
            return true; /* modal: swallow other keys while open */
    }
}

static void menu_destroy(cl_widget_t *w)
{
    cl_menu_t *m = CL_WIDGET_CAST(cl_menu, w);
    const cl_allocator_t *a = cl_application_allocator(w->app);
    size_t i;

    for (i = 0; i < m->count; i++)
        cl_free(a, m->items[i].text);
    cl_free(a, m->items);
}

cl_widget_t *cl_menu_create(cl_application_t *app)
{
    cl_widget_t *w = cl_widget_alloc(app, &cl_menu_class);
    cl_menu_t *m;

    if (!w)
        return NULL;
    m = CL_WIDGET_CAST(cl_menu, w);
    m->hovered = -1;
    return w;
}

cl_result_t cl_menu_add_item(cl_widget_t *menu, const char *text,
                             cl_action_fn fn, void *user)
{
    cl_menu_t *m = CL_WIDGET_CAST(cl_menu, menu);
    const cl_allocator_t *a;
    char *dup;
    size_t n;

    if (!m || !text)
        return CL_ERROR_INVALID_ARGUMENT;
    a = cl_application_allocator(menu->app);

    if (m->count == m->cap) {
        size_t nc = m->cap ? m->cap * 2 : 4;
        menu_item_t *ni = cl_realloc(a, m->items, nc * sizeof(*ni));

        if (!ni)
            return CL_ERROR_OUT_OF_MEMORY;
        m->items = ni;
        m->cap = nc;
    }

    n = strlen(text) + 1;
    dup = cl_alloc(a, n);
    if (!dup)
        return CL_ERROR_OUT_OF_MEMORY;
    memcpy(dup, text, n);

    m->items[m->count].text = dup;
    m->items[m->count].fn = fn;
    m->items[m->count].user = user;
    m->count++;
    cl_widget_invalidate_layout(menu);
    return CL_OK;
}

size_t cl_menu_count(cl_widget_t *menu)
{
    cl_menu_t *m = CL_WIDGET_CAST(cl_menu, menu);

    return m ? m->count : 0;
}
