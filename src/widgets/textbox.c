/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/textbox.h>
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <string.h>

#include "app/app_internal.h"
#include "core/foundation/foundation_internal.h"

#define TB_PAD_X 6.0f
#define TB_PAD_Y 5.0f
#define TB_RADIUS 4.0f
#define TB_DEFAULT_WIDTH 160.0f

/* Undo grouping: consecutive same-kind edits collapse into one undo step. */
typedef enum {
    TB_EDIT_NONE,
    TB_EDIT_TYPE,
    TB_EDIT_DELETE,
    TB_EDIT_OTHER
} tb_edit_kind_t;

typedef struct tb_snapshot {
    char *text; /* NUL-terminated copy of the buffer */
    size_t cursor;
    size_t anchor;
} tb_snapshot_t;

typedef struct cl_textbox {
    cl_widget_t base;
    char *buf;   /* UTF-8, NUL-terminated */
    size_t len;  /* bytes, excluding NUL */
    size_t cap;  /* allocated bytes */
    size_t cursor; /* byte offset [0..len] */
    size_t anchor; /* selection anchor; == cursor means no selection */
    char *placeholder;
    bool password;
    bool readonly;
    size_t max_length; /* codepoints; 0 = unlimited */
    float scroll_x;
    cl_text_changed_fn on_changed;
    void *on_changed_user;
    cl_text_changed_fn on_submit;
    void *on_submit_user;
    tb_snapshot_t *undo;
    size_t undo_count, undo_cap;
    tb_snapshot_t *redo;
    size_t redo_count, redo_cap;
    tb_edit_kind_t coalesce; /* kind of the current undo group */
    char *pending;           /* pre-edit snapshot text, or NULL */
    size_t pending_cursor, pending_anchor;
} cl_textbox_t;

static cl_size_t textbox_measure(cl_widget_t *w, cl_constraints_t c);
static void textbox_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool textbox_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool textbox_key_down(cl_widget_t *w, const cl_event_t *ev);
static bool textbox_text_input(cl_widget_t *w, const cl_event_t *ev);
static void textbox_focus_changed(cl_widget_t *w);
static void textbox_destroy(cl_widget_t *w);

static const cl_widget_vtable_t textbox_vtable = {
    .destroy = textbox_destroy,
    .measure = textbox_measure,
    .paint = textbox_paint,
    .mouse_down = textbox_mouse_down,
    .key_down = textbox_key_down,
    .text_input = textbox_text_input,
    .focus_gained = textbox_focus_changed,
    .focus_lost = textbox_focus_changed,
};

static const cl_widget_class_t cl_textbox_class = {
    .name = "cl_textbox",
    .base = NULL,
    .type_id = 0x74627831u, /* 'tbx1' */
    .instance_size = sizeof(cl_textbox_t),
    .vtable = &textbox_vtable,
};

/* ---- utf-8 helpers ------------------------------------------------------ */

static size_t cp_count(const char *buf, size_t nbytes)
{
    size_t i = 0;
    size_t k = 0;
    size_t n;
    uint32_t cp;

    while (i < nbytes && (n = cl_utf8_next(buf + i, &cp)) != 0) {
        if (i + n > nbytes)
            break;
        i += n;
        k++;
    }
    return k;
}

static size_t byte_offset_of_cp(const char *buf, size_t len, size_t k)
{
    size_t i = 0;
    size_t c = 0;
    size_t n;
    uint32_t cp;

    while (c < k && i < len && (n = cl_utf8_next(buf + i, &cp)) != 0) {
        if (i + n > len)
            break;
        i += n;
        c++;
    }
    return i;
}

