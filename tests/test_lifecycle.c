/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Application/window lifecycle and the custom-widget extension surface:
 * the close-request veto, cl_application_run's exit code, window destruction
 * and re-creation, disabled widgets ignoring input, the log callback, and a
 * custom widget built through the public widget_impl.h API.
 */
#include <copal/copal.h>
#include <copal/widget_impl.h>

#include <stdio.h>
#include <string.h>

#include "platform/mock/platform_mock.h"
#include "render/mock/renderer_mock.h"

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

/* ---- close-request veto -------------------------------------------------- */

static int close_calls;
static bool veto_close(cl_window_t *w, void *user)
{
    (void)w;
    (void)user;
    close_calls++;
    return false;
}

static bool allow_close(cl_window_t *w, void *user)
{
    (void)w;
    (void)user;
    close_calls++;
    return true;
}

static void test_on_close(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = 100, .height = 100 };
    cl_platform_t *plat = cl_platform_mock_create(cl_allocator_default());
    cl_application_t *app;
    cl_window_t *win;
    cl_platform_event_t pe = { 0 };

    ad.platform = plat;
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);
    win = cl_window_create(app, &wd);

    pe.kind = CL_PEV_QUIT;
    cl_window_set_on_close(win, veto_close, NULL);
    cl_platform_mock_push(plat, pe);
    CHECK(cl_application_step(app, false)); /* vetoed: keep running */
    CHECK(close_calls == 1);

    cl_window_set_on_close(win, allow_close, NULL);
    cl_platform_mock_push(plat, pe);
    CHECK(!cl_application_step(app, false)); /* allowed: quit */
    CHECK(close_calls == 2);

    cl_application_destroy(app);
}

/* ---- run() exit code ----------------------------------------------------- */

static void quit_task(void *user)
{
    cl_application_quit(user, 42);
}

static void test_run_exit_code(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_application_t *app;

    ad.platform = cl_platform_mock_create(cl_allocator_default());
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);

    CHECK(cl_application_post(app, quit_task, app) == CL_OK);
    CHECK(cl_application_run(app) == 42);

    cl_application_destroy(app);
}

/* ---- window destroy / re-create ------------------------------------------ */

static void test_window_lifecycle(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS, .width = 100,
                            .height = 100, .min_width = 40, .min_height = 30 };
    cl_platform_t *plat = cl_platform_mock_create(cl_allocator_default());
    cl_application_t *app;
    cl_window_t *win;
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS };

    ad.platform = plat;
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);

    win = cl_window_create(app, &wd);
    CHECK(win != NULL);
    /* the minimum size reached the platform backend */
    CHECK(cl_platform_mock_min_size(plat).w == 40.0f);
    CHECK(cl_platform_mock_min_size(plat).h == 30.0f);
    /* a second window is unsupported in the MVP */
    CHECK(cl_window_create(app, &wd) == NULL);
    CHECK(cl_last_error() == CL_ERROR_UNSUPPORTED);

    cl_window_set_content(win, cl_vbox_create(app, &vd));
    cl_window_destroy(win); /* destroys the content subtree too (ASan) */

    /* after destroy the slot is free again */
    win = cl_window_create(app, &wd);
    CHECK(win != NULL);

    cl_application_destroy(app);
}

/* ---- disabled widgets ignore input --------------------------------------- */

static int clicks;
static void on_click(cl_widget_t *w, void *user)
{
    (void)w;
    (void)user;
    clicks++;
}

static void test_disabled(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = 200, .height = 100 };
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS,
                          .align_cross = CL_ALIGN_STRETCH };
    cl_button_desc_t bd = { CL_BUTTON_DESC_INIT_FIELDS, .text = "go" };
    cl_platform_t *plat = cl_platform_mock_create(cl_allocator_default());
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *box;
    cl_widget_t *btn;
    cl_platform_event_t pe = { 0 };

    ad.platform = plat;
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);
    win = cl_window_create(app, &wd);
    box = cl_vbox_create(app, &vd);
    btn = cl_button_create(app, &bd);
    cl_button_set_on_click(btn, on_click, NULL);
    cl_widget_set_flex(btn, 1.0f);
    cl_widget_add_child(box, btn);
    cl_window_set_content(win, box);
    cl_application_step(app, false);

    cl_widget_set_enabled(btn, false);
    CHECK(!cl_widget_is_enabled(btn));
    CHECK(!cl_widget_focus(btn)); /* disabled widgets refuse focus */

    pe.kind = CL_PEV_MOUSE_DOWN;
    pe.pos = (cl_point_t){ 50, 50 };
    pe.button = CL_MOUSE_LEFT;
    cl_platform_mock_push(plat, pe);
    pe.kind = CL_PEV_MOUSE_UP;
    cl_platform_mock_push(plat, pe);
    cl_application_step(app, false);
    CHECK(clicks == 0); /* the disabled button ignored the click */

    cl_widget_set_enabled(btn, true);
    pe.kind = CL_PEV_MOUSE_DOWN;
    cl_platform_mock_push(plat, pe);
    pe.kind = CL_PEV_MOUSE_UP;
    cl_platform_mock_push(plat, pe);
    cl_application_step(app, false);
    CHECK(clicks == 1); /* re-enabled: works again */

    cl_application_destroy(app);
}

