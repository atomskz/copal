/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "render/gl/renderer_gl.h"
#include "render/gl/gl_loader.h"
#include "text/font_internal.h"
#include "core/foundation/foundation_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATLAS_W 512
#define ATLAS_H 512
#define MAX_GLYPHS 512
#define MAX_TEXT_GLYPHS 256
#define CL_GL_CLIP_STACK 16

typedef struct glyph {
    cl_font_t *font;
    uint32_t cp;
    float u0, v0, u1, v1;
    int w, h;
    int xoff, yoff;
    float advance;
} glyph_t;

typedef struct gl_renderer {
    cl_renderer_t base;
    const cl_allocator_t *a;
    cl_platform_t *platform;
    struct gl_api gl;
    bool init;
    bool ok;
    GLuint rect_prog;
    GLuint text_prog;
    GLuint rect_vao, rect_vbo;
    GLuint text_vao, text_vbo;
    GLint r_proj, r_rect, r_radius, r_color, r_border;
    GLint t_proj, t_color, t_atlas;
    GLuint atlas;
    int pen_x, pen_y, row_h;
    glyph_t glyphs[MAX_GLYPHS];
    int glyph_count;
    float proj[16];
    cl_size_t vp;   /* logical viewport size */
    float scale;    /* logical -> framebuffer pixel scale */
    int fb_w, fb_h; /* framebuffer size in physical px (matches glViewport) */
    cl_rect_t clip_stack[CL_GL_CLIP_STACK];
    int clip_depth;
} gl_renderer_t;

static const char *RECT_VS =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_unit;\n"
    "uniform mat4 u_proj;\n"
    "uniform vec4 u_rect;\n"
    "out vec2 v_pos;\n"
    "void main(){\n"
    "  vec2 p = u_rect.xy + a_unit * u_rect.zw;\n"
    "  v_pos = p;\n"
    "  gl_Position = u_proj * vec4(p, 0.0, 1.0);\n"
    "}\n";

static const char *RECT_FS =
    "#version 330 core\n"
    "in vec2 v_pos;\n"
    "uniform vec4 u_rect;\n"
    "uniform float u_radius;\n"
    "uniform vec4 u_color;\n"
    "uniform float u_border;\n"
    "out vec4 frag;\n"
    "float sdRoundBox(vec2 p, vec2 b, float r){\n"
    "  vec2 q = abs(p) - b + vec2(r);\n"
    "  return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;\n"
    "}\n"
    "void main(){\n"
    "  vec2 center = u_rect.xy + u_rect.zw * 0.5;\n"
    "  vec2 half_b = u_rect.zw * 0.5;\n"
    "  float d = sdRoundBox(v_pos - center, half_b, u_radius);\n"
    "  float aa = 1.0;\n"
    "  float a;\n"
    "  if (u_border <= 0.0) {\n"
    "    a = 1.0 - smoothstep(0.0, aa, d);\n"
    "  } else {\n"
    "    float outer = 1.0 - smoothstep(0.0, aa, d);\n"
    "    float inner = 1.0 - smoothstep(0.0, aa, d + u_border);\n"
    "    a = clamp(outer - inner, 0.0, 1.0);\n"
    "  }\n"
    "  frag = vec4(u_color.rgb, u_color.a * a);\n"
    "}\n";

static const char *TEXT_VS =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "uniform mat4 u_proj;\n"
    "out vec2 v_uv;\n"
    "void main(){\n"
    "  v_uv = a_uv;\n"
    "  gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *TEXT_FS =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_atlas;\n"
    "uniform vec4 u_color;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  float a = texture(u_atlas, v_uv).r;\n"
    "  frag = vec4(u_color.rgb, u_color.a * a);\n"
    "}\n";

static void *gl_get(void *ctx, const char *name)
{
    cl_platform_t *p = ctx;

    return p->ops->gl_get_proc(p, name);
}

