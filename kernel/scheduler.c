/*
 * scheduler.c — Round-robin scheduler + bump allocator for task stacks.
 *
 * See scheduler.h for the model. Reading order inside this file:
 *
 *   1) Cortex-M3 register aliases and constants used to program
 *      SysTick and the System Control Block (SCB).
 *   2) The TCB table and internal state.
 *   3) A tiny bump allocator into the user arena (replaced by
 *      mm/alloc.c first-fit in phase 5).
 *   4) task_create() — allocates a stack and pre-loads the exception
 *      frame the CPU will pop when we exception-return into the task.
 *   5) task_exit() — end-of-task cleanup, self-triggers PendSV.
 *   6) scheduler_pick_next() — the C half of the context switch, called
 *      by the assembly PendSV_Handler in context_switch.S.
 *   7) SysTick_Handler() — the "tick": just marks PendSV as pending.
 *   8) scheduler_start() — configures SysTick + PendSV priorities, sets
 *      an initial PSP scratch, kicks the first PendSV, and waits (WFI)
 *      until every task has finished, at which point control naturally
 *      falls back into the shell via EXC_RETURN=0xFFFFFFF9.
 */

#include "scheduler.h"
#include "../boot/uart.h"
#include "../mm/alloc.h"
#include "../mm/mutex.h"

/* --- 1. Cortex-M3 SCB / SysTick registers ---------------------------- */
#define SYST_CSR   (*(volatile uint32_t *)0xE000E010)  /* control + status */
#define SYST_RVR   (*(volatile uint32_t *)0xE000E014)  /* reload value     */
#define SYST_CVR   (*(volatile uint32_t *)0xE000E018)  /* current value    */

#define SCB_ICSR   (*(volatile uint32_t *)0xE000ED04)  /* interrupt ctrl+state */
#define SCB_SHPR3  (*(volatile uint32_t *)0xE000ED20)  /* handler priorities 12..15 */

#define SYST_CSR_ENABLE     (1u <<  0)
#define SYST_CSR_TICKINT    (1u <<  1)
#define SYST_CSR_CLKSOURCE  (1u <<  2)   /* 1 = processor clock */

#define SCB_ICSR_PENDSVSET  (1u << 28)

/* PendSV priority stays lowest (0xFF) so it always defers to real work.
 * SysTick priority is one step up (0xE0) so a pending SysTick preempts
 * a running task cleanly. */
#define PRIO_SYSTICK 0xE0u
#define PRIO_PENDSV  0xFFu

/* On the QEMU lm3s6965evb the CPU tick is much faster than a real chip.
 * A small reload keeps the round-robin very visible in short demos. */
#define SYSTICK_RELOAD 50000u

/* --- 2. Task table --------------------------------------------------- */
#define MAX_TASKS 8

typedef struct {
    uint32_t     *sp;           /* saved PSP */
    task_state_t  state;
    const char   *name;
    void        (*entry)(void);
    uint32_t     *stack_base;
    uint32_t      stack_size;
} tcb_t;

static tcb_t    tasks[MAX_TASKS];
static int      num_tasks     = 0;
static volatile int current_task = -1;    /* index in tasks[] currently RUNNING */
static volatile int first_switch = 1;     /* set to 0 after the very first PendSV */
static volatile int all_done    = 0;      /* set by scheduler_pick_next() when no READY left */

/* Scratch buffer used only for the very first PendSV entry: the initial
 * PSP points here so the compulsory stmdb r0!, {r4-r11} in the handler
 * has a harmless place to write. */
static uint32_t initial_psp_scratch[16] __attribute__((aligned(8)));

/* Stack allocation goes through mm/alloc.c (first-fit contiguous).
 * Every task_create() reserves one region owned by the task's name;
 * task_exit() frees it, and scheduler_start() drains the table via
 * mem_init() when the last task is DONE. */

/* --- 4. task_create -------------------------------------------------- *
 * Layout of the initial stack we prepare (grows down; top = highest addr).
 * When PendSV_Handler does its `ldmia sp!, {r4-r11}` then the CPU pops
 * the hardware frame on exception return, the task starts running at
 * `entry` with LR = task_exit.
 *
 *   +64  xPSR   = 0x01000000 (Thumb bit — MANDATORY)
 *   +60  PC     = entry
 *   +56  LR     = task_exit
 *   +52  r12    = 0
 *   +48  r3     = 0
 *   +44  r2     = 0
 *   +40  r1     = 0
 *   +36  r0     = 0                        <-- hw-saved frame ends here
 *   +32  r11    = 0
 *   +28  r10    = 0
 *   +24  r9     = 0
 *   +20  r8     = 0
 *   +16  r7     = 0
 *   +12  r6     = 0
 *   +8   r5     = 0
 *   +4   r4     = 0                        <-- sw-saved frame ends here
 *   +0   <-- TCB->sp points here
 */
