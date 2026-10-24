/*
 * bootloader.c — Stage-1 bootloader for the OS-Group08 batch OS.
 *
 * Runs directly out of the first 4 KiB of flash (region BL_FLASH). The
 * Cortex-M3 core fetches two words at reset from address 0x00000000:
 *   [0x00] initial MSP                 -> _estack (top of SRAM)
 *   [0x04] address of Reset_Handler    -> bl_reset_handler (below)
 *
 * The bootloader must NOT rely on any RAM that requires initialisation
 * (no .data, no .bss globals), because the OS Reset_Handler is what
 * copies .data and zeroes .bss — and that will only happen AFTER we
 * chain-load the OS.
 *
 * Responsibilities:
 *   1. Print a short banner over UART0 so we can prove stage-1 ran.
 *   2. Relocate the vector table (VTOR) to the OS at 0x00001000.
 *   3. Reload MSP and jump to the OS Reset_Handler.
 *
 * All bootloader code/data lives in the .bl_text / .bl_rodata / .bl_vector
 * sections placed in BL_FLASH by linker.ld.
 */

#include <stdint.h>

/* --- Section attributes for BL_FLASH placement ------------------------- */
#define BL_TEXT    __attribute__((section(".bl_text"), used))
#define BL_RODATA  __attribute__((section(".bl_rodata"), used))

/* --- Symbols supplied by the linker ------------------------------------ */
extern uint32_t _estack;
extern uint32_t _os_flash_base;         /* address of OS vector table (0x00001000) */

/* --- Cortex-M3 System Control Block ------------------------------------ */
#define SCB_VTOR   (*(volatile uint32_t *)0xE000ED08)   /* Vector Table Offset Register */

/* --- Minimal inline UART for bootloader use ---------------------------- *
 * We do not call boot/uart.c from here to keep the two stages fully
 * decoupled: the bootloader is standalone and could be flashed alone for
 * bring-up. QEMU's UART0 works without any init, so a raw poll+write is
 * enough.  A real board would need clock gating + baud setup as done in
 * uart_init(); the OS will run that later.
 */
#define UART0_DR   (*(volatile uint32_t *)0x4000C000)
#define UART0_FR   (*(volatile uint32_t *)0x4000C018)
#define UART_TXFF  (1u << 5)

BL_TEXT static void bl_putc(char c)
{
    while (UART0_FR & UART_TXFF) { /* wait for TX FIFO space */ }
    UART0_DR = (uint8_t)c;
}

BL_TEXT static void bl_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') bl_putc('\r');
        bl_putc(*s++);
    }
}

/* --- Chain-load the OS -------------------------------------------------- *
 * Steps (exactly the procedure described in the ARM Cortex-M3 Generic
 * User Guide, §"Relocating the vector table"):
 *   1. Point VTOR at the OS vector table.
 *   2. Load MSP from OS_VT[0].
 *   3. Jump to OS_VT[1] (OS Reset_Handler).
 *
 * The __attribute__((noreturn)) tells the compiler we never come back,
 * so it does not need to preserve any register or emit a return sequence.
 */
BL_TEXT __attribute__((noreturn))
static void bl_chainload_os(uint32_t os_vt_addr)
{
    volatile uint32_t *os_vt = (volatile uint32_t *)os_vt_addr;

    /* 1. Relocate vector table. dsb+isb ensure the new VTOR takes effect
     *    before any subsequent exception is taken. */
    SCB_VTOR = os_vt_addr;
    __asm__ volatile ("dsb\n\tisb" ::: "memory");

    /* 2 + 3. Load OS MSP and branch to OS Reset_Handler. */
    uint32_t os_msp   = os_vt[0];
    uint32_t os_entry = os_vt[1];
    __asm__ volatile (
        "msr msp, %0     \n\t"
        "bx  %1          \n\t"
        :
        : "r"(os_msp), "r"(os_entry)
        : "memory"
    );
    __builtin_unreachable();
}

/* --- Bootloader entry point (Reset_Handler for stage-1) ---------------- */
BL_TEXT __attribute__((noreturn))
void bl_reset_handler(void)
{
    bl_puts("\r\n[BL] stage-1 bootloader on Cortex-M3 (lm3s6965evb)\r\n");
    bl_puts("[BL] hardware minimum ready, chain-loading OS at 0x00001000...\r\n\r\n");

    bl_chainload_os((uint32_t)&_os_flash_base);

    /* Unreachable, but the compiler wants a terminator. */
    for (;;) { }
}

/* --- Stage-1 vector table (placed at address 0x00000000) --------------- *
 * We only populate the entries that can fire before we chain-load:
 *   [0] initial MSP
 *   [1] Reset_Handler
 *   [2] NMI  -> spin  (would only fire on external NMI signal, absent in QEMU)
 *   [3] HardFault -> spin
 * Anything else cannot fire because interrupts stay masked until the OS
 * enables them.
 */
BL_TEXT __attribute__((noreturn))
static void bl_fault_spin(void)
{
    bl_puts("\r\n[BL] fatal fault in stage-1, halting.\r\n");
    for (;;) { __asm__ volatile ("wfi"); }
}

__attribute__((section(".bl_vector"), used))
void (* const bl_vector_table[])(void) =
{
    (void (*)(void)) &_estack,   /* 0: initial MSP */
    bl_reset_handler,            /* 1: Reset       */
    bl_fault_spin,               /* 2: NMI         */
    bl_fault_spin                /* 3: HardFault   */
};
