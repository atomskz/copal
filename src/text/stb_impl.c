/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Single translation unit that instantiates stb_truetype. Warnings are disabled
 * for this third-party code: GCC/Clang via a per-file -w (CMakeLists.txt), MSVC
 * via the pragma below (a per-file /w would trigger D9025).
 */
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(_MSC_VER)
#  pragma warning(push, 0)
#endif
#include "stb_truetype.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
