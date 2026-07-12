/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_PLATFORM_MOCK_H
#define CL_PLATFORM_MOCK_H

/* Headless platform: scripted event queue, no window (ADR-010). */
#include <stdint.h>

#include <copal/allocator.h>

#include "platform/platform.h"

cl_platform_t *cl_platform_mock_create(const cl_allocator_t *a);
void cl_platform_mock_push(cl_platform_t *p, cl_platform_event_t ev);

/* Advance the mock monotonic clock by ms (drives timers deterministically). */
void cl_platform_mock_advance(cl_platform_t *p, uint64_t ms);

/* The timeout the most recent wait() was called with (-1 = block forever). */
int cl_platform_mock_last_wait_timeout(cl_platform_t *p);

/* The min_width/min_height the window was created with (introspection). */
cl_size_t cl_platform_mock_min_size(cl_platform_t *p);

/* Events dropped because the scripted queue was full (should stay 0). */
size_t cl_platform_mock_dropped_events(cl_platform_t *p);

#endif /* CL_PLATFORM_MOCK_H */
