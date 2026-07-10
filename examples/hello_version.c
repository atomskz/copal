/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/copal.h>

#include <stdio.h>

int main(void)
{
    printf("copal %s (runtime 0x%06x)\n",
           cl_version_string(), (unsigned)cl_version_runtime());
    return 0;
}
