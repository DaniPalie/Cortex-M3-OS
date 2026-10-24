/*
 * uart.h — Public interface for the UART0 driver (Stellaris lm3s6965).
 *
 * QEMU wires UART0 to the process stdio, so anything we write to UARTDR
 * appears on the terminal, and anything typed on stdin arrives at UARTDR.
 */

#ifndef OS_UART_H
#define OS_UART_H

#include <stdint.h>

void uart_init(void);                     /* enable UART0 (safe to call once) */
void uart_putc(char c);                   /* blocking TX of one character */
void uart_puts(const char *s);            /* blocking TX of a NUL-terminated string */
void uart_put_hex(uint32_t v);            /* debug helper: print 32-bit as 8 hex digits */
void uart_put_dec(uint32_t v);            /* debug helper: print unsigned decimal */

int  uart_rx_ready(void);                 /* non-blocking: 1 if a byte is available */
char uart_getc(void);                     /* blocking RX of one character */

/* Blocking line reader: fills buf (up to size-1 chars) until CR/LF is seen,
 * echoes each byte back, handles backspace. Always NUL-terminates.
 * Returns the number of characters stored (not counting the terminator). */
int  uart_gets(char *buf, int size);

#endif /* OS_UART_H */
