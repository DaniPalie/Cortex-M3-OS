/*
 * context_switch.S — PendSV_Handler for the Cortex-M3 scheduler.
 *
 * Whenever a context switch is needed (SysTick tick, or task_exit()),
 * the C code pends PendSV. PendSV has the lowest priority so it only
 * runs once every other interrupt has finished — that keeps ISR
 * latency low and lets this handler do the heavy work at leisure.
 *
 * On entry the CPU has already stacked (on PSP, because the interrupted
 * code was a task in Thread+PSP mode — except for the very first entry,
 * which comes from Thread+MSP and pushes on MSP; that MSP frame lives
 * until scheduler_start eventually returns):
 *     r0 r1 r2 r3 r12 LR PC xPSR    (8 words, "hw-saved" frame)
 *
 * Our job is to also save the "sw-saved" registers r4-r11 on the
 * outgoing PSP, ask C for the incoming task's PSP, restore that task's
 * r4-r11, and exception-return with EXC_RETURN=0xFFFFFFFD so the CPU
 * pops the hw-saved frame from the new PSP and jumps into the task.
 *
 * If scheduler_pick_next() returns 0, no task is left READY: we
 * exception-return with EXC_RETURN=0xFFFFFFF9 (Thread + MSP), which
 * pops the MSP frame that was pushed on the first PendSV entry —
 * putting execution back where scheduler_start() had pended us.
 */

    .syntax unified
    .cpu    cortex-m3
    .thumb

    .extern scheduler_pick_next

    .thumb_func
    .global PendSV_Handler
    .type   PendSV_Handler, %function

PendSV_Handler:
    /* r0 = current PSP (the outgoing task's saved SP; garbage on the
     *      very first entry — see initial_psp_scratch in scheduler.c). */
    mrs     r0, psp

    /* Save r4-r11 on the outgoing task's stack. On the first entry this
     * writes into initial_psp_scratch, harmlessly. r0 -= 32. */
    stmdb   r0!, {r4-r11}

    /* Call scheduler_pick_next(r0). It preserves r4-r11 per AAPCS.
     * Save/restore LR (EXC_RETURN) across the call — BL clobbers LR. */
    push    {lr}
    bl      scheduler_pick_next
    pop     {lr}

    /* r0 = new PSP, or 0 if the scheduler wants us to return to shell. */
    cmp     r0, #0
    beq     .Lreturn_to_shell

    /* Normal context switch: restore r4-r11 from the incoming stack,
     * update PSP, and exception-return into the task. We force
     * EXC_RETURN=0xFFFFFFFD (Thread+PSP) explicitly: on the FIRST
     * PendSV entry we came from Thread+MSP so LR would be
     * 0xFFFFFFF9 (return to MSP), which would undo the switch. */
    ldmia   r0!, {r4-r11}
    msr     psp, r0
    ldr     lr, =0xFFFFFFFD
    bx      lr

.Lreturn_to_shell:
    /* All tasks DONE. Return to Thread+MSP using EXC_RETURN=0xFFFFFFF9.
     * The hardware will pop the MSP frame that was pushed when the very
     * first PendSV was taken from scheduler_start(). */
    ldr     lr, =0xFFFFFFF9
    bx      lr

    .size   PendSV_Handler, . - PendSV_Handler
