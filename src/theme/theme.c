/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/theme.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <string.h>

#include "theme/theme_internal.h"

struct cl_theme {
    const cl_allocator_t *a;
    cl_color_t colors[CL_COLOR__COUNT];
    cl_font_t *font;
    float radius;
    cl_size_t button_pad;
};

cl_theme_t *cl_theme_default(cl_application_t *app)
{
    const cl_allocator_t *a = cl_application_allocator(app);
    cl_theme_t *t = cl_alloc(a, sizeof(*t));

    if (!t)
        return NULL;
    memset(t, 0, sizeof(*t));
    t->a = a;
    t->colors[CL_COLOR_SURFACE] = cl_rgba(250, 250, 252, 255);
    t->colors[CL_COLOR_SURFACE_HOVER] = cl_rgba(238, 240, 244, 255);
    t->colors[CL_COLOR_SURFACE_ACTIVE] = cl_rgba(220, 224, 230, 255);
    t->colors[CL_COLOR_TEXT] = cl_rgba(30, 32, 36, 255);
    t->colors[CL_COLOR_TEXT_MUTED] = cl_rgba(120, 124, 130, 255);
    t->colors[CL_COLOR_ACCENT] = cl_rgba(60, 120, 220, 255);
    t->colors[CL_COLOR_BORDER] = cl_rgba(205, 208, 214, 255);
    t->colors[CL_COLOR_FOCUS_RING] = cl_rgba(60, 120, 220, 255);
    t->colors[CL_COLOR_SELECTION] = cl_rgba(180, 205, 245, 255);
    t->radius = 6.0f;
    t->button_pad = (cl_size_t){ 12.0f, 7.0f };
    return t;
}

void cl_theme_free(cl_theme_t *theme)
{
    if (theme)
        cl_free(theme->a, theme);
}

cl_color_t cl_theme_color(cl_theme_t *theme, cl_color_role_t role)
{
    if (theme && role >= 0 && role < CL_COLOR__COUNT)
        return theme->colors[role];
    return cl_rgba(0, 0, 0, 255);
}

cl_font_t *cl_theme_font(cl_theme_t *theme)
{
    return theme ? theme->font : NULL;
}

void cl_theme_set_font(cl_theme_t *theme, cl_font_t *font)
{
    if (theme)
        theme->font = font;
}

float cl_theme_radius(cl_theme_t *theme)
{
    return theme ? theme->radius : 0.0f;
}

cl_size_t cl_theme_button_padding(cl_theme_t *theme)
{
    cl_size_t z = { 0, 0 };

    return theme ? theme->button_pad : z;
}