static size_t prev_boundary(const char *buf, size_t i)
{
    size_t j;

    if (i == 0)
        return 0;
    /*
     * Find the start of the codepoint ending at i by locating the nearest
     * lead byte (at most 4 back) and confirming its sequence ends exactly at
     * i. On invalid data, step back one byte so this agrees with the forward
     * decoder used by next_boundary.
     */
    for (j = i; j > 0 && i - j < 4;) {
        uint32_t cp;
        size_t n;

        j--;
        if (((unsigned char)buf[j] & 0xC0) == 0x80)
            continue;
        n = cl_utf8_next(buf + j, &cp);
        if (j + n == i)
            return j;
        break;
    }
    return i - 1;
}

static size_t next_boundary(const char *buf, size_t len, size_t i)
{
    uint32_t cp;
    size_t n;

    if (i >= len)
        return len;
    n = cl_utf8_next(buf + i, &cp);
    if (n == 0 || i + n > len)
        return len;
    return i + n;
}

/* ---- metrics ------------------------------------------------------------ */

static cl_font_t *textbox_font(cl_widget_t *w)
{
    return cl_theme_font(cl_application_theme(w->app));
}

static float star_advance(cl_font_t *font)
{
    return cl_text_measure(font, "*", CL_UNBOUNDED).w;
}

static float caret_x(cl_textbox_t *tb, cl_font_t *font, size_t offset)
{
    if (!font)
        return 0.0f;
    if (tb->password)
        return (float)cp_count(tb->buf, offset) * star_advance(font);
    return cl_text_measure_bytes(font, tb->buf, offset, CL_UNBOUNDED).w;
}

static size_t offset_at_x(cl_textbox_t *tb, cl_font_t *font, float x)
{
    size_t i = 0;
    size_t n;
    uint32_t cp;
    float px = 0.0f;

    if (!font || tb->len == 0)
        return 0;
    if (x <= 0.0f)
        return 0;

    if (tb->password) {
        float sa = star_advance(font);
        size_t k;

        if (sa <= 0.0f)
            return 0;
        k = (size_t)((x / sa) + 0.5f);
        return byte_offset_of_cp(tb->buf, tb->len, k);
    }

    while (i < tb->len && (n = cl_utf8_next(tb->buf + i, &cp)) != 0) {
        float adv;

        if (i + n > tb->len)
            break;
        adv = cl_text_measure_bytes(font, tb->buf + i, n, CL_UNBOUNDED).w;
        if (x < px + adv * 0.5f)
            return i;
        px += adv;
        i += n;
    }
    return tb->len;
}

static void update_scroll(cl_textbox_t *tb)
{
    cl_font_t *font = textbox_font(&tb->base);
    float cx = caret_x(tb, font, tb->cursor);
    float visible = tb->base.rect.w - 2.0f * TB_PAD_X;

    if (visible < 0.0f)
        visible = 0.0f;
    if (cx - tb->scroll_x < 0.0f)
        tb->scroll_x = cx;
    else if (cx - tb->scroll_x > visible)
        tb->scroll_x = cx - visible;
    if (tb->scroll_x < 0.0f)
        tb->scroll_x = 0.0f;
}

/* ---- editing ------------------------------------------------------------ */

static bool has_selection(const cl_textbox_t *tb)
{
    return tb->cursor != tb->anchor;
}

static void sel_range(const cl_textbox_t *tb, size_t *lo, size_t *hi)
{
    if (tb->cursor < tb->anchor) {
        *lo = tb->cursor;
        *hi = tb->anchor;
    } else {
        *lo = tb->anchor;
        *hi = tb->cursor;
    }
}

static bool ensure_cap(cl_textbox_t *tb, size_t need)
{
    size_t nc;
    char *nb;

    if (tb->cap >= need)
        return true;
    nc = tb->cap ? tb->cap * 2 : 16;
    if (nc < need)
        nc = need;
    nb = cl_realloc(cl_application_allocator(tb->base.app), tb->buf, nc);
    if (!nb)
        return false;
    tb->buf = nb;
    tb->cap = nc;
    return true;
}

static void notify_changed(cl_textbox_t *tb)
{
    if (tb->on_changed)
        tb->on_changed(&tb->base, tb->buf, tb->on_changed_user);
}

