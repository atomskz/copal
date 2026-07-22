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

/* Number of children in a widget's subtree list (uses the widget_impl base). */
static int child_count(cl_widget_t *w)
{
    int n = 0;
    cl_widget_t *c;

    for (c = w->first_child; c; c = c->next_sibling)
        n++;
    return n;
}

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

/* ---- hover applies the widget cursor --------------------------------------- */

static void test_cursor(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = 200, .height = 200 };
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS,
                          .align_cross = CL_ALIGN_STRETCH };
    cl_textbox_desc_t td = { CL_TEXTBOX_DESC_INIT_FIELDS };
    cl_platform_t *plat = cl_platform_mock_create(cl_allocator_default());
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *box;
    cl_widget_t *tb;
    cl_platform_event_t pe = { 0 };
    cl_rect_t r;

    ad.platform = plat;
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);
    win = cl_window_create(app, &wd);
    box = cl_vbox_create(app, &vd);
    tb = cl_textbox_create(app, &td);
    CHECK(cl_widget_cursor(tb) == CL_CURSOR_IBEAM); /* the default */
    cl_widget_add_child(box, tb);
    cl_window_set_content(win, box);
    cl_application_step(app, false);

    r = cl_widget_rect(tb);
    pe.kind = CL_PEV_MOUSE_MOVE;
    pe.pos = (cl_point_t){ r.x + 4, r.y + 4 };
    cl_platform_mock_push(plat, pe);
    cl_application_step(app, false);
    CHECK(cl_platform_mock_cursor(plat) == CL_CURSOR_IBEAM);

    pe.pos = (cl_point_t){ 190, 190 }; /* empty area below the textbox */
    cl_platform_mock_push(plat, pe);
    cl_application_step(app, false);
    CHECK(cl_platform_mock_cursor(plat) == CL_CURSOR_DEFAULT);

    cl_application_destroy(app);
}

/* ---- destroying widgets from callbacks (deferred destruction) ------------- */

static void kill_widget(cl_widget_t *w, void *user)
{
    (void)w;
    cl_widget_destroy((cl_widget_t *)user);
    cl_widget_destroy((cl_widget_t *)user); /* double destroy: no-op */
}

static void kill_self(cl_widget_t *w, void *user)
{
    (void)user;
    cl_widget_destroy(w);
}

static void click_at(cl_platform_t *plat, cl_point_t pos)
{
    cl_platform_event_t pe = { 0 };

    pe.kind = CL_PEV_MOUSE_DOWN;
    pe.pos = pos;
    pe.button = CL_MOUSE_LEFT;
    cl_platform_mock_push(plat, pe);
    pe.kind = CL_PEV_MOUSE_UP;
    cl_platform_mock_push(plat, pe);
}

/* A focusable widget whose focus_lost destroys itself: destroying it while it
 * holds focus re-enters cl_widget_destroy from inside the detach. */
typedef struct cl_selfkill {
    cl_widget_t base;
} cl_selfkill_t;

static void selfkill_focus_lost(cl_widget_t *w)
{
    cl_widget_destroy(w);
}

static const cl_widget_vtable_t selfkill_vtable = {
    .focus_lost = selfkill_focus_lost,
};

