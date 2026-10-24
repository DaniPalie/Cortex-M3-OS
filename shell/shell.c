/*
 * shell.c — Textual shell for the OS-Group08 batch OS.
 *
 * Command grammar (whitespace-separated tokens, case-sensitive):
 *
 *   help                          print the list of commands
 *   list                          print the program table
 *   run <name> [<name> ...]       execute one or more programs
 *   (empty line)                  just re-print the prompt
 *
 * Phase 3 executes at most one program per `run` line, sequentially, as
 * a plain function call: shell -> program.entry() -> shell.  Phase 4
 * will keep the same grammar but dispatch the argument list to the
 * scheduler so several programs run in parallel with round-robin.
 *
 * The shell uses only uart_gets() / uart_puts(): no dynamic memory, no
 * libc.  Command length is capped by SHELL_LINE_MAX.
 */

#include "shell.h"
#include "programs.h"
#include "../boot/uart.h"
#include "../kernel/scheduler.h"
#include "../mm/alloc.h"

#define SHELL_LINE_MAX   96   /* one command line */
#define SHELL_MAX_TOKENS 8    /* run + up to 7 program names */

/* --- Tiny string helpers (no libc available) ------------------------- */

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int is_space(char c) { return c == ' ' || c == '\t'; }

/* In-place tokeniser: replaces spaces with '\0', fills argv[], returns argc. */
static int tokenise(char *line, char *argv[], int max)
{
    int argc = 0;
    char *p = line;

    while (*p && argc < max) {
        while (is_space(*p)) *p++ = '\0';       /* skip leading spaces */
        if (!*p) break;
        argv[argc++] = p;                       /* start of a token */
        while (*p && !is_space(*p)) p++;        /* skip token body */
    }
    return argc;
}

/* --- Built-in commands ---------------------------------------------- */

static void cmd_help(void)
{
    uart_puts(
        "commands:\r\n"
        "  help                          this message\r\n"
        "  list                          list available programs\r\n"
        "  stat                          print user-arena occupancy\r\n"
        "  run <name> [<name> ...]       execute one or more programs in parallel\r\n"
    );
}

static void cmd_stat(void)
{
    mem_dump();
}

static void cmd_list(void)
{
    uart_puts("available programs:\r\n");
    for (uint32_t i = 0; i < program_count; ++i) {
        const program_t *p = &program_table[i];
        uart_puts("  ");
        uart_puts(p->name);
        /* pad names to a fixed column so descriptions line up */
        int len = 0;
        const char *n = p->name;
        while (*n++) len++;
        while (len < 14) { uart_putc(' '); len++; }
        uart_puts("(ram=");
        uart_put_dec(p->ram_bytes);
        uart_puts(" B) ");
        uart_puts(p->description);
        uart_puts("\r\n");
    }
}

/* Phase 4 semantics: register every named program as a task and hand
 * control to the round-robin scheduler. Unknown names abort the whole
 * `run` line so a typo does not silently start a smaller batch. */
static void cmd_run(int argc, char *argv[])
{
    if (argc < 2) {
        uart_puts("usage: run <name> [<name> ...]\r\n");
        return;
    }

    /* First pass: validate every name before creating any task. */
    for (int i = 1; i < argc; ++i) {
        if (!program_find(argv[i])) {
            uart_puts("run: unknown program '");
            uart_puts(argv[i]);
            uart_puts("'  (nothing started)\r\n");
            return;
        }
    }

    /* Second pass: create tasks.  Each task's stack size is taken from
     * the program table (program_t.ram_bytes) — that's the "resources
     * needed by each program" the assignment asks for. */
    uart_puts("[shell] launching ");
    uart_put_dec((uint32_t)(argc - 1));
    uart_puts(" task(s):");
    for (int i = 1; i < argc; ++i) {
        const program_t *p = program_find(argv[i]);
        int rc = task_create(p->entry, p->name, p->ram_bytes);
        if (rc != 0) {
            uart_puts("\r\n[shell] task_create failed for ");
            uart_puts(p->name);
            uart_puts(" (rc=");
            uart_put_dec((uint32_t)-rc);
            uart_puts(", free=");
            uart_put_dec(mem_free_bytes());
            uart_puts(" B) — batch aborted\r\n");
            /* Drop partially-registered tasks so the next `run` starts
             * from an empty scheduler and a drained arena. */
            scheduler_reset();
            return;
        }
        uart_puts(" ");
        uart_puts(p->name);
    }
    uart_puts("\r\n");

    /* Show the arena layout right after all stacks have been reserved
     * and before scheduler_start(): once the scheduler starts running,
     * each task frees its stack as it exits, so by the time we return
     * to the shell prompt the arena is already empty again. This is
     * the only moment the user can observe the first-fit placement. */
    uart_puts("[shell] arena layout after allocation:\r\n");
    mem_dump();

    scheduler_start();          /* returns when every task has DONE */

    uart_puts("[shell] all tasks finished, arena drained\r\n");
}

/* --- Main loop ------------------------------------------------------- */

void shell_run(void)
{
    char line[SHELL_LINE_MAX];
    char *argv[SHELL_MAX_TOKENS];

    uart_puts("\r\nOS-Group08 shell ready. Type 'help' for commands.\r\n");

    for (;;) {
        uart_puts("os> ");
        int n = uart_gets(line, SHELL_LINE_MAX);
        if (n == 0) continue;                   /* empty line, redraw prompt */

        int argc = tokenise(line, argv, SHELL_MAX_TOKENS);
        if (argc == 0) continue;

        if      (str_eq(argv[0], "help")) cmd_help();
        else if (str_eq(argv[0], "list")) cmd_list();
        else if (str_eq(argv[0], "stat")) cmd_stat();
        else if (str_eq(argv[0], "run"))  cmd_run(argc, argv);
        else {
            uart_puts("shell: unknown command '");
            uart_puts(argv[0]);
            uart_puts("'. Type 'help'.\r\n");
        }
    }
}