static void delete_range(cl_textbox_t *tb, size_t lo, size_t hi)
{
    if (hi <= lo || hi > tb->len)
        return;
    memmove(tb->buf + lo, tb->buf + hi, tb->len - hi + 1); /* incl NUL */
    tb->len -= hi - lo;
    tb->cursor = lo;
    tb->anchor = lo;
}

static void delete_selection(cl_textbox_t *tb)
{
    size_t lo;
    size_t hi;

    if (!has_selection(tb))
        return;
    sel_range(tb, &lo, &hi);
    delete_range(tb, lo, hi);
}

static void insert_text(cl_textbox_t *tb, const char *s, size_t slen)
{
    if (tb->readonly || slen == 0)
        return;
    delete_selection(tb);

    if (tb->max_length) {
        size_t cur_cp = cp_count(tb->buf, tb->len);
        size_t add_cp = cp_count(s, slen);

        if (cur_cp >= tb->max_length)
            return;
        if (cur_cp + add_cp > tb->max_length) {
            slen = byte_offset_of_cp(s, slen, tb->max_length - cur_cp);
            if (slen == 0)
                return;
        }
    }

    if (!ensure_cap(tb, tb->len + slen + 1))
        return;
    memmove(tb->buf + tb->cursor + slen, tb->buf + tb->cursor,
            tb->len - tb->cursor + 1); /* incl NUL */
    memcpy(tb->buf + tb->cursor, s, slen);
    tb->len += slen;
    tb->cursor += slen;
    tb->anchor = tb->cursor;
}

static void move_cursor(cl_textbox_t *tb, size_t pos, bool extend)
{
    tb->cursor = pos;
    if (!extend)
        tb->anchor = pos;
}

/* ---- clipboard ---------------------------------------------------------- */

static char *selection_dup(cl_textbox_t *tb)
{
    size_t lo;
    size_t hi;
    char *s;

    if (!has_selection(tb))
        return NULL;
    sel_range(tb, &lo, &hi);
    s = cl_alloc(cl_application_allocator(tb->base.app), hi - lo + 1);
    if (s) {
        memcpy(s, tb->buf + lo, hi - lo);
        s[hi - lo] = '\0';
    }
    return s;
}

/* Single-line box: drop line breaks pasted from the clipboard. */
static void strip_newlines(char *s)
{
    char *d = s;

    for (; *s; s++) {
        if (*s != '\n' && *s != '\r')
            *d++ = *s;
    }
    *d = '\0';
}

static void clipboard_copy(cl_textbox_t *tb)
{
    char *s;

    if (tb->password || !has_selection(tb))
        return;
    s = selection_dup(tb);
    if (s) {
        cl_app_clipboard_set(tb->base.app, s);
        cl_free(cl_application_allocator(tb->base.app), s);
    }
}

/* ---- undo / redo -------------------------------------------------------- */

#define TB_HISTORY_MAX 128

static char *tb_dup_n(cl_textbox_t *tb, const char *s, size_t n)
{
    char *p = cl_alloc(cl_application_allocator(tb->base.app), n + 1);

    if (p) {
        memcpy(p, s, n);
        p[n] = '\0';
    }
    return p;
}

static void stack_free(cl_textbox_t *tb, tb_snapshot_t *stack, size_t count)
{
    const cl_allocator_t *a = cl_application_allocator(tb->base.app);
    size_t i;

    for (i = 0; i < count; i++)
        cl_free(a, stack[i].text);
}

