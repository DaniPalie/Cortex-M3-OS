/*
 * p_alphabet.c — User program: prints the lowercase alphabet.
 *
 * Emits 'a b c ... z' followed by a newline. Together with p_counter
 * and p_hello it gives the scheduler three distinct output patterns
 * that are easy to tell apart in the interleaved log.
 */

#include "../boot/uart.h"

static void busy_wait(volatile unsigned n)
{
    while (n--) { __asm__ volatile ("nop"); }
}

void p_alphabet(void)
{
    uart_puts("  [p_alphabet] ");
    for (char c = 'a'; c <= 'z'; ++c) {
        uart_putc(c);
        uart_putc(' ');
        busy_wait(80000);
    }
    uart_puts("\r\n");
}
