/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/textbox.h>
#include <copal/widget_impl.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <stdio.h>
#include <string.h>

#include "app/app_internal.h"
#include "widget/widget_internal.h"
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

/* One visual line of the wrapped multiline layout. */
typedef struct tb_line {
    size_t start; /* byte offset of the line's first char */
    size_t len;   /* bytes on the line (excludes a trailing '\n' or wrap) */
} tb_line_t;

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
    bool multiline;
    size_t max_length; /* codepoints; 0 = unlimited */
    float scroll_x;
    float scroll_y;    /* multiline: vertical content offset (px) */
    float goal_x;      /* multiline: desired caret x for Up/Down */
    bool goal_valid;
    tb_line_t *lines;   /* multiline: visual line spans */
    size_t line_count;
    size_t line_cap;
    float layout_width; /* wrap width the layout was built for */
    bool layout_dirty;
    bool drag_select;   /* left button held: mouse moves extend the selection */
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
    char *preedit;           /* IME composition shown at the caret, or NULL */
    int preedit_cursor;      /* caret within the composition, in codepoints */
} cl_textbox_t;

static cl_size_t textbox_measure(cl_widget_t *w, cl_constraints_t c);
static void textbox_paint(cl_widget_t *w, cl_paint_context_t *ctx);
static bool textbox_mouse_down(cl_widget_t *w, const cl_event_t *ev);
static bool textbox_mouse_move(cl_widget_t *w, const cl_event_t *ev);
static bool textbox_mouse_up(cl_widget_t *w, const cl_event_t *ev);
static bool textbox_mouse_wheel(cl_widget_t *w, const cl_event_t *ev);
static bool textbox_key_down(cl_widget_t *w, const cl_event_t *ev);
static bool textbox_text_input(cl_widget_t *w, const cl_event_t *ev);
static bool textbox_text_edit(cl_widget_t *w, const cl_event_t *ev);
static void textbox_focus_changed(cl_widget_t *w);
static void textbox_destroy(cl_widget_t *w);
static void tb_clear_preedit(cl_textbox_t *tb);
static void tb_update_ime_rect(cl_textbox_t *tb);

static const cl_widget_vtable_t textbox_vtable = {
    .destroy = textbox_destroy,
    .measure = textbox_measure,
    .paint = textbox_paint,
    .mouse_down = textbox_mouse_down,
    .mouse_move = textbox_mouse_move,
    .mouse_up = textbox_mouse_up,
    .mouse_wheel = textbox_mouse_wheel,
    .key_down = textbox_key_down,
    .text_input = textbox_text_input,
    .text_edit = textbox_text_edit,
    .focus_gained = textbox_focus_changed,
    .focus_lost = textbox_focus_changed,
};