/* Push a snapshot (taking ownership of text) onto the undo or redo stack. */
static void stack_push(cl_textbox_t *tb, bool undo, char *text, size_t cursor,
                       size_t anchor)
{
    const cl_allocator_t *a = cl_application_allocator(tb->base.app);
    tb_snapshot_t **stack = undo ? &tb->undo : &tb->redo;
    size_t *count = undo ? &tb->undo_count : &tb->redo_count;
    size_t *cap = undo ? &tb->undo_cap : &tb->redo_cap;

    if (!text)
        return;
    if (*count >= TB_HISTORY_MAX) { /* bound memory: drop the oldest */
        cl_free(a, (*stack)[0].text);
        memmove(*stack, *stack + 1, (*count - 1) * sizeof(**stack));
        (*count)--;
    }
    if (*count == *cap) {
        size_t nc = *cap ? *cap * 2 : 16;
        tb_snapshot_t *ns = cl_realloc(a, *stack, nc * sizeof(*ns));

        if (!ns) {
            cl_free(a, text);
            return;
        }
        *stack = ns;
        *cap = nc;
    }
    (*stack)[*count].text = text;
    (*stack)[*count].cursor = cursor;
    (*stack)[*count].anchor = anchor;
    (*count)++;
}

static void clear_redo(cl_textbox_t *tb)
{
    stack_free(tb, tb->redo, tb->redo_count);
    tb->redo_count = 0;
}

static void clear_history(cl_textbox_t *tb)
{
    stack_free(tb, tb->undo, tb->undo_count);
    stack_free(tb, tb->redo, tb->redo_count);
    tb->undo_count = 0;
    tb->redo_count = 0;
    cl_free(cl_application_allocator(tb->base.app), tb->pending);
    tb->pending = NULL;
    tb->coalesce = TB_EDIT_NONE;
}

/* Capture the pre-edit state; pair with edit_commit(). */
static void edit_begin(cl_textbox_t *tb)
{
    cl_free(cl_application_allocator(tb->base.app), tb->pending);
    tb->pending = tb_dup_n(tb, tb->buf, tb->len);
    tb->pending_cursor = tb->cursor;
    tb->pending_anchor = tb->anchor;
}

/* Commit a captured edit onto the undo stack unless it was a no-op or a
 * continuation of the current same-kind group. Returns whether the buffer
 * actually changed (drives the on_changed notification). */
static bool edit_commit(cl_textbox_t *tb, tb_edit_kind_t kind)
{
    const cl_allocator_t *a = cl_application_allocator(tb->base.app);
    bool changed;

    if (!tb->pending)
        return false;
    changed = strcmp(tb->pending, tb->buf) != 0;
    if (!changed || (kind == tb->coalesce && kind != TB_EDIT_OTHER)) {
        cl_free(a, tb->pending);
        tb->pending = NULL;
        return changed;
    }
    clear_redo(tb);
    stack_push(tb, true, tb->pending, tb->pending_cursor, tb->pending_anchor);
    tb->pending = NULL; /* ownership moved (or freed) by stack_push */
    tb->coalesce = kind;
    return changed;
}

/* End the current undo group so the next edit starts a fresh one. */
static void edit_break(cl_textbox_t *tb)
{
    tb->coalesce = TB_EDIT_NONE;
}

static void restore_snapshot(cl_textbox_t *tb, const tb_snapshot_t *snap)
{
    size_t n = strlen(snap->text);

    if (!ensure_cap(tb, n + 1))
        return;
    memcpy(tb->buf, snap->text, n + 1);
    tb->len = n;
    tb->cursor = snap->cursor > n ? n : snap->cursor;
    tb->anchor = snap->anchor > n ? n : snap->anchor;
}

static bool history_apply(cl_textbox_t *tb, bool undo)
{
    tb_snapshot_t **from = undo ? &tb->undo : &tb->redo;
    size_t *from_count = undo ? &tb->undo_count : &tb->redo_count;
    tb_snapshot_t snap;

    if (*from_count == 0)
        return false;
    /* Save the current state onto the opposite stack, then restore. */
    stack_push(tb, !undo, tb_dup_n(tb, tb->buf, tb->len), tb->cursor,
               tb->anchor);
    snap = (*from)[--(*from_count)];
    restore_snapshot(tb, &snap);
    cl_free(cl_application_allocator(tb->base.app), snap.text);
    tb->coalesce = TB_EDIT_NONE;
    return true;
}

/* ---- vtable ------------------------------------------------------------- */

