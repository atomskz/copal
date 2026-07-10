/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_FONT_INTERNAL_H
#define CL_FONT_INTERNAL_H

#include <copal/font.h>

#include "stb_truetype.h"

/* Internal accessors used by the GL renderer to rasterize glyphs. */
const stbtt_fontinfo *cl_font_info(cl_font_t *f);
float cl_font_pixel_scale(cl_font_t *f);
float cl_font_ascent_px(cl_font_t *f);

#endif /* CL_FONT_INTERNAL_H */
