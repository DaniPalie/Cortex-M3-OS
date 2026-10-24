/*
 * scheduler.h — Round-robin scheduler for the OS-Group08 batch OS.
 *
 * Model:
 *   - Tasks run in Thread mode with PSP; kernel and ISRs run with MSP.
 *   - SysTick fires periodically (priority high) and just sets PendSV
 *     as pending.  The context switch itself happens in PendSV_Handler
 *     (priority lowest), so any pending real interrupt runs first and
 *     latency stays low.
 *   - The C helper scheduler_pick_next() is called from PendSV to pick
 *     the next READY task (round-robin, FIFO order).
 *   - When a task's entry function returns, the ABI-defined LR sends it
 *     to task_exit(), which flags the task DONE and yields.
 *   - When no task is READY any more, scheduler_start() returns to the
 *     caller (the shell), restoring MSP mode via EXC_RETURN=0xFFFFFFF9.
 *
 * task_create()/scheduler_start() are meant to be called by the shell:
 *
 *     task_create(p_hello,   "p_hello",   512);
 *     task_create(p_counter, "p_counter", 512);
 *     scheduler_start();       // returns when both are DONE
 */

#ifndef OS_SCHEDULER_H
#define OS_SCHEDULER_H

#include <stdint.h>

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,      /* waiting on a mutex (see mm/mutex.c) */
    TASK_DONE,
} task_state_t;

/* Public API ---------------------------------------------------------- */

/* Register a new task and reserve its stack in the user arena.
 * Returns 0 on success, negative on error (no free TCB slot, arena full). */
int  task_create(void (*entry)(void), const char *name, uint32_t stack_bytes);

/* Start the scheduler.  Returns when every registered task has DONE.
 * Must be called from Thread+MSP mode (typically the shell). */
void scheduler_start(void);

/* Discard all task_create() calls made so far and drain the user arena.
 * Called by the shell on the error path (e.g. task_create returned -1
 * because the arena is full) so a partially-registered batch does not
 * poison the next `run`. */
void scheduler_reset(void);

/* Called (implicitly, via LR) when a task's entry function returns.
 * Flags the task DONE and triggers PendSV to schedule someone else. */
void task_exit(void);

/* --- Helpers for the mutex layer (mm/mutex.c) ----------------------- *
 * These are meant to be called from a running task's context.  They
 * expose just enough of the scheduler internals to implement blocking
 * primitives without giving mutex code access to the whole TCB table. */

/* Return the id (index into the internal tasks[] table) of the task
 * currently RUNNING, or -1 if called from thread/handler kernel code. */
int  current_task_id(void);

/* Mark task `id` as BLOCKED. The scheduler will skip it until a
 * matching task_wake() flips it back to READY. */
void task_block(int id);

/* Move task `id` from BLOCKED back to READY. */
void task_wake (int id);

/* Voluntary yield: pend PendSV so the scheduler picks somebody else. */
void sched_yield(void);

#endif /* OS_SCHEDULER_H */