static cl_size_t textbox_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_font_t *font = textbox_font(w);
    float lh = font ? cl_font_metrics(font).line_height : 16.0f;

    (void)c;
    return (cl_size_t){ TB_DEFAULT_WIDTH, lh + 2.0f * TB_PAD_Y };
}

static void textbox_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);
    cl_font_t *font = cl_theme_font(cl_paint_theme(ctx));
    bool focused = cl_widget_has_focus(w);
    float lh = font ? cl_font_metrics(font).line_height : 16.0f;
    float text_x = w->rect.x + TB_PAD_X - tb->scroll_x;
    float text_y = w->rect.y + (w->rect.h - lh) * 0.5f;
    const char *draw = tb->buf;
    char *stars = NULL;
    cl_rect_t inner = { w->rect.x + TB_PAD_X, w->rect.y,
                        w->rect.w - 2.0f * TB_PAD_X, w->rect.h };

    cl_paint_fill_round_rect(ctx, w->rect, TB_RADIUS,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    cl_paint_stroke_round_rect(
        ctx, w->rect, TB_RADIUS, focused ? 2.0f : 1.0f,
        cl_paint_theme_color(ctx, focused ? CL_COLOR_FOCUS_RING
                                          : CL_COLOR_BORDER));

    if (tb->len == 0 && !focused && tb->placeholder) {
        if (font)
            cl_paint_draw_text(ctx, font, tb->placeholder,
                               (cl_point_t){ w->rect.x + TB_PAD_X, text_y },
                               cl_paint_theme_color(ctx, CL_COLOR_TEXT_MUTED));
        return;
    }

    if (tb->password && font && tb->len > 0) {
        size_t k = cp_count(tb->buf, tb->len);
        size_t i;

        stars = cl_alloc(cl_application_allocator(w->app), k + 1);
        if (stars) {
            for (i = 0; i < k; i++)
                stars[i] = '*';
            stars[k] = '\0';
            draw = stars;
        }
    }

    /* clip the moving text/caret to the inner area (excludes padding/border) */
    cl_paint_push_clip(ctx, inner);

    /* selection highlight */
    if (focused && has_selection(tb) && font) {
        size_t lo;
        size_t hi;
        float xlo;
        float xhi;

        sel_range(tb, &lo, &hi);
        xlo = caret_x(tb, font, lo);
        xhi = caret_x(tb, font, hi);
        cl_paint_fill_rect(ctx,
                           (cl_rect_t){ text_x + xlo, w->rect.y + TB_PAD_Y,
                                        xhi - xlo, w->rect.h - 2.0f * TB_PAD_Y },
                           cl_paint_theme_color(ctx, CL_COLOR_SELECTION));
    }

    if (font && draw && draw[0])
        cl_paint_draw_text(ctx, font, draw, (cl_point_t){ text_x, text_y },
                           cl_paint_theme_color(ctx, CL_COLOR_TEXT));

    /* caret */
    if (focused && !has_selection(tb) && font) {
        float cx = text_x + caret_x(tb, font, tb->cursor);

        cl_paint_fill_rect(ctx,
                           (cl_rect_t){ cx, text_y, 1.0f, lh },
                           cl_paint_theme_color(ctx, CL_COLOR_TEXT));
    }

    cl_paint_pop_clip(ctx);

    if (stars)
        cl_free(cl_application_allocator(w->app), stars);
}

static bool textbox_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);
    cl_font_t *font = textbox_font(w);
    float local_x = ev->data.mouse.pos.x - (w->rect.x + TB_PAD_X) + tb->scroll_x;
    size_t off = offset_at_x(tb, font, local_x);

    edit_break(tb); /* a click starts a fresh undo group */
    tb->cursor = off;
    if (!(ev->mods & CL_MOD_SHIFT))
        tb->anchor = off;
    update_scroll(tb);
    cl_widget_invalidate(w);
    return true;
}

