/*
 * programs.c — The static list of user programs.
 *
 * All entries point at symbols defined under `programs/`, linked into
 * flash: nothing here is copied to RAM, the programs execute in place.
 */

#include "programs.h"

/* --- Forward declarations of the program entry points ---------------- *
 * Each of these is defined in a matching file under programs/. Keeping
 * the extern declarations here avoids per-program headers.             */
extern void p_hello(void);
extern void p_counter(void);
extern void p_alphabet(void);
extern void p_race_A(void);
extern void p_race_B(void);
extern void p_safe_A(void);
extern void p_safe_B(void);
extern void p_bigmem(void);

/* --- The table itself ------------------------------------------------ *
 * ram_bytes = stack + data budget used starting from phase 5 (allocator).
 * For phase 3/4 the value is informational — printed by `list`.        */
const program_t program_table[] = {
    /* ram_bytes is the stack budget passed to task_create(); intentionally
     * different across programs so first-fit picks visible different holes. */
    { "p_hello",    "print a short greeting and return",             p_hello,     256 },
    { "p_counter",  "count from 1 to 10, print each number",         p_counter,   384 },
    { "p_alphabet", "print the lowercase alphabet A to Z",           p_alphabet,  512 },

    /* Phase 6 demo: races (no mutex) vs safe (console_mutex). */
    { "p_race_A",   "write 'A' pattern to UART WITHOUT mutex",       p_race_A,    512 },
    { "p_race_B",   "write 'B' pattern to UART WITHOUT mutex",       p_race_B,    512 },
    { "p_safe_A",   "write 'A' pattern to UART with console mutex",  p_safe_A,    512 },
    { "p_safe_B",   "write 'B' pattern to UART with console mutex",  p_safe_B,    512 },

    /* Edge-case tester: two copies overflow the user arena. */
    { "p_bigmem",   "40 KiB stack (two copies exhaust the arena)",   p_bigmem,  40960 },
};

const uint32_t program_count = sizeof(program_table) / sizeof(program_table[0]);

/* Basic strcmp — we have no libc. */
static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

const program_t *program_find(const char *name)
{
    for (uint32_t i = 0; i < program_count; ++i) {
        if (str_eq(program_table[i].name, name)) {
            return &program_table[i];
        }
    }
    return 0;
}
