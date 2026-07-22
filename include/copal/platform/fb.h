/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_PLATFORM_FB_H
#define CL_PLATFORM_FB_H

/*
 * Linear-framebuffer platform backend - for embedders and tests.
 *
 * A software platform that draws into a caller-provided 32-bit linear buffer
 * with no windowing system. It implements every REQUIRED platform op plus the
 * lockable-framebuffer pair, so the built-in software renderer
 * (render_backend = CL_RENDER_SOFTWARE, or AUTO with no GL) rasterises straight
 * into the buffer. This is the minimal-backend reference - the shape a
 * freestanding/UEFI embedder fills against its own GOP framebuffer - and the
 * substrate for host render-to-pixmap tests.
 *
 * Freestanding-safe: references nothing beyond the mem* family and the injected
 * allocator. There is no OS event source; events are delivered through a
 * push-queue (cl_platform_fb_push_event), the same pattern as the mock backend.
 */

#include <stdbool.h>

#include <copal/export.h>
#include <copal/allocator.h>
#include <copal/backend/platform.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create a linear-framebuffer platform drawing into *fb. The cl_pixmap_t is
 * copied, so it need not outlive the call, but the pixel memory it points at
 * must outlive the platform (the renderer writes into it every frame). @fb
 * must have non-NULL pixels, w > 0, h > 0 and pitch >= w * 4; the window's
 * logical size is taken from the framebuffer (scale 1.0), not the window desc.
 *
 * Returns NULL - recording CL_ERROR_INVALID_ARGUMENT for a degenerate
 * framebuffer or CL_ERROR_OUT_OF_MEMORY on allocation failure. Destroy through
 * ops->destroy (cl_application_destroy does this when the platform is injected).
 */
CL_API cl_platform_t *cl_platform_fb_create(const cl_allocator_t *a,
                                            const cl_pixmap_t *fb);

/*
 * Queue an input/system event to be returned by the next poll (FIFO). Not
 * thread-safe (like the rest of the backend): call it from the loop thread. A
 * full queue drops the event.
 */
CL_API void cl_platform_fb_push_event(cl_platform_t *p, cl_platform_event_t ev);

/*
 * Common 32-bit pixel formats, named by the uint32 word as the CPU reads one
 * pixel (host endianness) - exactly the channel masks the software renderer
 * applies. The little-endian byte order is noted where it differs (BGRX/RGBX).
 */
typedef enum cl_pixel_format {
    CL_PIXEL_ARGB8888, /* 0xAARRGGBB */
    CL_PIXEL_XRGB8888, /* 0x00RRGGBB, opaque; BGRX bytes on little-endian */
    CL_PIXEL_ABGR8888, /* 0xAABBGGRR */
    CL_PIXEL_XBGR8888, /* 0x00BBGGRR, opaque; RGBX bytes on little-endian */
    CL_PIXEL_RGBA8888, /* 0xRRGGBBAA */
    CL_PIXEL_BGRA8888  /* 0xBBGGRRAA */
} cl_pixel_format_t;

/*
 * Fill fb->r_mask/g_mask/b_mask/a_mask for a common 32-bit @fmt (an opaque
 * X-format leaves a_mask at 0). pixels/w/h/pitch are left untouched. Returns
 * false for an unknown format. A convenience for building a cl_pixmap_t; a
 * backend with an exotic layout sets the masks directly. (The GOP PixelFormat
 * to mask mapping belongs in the embedder, not here.)
 */
CL_API bool cl_pixmap_set_format(cl_pixmap_t *fb, cl_pixel_format_t fmt);

#ifdef __cplusplus
}
#endif

#endif /* CL_PLATFORM_FB_H */
