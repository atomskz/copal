/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_PLATFORM_MOCK_H
#define CL_PLATFORM_MOCK_H

/* Headless platform: scripted event queue, no window (ADR-010). */
#include <copal/allocator.h>

#include "platform/platform.h"

cl_platform_t *cl_platform_mock_create(const cl_allocator_t *a);
void cl_platform_mock_push(cl_platform_t *p, cl_platform_event_t ev);

#endif /* CL_PLATFORM_MOCK_H */
