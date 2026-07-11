/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "calc_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void calc_reset(calc_engine_t *e)
{
    strcpy(e->entry, "0");
    e->stored = 0.0;
    e->op = 0;
    e->fresh = true;
    e->error = false;
}

const char *calc_text(const calc_engine_t *e)
{
    return e->error ? "Error" : e->entry;
}

static double entry_value(const calc_engine_t *e)
{
    return strtod(e->entry, NULL);
}

/* Format v into the entry, normalising -0 and flagging non-finite results. */
static void set_entry(calc_engine_t *e, double v)
{
    if (v == 0.0)
        v = 0.0;
    if (v != v || v > 1e308 || v < -1e308) {
        e->error = true;
        return;
    }
    snprintf(e->entry, sizeof(e->entry), "%.10g", v);
}

static double apply(calc_engine_t *e, double a, char op, double b)
{
    switch (op) {
        case CALC_ADD:
            return a + b;

        case CALC_SUB:
            return a - b;

        case CALC_MUL:
            return a * b;

        case CALC_DIV:
            if (b == 0.0) {
                e->error = true;
                return 0.0;
            }
            return a / b;

        default:
            return b;
    }
}

static void input_digit(calc_engine_t *e, char d)
{
    size_t n;

    if (e->fresh) {
        e->entry[0] = '\0';
        e->fresh = false;
    }
    if (strcmp(e->entry, "0") == 0)
        e->entry[0] = '\0'; /* replace a lone leading zero */
    n = strlen(e->entry);
    if (n + 1 < sizeof(e->entry)) {
        e->entry[n] = d;
        e->entry[n + 1] = '\0';
    }
}

static void input_dot(calc_engine_t *e)
{
    size_t n;

    if (e->fresh) {
        strcpy(e->entry, "0");
        e->fresh = false;
    }
    if (strchr(e->entry, '.'))
        return;
    n = strlen(e->entry);
    if (n + 1 < sizeof(e->entry)) {
        e->entry[n] = '.';
        e->entry[n + 1] = '\0';
    }
}

static void input_op(calc_engine_t *e, char op)
{
    if (e->op != 0 && !e->fresh) {
        /* Chain: fold the pending operation before starting the next. */
        e->stored = apply(e, e->stored, e->op, entry_value(e));
        set_entry(e, e->stored);
    } else {
        e->stored = entry_value(e);
    }
    e->op = e->error ? 0 : op;
    e->fresh = true;
}

static void do_equals(calc_engine_t *e)
{
    if (e->op == 0) {
        e->fresh = true;
        return;
    }
    e->stored = apply(e, e->stored, e->op, entry_value(e));
    set_entry(e, e->stored);
    e->op = 0;
    e->fresh = true;
}

static void backspace(calc_engine_t *e)
{
    size_t n;

    if (e->fresh)
        return;
    n = strlen(e->entry);
    if (n > 0)
        e->entry[n - 1] = '\0';
    if (e->entry[0] == '\0' || strcmp(e->entry, "-") == 0) {
        strcpy(e->entry, "0");
        e->fresh = true;
    }
}

static void negate(calc_engine_t *e)
{
    size_t n = strlen(e->entry);

    if (strcmp(e->entry, "0") == 0)
        return;
    if (e->entry[0] == '-') {
        memmove(e->entry, e->entry + 1, n); /* includes the NUL */
    } else if (n + 1 < sizeof(e->entry)) {
        memmove(e->entry + 1, e->entry, n + 1);
        e->entry[0] = '-';
    }
}

void calc_input(calc_engine_t *e, int code)
{
    if (e->error && code != CALC_CLEAR)
        return; /* only Clear recovers from an error */

    if (code >= '0' && code <= '9') {
        input_digit(e, (char)code);
        return;
    }
    switch (code) {
        case CALC_DOT:
            input_dot(e);
            break;

        case CALC_ADD:
        case CALC_SUB:
        case CALC_MUL:
        case CALC_DIV:
            input_op(e, (char)code);
            break;

        case CALC_EQUALS:
            do_equals(e);
            break;

        case CALC_CLEAR:
            calc_reset(e);
            break;

        case CALC_BACK:
            backspace(e);
            break;

        case CALC_NEGATE:
            negate(e);
            break;

        case CALC_PERCENT:
            set_entry(e, entry_value(e) / 100.0);
            e->fresh = true;
            break;

        default:
            break;
    }
}
