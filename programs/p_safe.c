/*
 * p_safe.c — The same two demo programs as p_race.c, but wrapping
 *            each output line in mutex_lock/unlock of the global
 *            console_mutex.  Run as:
 *
 *     run p_safe_A p_safe_B
 *
 * Expected behaviour:  no interleaving inside any [<tag>: ...] line.
 * Round-robin still preempts the tasks, but any preemption that
 * happens while a task is inside the critical section leaves the
 * mutex held — the other task hits the lock, blocks (task state
 * moves to TASK_BLOCKED), and yields; the scheduler runs the
 * first task to completion of its line before waking the waiter.
 */

#include "../boot/uart.h"
#include "../mm/mutex.h"

#define ROUNDS     3
#define WIDTH     20
#define CHAR_DELAY 50000     /* same magnitude as p_race so preemption
                              * mid-line is guaranteed; the mutex must
                              * still keep the output atomic */

static void busy_wait(volatile unsigned n)
{
    while (n--) __asm__ volatile ("nop");
}

static void safe_line(char tag)
{
    mutex_lock(&console_mutex);

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

    mutex_unlock(&console_mutex);
}

void p_safe_A(void)
{
    for (int r = 0; r < ROUNDS; ++r) { safe_line('A'); busy_wait(20000); }
}

void p_safe_B(void)
{
    for (int r = 0; r < ROUNDS; ++r) { safe_line('B'); busy_wait(20000); }
}
