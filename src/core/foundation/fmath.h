/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_FMATH_H
#define CL_FMATH_H

/*
 * Freestanding math: copal-namespaced (cl_*) replacements for the libm
 * functions the software renderer and stb_truetype reference, so the core
 * carries no external libm symbol (see docs, freestanding/UEFI). Precision
 * targets font rasterization, not scientific use; the approximations are
 * checked against libm in tests/test_fmath.c.
 */

float cl_fabsf(float x);
float cl_sqrtf(float x);
float cl_floorf(float x);
float cl_ceilf(float x);

double cl_fabs(double x);
double cl_sqrt(double x);
double cl_floor(double x);
double cl_ceil(double x);
double cl_fmod(double x, double y);
double cl_cos(double x);
double cl_acos(double x);
double cl_pow(double x, double y); /* uses internal exp/log (fmath.c) */

#endif /* CL_FMATH_H */
