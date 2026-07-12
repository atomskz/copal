/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_WIDGET_IMPL_H
#define CL_WIDGET_IMPL_H

/*
 * Widget base layout for AUTHORS of custom widgets. Application code should not
 * need this header. Embed cl_widget as the FIRST member of a derived struct and
 * fill a static cl_widget_class_t + cl_widget_vtable_t (ARCHITECTURE §9).
 */
#include <stddef.h>
#include <stdint.h>

#include <copal/widget.h>
#include <copal/event.h>
#include <copal/render.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CL_WIDGET_RESERVED 24

enum cl_widget_flags {
    CL_WF_VISIBLE = 1u << 0,
    CL_WF_ENABLED = 1u << 1,
    CL_WF_FOCUSABLE = 1u << 2,
    /* bits 3-4 reserved (were DIRTY/DEAD; never implemented) */
    CL_WF_CLIP = 1u << 5 /* clip children to this widget's rect while painting */
};

typedef struct cl_widget_class cl_widget_class_t;

/*
 * Virtual method table. Every slot takes cl_widget_t* as its first parameter;
 * implementations downcast internally with CL_WIDGET_CAST. A NULL slot uses the
 * default behaviour (hit_test = rect test, on_event = fan out to mouse_*).
 */
typedef struct cl_widget_vtable {
    void (*destroy)(cl_widget_t *w);
    cl_size_t (*measure)(cl_widget_t *w, cl_constraints_t c);
    void (*arrange)(cl_widget_t *w, cl_rect_t rect);
    void (*paint)(cl_widget_t *w, cl_paint_context_t *ctx);
    /*
     * Clip rect for children while painting when CL_WF_CLIP is set; a NULL slot
     * means the widget's whole rect. The same rect bounds hit-testing, so a
     * point inside the widget but outside the clip (e.g. a scrollbar gutter)
     * hits the widget itself rather than a child painted underneath it.
     */
    cl_rect_t (*clip_rect)(cl_widget_t *w);
    /*
     * Scroll or otherwise adjust so the absolute rect `target` (a descendant's
     * rect) becomes visible within this widget. A NULL slot means the widget
     * cannot reveal children; cl_widget_reveal() then skips it.
     */
    void (*reveal)(cl_widget_t *w, cl_rect_t target);
    /*
     * Refine hit-testing inside the rect (non-rectangular shapes); return
     * false for a miss. A miss skips the widget and its whole subtree. A
     * NULL slot accepts every point inside the rect.
     */
    bool (*hit_test)(cl_widget_t *w, cl_point_t p);
    bool (*on_event)(cl_widget_t *w, const cl_event_t *ev);
    bool (*mouse_down)(cl_widget_t *w, const cl_event_t *ev);
    bool (*mouse_up)(cl_widget_t *w, const cl_event_t *ev);
    bool (*mouse_move)(cl_widget_t *w, const cl_event_t *ev);
    bool (*mouse_wheel)(cl_widget_t *w, const cl_event_t *ev);
    bool (*key_down)(cl_widget_t *w, const cl_event_t *ev);
    bool (*key_up)(cl_widget_t *w, const cl_event_t *ev);
    bool (*text_input)(cl_widget_t *w, const cl_event_t *ev);
    bool (*text_edit)(cl_widget_t *w, const cl_event_t *ev); /* IME pre-edit */
    void (*focus_gained)(cl_widget_t *w);
    void (*focus_lost)(cl_widget_t *w);
} cl_widget_vtable_t;

struct cl_widget_class {
    const char *name;
    const cl_widget_class_t *base;
    uint32_t type_id;
    size_t instance_size;
    const cl_widget_vtable_t *vtable;
};

struct cl_widget {
    const cl_widget_class_t *cls;
    cl_application_t *app;  /* weak */
    cl_window_t *window;    /* weak */
    cl_widget_t *parent;    /* weak */
    cl_widget_t *first_child;
    cl_widget_t *last_child;
    cl_widget_t *next_sibling;
    cl_rect_t rect;         /* absolute, assigned by arrange */
    cl_size_t measured;
    cl_size_t pref_size;    /* overrides measure per axis where > 0 */
    cl_insets_t margin;
    cl_align_t align_h;     /* cross-axis override; CL_ALIGN_AUTO = container */
    cl_align_t align_v;
    float flex;             /* main-axis growth weight in boxes; 0 = fixed */
    uint32_t flags;
    void *userdata;
    char *tooltip; /* owned UTF-8 hover text, or NULL (cl_widget_set_tooltip) */
    unsigned char reserved[CL_WIDGET_RESERVED];
};

/** cl_widget_alloc() - allocate a zeroed instance and init the base. */
CL_API cl_widget_t *cl_widget_alloc(cl_application_t *app,
                                    const cl_widget_class_t *cls);

/** cl_widget_init_base() - initialise the cl_widget base of an instance. */
CL_API void cl_widget_init_base(cl_widget_t *w, cl_application_t *app,
                                const cl_widget_class_t *cls);

/**
 * cl_widget_check_cast() - checked downcast; returns NULL on type mismatch.
 *
 * A mismatch on a live widget records CL_ERROR_INVALID_ARGUMENT (this is how
 * the typed accessors report a mixed-up handle: no-op + cl_last_error()).
 * NULL input stays silent. To probe a type without recording an error, use
 * cl_widget_is_a().
 */
CL_API void *cl_widget_check_cast(cl_widget_t *w, const cl_widget_class_t *cls);

/** cl_widget_is_a() - true if w is an instance of cls or a subclass. */
CL_API bool cl_widget_is_a(cl_widget_t *w, const cl_widget_class_t *cls);

#define CL_WIDGET_CAST(name, w) \
    ((name##_t *)cl_widget_check_cast((w), &name##_class))
#define CL_WIDGET_CAST_UNCHECKED(name, w) ((name##_t *)(w))

/*
 * Container plumbing: measure/arrange a child from the parent's own
 * measure/arrange slots. The wrappers apply the NULL-slot defaults, honour
 * the child's preferred size, record child->measured and assign child->rect
 * — call them instead of invoking the child's vtable directly.
 */
CL_API cl_size_t cl_widget_do_measure(cl_widget_t *child, cl_constraints_t c);
CL_API void cl_widget_do_arrange(cl_widget_t *child, cl_rect_t rect);

/** cl_widget_reveal() - scroll scrollable ancestors until w is visible. */
CL_API void cl_widget_reveal(cl_widget_t *w);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIDGET_IMPL_H */
