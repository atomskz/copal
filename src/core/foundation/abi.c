/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "foundation_internal.h"

#include <copal/version.h>

#include <string.h>

bool cl_abi_ok(uint32_t abi_version, size_t struct_size, size_t min_size)
{
    /* Same major (0x00MMmmpp -> major = high 16 bits) and at least the minimum
     * size. Appended tail fields keep the major and only grow struct_size, so
     * they stay compatible; a different major or an undersized struct does not. */
    if ((abi_version >> 16) == COPAL_VERSION_MAJOR && struct_size >= min_size)
        return true;
    cl_set_last_error(CL_ERROR_ABI_MISMATCH);
    return false;
}

void cl_desc_fill(void *dst, size_t dst_size, const void *src, size_t src_size)
{
    size_t n = src_size < dst_size ? src_size : dst_size;

    memset(dst, 0, dst_size);
    memcpy(dst, src, n);
}