static GLuint compile_shader(struct gl_api *gl, GLenum type, const char *src)
{
    GLuint s = gl->CreateShader(type);
    GLint ok = 0;

    gl->ShaderSource(s, 1, (const GLchar *const *)&src, NULL);
    gl->CompileShader(s);
    gl->GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];

        gl->GetShaderInfoLog(s, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "copal gl: shader compile failed: %s\n", log);
        gl->DeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(struct gl_api *gl, const char *vs, const char *fs)
{
    GLuint v = compile_shader(gl, GL_VERTEX_SHADER, vs);
    GLuint f = compile_shader(gl, GL_FRAGMENT_SHADER, fs);
    GLuint p;
    GLint ok = 0;

    if (!v || !f) {
        if (v)
            gl->DeleteShader(v);
        if (f)
            gl->DeleteShader(f);
        return 0;
    }
    p = gl->CreateProgram();
    gl->AttachShader(p, v);
    gl->AttachShader(p, f);
    gl->LinkProgram(p);
    gl->GetProgramiv(p, GL_LINK_STATUS, &ok);
    gl->DeleteShader(v);
    gl->DeleteShader(f);
    if (!ok) {
        char log[512];

        gl->GetProgramInfoLog(p, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "copal gl: link failed: %s\n", log);
        gl->DeleteProgram(p);
        return 0;
    }
    return p;
}

static void gl_init(gl_renderer_t *r)
{
    struct gl_api *gl = &r->gl;
    static const float unit[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    unsigned char *zero;

    r->init = true;
    if (!cl_gl_load(gl, gl_get, r->platform)) {
        fprintf(stderr, "copal gl: failed to load GL functions\n");
        return;
    }

    if (getenv("COPAL_GL_DEBUG")) {
        const GLubyte *ver = gl->GetString(GL_VERSION);
        const GLubyte *ren = gl->GetString(GL_RENDERER);

        fprintf(stderr, "copal gl: %s | %s\n", ver ? (const char *)ver : "?",
                ren ? (const char *)ren : "?");
    }

    r->rect_prog = link_program(gl, RECT_VS, RECT_FS);
    r->text_prog = link_program(gl, TEXT_VS, TEXT_FS);
    if (!r->rect_prog || !r->text_prog)
        return;

    r->r_proj = gl->GetUniformLocation(r->rect_prog, "u_proj");
    r->r_rect = gl->GetUniformLocation(r->rect_prog, "u_rect");
    r->r_radius = gl->GetUniformLocation(r->rect_prog, "u_radius");
    r->r_color = gl->GetUniformLocation(r->rect_prog, "u_color");
    r->r_border = gl->GetUniformLocation(r->rect_prog, "u_border");
    r->t_proj = gl->GetUniformLocation(r->text_prog, "u_proj");
    r->t_color = gl->GetUniformLocation(r->text_prog, "u_color");
    r->t_atlas = gl->GetUniformLocation(r->text_prog, "u_atlas");

    gl->GenVertexArrays(1, &r->rect_vao);
    gl->BindVertexArray(r->rect_vao);
    gl->GenBuffers(1, &r->rect_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, r->rect_vbo);
    gl->BufferData(GL_ARRAY_BUFFER, sizeof(unit), unit, GL_STATIC_DRAW);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * (GLsizei)sizeof(float),
                            (void *)0);

    gl->GenVertexArrays(1, &r->text_vao);
    gl->BindVertexArray(r->text_vao);
    gl->GenBuffers(1, &r->text_vbo);
    gl->BindBuffer(GL_ARRAY_BUFFER, r->text_vbo);
    gl->EnableVertexAttribArray(0);
    gl->VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float),
                            (void *)0);
    gl->EnableVertexAttribArray(1);
    gl->VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float),
                            (void *)(2 * sizeof(float)));

    gl->GenTextures(1, &r->atlas);
    gl->BindTexture(GL_TEXTURE_2D, r->atlas);
    gl->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    zero = calloc(ATLAS_W * ATLAS_H, 1);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_W, ATLAS_H, 0, GL_RED,
                   GL_UNSIGNED_BYTE, zero);
    free(zero);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->BindVertexArray(0);

    r->ok = true;
}