static const cl_widget_class_t cl_selfkill_class = {
    .name = "cl_selfkill",
    .instance_size = sizeof(cl_selfkill_t),
    .vtable = &selfkill_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static void test_destroy_from_callback(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = 200, .height = 200 };
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS,
                          .align_cross = CL_ALIGN_STRETCH };
    cl_button_desc_t bd = { CL_BUTTON_DESC_INIT_FIELDS, .text = "x" };
    cl_platform_t *plat = cl_platform_mock_create(cl_allocator_default());
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *box;
    cl_widget_t *killer;
    cl_widget_t *victim;
    cl_rect_t kr;

    ad.platform = plat;
    ad.renderer = cl_renderer_mock_create(cl_allocator_default());
    app = cl_application_create(&ad);
    win = cl_window_create(app, &wd);
    box = cl_vbox_create(app, &vd);
    killer = cl_button_create(app, &bd);
    victim = cl_button_create(app, &bd);
    cl_widget_set_flex(killer, 1.0f);
    cl_widget_set_flex(victim, 1.0f);
    cl_widget_add_child(box, killer);
    cl_widget_add_child(box, victim);
    cl_window_set_content(win, box);
    cl_application_step(app, false);

    /* destroying a SIBLING mid-dispatch: freed only after the iteration */
    CHECK(child_count(box) == 2);
    cl_button_set_on_click(killer, kill_widget, victim);
    kr = cl_widget_rect(killer);
    click_at(plat, (cl_point_t){ kr.x + kr.w * 0.5f, kr.y + kr.h * 0.5f });
    cl_application_step(app, false);
    CHECK(cl_widget_parent(killer) == box); /* the killer survives */
    CHECK(child_count(box) == 1);           /* the victim was detached + reaped */

    /* destroying SELF from the click callback */
    cl_application_step(app, false);
    cl_button_set_on_click(killer, kill_self, NULL);
    kr = cl_widget_rect(killer);
    click_at(plat, (cl_point_t){ kr.x + kr.w * 0.5f, kr.y + kr.h * 0.5f });
    cl_application_step(app, false); /* no crash under ASan; reaped */

    /* a dead widget cannot be re-adopted */
    {
        cl_widget_t *loner = cl_button_create(app, &bd);

        cl_widget_add_child(box, loner);
        cl_widget_destroy(loner);
        CHECK(cl_widget_add_child(box, loner) == CL_ERROR_INVALID_ARGUMENT);
        cl_application_step(app, false); /* reap */
    }

    /* destroying a FOCUSED widget whose focus_lost destroys itself: the detach
     * fires focus_lost before the node is queued, so the nested destroy must be
     * a no-op rather than queuing (and later double-freeing) the same node. */
    {
        cl_widget_t *sk = cl_widget_alloc(app, &cl_selfkill_class);

        cl_widget_set_focusable(sk, true);
        cl_widget_add_child(box, sk);
        cl_application_step(app, false);
        CHECK(cl_widget_focus(sk));
        CHECK(cl_widget_has_focus(sk));
        cl_widget_destroy(sk);           /* detach -> focus_lost -> destroy(sk) */
        cl_application_step(app, false);  /* reap: exactly one free, no crash */
    }

    cl_application_destroy(app);
}

/* ---- hover highlights the button ------------------------------------------ */

