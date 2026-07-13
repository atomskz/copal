/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "render/mock/renderer_mock.h"

#include <string.h>

#define CL_MOCK_MAX_COMMANDS 256
#define CL_MOCK_CLIP_STACK 16
#define CL_MOCK_TF_STACK 16

typedef struct mock_renderer {
    cl_renderer_t base;
    const cl_allocator_t *a;
    cl_mock_command_t cmds[CL_MOCK_MAX_COMMANDS];
    size_t count;
    size_t dropped; /* commands lost to a full buffer (see _dropped()) */
    cl_color_t clear;
    cl_rect_t clip_stack[CL_MOCK_CLIP_STACK];
    size_t clip_depth;
    /* Transform stack: composed translate+scale per level (identity at 0). */
    struct mock_tf {
        float s, tx, ty;
    } tf_stack[CL_MOCK_TF_STACK];
    size_t tf_depth;
    float op_stack[CL_MOCK_TF_STACK]; /* composed group opacity per level */
    size_t op_depth;
} mock_renderer_t;

static struct mock_tf mock_tf_cur(const mock_renderer_t *m)
{
    size_t top = m->tf_depth < CL_MOCK_TF_STACK ? m->tf_depth
                                                : CL_MOCK_TF_STACK;

    if (top == 0)
        return (struct mock_tf){ 1.0f, 0.0f, 0.0f };
    return m->tf_stack[top - 1];
}

static cl_rect_t mock_tf_rect(const mock_renderer_t *m, cl_rect_t rc)
{
    struct mock_tf t = mock_tf_cur(m);

    return (cl_rect_t){ rc.x * t.s + t.tx, rc.y * t.s + t.ty, rc.w * t.s,
                        rc.h * t.s };
}

static float mock_op_cur(const mock_renderer_t *m)
{
    size_t top = m->op_depth < CL_MOCK_TF_STACK ? m->op_depth
                                                : CL_MOCK_TF_STACK;

    return top == 0 ? 1.0f : m->op_stack[top - 1];
}

static cl_rect_t mock_clip_top(const mock_renderer_t *m)
{
    size_t idx = m->clip_depth < CL_MOCK_CLIP_STACK ? m->clip_depth
                                                    : CL_MOCK_CLIP_STACK - 1;

    return m->clip_stack[idx];
}

static cl_rect_t rect_intersect(cl_rect_t a, cl_rect_t b)
{
    float x0 = a.x > b.x ? a.x : b.x;
    float y0 = a.y > b.y ? a.y : b.y;
    float x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    float y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    cl_rect_t out = { x0, y0, x1 - x0, y1 - y0 };

    if (out.w < 0.0f)
        out.w = 0.0f;
    if (out.h < 0.0f)
        out.h = 0.0f;
    return out;
}

static void mock_record(mock_renderer_t *m, cl_mock_command_t *cmd)
{
    cmd->clip = mock_clip_top(m);
    if (m->count < CL_MOCK_MAX_COMMANDS)
        m->cmds[m->count++] = *cmd;
    else
        m->dropped++; /* a negative assert on this frame would lie */
}

/* Record a DRAW command: the current transform is applied to its geometry
 * and the group opacity multiplied into its colour, so tests assert final
 * on-screen values (renderer_mock.h). */
static void mock_record_draw(mock_renderer_t *m, cl_mock_command_t *cmd)
{
    struct mock_tf t = mock_tf_cur(m);

    cmd->rect = mock_tf_rect(m, cmd->rect);
    cmd->pos.x = cmd->pos.x * t.s + t.tx;
    cmd->pos.y = cmd->pos.y * t.s + t.ty;
    cmd->radius *= t.s;
    cmd->width *= t.s;
    cmd->color.a = (uint8_t)((float)cmd->color.a * mock_op_cur(m) + 0.5f);
    mock_record(m, cmd);
}

