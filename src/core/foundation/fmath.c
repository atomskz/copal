/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Freestanding math (see fmath.h). GCC/Clang lower sqrt/abs to hardware ops
 * (the target is built with -fno-math-errno so no errno-setting libm call is
 * emitted); floor/ceil/fmod are exact bit/arithmetic tricks; the transcendentals
 * are compact approximations sufficient for font rasterization. On MSVC (a
 * hosted Windows target, never freestanding) the CRT math is used for sqrt.
 *
 * fmod/cos/acos/pow are reached only from stb_truetype's SDF path, which copal
 * never invokes at run time. But stb references those symbols from the same
 * translation unit as the glyph rasterizer copal does use, so the helpers must
 * exist to keep libm off the freestanding symbol surface. Runtime-dead,
 * link-live: do not drop them (or their tests) as unused.
 */
#include "core/foundation/fmath.h"

#include <stdint.h>

#define CL_PI 3.14159265358979323846
#define CL_LN2 0.69314718055994530942
/* Above these magnitudes a float/double is already integral. */
#define CL_F_INTLIM 8388608.0f          /* 2^23 */
#define CL_D_INTLIM 4503599627370496.0  /* 2^52 */

/* ---- |x|: clear the sign bit ---- */
float cl_fabsf(float x)
{
    union {
        float f;
        uint32_t u;
    } v;

    v.f = x;
    v.u &= 0x7FFFFFFFu;
    return v.f;
}

double cl_fabs(double x)
{
    union {
        double f;
        uint64_t u;
    } v;

    v.f = x;
    v.u &= (uint64_t)0x7FFFFFFFFFFFFFFFull;
    return v.f;
}

/* ---- sqrt: the hardware instruction ---- */
#if defined(__GNUC__) || defined(__clang__)
float cl_sqrtf(float x)
{
    return __builtin_sqrtf(x);
}
double cl_sqrt(double x)
{
    return __builtin_sqrt(x);
}
#else
#include <math.h>
float cl_sqrtf(float x)
{
    return sqrtf(x);
}
double cl_sqrt(double x)
{
    return sqrt(x);
}
#endif

/* ---- floor / ceil: truncate toward zero, then adjust ---- */
float cl_floorf(float x)
{
    float t;

    if (cl_fabsf(x) >= CL_F_INTLIM)
        return x;
    t = (float)(int32_t)x;
    return t > x ? t - 1.0f : t;
}

float cl_ceilf(float x)
{
    float t;

    if (cl_fabsf(x) >= CL_F_INTLIM)
        return x;
    t = (float)(int32_t)x;
    return t < x ? t + 1.0f : t;
}

double cl_floor(double x)
{
    double t;

    if (cl_fabs(x) >= CL_D_INTLIM)
        return x;
    t = (double)(int64_t)x;
    return t > x ? t - 1.0 : t;
}

double cl_ceil(double x)
{
    double t;

    if (cl_fabs(x) >= CL_D_INTLIM)
        return x;
    t = (double)(int64_t)x;
    return t < x ? t + 1.0 : t;
}

/* ---- fmod: exact remainder by repeated subtraction of the largest |y|*2^k
 * that does not exceed the running value. Each subtraction falls in Sterbenz's
 * range (d <= r < 2d), so it is exact - the result matches libm bit for bit. */
double cl_fmod(double x, double y)
{
    double ax, ay, r, d;

    if (y == 0.0)
        return 0.0;
    ax = cl_fabs(x);
    ay = cl_fabs(y);
    if (ax < ay)
        return x;
    r = ax;
    while (r >= ay) {
        d = ay;
        while (r - d >= d) /* grow d to the largest ay*2^k with 2d <= r */
            d += d;
        r -= d; /* exact */
    }
    return x < 0.0 ? -r : r;
}

/* ---- cos: fold to [0, pi/2] by symmetry, then a Taylor polynomial.
 * The range reduction (x - floor(x/2pi)*2pi) targets the small angles of font
 * work; for very large |x| the subtraction cancels and precision drops. copal
 * never passes such arguments - cos is on stb's SDF path, which it never uses. */
double cl_cos(double x)
{
    double s = 1.0, x2;

    x = cl_fabs(x);                              /* cos is even */
    x -= cl_floor(x / (2.0 * CL_PI)) * (2.0 * CL_PI); /* -> [0, 2pi) */
    if (x > CL_PI)
        x = 2.0 * CL_PI - x;                     /* -> [0, pi] */
    if (x > CL_PI / 2.0) {                       /* cos(x) = -cos(pi - x) */
        x = CL_PI - x;
        s = -1.0;
    }
    x2 = x * x;                                  /* x now in [0, pi/2] */
    return s * (1.0 - x2 / 2.0 + x2 * x2 / 24.0 - x2 * x2 * x2 / 720.0 +
                x2 * x2 * x2 * x2 / 40320.0);
}

/* ---- acos: reflected polynomial approximation (error ~1e-4) ---- */
double cl_acos(double x)
{
    double a = cl_fabs(x), r;

    if (a > 1.0)
        a = 1.0;
    r = cl_sqrt(1.0 - a) *
        (1.5707288 + a * (-0.2121144 + a * (0.0742610 + a * -0.0187293)));
    return x < 0.0 ? CL_PI - r : r;
}

/* ---- log: exponent from the bits + an atanh series on the mantissa ----
 * Internal: only cl_pow needs it, and cl_log would clash with the log sink. */
static double fm_log(double x)
{
    union {
        double f;
        uint64_t u;
    } v;
    int e;
    double m, t, t2, s;

    if (x <= 0.0)
        return 0.0; /* domain guard; callers pass x > 0 */
    v.f = x;
    e = (int)((v.u >> 52) & 0x7FF) - 1023;
    v.u = (v.u & (uint64_t)0x000FFFFFFFFFFFFFull) | ((uint64_t)1023 << 52);
    m = v.f; /* mantissa in [1, 2) */
    if (m > 1.4142135623730951) { /* recenter to [sqrt(0.5), sqrt(2)) */
        m *= 0.5;
        e += 1;
    }
    t = (m - 1.0) / (m + 1.0); /* small -> fast convergence */
    t2 = t * t;
    s = 2.0 * t *
        (1.0 + t2 / 3.0 + t2 * t2 / 5.0 + t2 * t2 * t2 / 7.0 +
         t2 * t2 * t2 * t2 / 9.0);
    return (double)e * CL_LN2 + s;
}

/* ---- exp: exp(x) = 2^k * exp(r), r = x - k*ln2 in [-ln2/2, ln2/2] ---- */
static double fm_exp(double x)
{
    union {
        double f;
        uint64_t u;
    } v;
    double k = cl_floor(x / CL_LN2 + 0.5), r, er;
    int ki = (int)k;

    r = x - k * CL_LN2;
    if (ki < -1022)
        return 0.0; /* 2^ki underflows to zero; skip the polynomial */
    er = 1.0 +
         r * (1.0 +
              r * (0.5 +
                   r * (1.0 / 6.0 +
                        r * (1.0 / 24.0 +
                             r * (1.0 / 120.0 + r * (1.0 / 720.0))))));
    if (ki > 1023)
        ki = 1023;
    v.u = (uint64_t)(ki + 1023) << 52; /* 2^ki */
    return er * v.f;
}

/* ---- pow: callers (stb SDF path) pass a positive base only ---- */
double cl_pow(double x, double y)
{
    if (x <= 0.0)
        return 0.0;
    return fm_exp(y * fm_log(x));
}
