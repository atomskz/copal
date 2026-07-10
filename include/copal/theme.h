/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_THEME_H
#define CL_THEME_H

#include <copal/export.h>
#include <copal/types.h>
#include <copal/font.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_theme cl_theme_t;

/* Semantic colour roles; widgets request colours by role, not literal values. */
typedef enum cl_color_role {
    CL_COLOR_SURFACE,
    CL_COLOR_SURFACE_HOVER,
    CL_COLOR_SURFACE_ACTIVE,
    CL_COLOR_TEXT,
    CL_COLOR_TEXT_MUTED,
    CL_COLOR_ACCENT,
    CL_COLOR_BORDER,
    CL_COLOR_FOCUS_RING,
    CL_COLOR__COUNT
} cl_color_role_t;

/* Reusable text style; NULL font/align fall back to theme defaults. */
typedef struct cl_text_style {
    cl_font_t *font;
    cl_color_t color;
    cl_align_t align;
} cl_text_style_t;

/** cl_theme_color() - resolve a colour role to an RGBA value. */
CL_API cl_color_t cl_theme_color(cl_theme_t *theme, cl_color_role_t role);

/** cl_theme_font() - the theme's default font (may be NULL until set). */
CL_API cl_font_t *cl_theme_font(cl_theme_t *theme);

/** cl_theme_set_font() - set the theme's default font (borrowed, not owned). */
CL_API void cl_theme_set_font(cl_theme_t *theme, cl_font_t *font);

#ifdef __cplusplus
}
#endif

#endif /* CL_THEME_H */
