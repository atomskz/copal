/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/theme.h>
#include <copal/application.h>
#include <copal/allocator.h>

#include <string.h>

#include "theme/theme_internal.h"

struct cl_theme {
    const cl_allocator_t *a;
    cl_theme_variant_t variant;
    cl_color_t colors[CL_COLOR__COUNT];
    cl_font_t *font;
    float radius;
    cl_size_t button_pad;
};

static void fill_light(cl_theme_t *t)
{
    t->colors[CL_COLOR_BACKGROUND] = cl_rgba(245, 246, 248, 255);
    t->colors[CL_COLOR_SURFACE] = cl_rgba(250, 250, 252, 255);
    t->colors[CL_COLOR_SURFACE_HOVER] = cl_rgba(238, 240, 244, 255);
    t->colors[CL_COLOR_SURFACE_ACTIVE] = cl_rgba(220, 224, 230, 255);
    t->colors[CL_COLOR_SURFACE_RAISED] = cl_rgba(255, 255, 255, 255);
    t->colors[CL_COLOR_TEXT] = cl_rgba(30, 32, 36, 255);
    t->colors[CL_COLOR_TEXT_MUTED] = cl_rgba(120, 124, 130, 255);
    t->colors[CL_COLOR_ACCENT] = cl_rgba(60, 120, 220, 255);
    t->colors[CL_COLOR_BORDER] = cl_rgba(205, 208, 214, 255);
    t->colors[CL_COLOR_FOCUS_RING] = cl_rgba(60, 120, 220, 255);
    t->colors[CL_COLOR_SELECTION] = cl_rgba(180, 205, 245, 255);
    t->colors[CL_COLOR_SHADOW] = cl_rgba(0, 0, 0, 40);
}

static void fill_dark(cl_theme_t *t)
{
    t->colors[CL_COLOR_BACKGROUND] = cl_rgba(24, 26, 30, 255);
    t->colors[CL_COLOR_SURFACE] = cl_rgba(35, 37, 42, 255);
    t->colors[CL_COLOR_SURFACE_HOVER] = cl_rgba(45, 48, 54, 255);
    t->colors[CL_COLOR_SURFACE_ACTIVE] = cl_rgba(55, 58, 66, 255);
    t->colors[CL_COLOR_SURFACE_RAISED] = cl_rgba(48, 51, 58, 255);
    t->colors[CL_COLOR_TEXT] = cl_rgba(232, 234, 238, 255);
    t->colors[CL_COLOR_TEXT_MUTED] = cl_rgba(148, 152, 160, 255);
    t->colors[CL_COLOR_ACCENT] = cl_rgba(90, 150, 240, 255);
    t->colors[CL_COLOR_BORDER] = cl_rgba(66, 70, 78, 255);
    t->colors[CL_COLOR_FOCUS_RING] = cl_rgba(90, 150, 240, 255);
    t->colors[CL_COLOR_SELECTION] = cl_rgba(52, 84, 140, 255);
    t->colors[CL_COLOR_SHADOW] = cl_rgba(0, 0, 0, 110);
}

static void apply_variant(cl_theme_t *t, cl_theme_variant_t variant)
{
    t->variant = variant;
    if (variant == CL_THEME_DARK)
        fill_dark(t);
    else
        fill_light(t);
}

cl_theme_t *cl_theme_default(cl_application_t *app)
{
    const cl_allocator_t *a = cl_application_allocator(app);
    cl_theme_t *t = cl_alloc(a, sizeof(*t));

    if (!t)
        return NULL;
    memset(t, 0, sizeof(*t));
    t->a = a;
    t->radius = 6.0f;
    t->button_pad = (cl_size_t){ 12.0f, 7.0f };
    apply_variant(t, CL_THEME_LIGHT);
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

void cl_theme_set_color(cl_theme_t *theme, cl_color_role_t role,
                        cl_color_t color)
{
    if (theme && role >= 0 && role < CL_COLOR__COUNT)
        theme->colors[role] = color;
}

void cl_theme_set_variant(cl_theme_t *theme, cl_theme_variant_t variant)
{
    if (theme)
        apply_variant(theme, variant);
}

cl_theme_variant_t cl_theme_variant(cl_theme_t *theme)
{
    return theme ? theme->variant : CL_THEME_LIGHT;
}

void cl_theme_set_radius(cl_theme_t *theme, float radius)
{
    if (theme)
        theme->radius = radius;
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
