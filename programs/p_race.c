/*
 * p_race.c — Two user programs that write to the UART WITHOUT the
 *            console mutex.  Meant to be run together, e.g.
 *
 *     run p_race_A p_race_B
 *
 * The busy_wait between characters is deliberately short so a
 * SysTick preempts each program mid-line, making the interleaving
 * on the shared UART unmistakable.  Compare with p_safe_[AB] to
 * see the mutex fix.
 */

#include "../boot/uart.h"

#define ROUNDS      3
#define WIDTH      20
#define CHAR_DELAY 50000    /* ~ several SysTick ticks per line so
                             * preemption lands mid-string, guaranteed */

static void busy_wait(volatile unsigned n)
{
    while (n--) __asm__ volatile ("nop");
}

/* Emit "[<tag>: XXXX...X]\r\n" one character at a time.  Everything
 * that appears between the '[' and the ']' should belong to the same
 * program, but no synchronisation guarantees that. */
static void racy_line(char tag)
{
    uart_putc('[');
    uart_putc(tag);
    uart_putc(':');
    uart_putc(' ');
    for (int i = 0; i < WIDTH; ++i) {
        uart_putc(tag);
        busy_wait(CHAR_DELAY);
    }
    uart_putc(']');
    uart_putc('\r');
    uart_putc('\n');
}

void p_race_A(void)
{
    for (int r = 0; r < ROUNDS; ++r) { racy_line('A'); busy_wait(20000); }
}

void p_race_B(void)
{
    for (int r = 0; r < ROUNDS; ++r) { racy_line('B'); busy_wait(20000); }
}