static glyph_t *get_glyph(gl_renderer_t *r, cl_font_t *font, uint32_t cp)
{
    struct gl_api *gl = &r->gl;
    const stbtt_fontinfo *info = cl_font_info(font);
    float scale = cl_font_pixel_scale(font);
    unsigned char *bmp;
    glyph_t *g;
    int w = 0;
    int h = 0;
    int xoff = 0;
    int yoff = 0;
    int adv = 0;
    int lsb = 0;
    int i;

    for (i = 0; i < r->glyph_count; i++) {
        if (r->glyphs[i].font == font && r->glyphs[i].cp == cp)
            return &r->glyphs[i];
    }
    if (r->glyph_count >= MAX_GLYPHS)
        return NULL;

    bmp = stbtt_GetCodepointBitmap(info, 0, scale, (int)cp, &w, &h, &xoff,
                                   &yoff);
    stbtt_GetCodepointHMetrics(info, (int)cp, &adv, &lsb);

    g = &r->glyphs[r->glyph_count++];
    memset(g, 0, sizeof(*g));
    g->font = font;
    g->cp = cp;
    g->advance = (float)adv * scale;
    g->xoff = xoff;
    g->yoff = yoff;

    if (bmp && w > 0 && h > 0) {
        if (r->pen_x + w + 1 > ATLAS_W) {
            r->pen_x = 0;
            r->pen_y += r->row_h + 1;
            r->row_h = 0;
        }
        if (r->pen_y + h + 1 <= ATLAS_H) {
            gl->BindTexture(GL_TEXTURE_2D, r->atlas);
            gl->TexSubImage2D(GL_TEXTURE_2D, 0, r->pen_x, r->pen_y, w, h,
                              GL_RED, GL_UNSIGNED_BYTE, bmp);
            g->w = w;
            g->h = h;
            g->u0 = (float)r->pen_x / ATLAS_W;
            g->v0 = (float)r->pen_y / ATLAS_H;
            g->u1 = (float)(r->pen_x + w) / ATLAS_W;
            g->v1 = (float)(r->pen_y + h) / ATLAS_H;
            r->pen_x += w + 1;
            if (h > r->row_h)
                r->row_h = h;
        }
    }
    if (bmp)
        stbtt_FreeBitmap(bmp, NULL);
    return g;
}

static void set_proj(gl_renderer_t *r, float w, float h)
{
    memset(r->proj, 0, sizeof(r->proj));
    r->proj[0] = 2.0f / w;
    r->proj[5] = -2.0f / h;
    r->proj[10] = -1.0f;
    r->proj[12] = -1.0f;
    r->proj[13] = 1.0f;
    r->proj[15] = 1.0f;
}