static bool textbox_key_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);
    bool shift = (ev->mods & CL_MOD_SHIFT) != 0;
    bool ctrl = (ev->mods & CL_MOD_CTRL) != 0;
    cl_key_t key = ev->data.key.key;
    bool handled = true;
    bool force_notify = false;
    tb_edit_kind_t commit = TB_EDIT_NONE;
    size_t lo;
    size_t hi;

    switch (key) {
        case CL_KEY_LEFT:
            edit_break(tb);
            if (ctrl) { /* word motion not yet implemented: let it bubble */
                handled = false;
            } else if (has_selection(tb) && !shift) {
                sel_range(tb, &lo, &hi);
                move_cursor(tb, lo, false);
            } else {
                move_cursor(tb, prev_boundary(tb->buf, tb->cursor), shift);
            }
            break;

        case CL_KEY_RIGHT:
            edit_break(tb);
            if (ctrl) {
                handled = false;
            } else if (has_selection(tb) && !shift) {
                sel_range(tb, &lo, &hi);
                move_cursor(tb, hi, false);
            } else {
                move_cursor(tb, next_boundary(tb->buf, tb->len, tb->cursor),
                            shift);
            }
            break;

        case CL_KEY_HOME:
            edit_break(tb);
            if (ctrl)
                handled = false;
            else
                move_cursor(tb, 0, shift);
            break;

        case CL_KEY_END:
            edit_break(tb);
            if (ctrl)
                handled = false;
            else
                move_cursor(tb, tb->len, shift);
            break;

        case CL_KEY_BACKSPACE:
            if (tb->readonly)
                break;
            edit_begin(tb);
            if (has_selection(tb)) {
                delete_selection(tb);
            } else if (tb->cursor > 0) {
                delete_range(tb, prev_boundary(tb->buf, tb->cursor),
                             tb->cursor);
            }
            commit = TB_EDIT_DELETE;
            break;

        case CL_KEY_DELETE:
            if (tb->readonly)
                break;
            edit_begin(tb);
            if (has_selection(tb))
                delete_selection(tb);
            else if (tb->cursor < tb->len)
                delete_range(tb, tb->cursor,
                             next_boundary(tb->buf, tb->len, tb->cursor));
            commit = TB_EDIT_DELETE;
            break;

        case CL_KEY_ENTER:
            if (tb->on_submit)
                tb->on_submit(w, tb->buf, tb->on_submit_user);
            else
                handled = false;
            break;

        case CL_KEY_A:
            if (ctrl) {
                edit_break(tb);
                tb->anchor = 0;
                tb->cursor = tb->len;
            } else {
                handled = false;
            }
            break;

        case CL_KEY_C:
            if (ctrl) {
                edit_break(tb); /* a copy ends the current typing group */
                clipboard_copy(tb);
            } else {
                handled = false;
            }
            break;

        case CL_KEY_X:
            if (ctrl) {
                /* password: no cut at all (mirrors copy being suppressed) */
                edit_begin(tb);
                clipboard_copy(tb);
                if (!tb->password && !tb->readonly)
                    delete_selection(tb);
                commit = TB_EDIT_OTHER;
            } else {
                handled = false;
            }
            break;

        case CL_KEY_V:
            if (ctrl) {
                if (!tb->readonly) {
                    char *clip = cl_app_clipboard_get(w->app);

                    if (clip) {
                        strip_newlines(clip);
                        /* only a non-empty paste is an edit; empty (e.g. a
                         * newline-only clipboard) must not fire on_changed */
                        if (clip[0]) {
                            edit_begin(tb);
                            insert_text(tb, clip, strlen(clip));
                            commit = TB_EDIT_OTHER;
                        }
                        cl_free(cl_application_allocator(w->app), clip);
                    }
                }
            } else {
                handled = false;
            }
            break;

        case CL_KEY_Z:
            if (ctrl)
                handled = force_notify =
                    shift ? history_apply(tb, false) : history_apply(tb, true);
            else
                handled = false;
            break;

        case CL_KEY_Y:
            if (ctrl)
                handled = force_notify = history_apply(tb, false);
            else
                handled = false;
            break;

        default:
            handled = false;
            break;
    }

    if (commit != TB_EDIT_NONE && edit_commit(tb, commit))
        force_notify = true; /* a real (buffer-changing) edit */
    if (handled) {
        if (force_notify)
            notify_changed(tb);
        update_scroll(tb);
        cl_widget_invalidate(w);
    }
    return handled;
}

