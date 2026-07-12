/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/version.h>

#define CL_STRINGIZE_(x) #x
#define CL_STRINGIZE(x) CL_STRINGIZE_(x)

uint32_t cl_version_runtime(void)
{
    return COPAL_VERSION;
}

const char *cl_version_string(void)
{
    return CL_STRINGIZE(COPAL_VERSION_MAJOR) "."
           CL_STRINGIZE(COPAL_VERSION_MINOR) "."
           CL_STRINGIZE(COPAL_VERSION_PATCH);
}
