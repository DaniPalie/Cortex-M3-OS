/*
 * uart.c — Minimal driver for UART0 of the Stellaris lm3s6965 in QEMU.
 *
 * Register map (subset used here, cf. Stellaris LM3S6965 datasheet §12):
 *   UART0_BASE = 0x4000C000
 *     +0x000  UARTDR   Data register (TX write / RX read)
 *     +0x018  UARTFR   Flag register (TXFF bit 5, RXFE bit 4, BUSY bit 3)
 *     +0x024  UARTIBRD Integer baud rate divisor
 *     +0x028  UARTFBRD Fractional baud rate divisor
 *     +0x02C  UARTLCRH Line control (word length, FIFO enable)
 *     +0x030  UARTCTL  Control (UARTEN bit 0, TXE bit 8, RXE bit 9)
 *
 * Notes on QEMU's model:
 *   - UART0 is wired to stdio when qemu is launched with `-nographic`.
 *   - Clock gating (SYSCTL RCGC1/RCGC2) is NOT enforced by QEMU, so writes
 *     to UARTDR would work even without uart_init(). We still perform the
 *     init sequence because it is what a real board requires and it makes
 *     the code correct in principle, not just "works because QEMU is lax".
 */

#include "uart.h"

/* --- Register addresses ------------------------------------------------ */
#define UART0_BASE   0x4000C000u
#define UART0_DR     (*(volatile uint32_t *)(UART0_BASE + 0x000))
#define UART0_FR     (*(volatile uint32_t *)(UART0_BASE + 0x018))
#define UART0_IBRD   (*(volatile uint32_t *)(UART0_BASE + 0x024))
#define UART0_FBRD   (*(volatile uint32_t *)(UART0_BASE + 0x028))
#define UART0_LCRH   (*(volatile uint32_t *)(UART0_BASE + 0x02C))
#define UART0_CTL    (*(volatile uint32_t *)(UART0_BASE + 0x030))

/* System control (clock gating) — only used symbolically here */
#define SYSCTL_RCGC1 (*(volatile uint32_t *)0x400FE104)   /* UART clock enable */
#define SYSCTL_RCGC2 (*(volatile uint32_t *)0x400FE108)   /* GPIO clock enable */

/* Flag register bits */
#define UART_FR_TXFF (1u << 5)   /* TX FIFO full           */
#define UART_FR_RXFE (1u << 4)   /* RX FIFO empty          */
#define UART_FR_BUSY (1u << 3)   /* UART busy transmitting */

/* Line control bits */
#define UART_LCRH_WLEN_8 (0x3u << 5)   /* 8 data bits            */
#define UART_LCRH_FEN    (1u   << 4)   /* enable FIFOs           */

/* Control bits */
#define UART_CTL_UARTEN  (1u << 0)
#define UART_CTL_TXE     (1u << 8)
#define UART_CTL_RXE     (1u << 9)

/* --- Public API -------------------------------------------------------- */

void uart_init(void)
{
    /* 1. Enable clocks (harmless on QEMU, required on real silicon). */
    SYSCTL_RCGC1 |= (1u << 0);   /* UART0 module clock */
    SYSCTL_RCGC2 |= (1u << 0);   /* GPIOA clock (UART0 pins live on PA0/PA1) */

    /* 2. Disable UART while reconfiguring. */
    UART0_CTL = 0;

    /* 3. Set baud rate 115200 assuming 16 MHz clock (BRD = 16e6 / (16*115200)
     *    = 8.6805..., IBRD=8, FBRD=round(0.6805*64)=44). QEMU ignores baud. */
    UART0_IBRD = 8;
    UART0_FBRD = 44;

    /* 4. 8N1, FIFOs enabled. */
    UART0_LCRH = UART_LCRH_WLEN_8 | UART_LCRH_FEN;

    /* 5. Re-enable, with both TX and RX. */
    UART0_CTL = UART_CTL_UARTEN | UART_CTL_TXE | UART_CTL_RXE;
}

void uart_putc(char c)
{
    while (UART0_FR & UART_FR_TXFF) { /* wait until TX FIFO has room */ }
    UART0_DR = (uint8_t) c;
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') uart_putc('\r');   /* poor man's CRLF conversion */
        uart_putc(*s++);
    }
}

void uart_put_hex(uint32_t v)
{
    static const char digits[] = "0123456789abcdef";
    uart_putc('0'); uart_putc('x');
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(digits[(v >> i) & 0xF]);
    }
}

void uart_put_dec(uint32_t v)
{
    char buf[11];                        /* enough for 2^32 -> "4294967295" */
    int i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v && i < (int)sizeof(buf)) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i--) uart_putc(buf[i]);
}

int uart_rx_ready(void)
{
    return (UART0_FR & UART_FR_RXFE) == 0;
}

char uart_getc(void)
{
    while (UART0_FR & UART_FR_RXFE) { /* wait until RX FIFO has a byte */ }
    return (char)(UART0_DR & 0xFF);
}

int uart_gets(char *buf, int size)
{
    int i = 0;
    if (size < 1) return 0;

    for (;;) {
        char c = uart_getc();

        /* Enter -> end of line. Some terminals send CR, some LF, some CRLF. */
        if (c == '\r' || c == '\n') {
            uart_puts("\r\n");
            buf[i] = '\0';
            return i;
        }

        /* Backspace / DEL — erase the last character if there is one. */
        if (c == 0x08 || c == 0x7F) {
            if (i > 0) {
                i--;
                uart_puts("\b \b");    /* move back, overwrite with space, move back */
            }
            continue;
        }

        /* Printable ASCII: store and echo, respecting buffer capacity. */
        if (i < size - 1 && c >= 0x20 && c < 0x7F) {
            buf[i++] = c;
            uart_putc(c);
        }
    }
}