static const cl_widget_class_t cl_textbox_class = {
    .name = "cl_textbox",
    .base = NULL,
    .type_id = 0x74627831u, /* 'tbx1' */
    .instance_size = sizeof(cl_textbox_t),
    .vtable = &textbox_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
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

/* ---- word boundaries (whitespace-delimited; motion + double click) ------ */

static uint32_t cp_at(const cl_textbox_t *tb, size_t i)
{
    uint32_t cp = 0;

    if (i < tb->len)
        cl_utf8_next(tb->buf + i, &cp);
    return cp;
}

static bool cp_is_space(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r';
}

/* Start of the previous word: skip spaces backwards, then the word. */
static size_t word_prev(const cl_textbox_t *tb, size_t i)
{
    while (i > 0 && cp_is_space(cp_at(tb, prev_boundary(tb->buf, i))))
        i = prev_boundary(tb->buf, i);
    while (i > 0 && !cp_is_space(cp_at(tb, prev_boundary(tb->buf, i))))
        i = prev_boundary(tb->buf, i);
    return i;
}

/* Start of the next word: skip the current word, then the spaces. */
static size_t word_next(const cl_textbox_t *tb, size_t i)
{
    while (i < tb->len && !cp_is_space(cp_at(tb, i)))
        i = next_boundary(tb->buf, tb->len, i);
    while (i < tb->len && cp_is_space(cp_at(tb, i)))
        i = next_boundary(tb->buf, tb->len, i);
    return i;
}

/* Select the word (or whitespace run) around byte offset `off`. */
static void select_word_at(cl_textbox_t *tb, size_t off)
{
    size_t a, b;
    bool sp;

    if (tb->len == 0)
        return;
    if (off >= tb->len)
        off = prev_boundary(tb->buf, tb->len);
    sp = cp_is_space(cp_at(tb, off));
    a = off;
    while (a > 0) {
        size_t prev = prev_boundary(tb->buf, a);

        if (cp_is_space(cp_at(tb, prev)) != sp)
            break;
        a = prev;
    }
    b = next_boundary(tb->buf, tb->len, off);
    while (b < tb->len && cp_is_space(cp_at(tb, b)) == sp)
        b = next_boundary(tb->buf, tb->len, b);
    tb->anchor = a;
    tb->cursor = b;
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

/* ---- multiline layout --------------------------------------------------- */

static float line_height_of(cl_font_t *font)
{
    return font ? cl_font_metrics(font).line_height : 16.0f;
}

static float measure_span(cl_font_t *font, const char *p, size_t n)
{
    return font ? cl_text_measure_bytes(font, p, n, CL_UNBOUNDED).w : 0.0f;
}

/*
 * Draw exactly @n bytes of @s. The renderer consumes NUL-terminated strings,
 * so copy the span out (stack for the common case) instead of temporarily
 * poking a NUL into the text buffer: paint must not mutate widget state.
 */
static void draw_span(cl_paint_context_t *ctx, cl_font_t *font,
                      const cl_allocator_t *a, const char *s, size_t n,
                      cl_point_t pos, cl_color_t col)
{
    char stack[256];
    char *tmp = stack;

    if (n == 0)
        return;
    if (n + 1 > sizeof(stack)) {
        tmp = cl_alloc(a, n + 1);
        if (!tmp)
            return; /* skip the span this frame rather than crash */
    }
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    cl_paint_draw_text(ctx, font, tmp, pos, col);
    if (tmp != stack)
        cl_free(a, tmp);
}

static bool lines_reserve(cl_textbox_t *tb, size_t need)
{
    size_t nc;
    tb_line_t *nl;

    if (tb->line_cap >= need)
        return true;
    nc = tb->line_cap ? tb->line_cap * 2 : 8;
    if (nc < need)
        nc = need;
    nl = cl_realloc(cl_application_allocator(tb->base.app), tb->lines,
                    nc * sizeof(*nl));
    if (!nl)
        return false;
    tb->lines = nl;
    tb->line_cap = nc;
    return true;
}

static void emit_line(cl_textbox_t *tb, size_t start, size_t len)
{
    if (!lines_reserve(tb, tb->line_count + 1))
        return;
    tb->lines[tb->line_count].start = start;
    tb->lines[tb->line_count].len = len;
    tb->line_count++;
}

/* Greedy word-wrap: break at the last space that fits, or mid-word if a single
 * word is too wide; explicit '\n' always starts a new line. Always >= 1 line. */
static void tb_relayout(cl_textbox_t *tb, cl_font_t *font, float wrap_w)
{
    size_t i = 0;

    tb->line_count = 0;
    tb->layout_width = wrap_w;
    tb->layout_dirty = false;

    for (;;) {
        size_t line_start = i;
        size_t last_break = 0; /* byte after the last space that fit; 0 = none */
        float width = 0.0f;
        size_t j = i;
        bool hard = false;
        bool wrapped = false;

        while (j < tb->len) {
            uint32_t cp;
            size_t n = cl_utf8_next(tb->buf + j, &cp);
            float adv;

            if (n == 0 || j + n > tb->len)
                break;
            if (cp == '\n') {
                hard = true;
                break;
            }
            adv = measure_span(font, tb->buf + j, n);
            if (wrap_w > 0.0f && width + adv > wrap_w && j > line_start) {
                wrapped = true;
                break;
            }
            width += adv;
            j += n;
            if (cp == ' ')
                last_break = j; /* break after this space */
        }

        if (hard) {
            emit_line(tb, line_start, j - line_start);
            i = j + 1; /* skip the '\n' */
            continue;
        }
        if (wrapped) {
            size_t brk = (last_break > line_start) ? last_break : j;

            emit_line(tb, line_start, brk - line_start);
            i = brk;
            continue;
        }
        emit_line(tb, line_start, tb->len - line_start); /* trailing line */
        break;
    }
    if (tb->line_count == 0)
        emit_line(tb, 0, 0); /* empty buffer still has one line */
}

static float tb_wrap_width(cl_textbox_t *tb)
{
    float w = tb->base.rect.w - 2.0f * TB_PAD_X;

    return w > 0.0f ? w : 0.0f;
}

static void tb_ensure_layout(cl_textbox_t *tb, cl_font_t *font)
{
    float wrap_w = tb_wrap_width(tb);

    if (tb->layout_dirty || wrap_w != tb->layout_width || tb->line_count == 0)
        tb_relayout(tb, font, wrap_w);
}

/* Index of the visual line containing byte offset (last line with start <= off). */
static size_t tb_line_of(cl_textbox_t *tb, size_t offset)
{
    size_t i;

    for (i = tb->line_count; i > 0; i--) {
        if (tb->lines[i - 1].start <= offset)
            return i - 1;
    }
    return 0;
}

/* Byte offset within a visual line nearest to x (measured from the line start). */
static size_t offset_in_line_at_x(cl_textbox_t *tb, cl_font_t *font, size_t line,
                                  float x)
{
    tb_line_t L;
    size_t end;
    size_t i;
    float px = 0.0f;
    uint32_t cp;
    size_t n;

    if (line >= tb->line_count) /* only reachable if layout OOM'd */
        return 0;
    L = tb->lines[line];
    end = L.start + L.len;
    i = L.start;

    if (!font || x <= 0.0f)
        return L.start;
    while (i < end && (n = cl_utf8_next(tb->buf + i, &cp)) != 0) {
        float adv;

        if (i + n > end)
            break;
        adv = measure_span(font, tb->buf + i, n);
        if (x < px + adv * 0.5f)
            return i;
        px += adv;
        i += n;
    }
    return end;
}

static void caret_pos(cl_textbox_t *tb, cl_font_t *font, size_t offset,
                      float *out_x, size_t *out_line)
{
    size_t line;

    if (tb->line_count == 0) { /* only reachable if layout OOM'd */
        *out_line = 0;
        *out_x = 0.0f;
        return;
    }
    line = tb_line_of(tb, offset);
    *out_line = line;
    *out_x = measure_span(font, tb->buf + tb->lines[line].start,
                          offset - tb->lines[line].start);
}

static size_t offset_at_point(cl_textbox_t *tb, cl_font_t *font, float cx,
                              float cy)
{
    float lh = line_height_of(font);
    long li;

    if (tb->line_count == 0)
        return 0;
    li = (long)(cy / lh);
    if (li < 0)
        li = 0;
    if ((size_t)li >= tb->line_count)
        li = (long)tb->line_count - 1;
    return offset_in_line_at_x(tb, font, (size_t)li, cx);
}

static void update_scroll_y(cl_textbox_t *tb, cl_font_t *font)
{
    float lh = line_height_of(font);
    float cy = (float)tb_line_of(tb, tb->cursor) * lh;
    float view_h = tb->base.rect.h - 2.0f * TB_PAD_Y;
    float content_h = (float)tb->line_count * lh;
    float max_scroll;

    if (view_h < 0.0f)
        view_h = 0.0f;
    if (cy - tb->scroll_y < 0.0f)
        tb->scroll_y = cy;
    else if (cy + lh - tb->scroll_y > view_h)
        tb->scroll_y = cy + lh - view_h;
    max_scroll = content_h - view_h;
    if (max_scroll < 0.0f)
        max_scroll = 0.0f;
    if (tb->scroll_y > max_scroll)
        tb->scroll_y = max_scroll;
    if (tb->scroll_y < 0.0f)
        tb->scroll_y = 0.0f;
}

/* ---- scroll ------------------------------------------------------------- */

static void update_scroll(cl_textbox_t *tb)
{
    cl_font_t *font = textbox_font(&tb->base);
    float cx;
    float visible;

    if (tb->multiline) {
        tb_ensure_layout(tb, font);
        update_scroll_y(tb, font);
        return;
    }

    cx = caret_x(tb, font, tb->cursor);
    visible = tb->base.rect.w - 2.0f * TB_PAD_X;
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
    tb->layout_dirty = true;
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
    tb->layout_dirty = true;
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
    tb->layout_dirty = true;
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
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);
    cl_font_t *font = textbox_font(w);
    float lh = line_height_of(font);

    (void)c;
    if (tb->multiline) {
        /* Default to four visible lines; honour an explicit preferred size. */
        float ww = w->pref_size.w > 0.0f ? w->pref_size.w : TB_DEFAULT_WIDTH;
        float hh = w->pref_size.h > 0.0f ? w->pref_size.h
                                         : 4.0f * lh + 2.0f * TB_PAD_Y;

        return (cl_size_t){ ww, hh };
    }
    return (cl_size_t){ TB_DEFAULT_WIDTH, lh + 2.0f * TB_PAD_Y };
}

/* Paint the wrapped, vertically-scrolled multiline layout. */
static void textbox_paint_multi(cl_widget_t *w, cl_paint_context_t *ctx,
                                cl_font_t *font, bool focused)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);
    float lh = line_height_of(font);
    float text_x = w->rect.x + TB_PAD_X;
    float text_top = w->rect.y + TB_PAD_Y;
    float view_h = w->rect.h - 2.0f * TB_PAD_Y;
    cl_rect_t inner = { text_x, text_top, w->rect.w - 2.0f * TB_PAD_X, view_h };
    cl_color_t text_col = cl_paint_theme_color(ctx, CL_COLOR_TEXT);
    bool composing = tb->preedit && tb->preedit[0];
    /* Hide the (still-live) selection while composing; commit replaces it. */
    bool sel = focused && has_selection(tb) && !composing;
    size_t caret_line = 0;
    float caret_cx = 0.0f;
    size_t lo = 0;
    size_t hi = 0;
    size_t first;
    size_t i;

    tb_ensure_layout(tb, font);

    if (tb->len == 0 && !focused && tb->placeholder && font) {
        cl_paint_draw_text(ctx, font, tb->placeholder,
                           (cl_point_t){ text_x, text_top },
                           cl_paint_theme_color(ctx, CL_COLOR_TEXT_MUTED));
        return;
    }
    if (!font)
        return;
    if (sel)
        sel_range(tb, &lo, &hi);
    if (composing)
        caret_pos(tb, font, tb->cursor, &caret_cx, &caret_line);

    /* Keep the offset in range if a resize shrank the content since the last
     * caret move (which is what normally clamps it). */
    {
        float max_scroll = (float)tb->line_count * lh - view_h;

        if (max_scroll < 0.0f)
            max_scroll = 0.0f;
        if (tb->scroll_y > max_scroll)
            tb->scroll_y = max_scroll;
        if (tb->scroll_y < 0.0f)
            tb->scroll_y = 0.0f;
    }

    cl_paint_push_clip(ctx, inner);
    first = (size_t)(tb->scroll_y / lh);
    for (i = first; i < tb->line_count; i++) {
        tb_line_t L = tb->lines[i];
        size_t end = L.start + L.len;
        float ly = text_top + (float)i * lh - tb->scroll_y;

        if (ly >= text_top + view_h)
            break; /* past the bottom of the viewport */

        if (sel && lo <= end && hi >= L.start) {
            size_t s = lo > L.start ? lo : L.start;
            size_t e = hi < end ? hi : end;
            float xs = measure_span(font, tb->buf + L.start, s - L.start);
            float xe = e > s ? measure_span(font, tb->buf + L.start, e - L.start)
                             : xs;
            float ww = xe - xs;

            if (hi > end)
                ww += lh * 0.3f; /* the newline/wrap into the next line */
            if (ww > 0.0f)
                cl_paint_fill_rect(
                    ctx, (cl_rect_t){ text_x + xs, ly, ww, lh },
                    cl_paint_theme_color(ctx, CL_COLOR_SELECTION));
        }

        if (composing && i == caret_line) {
            /* draw this line around the composition: text before the caret,
             * the underlined composition, then the rest shifted right */
            float px = text_x + caret_cx;
            float pw = cl_text_measure(font, tb->preedit, CL_UNBOUNDED).w;

            if (tb->cursor > L.start)
                draw_span(ctx, font, cl_application_allocator(w->app),
                          tb->buf + L.start, tb->cursor - L.start,
                          (cl_point_t){ text_x, ly }, text_col);
            cl_paint_draw_text(ctx, font, tb->preedit,
                               (cl_point_t){ px, ly }, text_col);
            cl_paint_fill_rect(ctx,
                               (cl_rect_t){ px, ly + lh - 2.0f, pw, 1.0f },
                               text_col);
            if (end > tb->cursor)
                draw_span(ctx, font, cl_application_allocator(w->app),
                          tb->buf + tb->cursor, end - tb->cursor,
                          (cl_point_t){ px + pw, ly }, text_col);
        } else if (L.len > 0) {
            /* draw exactly this line */
            draw_span(ctx, font, cl_application_allocator(w->app),
                      tb->buf + L.start, L.len,
                      (cl_point_t){ text_x, ly }, text_col);
        }
    }

    if (composing) {
        size_t pb = byte_offset_of_cp(tb->preedit, strlen(tb->preedit),
                                      (size_t)tb->preedit_cursor);
        float cxp = text_x + caret_cx +
                    cl_text_measure_bytes(font, tb->preedit, pb, CL_UNBOUNDED).w;
        float cyv = text_top + (float)caret_line * lh - tb->scroll_y;

        cl_paint_fill_rect(ctx, (cl_rect_t){ cxp, cyv, 1.0f, lh }, text_col);
    } else if (focused && !has_selection(tb)) {
        size_t line;
        float cx;
        float cyv;

        caret_pos(tb, font, tb->cursor, &cx, &line);
        cyv = text_top + (float)line * lh - tb->scroll_y;
        cl_paint_fill_rect(ctx, (cl_rect_t){ text_x + cx, cyv, 1.0f, lh },
                           text_col);
    }
    cl_paint_pop_clip(ctx);
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
    bool composing = tb->preedit && tb->preedit[0] && !tb->password;
    cl_rect_t inner = { w->rect.x + TB_PAD_X, w->rect.y,
                        w->rect.w - 2.0f * TB_PAD_X, w->rect.h };

    cl_paint_fill_round_rect(ctx, w->rect, TB_RADIUS,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    cl_paint_stroke_round_rect(
        ctx, w->rect, TB_RADIUS, focused ? 2.0f : 1.0f,
        cl_paint_theme_color(ctx, focused ? CL_COLOR_FOCUS_RING
                                          : CL_COLOR_BORDER));

    if (tb->multiline) {
        textbox_paint_multi(w, ctx, font, focused);
        return;
    }

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

    /* selection highlight (hidden while composing; commit replaces it) */
    if (focused && has_selection(tb) && font && !composing) {
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

    if (composing && font) {
        cl_color_t tc = cl_paint_theme_color(ctx, CL_COLOR_TEXT);
        float px = text_x + caret_x(tb, font, tb->cursor);
        float pw = cl_text_measure(font, tb->preedit, CL_UNBOUNDED).w;
        size_t pb;

        /* buffer text before the caret, the composition (underlined), then the
         * remaining buffer text shifted right by the composition width */
        draw_span(ctx, font, cl_application_allocator(w->app), tb->buf,
                  tb->cursor, (cl_point_t){ text_x, text_y }, tc);
        cl_paint_draw_text(ctx, font, tb->preedit,
                           (cl_point_t){ px, text_y }, tc);
        cl_paint_fill_rect(ctx, (cl_rect_t){ px, text_y + lh - 2.0f, pw, 1.0f },
                           tc);
        if (tb->buf[tb->cursor])
            cl_paint_draw_text(ctx, font, tb->buf + tb->cursor,
                               (cl_point_t){ px + pw, text_y }, tc);
        pb = byte_offset_of_cp(tb->preedit, strlen(tb->preedit),
                               (size_t)tb->preedit_cursor);
        cl_paint_fill_rect(
            ctx,
            (cl_rect_t){ px + cl_text_measure_bytes(font, tb->preedit, pb,
                                                    CL_UNBOUNDED).w,
                         text_y, 1.0f, lh },
            tc);
    } else {
        if (font && draw && draw[0])
            cl_paint_draw_text(ctx, font, draw,
                               (cl_point_t){ text_x, text_y },
                               cl_paint_theme_color(ctx, CL_COLOR_TEXT));

        /* caret */
        if (focused && !has_selection(tb) && font) {
            float cx = text_x + caret_x(tb, font, tb->cursor);

            cl_paint_fill_rect(ctx, (cl_rect_t){ cx, text_y, 1.0f, lh },
                               cl_paint_theme_color(ctx, CL_COLOR_TEXT));
        }
    }

    cl_paint_pop_clip(ctx);

    if (stars)
        cl_free(cl_application_allocator(w->app), stars);
}

/* Byte offset of the caret position for a mouse event at window pos. */
static size_t tb_offset_at_pos(cl_textbox_t *tb, cl_font_t *font,
                               cl_point_t pos)
{
    cl_widget_t *w = &tb->base;

    if (tb->multiline) {
        float cx = pos.x - (w->rect.x + TB_PAD_X);
        float cy = pos.y - (w->rect.y + TB_PAD_Y) + tb->scroll_y;

        tb_ensure_layout(tb, font);
        return offset_at_point(tb, font, cx, cy);
    } else {
        float local_x = pos.x - (w->rect.x + TB_PAD_X) + tb->scroll_x;

        return offset_at_x(tb, font, local_x);
    }
}

static bool textbox_mouse_down(cl_widget_t *w, const cl_event_t *ev)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);
    cl_font_t *font = textbox_font(w);
    size_t off;

    if (ev->data.mouse.button != CL_MOUSE_LEFT)
        return false; /* only the primary button places the caret */
    off = tb_offset_at_pos(tb, font, ev->data.mouse.pos);

    edit_break(tb); /* a click starts a fresh undo group */
    tb_clear_preedit(tb); /* a click cancels any in-progress composition */
    if (ev->data.mouse.clicks >= 2) {
        select_word_at(tb, off); /* double click selects the word */
        tb->drag_select = false;
    } else {
        tb->cursor = off;
        if (!(ev->mods & CL_MOD_SHIFT))
            tb->anchor = off;
        tb->drag_select = true; /* dragging now extends the selection */
    }
    tb->goal_valid = false;
    update_scroll(tb);
    cl_widget_invalidate(w);
    return true;
}

