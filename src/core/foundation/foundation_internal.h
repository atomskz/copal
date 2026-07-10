/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_FOUNDATION_INTERNAL_H
#define CL_FOUNDATION_INTERNAL_H

#include <copal/error.h>

/* Internal (not exported): record the calling thread's last error code. */
void cl_set_last_error(cl_result_t result);

#endif /* CL_FOUNDATION_INTERNAL_H */
