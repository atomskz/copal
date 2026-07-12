/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "example_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cl_render_backend_t example_backend(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--software") == 0)
            return CL_RENDER_SOFTWARE;
        if (strcmp(argv[i], "--gl") == 0)
            return CL_RENDER_GL;
    }
    return CL_RENDER_AUTO;
}

/*
 * When built with AddressSanitizer, LeakSanitizer also flags process-lifetime
 * allocations made deep inside SDL, D-Bus and the Mesa GL driver during init.
 * Those are not copal leaks (copal frees everything it owns on shutdown), so
 * suppress them here to keep the leak report focused on real regressions.
 */
#if defined(__SANITIZE_ADDRESS__)
#  define COPAL_HAS_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define COPAL_HAS_ASAN 1
#  endif
#endif

#ifdef COPAL_HAS_ASAN
const char *__lsan_default_suppressions(void);
const char *__lsan_default_suppressions(void)
{
    return "leak:libdbus-1\n"
           "leak:libgallium\n"
           "leak:libSDL2\n"
           "leak:libEGL\n"
           "leak:libGLdispatch\n"
           "leak:libicuuc\n"
           "leak:swrast\n"
           "leak:dri\n";
}
#endif

cl_font_t *example_load_font(cl_application_t *app, float size_px)
{
    static const char *candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
    };
    const char *env = getenv("COPAL_FONT");
    cl_font_t *font;
    size_t i;

    if (env) {
        font = cl_font_load_file(app, env, size_px);
        if (font)
            return font;
        fprintf(stderr, "warning: COPAL_FONT=%s could not be loaded\n", env);
    }
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        font = cl_font_load_file(app, candidates[i], size_px);
        if (font)
            return font;
    }
    fprintf(stderr, "warning: no usable system font found "
                    "(set COPAL_FONT=/path/to/font.ttf); text will not "
                    "render\n");
    return NULL;
}

int example_run(cl_application_t *app)
{
    /* COPAL_MAX_FRAMES=N renders N frames then exits (headless check). */
    const char *max_frames = getenv("COPAL_MAX_FRAMES");

    if (max_frames) {
        int n = atoi(max_frames);

        while (n-- > 0)
            cl_application_step(app, false);
        return 0;
    }
    return cl_application_run(app);
}