static bool textbox_mouse_move(cl_widget_t *w, const cl_event_t *ev)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);
    cl_font_t *font = textbox_font(w);
    size_t off;

    if (!tb->drag_select)
        return false;
    /* The mouse capture routes moves here while the button is held, even
     * outside the widget: keep extending toward the pointer. */
    off = tb_offset_at_pos(tb, font, ev->data.mouse.pos);
    if (off != tb->cursor) {
        tb->cursor = off; /* the anchor stays: this is the selection */
        tb->goal_valid = false;
        update_scroll(tb);
        cl_widget_invalidate(w);
    }
    return true;
}

static bool textbox_mouse_up(cl_widget_t *w, const cl_event_t *ev)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);

    if (ev->data.mouse.button != CL_MOUSE_LEFT || !tb->drag_select)
        return false;
    tb->drag_select = false;
    return true;
}

static bool textbox_mouse_wheel(cl_widget_t *w, const cl_event_t *ev)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);
    cl_font_t *font = textbox_font(w);
    float lh;
    float view_h;
    float max_scroll;
    float before;

    if (!tb->multiline)
        return false; /* single line: let an outer scroller take the wheel */
    tb_ensure_layout(tb, font);
    lh = line_height_of(font);
    view_h = w->rect.h - 2.0f * TB_PAD_Y;
    if (view_h < 0.0f)
        view_h = 0.0f;
    max_scroll = (float)tb->line_count * lh - view_h;
    if (max_scroll <= 0.0f)
        return false; /* nothing to scroll: bubble to an outer container */
    before = tb->scroll_y;
    tb->scroll_y -= ev->data.wheel.dy * lh * 3.0f; /* three lines per notch */
    if (tb->scroll_y < 0.0f)
        tb->scroll_y = 0.0f;
    if (tb->scroll_y > max_scroll)
        tb->scroll_y = max_scroll;
    if (tb->scroll_y == before)
        return false; /* already at the edge */
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
    bool submit = false;
    tb_edit_kind_t commit = TB_EDIT_NONE;
    size_t lo;
    size_t hi;

    /* Any motion other than Up/Down resets the goal column for the next
     * Up/Down (which preserves the caret's x across short lines). */
    if (key != CL_KEY_UP && key != CL_KEY_DOWN)
        tb->goal_valid = false;

    switch (key) {
        case CL_KEY_LEFT:
            edit_break(tb);
            if (ctrl) { /* word motion (Shift extends the selection) */
                move_cursor(tb, word_prev(tb, tb->cursor), shift);
            } else if (has_selection(tb) && !shift) {
                sel_range(tb, &lo, &hi);
                move_cursor(tb, lo, false);
            } else {
                move_cursor(tb, prev_boundary(tb->buf, tb->cursor), shift);
            }
            break;

        case CL_KEY_RIGHT:
            edit_break(tb);
            if (ctrl) { /* word motion (Shift extends the selection) */
                move_cursor(tb, word_next(tb, tb->cursor), shift);
            } else if (has_selection(tb) && !shift) {
                sel_range(tb, &lo, &hi);
                move_cursor(tb, hi, false);
            } else {
                move_cursor(tb, next_boundary(tb->buf, tb->len, tb->cursor),
                            shift);
            }
            break;

        case CL_KEY_UP:
        case CL_KEY_DOWN:
            if (!tb->multiline) {
                handled = false;
                break;
            }
            edit_break(tb);
            {
                cl_font_t *font = textbox_font(w);
                size_t line;
                float cx;

                tb_ensure_layout(tb, font);
                caret_pos(tb, font, tb->cursor, &cx, &line);
                if (!tb->goal_valid) {
                    tb->goal_x = cx;
                    tb->goal_valid = true;
                }
                if (key == CL_KEY_UP)
                    tb->cursor = line == 0 ? 0
                        : offset_in_line_at_x(tb, font, line - 1, tb->goal_x);
                else
                    tb->cursor = line + 1 >= tb->line_count ? tb->len
                        : offset_in_line_at_x(tb, font, line + 1, tb->goal_x);
                if (!shift)
                    tb->anchor = tb->cursor;
            }
            break;

        case CL_KEY_HOME:
            edit_break(tb);
            if (tb->multiline && !ctrl) {
                cl_font_t *font = textbox_font(w);

                tb_ensure_layout(tb, font);
                if (tb->line_count > 0) /* 0 only if a layout alloc failed */
                    move_cursor(tb, tb->lines[tb_line_of(tb, tb->cursor)].start,
                                shift);
            } else if (ctrl && !tb->multiline) {
                handled = false;
            } else {
                move_cursor(tb, 0, shift); /* Ctrl+Home (multiline) = doc start */
            }
            break;

        case CL_KEY_END:
            edit_break(tb);
            if (tb->multiline && !ctrl) {
                cl_font_t *font = textbox_font(w);

                tb_ensure_layout(tb, font);
                if (tb->line_count > 0) { /* 0 only if a layout alloc failed */
                    tb_line_t L = tb->lines[tb_line_of(tb, tb->cursor)];

                    move_cursor(tb, L.start + L.len, shift);
                }
            } else if (ctrl && !tb->multiline) {
                handled = false;
            } else {
                move_cursor(tb, tb->len, shift); /* Ctrl+End (multiline) = end */
            }
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
            if (tb->multiline) {
                if (!tb->readonly) {
                    edit_begin(tb);
                    insert_text(tb, "\n", 1);
                    commit = TB_EDIT_OTHER; /* a newline breaks the undo group */
                }
            } else if (tb->on_submit) {
                submit = true; /* fired below, after the state updates */
            } else {
                handled = false;
            }
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
                        if (!tb->multiline)
                            strip_newlines(clip); /* single-line: drop breaks */
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
        update_scroll(tb);
        cl_widget_invalidate(w);
        /* Callbacks last: they may destroy the textbox. */
        if (force_notify)
            notify_changed(tb);
        if (submit && tb->on_submit)
            tb->on_submit(w, tb->buf, tb->on_submit_user);
    }
    return handled;
}

