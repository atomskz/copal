/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_PLATFORM_INTERNAL_H
#define CL_PLATFORM_INTERNAL_H

/*
 * The platform SPI is public since 0.2 (custom backends inject through
 * cl_application_desc_t); this internal header remains as the include point
 * for in-tree code. See ARCHITECTURE §3.2 and §13.
 */
#include <copal/backend/platform.h>

#endif /* CL_PLATFORM_INTERNAL_H */