static void gl_begin_frame(cl_renderer_t *rr, cl_size_t size, float scale,
                           cl_color_t clear)
{
    gl_renderer_t *r = (gl_renderer_t *)rr;

    if (!r->init)
        gl_init(r);
    if (!r->ok)
        return;

    r->vp = size;
    r->scale = scale;
    r->fb_w = (int)(size.w * scale);
    r->fb_h = (int)(size.h * scale);
    r->clip_depth = 0;
    set_proj(r, size.w, size.h);
    r->gl.Viewport(0, 0, (GLsizei)r->fb_w, (GLsizei)r->fb_h);
    r->gl.Disable(GL_SCISSOR_TEST);
    r->gl.ClearColor((float)clear.r / 255.0f, (float)clear.g / 255.0f,
                     (float)clear.b / 255.0f, (float)clear.a / 255.0f);
    r->gl.Clear(GL_COLOR_BUFFER_BIT);
    r->gl.Enable(GL_BLEND);
    r->gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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

/* Apply a logical-pixel rect as the GL scissor (framebuffer px, y flipped). */
static void gl_apply_scissor(gl_renderer_t *r, cl_rect_t c)
{
    float s = r->scale;
    int left = (int)floorf(c.x * s);
    int right = (int)ceilf((c.x + c.w) * s);
    int top = (int)floorf(c.y * s);
    int bottom = (int)ceilf((c.y + c.h) * s);
    int w = right - left;
    int h = bottom - top;
    int y = r->fb_h - bottom; /* GL scissor origin is bottom-left */

    /* Clamp a negative origin by shrinking the extent, not shifting the box. */
    if (left < 0) {
        w += left;
        left = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;
    r->gl.Scissor(left, y, (GLsizei)w, (GLsizei)h);
}

static cl_rect_t gl_clip_cur(const gl_renderer_t *r)
{
    int top;

    if (r->clip_depth == 0)
        return (cl_rect_t){ 0.0f, 0.0f, r->vp.w, r->vp.h };
    top = r->clip_depth < CL_GL_CLIP_STACK ? r->clip_depth : CL_GL_CLIP_STACK;
    return r->clip_stack[top - 1];
}

static void gl_push_clip(cl_renderer_t *rr, cl_rect_t rect)
{
    gl_renderer_t *r = (gl_renderer_t *)rr;
    cl_rect_t merged;
    int idx;

    if (!r->ok)
        return;
    merged = rect_intersect(gl_clip_cur(r), rect);
    idx = r->clip_depth++; /* always advance so a later pop stays balanced */
    if (idx < CL_GL_CLIP_STACK) {
        r->clip_stack[idx] = merged;
        r->gl.Enable(GL_SCISSOR_TEST);
        gl_apply_scissor(r, merged);
    }
    /* Past capacity: keep the current (tighter-or-equal) scissor in force. */
}

static void gl_pop_clip(cl_renderer_t *rr)
{
    gl_renderer_t *r = (gl_renderer_t *)rr;

    if (!r->ok || r->clip_depth == 0)
        return;
    r->clip_depth--;
    if (r->clip_depth == 0)
        r->gl.Disable(GL_SCISSOR_TEST);
    else if (r->clip_depth <= CL_GL_CLIP_STACK)
        gl_apply_scissor(r, r->clip_stack[r->clip_depth - 1]);
    /* else still past capacity: current scissor already correct */
}

static void gl_end_frame(cl_renderer_t *rr)
{
    (void)rr;
}

static void draw_rect(gl_renderer_t *r, cl_rect_t rc, float radius,
                      cl_color_t c, float border)
{
    if (!r->ok)
        return;
    r->gl.UseProgram(r->rect_prog);
    r->gl.UniformMatrix4fv(r->r_proj, 1, GL_FALSE, r->proj);
    r->gl.Uniform4f(r->r_rect, rc.x, rc.y, rc.w, rc.h);
    r->gl.Uniform1f(r->r_radius, radius);
    r->gl.Uniform4f(r->r_color, (float)c.r / 255.0f, (float)c.g / 255.0f,
                    (float)c.b / 255.0f, (float)c.a / 255.0f);
    r->gl.Uniform1f(r->r_border, border);
    r->gl.BindVertexArray(r->rect_vao);
    r->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void gl_fill_rect(cl_renderer_t *rr, cl_rect_t rc, cl_color_t c)
{
    draw_rect((gl_renderer_t *)rr, rc, 0.0f, c, 0.0f);
}

static void gl_fill_round_rect(cl_renderer_t *rr, cl_rect_t rc, float radius,
                               cl_color_t c)
{
    draw_rect((gl_renderer_t *)rr, rc, radius, c, 0.0f);
}

static void gl_stroke_round_rect(cl_renderer_t *rr, cl_rect_t rc, float radius,
                                 float width, cl_color_t c)
{
    draw_rect((gl_renderer_t *)rr, rc, radius, c, width);
}

static void gl_draw_text(cl_renderer_t *rr, cl_font_t *font, const char *utf8,
                         cl_point_t pos, cl_color_t c)
{
    gl_renderer_t *r = (gl_renderer_t *)rr;
    float verts[MAX_TEXT_GLYPHS * 24];
    int nv = 0;
    float baseline;
    float penx = pos.x;
    size_t i = 0;
    size_t n;
    uint32_t cp;

    if (!r->ok || !font || !utf8)
        return;
    baseline = pos.y + cl_font_ascent_px(font);

    while ((n = cl_utf8_next(utf8 + i, &cp)) != 0) {
        glyph_t *g = get_glyph(r, font, cp);

        i += n;
        if (!g)
            break;
        if (g->w > 0 && g->h > 0 && nv <= MAX_TEXT_GLYPHS * 24 - 24) {
            float x0 = penx + (float)g->xoff;
            float y0 = baseline + (float)g->yoff;
            float x1 = x0 + (float)g->w;
            float y1 = y0 + (float)g->h;
            const float quad[24] = {
                x0, y0, g->u0, g->v0, x1, y0, g->u1, g->v0,
                x1, y1, g->u1, g->v1, x0, y0, g->u0, g->v0,
                x1, y1, g->u1, g->v1, x0, y1, g->u0, g->v1,
            };

            memcpy(&verts[nv], quad, sizeof(quad));
            nv += 24;
        }
        penx += g->advance;
    }
    if (nv == 0)
        return;

    r->gl.UseProgram(r->text_prog);
    r->gl.UniformMatrix4fv(r->t_proj, 1, GL_FALSE, r->proj);
    r->gl.Uniform4f(r->t_color, (float)c.r / 255.0f, (float)c.g / 255.0f,
                    (float)c.b / 255.0f, (float)c.a / 255.0f);
    r->gl.ActiveTexture(GL_TEXTURE0);
    r->gl.BindTexture(GL_TEXTURE_2D, r->atlas);
    r->gl.Uniform1i(r->t_atlas, 0);
    r->gl.BindVertexArray(r->text_vao);
    r->gl.BindBuffer(GL_ARRAY_BUFFER, r->text_vbo);
    r->gl.BufferData(GL_ARRAY_BUFFER, nv * (GLsizeiptr)sizeof(float), verts,
                     GL_DYNAMIC_DRAW);
    r->gl.DrawArrays(GL_TRIANGLES, 0, nv / 4);
}

static void gl_destroy(cl_renderer_t *rr)
{
    gl_renderer_t *r = (gl_renderer_t *)rr;

    if (r->ok) {
        r->gl.DeleteProgram(r->rect_prog);
        r->gl.DeleteProgram(r->text_prog);
        r->gl.DeleteBuffers(1, &r->rect_vbo);
        r->gl.DeleteBuffers(1, &r->text_vbo);
        r->gl.DeleteVertexArrays(1, &r->rect_vao);
        r->gl.DeleteVertexArrays(1, &r->text_vao);
        r->gl.DeleteTextures(1, &r->atlas);
    }
    cl_free(r->a, r);
}

static const cl_renderer_ops_t gl_ops = {
    .begin_frame = gl_begin_frame,
    .end_frame = gl_end_frame,
    .fill_rect = gl_fill_rect,
    .fill_round_rect = gl_fill_round_rect,
    .stroke_round_rect = gl_stroke_round_rect,
    .draw_text = gl_draw_text,
    .push_clip = gl_push_clip,
    .pop_clip = gl_pop_clip,
    .destroy = gl_destroy,
};

cl_renderer_t *cl_renderer_gl_create(const cl_allocator_t *a, cl_platform_t *p)
{
    gl_renderer_t *r = cl_alloc(a, sizeof(*r));

    if (!r)
        return NULL;
    memset(r, 0, sizeof(*r));
    r->base.ops = &gl_ops;
    r->a = a;
    r->platform = p;
    return &r->base;
}