static void mock_begin_frame(cl_renderer_t *r, cl_size_t size, float scale,
                             cl_color_t clear)
{
    mock_renderer_t *m = (mock_renderer_t *)r;
    (void)scale;
    m->count = 0;
    m->dropped = 0;
    m->clear = clear;
    m->clip_depth = 0;
    m->clip_stack[0] = (cl_rect_t){ 0.0f, 0.0f, size.w, size.h };
    m->tf_depth = 0;
    m->op_depth = 0;
}

static void mock_end_frame(cl_renderer_t *r)
{
    (void)r;
}

static void mock_fill_rect(cl_renderer_t *r, cl_rect_t rect, cl_color_t color)
{
    cl_mock_command_t c = { 0 };
    c.kind = CL_MOCK_FILL_RECT;
    c.rect = rect;
    c.color = color;
    mock_record_draw((mock_renderer_t *)r, &c);
}

static void mock_fill_round_rect(cl_renderer_t *r, cl_rect_t rect, float radius,
                                 cl_color_t color)
{
    cl_mock_command_t c = { 0 };
    c.kind = CL_MOCK_FILL_ROUND;
    c.rect = rect;
    c.radius = radius;
    c.color = color;
    mock_record_draw((mock_renderer_t *)r, &c);
}

static void mock_stroke_round_rect(cl_renderer_t *r, cl_rect_t rect,
                                   float radius, float width, cl_color_t color)
{
    cl_mock_command_t c = { 0 };
    c.kind = CL_MOCK_STROKE_ROUND;
    c.rect = rect;
    c.radius = radius;
    c.width = width;
    c.color = color;
    mock_record_draw((mock_renderer_t *)r, &c);
}

static void mock_draw_text(cl_renderer_t *r, cl_font_t *font, const char *utf8,
                           cl_point_t pos, cl_color_t color)
{
    cl_mock_command_t c = { 0 };
    (void)font;
    c.kind = CL_MOCK_TEXT;
    c.pos = pos;
    c.color = color;
    if (utf8) {
        size_t n = strlen(utf8);
        if (n >= sizeof(c.text))
            n = sizeof(c.text) - 1;
        memcpy(c.text, utf8, n);
        c.text[n] = '\0';
    }
    mock_record_draw((mock_renderer_t *)r, &c);
}

static void mock_draw_image(cl_renderer_t *rr, cl_image_t *img, cl_rect_t dst)
{
    cl_mock_command_t c;

    memset(&c, 0, sizeof(c));
    c.kind = CL_MOCK_IMAGE;
    c.rect = dst;
    c.image = img;
    mock_record_draw((mock_renderer_t *)rr, &c);
}

static void mock_push_clip(cl_renderer_t *r, cl_rect_t rect)
{
    mock_renderer_t *m = (mock_renderer_t *)r;
    cl_mock_command_t c = { 0 };
    /* Clip rects transform like geometry (both for the stack and the record). */
    cl_rect_t merged = rect_intersect(mock_clip_top(m), mock_tf_rect(m, rect));

    rect = mock_tf_rect(m, rect);

    m->clip_depth++; /* always advance so a later pop stays balanced */
    if (m->clip_depth < CL_MOCK_CLIP_STACK)
        m->clip_stack[m->clip_depth] = merged;
    c.kind = CL_MOCK_PUSH_CLIP;
    c.rect = rect;
    c.clip = mock_clip_top(m); /* effective clip now in force */
    if (m->count < CL_MOCK_MAX_COMMANDS)
        m->cmds[m->count++] = c;
}

static void mock_pop_clip(cl_renderer_t *r)
{
    mock_renderer_t *m = (mock_renderer_t *)r;
    cl_mock_command_t c = { 0 };

    if (m->clip_depth > 0)
        m->clip_depth--;
    c.kind = CL_MOCK_POP_CLIP;
    c.clip = mock_clip_top(m);
    if (m->count < CL_MOCK_MAX_COMMANDS)
        m->cmds[m->count++] = c;
}

