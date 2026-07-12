/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <copal/widgets/radiogroup.h>
#include <copal/widgets/radiobutton.h>
#include <copal/widget_impl.h>
#include <copal/application.h>

#include "widget/widget_internal.h"
#include "core/foundation/foundation_internal.h"

typedef struct cl_radiogroup {
    cl_widget_t base;
    float spacing;
    int group_id; /* unique exclusion group shared by the child radios */
    cl_radiogroup_fn on_change;
    void *user;
} cl_radiogroup_t;

static cl_size_t radiogroup_measure(cl_widget_t *w, cl_constraints_t c);
static void radiogroup_arrange(cl_widget_t *w, cl_rect_t rect);

static const cl_widget_vtable_t radiogroup_vtable = {
    .measure = radiogroup_measure,
    .arrange = radiogroup_arrange,
};

static const cl_widget_class_t cl_radiogroup_class = {
    .name = "cl_radiogroup",
    .base = NULL,
    .type_id = 0x72677270u, /* 'rgrp' */
    .instance_size = sizeof(cl_radiogroup_t),
    .vtable = &radiogroup_vtable,
    .vtable_size = sizeof(cl_widget_vtable_t),
};

static cl_size_t radiogroup_measure(cl_widget_t *w, cl_constraints_t c)
{
    cl_radiogroup_t *g = CL_WIDGET_CAST(cl_radiogroup, w);
    cl_size_t out = { 0, 0 };
    cl_widget_t *ch;
    int n = 0;

    for (ch = w->first_child; ch; ch = ch->next_sibling) {
        cl_size_t cs;

        if (!(ch->flags & CL_WF_VISIBLE))
            continue;
        cs = cl_widget_do_measure(ch, c);
        if (cs.w > out.w)
            out.w = cs.w;
        out.h += cs.h;
        n++;
    }
    if (n > 1)
        out.h += g->spacing * (float)(n - 1);
    return out;
}

static void radiogroup_arrange(cl_widget_t *w, cl_rect_t rect)
{
    cl_radiogroup_t *g = CL_WIDGET_CAST(cl_radiogroup, w);
    cl_widget_t *ch;
    float y = rect.y;

    for (ch = w->first_child; ch; ch = ch->next_sibling) {
        if (!(ch->flags & CL_WF_VISIBLE))
            continue;
        cl_widget_do_arrange(
            ch, (cl_rect_t){ rect.x, y, rect.w, ch->measured.h });
        y += ch->measured.h + g->spacing;
    }
}

/* Index of a radio among the group's children, or -1. */
static int option_index(cl_radiogroup_t *g, cl_widget_t *rb)
{
    cl_widget_t *ch;
    int i = 0;

    for (ch = g->base.first_child; ch; ch = ch->next_sibling, i++) {
        if (ch == rb)
            return i;
    }
    return -1;
}

static void option_selected(cl_widget_t *rb, bool selected, void *user)
{
    cl_radiogroup_t *g = user;

    if (selected && g->on_change)
        g->on_change(&g->base, option_index(g, rb),
                     g->user); /* last: may destroy */
}

cl_widget_t *cl_radiogroup_create(cl_application_t *app,
                                  const cl_radiogroup_desc_t *desc)
{
    /* Each group gets a fresh positive exclusion id, well clear of the small
     * hand-picked ids applications use for standalone radios. */
    static int next_group_id = 0x40000000;
    cl_widget_t *w;
    cl_radiogroup_t *g;

    if (!CL_DESC_ABI_OK(desc, cl_radiogroup_desc_t))
        return NULL;
    w = cl_widget_alloc(app, &cl_radiogroup_class);
    if (!w)
        return NULL;
    g = CL_WIDGET_CAST(cl_radiogroup, w);
    g->group_id = next_group_id++;
    if (desc)
        g->spacing = desc->spacing;
    return w;
}

cl_widget_t *cl_radiogroup_add(cl_widget_t *group, const char *text)
{
    cl_radiogroup_t *g = CL_WIDGET_CAST(cl_radiogroup, group);
    cl_widget_t *rb;

    if (!g || !text) {
        cl_set_last_error(CL_ERROR_INVALID_ARGUMENT);
        return NULL;
    }
    rb = cl_radiobutton_create(
        group->app, &(cl_radiobutton_desc_t){ CL_RADIOBUTTON_DESC_INIT_FIELDS,
                                              .text = text,
                                              .group = g->group_id });
    if (!rb)
        return NULL;
    cl_radiobutton_set_on_select(rb, option_selected, g);
    cl_widget_add_child(group, rb);
    return rb;
}

size_t cl_radiogroup_count(cl_widget_t *group)
{
    cl_radiogroup_t *g = CL_WIDGET_CAST(cl_radiogroup, group);
    cl_widget_t *ch;
    size_t n = 0;

    if (!g)
        return 0;
    for (ch = group->first_child; ch; ch = ch->next_sibling)
        n++;
    return n;
}

int cl_radiogroup_selected(cl_widget_t *group)
{
    cl_radiogroup_t *g = CL_WIDGET_CAST(cl_radiogroup, group);
    cl_widget_t *ch;
    int i = 0;

    if (!g)
        return -1;
    for (ch = group->first_child; ch; ch = ch->next_sibling, i++) {
        if (cl_radiobutton_is_selected(ch))
            return i;
    }
    return -1;
}

void cl_radiogroup_set_selected(cl_widget_t *group, int index)
{
    cl_radiogroup_t *g = CL_WIDGET_CAST(cl_radiogroup, group);
    cl_widget_t *ch;
    int i = 0;

    if (!g)
        return;
    for (ch = group->first_child; ch; ch = ch->next_sibling, i++) {
        if (i == index) {
            cl_radiobutton_set_selected(ch, true); /* deselects the others */
            return;
        }
    }
    /* index -1 (or out of range): clear the selection */
    for (ch = group->first_child; ch; ch = ch->next_sibling)
        cl_radiobutton_set_selected(ch, false);
}

void cl_radiogroup_set_on_change(cl_widget_t *group, cl_radiogroup_fn fn,
                                 void *user)
{
    cl_radiogroup_t *g = CL_WIDGET_CAST(cl_radiogroup, group);

    if (g) {
        g->on_change = fn;
        g->user = user;
    }
}
