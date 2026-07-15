/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Checks the freestanding cl_* math (src/core/foundation/fmath.c) against libm
 * within a tolerance sized for font rasterization. The renderer/stb paths that
 * matter are exact (fabs/sqrt/floor/ceil); the transcendentals are compact
 * approximations, so this guards them from drifting. Runs hosted (needs libm
 * for the reference).
 */
#include "core/foundation/fmath.h"

#include <math.h>
#include <stdio.h>

static int failures;

static void near_d(const char *what, double x, double got, double want,
                   double tol)
{
    double d = got - want < 0 ? want - got : got - want;

    if (d > tol) {
        fprintf(stderr, "FAIL %s(%.6g): got %.10g want %.10g (|d|=%.3g > %.3g)\n",
                what, x, got, want, d, tol);
        failures++;
    }
}

int main(void)
{
    int i;

    /* Exact: fabs, floor, ceil (both widths). sqrt is the hardware op. */
    for (i = -4000; i <= 4000; i++) {
        double x = i * 0.017;
        float xf = (float)x;

        near_d("fabs", x, cl_fabs(x), fabs(x), 0.0);
        near_d("fabsf", x, cl_fabsf(xf), fabsf(xf), 0.0);
        near_d("floor", x, cl_floor(x), floor(x), 0.0);
        near_d("ceil", x, cl_ceil(x), ceil(x), 0.0);
        near_d("floorf", x, cl_floorf(xf), floorf(xf), 0.0);
        near_d("ceilf", x, cl_ceilf(xf), ceilf(xf), 0.0);
        if (x >= 0.0) {
            near_d("sqrt", x, cl_sqrt(x), sqrt(x), 1e-12);
            near_d("sqrtf", x, cl_sqrtf(xf), sqrtf(xf), 1e-5);
        }
    }

    /* fmod with ratios kept away from integer boundaries. */
    for (i = 1; i <= 2000; i++) {
        double x = i * 0.37 + 0.13, y = 1.0 + (i % 5) * 0.7;

        near_d("fmod", x, cl_fmod(x, y), fmod(x, y), 1e-6);
    }

    /* cos over several periods; acos on [-1, 1]. */
    for (i = -4000; i <= 4000; i++) {
        double x = i * 0.01;

        near_d("cos", x, cl_cos(x), cos(x), 1.5e-4);
    }
    for (i = -1000; i <= 1000; i++) {
        double x = i / 1000.0;

        near_d("acos", x, cl_acos(x), acos(x), 2e-4);
    }

    /* pow as the cube root stb uses; this exercises the internal exp+log. */
    for (i = 1; i <= 6000; i++) {
        double x = i * 0.01;

        near_d("pow", x, cl_pow(x, 1.0 / 3.0), pow(x, 1.0 / 3.0),
               pow(x, 1.0 / 3.0) * 1e-6 + 1e-9);
    }

    if (!failures)
        printf("fmath matches libm within tolerance\n");
    return failures ? 1 : 0;
}
