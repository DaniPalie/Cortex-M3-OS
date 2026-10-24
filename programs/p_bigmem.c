/*
 * p_bigmem.c — Deliberately large-stack test program.
 *
 * Its ram_bytes budget in the program_table (40 KiB) is chosen so a
 * single instance still fits in the ~60 KiB user arena, but two
 * instances at the same time cannot. This is the fastest way to
 * exercise the "RAM insufficient" error path of mem_alloc /
 * task_create / cmd_run: try
 *
 *     run p_bigmem p_bigmem
 *
 * and observe the shell abort the batch with a clear diagnostic.
 */

#include "../boot/uart.h"

void p_bigmem(void)
{
    uart_puts("  [p_bigmem] running with a large stack allocation\r\n");
}