/* ---- IME composition ---------------------------------------------------- */

static void tb_clear_preedit(cl_textbox_t *tb)
{
    if (tb->preedit) {
        cl_free(cl_application_allocator(tb->base.app), tb->preedit);
        tb->preedit = NULL;
        tb->preedit_cursor = 0;
    }
}

/* Place the IME candidate window at the caret (logical window coords). */
static void tb_update_ime_rect(cl_textbox_t *tb)
{
    cl_widget_t *w = &tb->base;
    cl_font_t *font = textbox_font(w);
    float lh = line_height_of(font);
    cl_rect_t caret;

    if (tb->multiline) {
        size_t line;
        float cx;

        tb_ensure_layout(tb, font);
        caret_pos(tb, font, tb->cursor, &cx, &line);
        caret.x = w->rect.x + TB_PAD_X + cx;
        caret.y = w->rect.y + TB_PAD_Y + (float)line * lh - tb->scroll_y;
    } else {
        caret.x = w->rect.x + TB_PAD_X - tb->scroll_x +
                  caret_x(tb, font, tb->cursor);
        caret.y = w->rect.y + (w->rect.h - lh) * 0.5f;
    }
    caret.w = 1.0f;
    caret.h = lh;
    cl_app_set_ime_rect(w->app, caret);
}

