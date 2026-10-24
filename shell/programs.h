/*
 * programs.h — Static table of user programs known to the OS.
 *
 * Each entry describes a program that lives in flash (XIP): the shell can
 * list them and start any of them. Adding a new program is a two-step
 * change: (1) write programs/p_XXX.c defining a `void p_XXX(void)`
 * function, (2) add a row to program_table[] in shell/programs.c.
 *
 * The struct is designed to satisfy the requirement of the assignment
 * ("For each program, the OS must know: entry point, RAM required,
 *  resources needed") — the RAM and resource fields are already here
 * even though they are only consumed starting from phase 4/5.
 */

#ifndef OS_PROGRAMS_H
#define OS_PROGRAMS_H

#include <stdint.h>

/* Every user program has this signature: no args, no return value. When
 * the entry function returns, the program is considered terminated. */
typedef void (*program_entry_t)(void);

typedef struct {
    const char       *name;         /* short identifier used by the shell (e.g. "p_counter") */
    const char       *description;  /* one-line explanation shown by `list` */
    program_entry_t   entry;        /* address in flash to jump to        */
    uint32_t          ram_bytes;    /* data + stack the program needs (Phase 5) */
} program_t;

/* Provided by shell/programs.c ---------------------------------------- */
extern const program_t program_table[];
extern const uint32_t  program_count;

/* Look up by name; returns NULL if no match. Case-sensitive. */
const program_t *program_find(const char *name);

#endif /* OS_PROGRAMS_H */
