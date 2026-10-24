/*
 * startup.c — Stage-2 (OS) reset handler + vector table for ARM Cortex-M3.
 *
 * This file is entered ONLY after the stage-1 bootloader (boot/bootloader.c)
 * has relocated VTOR to 0x00001000 and jumped to Reset_Handler. When we get
 * here, MSP has just been reloaded from isr_vector_table[0] and no RAM has
 * been initialised yet — that is our job below.
 *
 * We provide the full system-exception block (indices 0..15) so later phases
 * (scheduler) can install real handlers for SVCall, PendSV and SysTick just
 * by defining a strong symbol with the same name — the weak aliases here get
 * silently overridden at link time.
 *
 * External IRQs (index 16+) are not populated: this bare-metal target uses
 * only SysTick from the "system" block; any other IRQ firing would be a bug.
 */

#include <stdint.h>

/* Symbols provided by linker.ld ----------------------------------------- */
extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata;
extern uint32_t _sbss,   _ebss;

/* OS entry point (defined in kernel/main.c) ----------------------------- */
extern int main(void);

/* Forward declarations -------------------------------------------------- */
void Reset_Handler(void);
void Default_Handler(void);

/* Weak aliases: any module can override these by defining a strong symbol
 * with the same name (e.g. kernel/scheduler.c will provide the real
 * SysTick_Handler and PendSV_Handler in Phase 4).
 */
#define WEAK_ALIAS __attribute__((weak, alias("Default_Handler")))

void NMI_Handler(void)          WEAK_ALIAS;
void HardFault_Handler(void)    WEAK_ALIAS;
void MemManage_Handler(void)    WEAK_ALIAS;
void BusFault_Handler(void)     WEAK_ALIAS;
void UsageFault_Handler(void)   WEAK_ALIAS;
void SVC_Handler(void)          WEAK_ALIAS;
void DebugMon_Handler(void)     WEAK_ALIAS;
void PendSV_Handler(void)       WEAK_ALIAS;
void SysTick_Handler(void)      WEAK_ALIAS;

/* --------------------------------------------------------------------- *
 *  Vector table                                                         *
 * --------------------------------------------------------------------- */

__attribute__((section(".isr_vector"), used))
void (* const vector_table[])(void) =
{
    (void (*)(void)) &_estack,   /*  0: Initial MSP                       */
    Reset_Handler,               /*  1: Reset                             */
    NMI_Handler,                 /*  2: NMI                               */
    HardFault_Handler,           /*  3: Hard fault                        */
    MemManage_Handler,           /*  4: Memory management fault           */
    BusFault_Handler,            /*  5: Bus fault                         */
    UsageFault_Handler,          /*  6: Usage fault                       */
    0, 0, 0, 0,                  /*  7-10: reserved                       */
    SVC_Handler,                 /* 11: SVCall                            */
    DebugMon_Handler,            /* 12: Debug monitor                     */
    0,                           /* 13: reserved                          */
    PendSV_Handler,              /* 14: PendSV — context switch trampoline */
    SysTick_Handler              /* 15: SysTick — scheduler tick          */
};

/* --------------------------------------------------------------------- *
 *  Reset_Handler                                                        *
 * --------------------------------------------------------------------- *
 * 1. copy .data from FLASH (LMA) to SRAM (VMA);
 * 2. zero .bss;
 * 3. jump to main().
 * We do NOT set MSP here — the CPU already loaded it from vector_table[0].
 */
void Reset_Handler(void)
{
    /* .data: initialised globals -------------------------------------- */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* .bss: zero-initialised globals ---------------------------------- */
    for (dst = &_sbss; dst < &_ebss; ) {
        *dst++ = 0;
    }

    /* Jump to the OS entry point. Should never return; if it does, spin. */
    (void) main();
    for (;;) { __asm__ volatile ("wfi"); }
}

/* --------------------------------------------------------------------- *
 *  Default_Handler                                                      *
 * --------------------------------------------------------------------- *
 * Catch-all for unimplemented exceptions. Traps here means an unexpected
 * fault fired — halt so gdb (via `qemu -s -S`) can inspect the state.
 */
void Default_Handler(void)
{
    for (;;) { __asm__ volatile ("bkpt #0"); }
}