static bool textbox_text_input(cl_widget_t *w, const cl_event_t *ev)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);
    const char *s = ev->data.text.utf8;

    if (!s || !s[0] || tb->readonly)
        return true;
    edit_begin(tb);
    insert_text(tb, s, strlen(s));
    if (edit_commit(tb, TB_EDIT_TYPE)) /* fire only when bytes changed */
        notify_changed(tb);
    update_scroll(tb);
    cl_widget_invalidate(w);
    return true;
}

static void textbox_focus_changed(cl_widget_t *w)
{
    cl_widget_invalidate(w);
}

static void textbox_destroy(cl_widget_t *w)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);
    const cl_allocator_t *a = cl_application_allocator(w->app);

    stack_free(tb, tb->undo, tb->undo_count);
    stack_free(tb, tb->redo, tb->redo_count);
    cl_free(a, tb->undo);
    cl_free(a, tb->redo);
    cl_free(a, tb->pending);
    cl_free(a, tb->buf);
    cl_free(a, tb->placeholder);
}

/* ---- public ------------------------------------------------------------- */

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

static void set_text_internal(cl_textbox_t *tb, const char *utf8)
{
    size_t n = utf8 ? strlen(utf8) : 0;

    if (tb->max_length && cp_count(utf8, n) > tb->max_length)
        n = byte_offset_of_cp(utf8, n, tb->max_length);
    if (!ensure_cap(tb, n + 1))
        return;
    if (n)
        memcpy(tb->buf, utf8, n);
    tb->buf[n] = '\0';
    tb->len = n;
    tb->cursor = n;
    tb->anchor = n;
    tb->scroll_x = 0.0f;
    clear_history(tb); /* a programmatic set is not an undoable edit */
}

cl_widget_t *cl_textbox_create(cl_application_t *app,
                               const cl_textbox_desc_t *desc)
{
    cl_widget_t *w = cl_widget_alloc(app, &cl_textbox_class);
    cl_textbox_t *tb;

    if (!w)
        return NULL;
    tb = CL_WIDGET_CAST(cl_textbox, w);
    w->flags |= CL_WF_FOCUSABLE;

    /* buffer always holds at least a NUL */
    if (!ensure_cap(tb, 1)) {
        cl_widget_destroy(w);
        return NULL;
    }
    tb->buf[0] = '\0';

    if (desc) {
        tb->password = desc->password;
        tb->readonly = desc->readonly;
        tb->max_length = desc->max_length;
        tb->placeholder = dup_str(cl_application_allocator(app),
                                  desc->placeholder);
        if (desc->text)
            set_text_internal(tb, desc->text);
    }
    return w;
}

void cl_textbox_set_text(cl_widget_t *tb_w, const char *utf8)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, tb_w);

    if (!tb)
        return;
    set_text_internal(tb, utf8);
    cl_widget_invalidate_layout(tb_w);
}

const char *cl_textbox_text(cl_widget_t *tb_w)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, tb_w);

    return tb ? tb->buf : NULL;
}

void cl_textbox_set_on_changed(cl_widget_t *tb_w, cl_text_changed_fn fn,
                               void *user)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, tb_w);

    if (tb) {
        tb->on_changed = fn;
        tb->on_changed_user = user;
    }
}

void cl_textbox_set_on_submit(cl_widget_t *tb_w, cl_text_changed_fn fn,
                              void *user)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, tb_w);

    if (tb) {
        tb->on_submit = fn;
        tb->on_submit_user = user;
    }
}
