/*
 * main.c — OS entry point (stage-2, reached after chain-load from stage-1).
 *
 * Phase 3 responsibility: bring up the UART, print the boot banner,
 * then hand control to the interactive shell for good. shell_run()
 * never returns.
 */

#include <stdint.h>
#include "../boot/uart.h"
#include "../shell/shell.h"

extern unsigned long _user_arena_start;
extern unsigned long _user_arena_end;

static void print_banner(void)
{
    uart_puts("========================================\r\n");
    uart_puts(" OS-Group08 — Cortex-M3 Batch OS\r\n");
    uart_puts(" Full feature set: boot + shell + RR sched + mutex\r\n");
    uart_puts("========================================\r\n");

    /* VTOR was moved to 0x00001000 by the bootloader; print it back so we
     * can visually confirm the chain-load actually happened. */
    uint32_t vtor = *(volatile uint32_t *)0xE000ED08;
    uart_puts(" VTOR             = ");
    uart_put_hex(vtor);

    uart_puts("\r\n user arena start = ");
    uart_put_hex((unsigned long)&_user_arena_start);
    uart_puts("\r\n user arena end   = ");
    uart_put_hex((unsigned long)&_user_arena_end);
    uart_puts("\r\n user arena size  = ");
    uart_put_dec((unsigned long)&_user_arena_end - (unsigned long)&_user_arena_start);
    uart_puts(" bytes\r\n");
}

int main(void)
{
    uart_init();
    print_banner();

    shell_run();     /* never returns */

    for (;;) { __asm__ volatile ("wfi"); }
    return 0;
}