static bool textbox_text_edit(cl_widget_t *w, const cl_event_t *ev)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);
    const char *s = ev->data.edit.utf8;

    if (tb->readonly)
        return true;
    tb_clear_preedit(tb);
    if (s && s[0]) {
        tb->preedit = cl_strdup(cl_application_allocator(w->app), s);
        /* a negative cursor (signed platform field) clamps to the start */
        tb->preedit_cursor =
            ev->data.edit.cursor < 0 ? 0 : ev->data.edit.cursor;
    }
    update_scroll(tb); /* keep the caret line visible while composing */
    tb_update_ime_rect(tb);
    cl_widget_invalidate(w);
    return true;
}

static bool textbox_text_input(cl_widget_t *w, const cl_event_t *ev)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);
    const char *s = ev->data.text.utf8;
    bool changed;

    if (!s || !s[0] || tb->readonly)
        return true;
    tb_clear_preedit(tb); /* the composition is committed by this input */
    edit_begin(tb);
    insert_text(tb, s, strlen(s));
    changed = edit_commit(tb, TB_EDIT_TYPE); /* true only when bytes changed */
    update_scroll(tb);
    tb_update_ime_rect(tb);
    cl_widget_invalidate(w);
    /* Last: the callback may destroy the textbox. */
    if (changed)
        notify_changed(tb);
    return true;
}

