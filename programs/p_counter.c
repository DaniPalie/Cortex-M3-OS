/*
 * p_counter.c — User program: counts from 1 to 10.
 *
 * Prints each number on its own line, with a short busy-wait between
 * iterations so the interleaving is visible once the round-robin
 * scheduler comes in (phase 4).
 */

#include "../boot/uart.h"

/* Small delay purely to slow the loop enough to be observable.
 * `volatile` prevents the compiler from optimising the loop away. */
static void busy_wait(volatile unsigned n)
{
    while (n--) { __asm__ volatile ("nop"); }
}

void p_counter(void)
{
    for (unsigned i = 1; i <= 10; ++i) {
        uart_puts("  [p_counter] ");
        uart_put_dec(i);
        uart_puts("\r\n");
        busy_wait(200000);
    }
}