int task_create(void (*entry)(void), const char *name, uint32_t stack_bytes)
{
    if (num_tasks >= MAX_TASKS) return -1;

    stack_bytes = (stack_bytes + 7u) & ~7u;                 /* 8-byte round-up */
    uint32_t *stack = (uint32_t *) mem_alloc(stack_bytes, name);
    if (!stack) return -2;

    tcb_t *t = &tasks[num_tasks];
    t->name       = name;
    t->entry      = entry;
    t->state      = TASK_READY;
    t->stack_base = stack;
    t->stack_size = stack_bytes;

    /* Top of stack (exclusive), then push the 16-word initial frame. */
    uint32_t *sp = stack + (stack_bytes / 4);

    *(--sp) = 0x01000000u;              /* xPSR: Thumb bit set          */
    *(--sp) = (uint32_t) entry;         /* PC: task entry               */
    *(--sp) = (uint32_t) task_exit;     /* LR: fallback when entry ret  */
    *(--sp) = 0; *(--sp) = 0;           /* r12, r3                      */
    *(--sp) = 0; *(--sp) = 0;           /* r2, r1                       */
    *(--sp) = 0;                        /* r0                           */
    for (int i = 0; i < 8; ++i) *(--sp) = 0;  /* r11..r4                */

    t->sp = sp;
    num_tasks++;
    return 0;
}

/* --- 5. task_exit ---------------------------------------------------- *
 * A task's entry is called with LR=task_exit (see task_create), so when
 * the entry returns we land here.  Flag DONE and trigger PendSV so the
 * scheduler picks somebody else (or returns to the shell if all done). */
void task_exit(void)
{
    /* Just flag the task DONE.  The stack region is freed by the
     * scheduler in the next PendSV, running on MSP, so we never free
     * the very stack we are currently using. */
    if (current_task >= 0) tasks[current_task].state = TASK_DONE;
    SCB_ICSR = SCB_ICSR_PENDSVSET;
    __asm__ volatile ("dsb\n\tisb");
    /* We wait here until PendSV switches us out; we will not return.   */
    for (;;) { __asm__ volatile ("wfi"); }
}

/* --- 6. scheduler_pick_next -----------------------------------------
 * Called by PendSV_Handler (see context_switch.S) with r0 = the saved
 * PSP of the task that was just running.  Returns the PSP of the task
 * that should run next, or 0 to signal "no READY task, return to shell".
 */
uint32_t *scheduler_pick_next(uint32_t *current_psp)
{
    /* Bookkeeping for the outgoing task:
     *   - RUNNING -> READY : normal preemption; save PSP so we can
     *                       resume where we left off next time.
     *   - BLOCKED         : task yielded voluntarily (mutex slow path).
     *                       Save PSP too — otherwise, when unlock
     *                       eventually flips us back to READY, PendSV
     *                       would restore a stale frame and HardFault.
     *   - DONE            : task exited; return its stack to the
     *                       allocator now, safely on MSP.
     *   - (first PendSV)  : no outgoing task, just clear the flag.
     */
    if (!first_switch) {
        if (current_task >= 0) {
            task_state_t st = tasks[current_task].state;
            if (st == TASK_RUNNING) {
                tasks[current_task].sp    = current_psp;
                tasks[current_task].state = TASK_READY;
            } else if (st == TASK_BLOCKED) {
                tasks[current_task].sp    = current_psp;
            } else if (st == TASK_DONE && tasks[current_task].stack_base) {
                mem_free(tasks[current_task].stack_base);
                tasks[current_task].stack_base = 0;
            }
        }
    } else {
        first_switch = 0;
    }

    /* Round-robin scan starting from the slot AFTER the current one.
     * BLOCKED tasks are skipped; only READY tasks are candidates. */
    int start = (current_task + 1) % (num_tasks > 0 ? num_tasks : 1);
    for (int i = 0; i < num_tasks; ++i) {
        int idx = (start + i) % num_tasks;
        if (tasks[idx].state == TASK_READY) {
            current_task = idx;
            tasks[idx].state = TASK_RUNNING;
            return tasks[idx].sp;
        }
    }

    /* No READY task.  Two sub-cases:
     *   - some task is BLOCKED  -> we are deadlocked (holder never
     *     released the mutex). Log it and unwind to the shell.
     *   - none blocked           -> normal end of batch. */
    for (int i = 0; i < num_tasks; ++i) {
        if (tasks[i].state == TASK_BLOCKED) {
            uart_puts("[sched] DEADLOCK: task blocked with nobody to wake it\r\n");
            break;
        }
    }
    all_done = 1;
    return 0;
}

