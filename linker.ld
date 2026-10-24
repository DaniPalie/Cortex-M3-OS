/*
 * linker.ld — Memory map for the OS-Group08 batch OS
 *
 * Target: ARM Cortex-M3, QEMU machine "lm3s6965evb"
 *
 * FLASH is split in two contiguous regions so the "two-stage boot" is
 * visible directly in the memory map:
 *
 *   0x00000000  BL_FLASH  4 KiB   Stage-1 bootloader
 *     0x0000..0x000F      : bl_vector_table (initial MSP + Reset_Handler)
 *     0x0010..           : bootloader code (bl_reset_handler, chain-loader)
 *
 *   0x00001000  OS_FLASH  252 KiB Stage-2 operating system
 *     0x1000..0x10XX      : isr_vector_table (full Cortex-M3 exceptions)
 *     0x10XX..           : kernel, shell, mm, programs (XIP)
 *
 * SRAM (64 KiB @ 0x20000000) is shared by both stages:
 *
 *   [ .data ][ .bss ] [ USER ARENA (mm/alloc.c) ] [ MSP stack, 4 KiB ]
 *                    ^_user_arena_start           ^_user_arena_end   ^_estack
 *
 * Symbols exported to C:
 *   _sidata, _sdata, _edata, _sbss, _ebss  : sections copied/zeroed by OS Reset_Handler
 *   _user_arena_start, _user_arena_end     : bounds used by mm/alloc.c
 *   _estack                                : initial Main Stack Pointer
 *   _os_flash_base                         : where the OS vector table lives (used by BL)
 */

MEMORY
{
    BL_FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4K
    OS_FLASH (rx)  : ORIGIN = 0x00001000, LENGTH = 256K - 4K
    SRAM     (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

/* Kernel stack size and OS entry point address, exported to code. */
_kernel_stack_size = 4K;
_estack            = ORIGIN(SRAM) + LENGTH(SRAM);
_os_flash_base     = ORIGIN(OS_FLASH);

SECTIONS
{
    /* ===================================================================
     *  Stage-1 bootloader (must live at address 0x00000000)
     * =================================================================== */

    .bl_vector 0x00000000 :
    {
        KEEP(*(.bl_vector))       /* bl_vector_table only */
    } > BL_FLASH

    .bl_text :
    {
        *(.bl_text*)              /* bootloader code (see boot/bootloader.c) */
        *(.bl_rodata*)
        . = ALIGN(4);
    } > BL_FLASH

    /* ===================================================================
     *  Stage-2 OS (starts at 0x00001000; VTOR is moved here by stage-1)
     * =================================================================== */

    .isr_vector 0x00001000 :
    {
        KEEP(*(.isr_vector))      /* OS vector table (all 16 sys exceptions) */
    } > OS_FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > OS_FLASH

    /* ===================================================================
     *  SRAM: initialised data, zero-init data, user arena
     * =================================================================== */

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> OS_FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _ebss = .;
        _user_arena_start = .;    /* arena starts right after .bss */
    } > SRAM

    _user_arena_end = ORIGIN(SRAM) + LENGTH(SRAM) - _kernel_stack_size;

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.ARM.extab*) }
}

/* Link-time sanity checks. */
ASSERT(_user_arena_start < _user_arena_end, "SRAM exhausted: no room for user arena");
ASSERT(SIZEOF(.bl_vector) + SIZEOF(.bl_text) <= LENGTH(BL_FLASH), "Bootloader too big for BL_FLASH");