static bool same_color(cl_color_t a, cl_color_t b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/* true if some fill_round_rect used the given colour in the last frame */
static bool painted_round_with(cl_renderer_t *rend, cl_color_t c)
{
    size_t i;
    size_t n = cl_renderer_mock_count(rend);

    for (i = 0; i < n; i++) {
        const cl_mock_command_t *cmd = cl_renderer_mock_get(rend, i);

        if (cmd->kind == CL_MOCK_FILL_ROUND && same_color(cmd->color, c))
            return true;
    }
    return false;
}

static void test_button_hover(void)
{
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                            .width = 200, .height = 100 };
    cl_vbox_desc_t vd = { CL_VBOX_DESC_INIT_FIELDS,
                          .align_cross = CL_ALIGN_STRETCH };
    cl_button_desc_t bd = { CL_BUTTON_DESC_INIT_FIELDS, .text = "hover me" };
    cl_platform_t *plat = cl_platform_mock_create(cl_allocator_default());
    cl_renderer_t *rend = cl_renderer_mock_create(cl_allocator_default());
    cl_application_t *app;
    cl_window_t *win;
    cl_widget_t *box;
    cl_widget_t *btn;
    cl_color_t hover_col;
    cl_platform_event_t pe = { 0 };

    ad.platform = plat;
    ad.renderer = rend;
    app = cl_application_create(&ad);
    hover_col = cl_theme_color(cl_application_theme(app),
                               CL_COLOR_SURFACE_HOVER);
    win = cl_window_create(app, &wd);
    box = cl_vbox_create(app, &vd);
    btn = cl_button_create(app, &bd);
    cl_widget_set_flex(btn, 1.0f);
    cl_widget_add_child(box, btn);
    cl_window_set_content(win, box);
    cl_application_step(app, false);
    CHECK(!painted_round_with(rend, hover_col)); /* not hovered yet */

    pe.kind = CL_PEV_MOUSE_MOVE;
    pe.pos = (cl_point_t){ 100, 50 }; /* over the stretched button */
    cl_platform_mock_push(plat, pe);
    cl_application_step(app, false);
    CHECK(painted_round_with(rend, hover_col)); /* highlighted */

    pe.pos = (cl_point_t){ 100, 50 };
    cl_platform_mock_push(plat, pe); /* no change: same widget */
    cl_application_step(app, false);

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

/* ---- backend SPI handshake ------------------------------------------------ */

static void test_backend_abi(void)
{
    /* An ops table from "different headers": undersized (missing ops the
     * library would call) or an incompatible major version. */
    static const cl_platform_ops_t bad_size_ops = {
        .struct_size = sizeof(cl_platform_ops_t) - 8,
        .abi_version = COPAL_VERSION,
    };
    static const cl_platform_ops_t bad_ver_ops = {
        .struct_size = sizeof(cl_platform_ops_t),
        .abi_version = COPAL_VERSION_ENCODE(COPAL_VERSION_MAJOR + 1, 0, 0),
    };
    cl_platform_t bad = { &bad_size_ops };
    cl_application_desc_t ad = { CL_APPLICATION_DESC_INIT_FIELDS };
    cl_renderer_t *rend = cl_renderer_mock_create(cl_allocator_default());

    ad.renderer = rend;
    ad.platform = &bad;
    CHECK(cl_application_create(&ad) == NULL);
    CHECK(cl_last_error() == CL_ERROR_ABI_MISMATCH);

    bad.ops = &bad_ver_ops;
    CHECK(cl_application_create(&ad) == NULL);
    CHECK(cl_last_error() == CL_ERROR_ABI_MISMATCH);

    /* the injected renderer stays with the caller on failure */
    rend->ops->destroy(rend);
}

/* ---- a custom widget through the public extension API -------------------- */

typedef struct cl_probe {
    cl_widget_t base;
    int painted;
    int entered;
    int left;
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

static void probe_enter(cl_widget_t *w)
{
    CL_WIDGET_CAST_UNCHECKED(cl_probe, w)->entered++;
}

static void probe_leave(cl_widget_t *w)
{
    CL_WIDGET_CAST_UNCHECKED(cl_probe, w)->left++;
}

static const cl_widget_vtable_t probe_vtable = {
    .measure = probe_measure,
    .paint = probe_paint,
    .hit_test = probe_hit,
    .mouse_enter = probe_enter,
    .mouse_leave = probe_leave,
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

    /* hover: enter once while the pointer stays inside, leave on exit */
    {
        cl_rect_t r = cl_widget_rect(w);
        cl_platform_event_t pe = { 0 };

        pe.kind = CL_PEV_MOUSE_MOVE;
        pe.pos = (cl_point_t){ r.x + 2, r.y + 2 }; /* solid left half */
        cl_platform_mock_push(plat, pe);
        pe.pos = (cl_point_t){ r.x + 4, r.y + 4 }; /* still inside: no re-enter */
        cl_platform_mock_push(plat, pe);
        cl_application_step(app, false);
        CHECK(p->entered == 1);
        CHECK(p->left == 0);

        pe.pos = (cl_point_t){ 190, 90 }; /* empty corner of the window */
        cl_platform_mock_push(plat, pe);
        cl_application_step(app, false);
        CHECK(p->left == 1);
    }

    cl_application_destroy(app);
}

int main(void)
{
    test_on_close();
    test_backend_abi();
    test_run_exit_code();
    test_window_lifecycle();
    test_disabled();
    test_button_hover();
    test_cursor();
    test_destroy_from_callback();
    test_log_callback();
    test_custom_widget();

    if (failures == 0)
        printf("all lifecycle tests passed\n");
    return failures == 0 ? 0 : 1;
}
