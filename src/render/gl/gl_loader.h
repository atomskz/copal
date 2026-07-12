/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_GL_LOADER_H
#define CL_GL_LOADER_H

/* Portable core-profile GL function loading (no prototypes, just typedefs). */
#include <stdbool.h>

#include <GL/glcorearb.h>

#define CL_GL_FUNCS(X)                              \
    X(PFNGLGETSTRINGPROC, GetString)                \
    X(PFNGLVIEWPORTPROC, Viewport)                  \
    X(PFNGLCLEARCOLORPROC, ClearColor)              \
    X(PFNGLCLEARPROC, Clear)                        \
    X(PFNGLENABLEPROC, Enable)                      \
    X(PFNGLDISABLEPROC, Disable)                    \
    X(PFNGLSCISSORPROC, Scissor)                    \
    X(PFNGLBLENDFUNCPROC, BlendFunc)                \
    X(PFNGLDRAWARRAYSPROC, DrawArrays)              \
    X(PFNGLGENTEXTURESPROC, GenTextures)            \
    X(PFNGLBINDTEXTUREPROC, BindTexture)            \
    X(PFNGLTEXIMAGE2DPROC, TexImage2D)              \
    X(PFNGLTEXSUBIMAGE2DPROC, TexSubImage2D)        \
    X(PFNGLTEXPARAMETERIPROC, TexParameteri)        \
    X(PFNGLDELETETEXTURESPROC, DeleteTextures)      \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture)        \
    X(PFNGLPIXELSTOREIPROC, PixelStorei)            \
    X(PFNGLCREATESHADERPROC, CreateShader)          \
    X(PFNGLSHADERSOURCEPROC, ShaderSource)          \
    X(PFNGLCOMPILESHADERPROC, CompileShader)        \
    X(PFNGLGETSHADERIVPROC, GetShaderiv)            \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)  \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram)        \
    X(PFNGLATTACHSHADERPROC, AttachShader)          \
    X(PFNGLLINKPROGRAMPROC, LinkProgram)            \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv)          \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)\
    X(PFNGLUSEPROGRAMPROC, UseProgram)              \
    X(PFNGLDELETESHADERPROC, DeleteShader)          \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram)        \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
    X(PFNGLUNIFORM1FPROC, Uniform1f)                \
    X(PFNGLUNIFORM2FPROC, Uniform2f)                \
    X(PFNGLUNIFORM4FPROC, Uniform4f)                \
    X(PFNGLUNIFORM1IPROC, Uniform1i)                \
    X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv)  \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays)    \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray)    \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
    X(PFNGLGENBUFFERSPROC, GenBuffers)              \
    X(PFNGLBINDBUFFERPROC, BindBuffer)              \
    X(PFNGLBUFFERDATAPROC, BufferData)              \
    X(PFNGLBUFFERSUBDATAPROC, BufferSubData)        \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers)        \
    X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray)

struct gl_api {
#define CL_GL_DECL(type, name) type name;
    CL_GL_FUNCS(CL_GL_DECL)
#undef CL_GL_DECL
};

typedef void *(*cl_gl_get_proc_fn)(void *ctx, const char *name);

/* Load all functions; returns false if any is missing. */
bool cl_gl_load(struct gl_api *gl, cl_gl_get_proc_fn get, void *ctx);

#endif /* CL_GL_LOADER_H */
