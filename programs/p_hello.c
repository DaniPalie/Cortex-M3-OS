/*
 * p_hello.c — Trivial user program.
 *
 * Prints a short greeting and returns. Used as the simplest smoke test
 * for the shell -> program dispatch path.
 */

#include "../boot/uart.h"

void p_hello(void)
{
    uart_puts("  [p_hello] hello from user program!\r\n");
}