static void mock_push_transform(cl_renderer_t *r, cl_point_t offset,
                                float scale)
{
    mock_renderer_t *m = (mock_renderer_t *)r;
    struct mock_tf cur = mock_tf_cur(m);
    struct mock_tf next;
    cl_mock_command_t c = { 0 };

    /* Compose: p -> cur(p * scale + offset). */
    next.s = cur.s * scale;
    next.tx = cur.tx + cur.s * offset.x;
    next.ty = cur.ty + cur.s * offset.y;
    if (m->tf_depth < CL_MOCK_TF_STACK)
        m->tf_stack[m->tf_depth] = next;
    m->tf_depth++; /* always advance so a later pop stays balanced */
    c.kind = CL_MOCK_PUSH_TRANSFORM;
    c.pos = offset; /* raw, local parameters */
    c.width = scale;
    mock_record(m, &c);
}

static void mock_pop_transform(cl_renderer_t *r)
{
    mock_renderer_t *m = (mock_renderer_t *)r;
    cl_mock_command_t c = { 0 };

    if (m->tf_depth > 0)
        m->tf_depth--;
    c.kind = CL_MOCK_POP_TRANSFORM;
    mock_record(m, &c);
}

static void mock_push_opacity(cl_renderer_t *r, float alpha)
{
    mock_renderer_t *m = (mock_renderer_t *)r;
    cl_mock_command_t c = { 0 };
    float next;

    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    next = mock_op_cur(m) * alpha;
    if (m->op_depth < CL_MOCK_TF_STACK)
        m->op_stack[m->op_depth] = next;
    m->op_depth++; /* always advance so a later pop stays balanced */
    c.kind = CL_MOCK_PUSH_OPACITY;
    c.width = alpha; /* raw, local parameter */
    mock_record(m, &c);
}

static void mock_pop_opacity(cl_renderer_t *r)
{
    mock_renderer_t *m = (mock_renderer_t *)r;
    cl_mock_command_t c = { 0 };

    if (m->op_depth > 0)
        m->op_depth--;
    c.kind = CL_MOCK_POP_OPACITY;
    mock_record(m, &c);
}

static void mock_destroy(cl_renderer_t *r)
{
    mock_renderer_t *m = (mock_renderer_t *)r;
    cl_free(m->a, m);
}

static const cl_renderer_ops_t mock_ops = {
    .struct_size = sizeof(cl_renderer_ops_t),
    .abi_version = COPAL_VERSION,
    .begin_frame = mock_begin_frame,
    .end_frame = mock_end_frame,
    .fill_rect = mock_fill_rect,
    .fill_round_rect = mock_fill_round_rect,
    .stroke_round_rect = mock_stroke_round_rect,
    .draw_text = mock_draw_text,
    .draw_image = mock_draw_image,
    .push_clip = mock_push_clip,
    .pop_clip = mock_pop_clip,
    .push_transform = mock_push_transform,
    .pop_transform = mock_pop_transform,
    .push_opacity = mock_push_opacity,
    .pop_opacity = mock_pop_opacity,
    .destroy = mock_destroy,
};

cl_renderer_t *cl_renderer_mock_create(const cl_allocator_t *a)
{
    mock_renderer_t *m = cl_alloc(a, sizeof(*m));

    if (!m)
        return NULL;
    memset(m, 0, sizeof(*m));
    m->base.ops = &mock_ops;
    m->a = a;
    return &m->base;
}

size_t cl_renderer_mock_count(cl_renderer_t *r)
{
    return ((mock_renderer_t *)r)->count;
}

const cl_mock_command_t *cl_renderer_mock_get(cl_renderer_t *r, size_t i)
{
    mock_renderer_t *m = (mock_renderer_t *)r;

    return i < m->count ? &m->cmds[i] : NULL;
}

cl_color_t cl_renderer_mock_clear_color(cl_renderer_t *r)
{
    return ((mock_renderer_t *)r)->clear;
}

size_t cl_renderer_mock_dropped(cl_renderer_t *r)
{
    return ((mock_renderer_t *)r)->dropped;
}
