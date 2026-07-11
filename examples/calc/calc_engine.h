/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CALC_ENGINE_H
#define CALC_ENGINE_H

/*
 * Calculator model: a small, UI-free state machine. It knows nothing about
 * copal, so it can be reasoned about (and unit-tested) on its own. The view
 * feeds it key codes and reads back the text to display.
 */
#include <stdbool.h>

/* Key codes accepted by calc_input(). Digits are their own ASCII characters. */
enum {
    CALC_CLEAR   = 'C',
    CALC_NEGATE  = '~', /* +/- */
    CALC_PERCENT = '%',
    CALC_DIV     = '/',
    CALC_MUL     = '*',
    CALC_SUB     = '-',
    CALC_ADD     = '+',
    CALC_DOT     = '.',
    CALC_BACK    = '<', /* backspace */
    CALC_EQUALS  = '='
};

#define CALC_ENTRY_MAX 20

typedef struct calc_engine {
    char   entry[CALC_ENTRY_MAX]; /* text of the current entry / result */
    double stored;                /* left-hand operand of a pending op */
    char   op;                    /* pending operator, or 0 */
    bool   fresh;                 /* next digit starts a new entry */
    bool   error;                 /* set on divide-by-zero / overflow */
} calc_engine_t;

/** calc_reset() - clear to the initial "0" state. */
void calc_reset(calc_engine_t *e);

/** calc_input() - feed one key code (a digit char or a CALC_* code). */
void calc_input(calc_engine_t *e, int code);

/** calc_text() - the text to display (NUL-terminated, always valid). */
const char *calc_text(const calc_engine_t *e);

#endif /* CALC_ENGINE_H */