/* ---- the log callback receives diagnostics ------------------------------- */

static char last_log[128];
static void log_sink(cl_log_level_t level, const char *msg, void *user)
{
    (void)level;
    (void)user;
    snprintf(last_log, sizeof(last_log), "%s", msg);
}

static void test_log_callback(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_application_t *app;
    unsigned char junk[32];

    cl_set_log_callback(log_sink, NULL);
    ad.platform = cl_platform_mock_create(cl_allocator_default());
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);

    memset(junk, 'j', sizeof(junk));
    CHECK(cl_font_load_memory(app, junk, sizeof(junk), 12.0f) == NULL);
    CHECK(strstr(last_log, "not a supported font") != NULL);

    cl_set_log_callback(NULL, NULL);
    cl_application_destroy(app);
}

/* ---- a custom widget through the public extension API -------------------- */

typedef struct cl_probe {
    cl_widget_t base;
    int painted;
} cl_probe_t;

static cl_size_t probe_measure(cl_widget_t *w, cl_constraints_t c)
{
    (void)w;
    (void)c;
    return (cl_size_t){ 33.0f, 21.0f };
}

static void probe_paint(cl_widget_t *w, cl_paint_context_t *ctx)
{
    cl_probe_t *p = CL_WIDGET_CAST_UNCHECKED(cl_probe, w);

    (void)ctx;
    p->painted++;
}

/* Only the left half of the rect is "solid". */
static bool probe_hit(cl_widget_t *w, cl_point_t pt)
{
    return pt.x < w->rect.x + w->rect.w * 0.5f;
}

static const cl_widget_vtable_t probe_vtable = {
    .measure = probe_measure,
    .paint = probe_paint,
    .hit_test = probe_hit,
};

static const cl_widget_class_t cl_probe_class = {
    .name = "cl_probe",
    .type_id = 0x70726f62u, /* 'prob' */
    .instance_size = sizeof(cl_probe_t),
    .vtable = &probe_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static void test_custom_widget(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = 200, .height = 100 };
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS };
    cl_platform_t *plat = cl_platform_mock_create(cl_allocator_default());
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *box;
    cl_widget_t *w;
    cl_probe_t *p;

    ad.platform = plat;
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);
    win = cl_window_create(app, &wd);
    box = cl_vbox_create(app, &vd);

    w = cl_widget_alloc(app, &cl_probe_class);
    CHECK(w != NULL);
    /* RTTI helpers */
    CHECK(cl_widget_is_a(w, &cl_probe_class));
    CHECK(!cl_widget_is_a(box, &cl_probe_class));
    p = CL_WIDGET_CAST(cl_probe, w);
    CHECK(p != NULL);
    CHECK(cl_widget_check_cast(box, &cl_probe_class) == NULL);
    CHECK(cl_last_error() == CL_ERROR_INVALID_ARGUMENT); /* mixed handles */

    cl_widget_add_child(box, w);
    cl_window_set_content(win, box);
    cl_application_step(app, false);

    /* measure honoured and paint invoked */
    CHECK(cl_widget_rect(w).w == 33.0f);
    CHECK(cl_widget_rect(w).h == 21.0f);
    CHECK(p->painted >= 1);

    cl_application_destroy(app);
}

int main(void)
{
    test_on_close();
    test_run_exit_code();
    test_window_lifecycle();
    test_disabled();
    test_log_callback();
    test_custom_widget();

    if (failures == 0)
        printf("all lifecycle tests passed\n");
    return failures == 0 ? 0 : 1;
}