/* --- 7. SysTick_Handler ---------------------------------------------- *
 * Priority PRIO_SYSTICK, higher than PendSV. We only mark PendSV as
 * pending so the actual context switch runs at PendSV priority, minimum,
 * with no other exception waiting. */
void SysTick_Handler(void)
{
    SCB_ICSR = SCB_ICSR_PENDSVSET;
}

/* --- Helpers exported to mm/mutex.c ---------------------------------- */

int current_task_id(void) { return current_task; }

void task_block(int id)
{
    if (id < 0 || id >= num_tasks) return;
    if (tasks[id].state == TASK_RUNNING || tasks[id].state == TASK_READY) {
        tasks[id].state = TASK_BLOCKED;
    }
}

void task_wake(int id)
{
    if (id < 0 || id >= num_tasks) return;
    if (tasks[id].state == TASK_BLOCKED) {
        tasks[id].state = TASK_READY;
    }
}

void sched_yield(void)
{
    SCB_ICSR = SCB_ICSR_PENDSVSET;
    __asm__ volatile ("dsb\n\tisb");
}

/* --- 8. scheduler_start ---------------------------------------------- *
 * Bootstrap sequence, running in Thread+MSP:
 *   a. set SysTick + PendSV priorities in SHPR3;
 *   b. arm SysTick (processor clock, ticked interrupts, running);
 *   c. seed PSP with initial_psp_scratch so the first stmdb r0!,{r4-r11}
 *      in PendSV_Handler writes to a harmless place;
 *   d. pend PendSV — it fires immediately since we are in Thread mode
 *      with nothing higher active.  The handler picks task 0 and
 *      exception-returns into it (EXC_RETURN=0xFFFFFFFD).
 *   e. WFI-loop until scheduler_pick_next() flags all_done.  When the
 *      last task exits, PendSV returns with EXC_RETURN=0xFFFFFFF9 which
 *      unwinds back to the exception frame the hardware pushed on MSP
 *      the very first time PendSV was entered here — that puts the CPU
 *      right after our initial "pend PendSV" instruction.
 *   f. disable SysTick, reset state so the shell can run another batch.
 */
void scheduler_start(void)
{
    if (num_tasks == 0) return;

    /* a) priorities: PendSV lowest, SysTick one step higher. */
    uint32_t shpr3 = SCB_SHPR3;
    shpr3 = (shpr3 & 0x00FFFFFFu) | ((uint32_t)PRIO_SYSTICK << 24);   /* byte3 = SysTick */
    shpr3 = (shpr3 & 0xFF00FFFFu) | ((uint32_t)PRIO_PENDSV  << 16);   /* byte2 = PendSV  */
    SCB_SHPR3 = shpr3;

    /* b) arm SysTick. */
    SYST_RVR = SYSTICK_RELOAD - 1;
    SYST_CVR = 0;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;

    /* c) seed PSP with the scratch buffer. */
    uint32_t *scratch_top = &initial_psp_scratch[16];
    __asm__ volatile ("msr psp, %0\n\tisb" : : "r"(scratch_top));

    /* d) reset the shared console mutex so a previous batch that
     *    (hypothetically) died mid-critical-section cannot poison us. */
    mutex_init(&console_mutex);

    /* e) pend PendSV; barrier ensures it takes effect before the WFI. */
    current_task = -1;
    first_switch = 1;
    all_done     = 0;
    SCB_ICSR     = SCB_ICSR_PENDSVSET;
    __asm__ volatile ("dsb\n\tisb");

    /* f) sleep until the last task exits.  Interrupts wake us; we then
     *    check all_done.  When PendSV returns with EXC_RETURN=F9 after
     *    all_done was set, the hardware unwinds through here anyway. */
    while (!all_done) { __asm__ volatile ("wfi"); }

    /* g) disarm SysTick and reset the table for the next `run` line.
     *    mem_init() drains any straggling region (there shouldn't be
     *    one — every task freed its stack in scheduler_pick_next). */
    scheduler_reset();
}

void scheduler_reset(void)
{
    SYST_CSR = 0;
    for (int i = 0; i < MAX_TASKS; ++i) tasks[i].state = TASK_UNUSED;
    num_tasks    = 0;
    current_task = -1;
    first_switch = 1;
    all_done     = 0;
    mem_init();
    mutex_init(&console_mutex);
}
