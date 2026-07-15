/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Single translation unit that instantiates stb_truetype. Warnings are disabled
 * for this third-party code: GCC/Clang via a per-file -w (CMakeLists.txt), MSVC
 * via the pragma below (a per-file /w would trigger D9025).
 */
#define STB_TRUETYPE_IMPLEMENTATION

/* Route stb_truetype's libm use through copal's freestanding math (fmath) so
 * the core carries no libm symbol. Each STBTT_* macro guards a whole block in
 * stb_truetype.h, so the paired ones (sqrt+pow, cos+acos) are defined together.
 * cos/acos/fmod/pow are reached only from the SDF path, which copal never uses. */
#include "core/foundation/fmath.h"
#include "core/foundation/foundation_internal.h" /* cl_strlen for STBTT_strlen */
#define STBTT_ifloor(x) ((int)cl_floor(x))
#define STBTT_iceil(x) ((int)cl_ceil(x))
#define STBTT_sqrt(x) cl_sqrt(x)
#define STBTT_pow(x, y) cl_pow(x, y)
#define STBTT_fmod(x, y) cl_fmod(x, y)
#define STBTT_cos(x) cl_cos(x)
#define STBTT_acos(x) cl_acos(x)
#define STBTT_fabs(x) cl_fabs(x)
#define STBTT_strlen(x) cl_strlen(x)

#if defined(_MSC_VER)
#  pragma warning(push, 0)
#endif
#include "stb_truetype.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