static void textbox_focus_changed(cl_widget_t *w)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, w);

    if (cl_widget_has_focus(w))
        tb_update_ime_rect(tb); /* position the candidate window */
    else
        tb_clear_preedit(tb); /* composition is abandoned when focus leaves */
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
    cl_free(a, tb->lines);
    cl_free(a, tb->preedit);
}

/* ---- public ------------------------------------------------------------- */

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
    tb->scroll_y = 0.0f;
    tb->goal_valid = false;
    tb->layout_dirty = true;
    tb_clear_preedit(tb);
    clear_history(tb); /* a programmatic set is not an undoable edit */
}

cl_widget_t *cl_textbox_create(cl_application_t *app,
                               const cl_textbox_desc_t *desc)
{
    cl_widget_t *w;
    cl_textbox_t *tb;

    if (!CL_DESC_ABI_OK(desc, cl_textbox_desc_t))
        return NULL;
    /* Multiline painting has no masking path: it would show the secret in
     * plain text, so the combination is rejected outright. */
    if (desc && desc->password && desc->multiline) {
        cl_set_last_error(CL_ERROR_INVALID_ARGUMENT);
        return NULL;
    }
    w = cl_widget_alloc(app, &cl_textbox_class);
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

    tb->layout_dirty = true;

    if (desc) {
        tb->password = desc->password;
        tb->readonly = desc->readonly;
        tb->multiline = desc->multiline;
        tb->max_length = desc->max_length;
        tb->placeholder = cl_strdup(cl_application_allocator(app),
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

size_t cl_textbox_line_count(cl_widget_t *tb_w)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, tb_w);

    if (!tb)
        return 0;
    if (!tb->multiline)
        return 1;
    tb_ensure_layout(tb, textbox_font(tb_w));
    return tb->line_count;
}

size_t cl_textbox_cursor_line(cl_widget_t *tb_w)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, tb_w);

    if (!tb || !tb->multiline)
        return 0;
    tb_ensure_layout(tb, textbox_font(tb_w));
    return tb_line_of(tb, tb->cursor);
}

const char *cl_textbox_preedit(cl_widget_t *tb_w)
{
    cl_textbox_t *tb = CL_WIDGET_CAST(cl_textbox, tb_w);

    return tb ? tb->preedit : NULL;
}
