/*
 * mutex.h — Blocking mutex for OS-Group08, backed by the ARMv7-M
 *           exclusive monitor (LDREX/STREX).
 *
 * Design:
 *   - owner_id      : id of the task currently holding the lock, or
 *                     MUTEX_UNLOCKED (-1) if the mutex is free.
 *   - wait_queue[]  : FIFO of task ids blocked on this mutex. FIFO
 *                     order gives the bounded-waiting property from
 *                     chapter 3: no task can be starved indefinitely.
 *
 * Fast path (uncontended): mutex_lock() succeeds via a LDREX/STREX
 *   atomic write to owner_id and returns immediately.
 * Slow path (contended)  : the task adds itself to wait_queue, flips
 *   its scheduler state to TASK_BLOCKED, and yields.  When the holder
 *   calls mutex_unlock() the head of the queue receives the ownership
 *   directly (no wakeup race) and is moved back to TASK_READY.
 *
 * Correctness argument for the three critical-section properties:
 *   - Mutual exclusion: owner_id is a single atomically-updated cell.
 *   - Progress        : if no task holds the mutex, any calling task
 *                       succeeds on the LDREX/STREX fast path.
 *   - Bounded waiting : FIFO queue + direct ownership transfer on
 *                       unlock guarantee an at-most (n-1) wait, where
 *                       n is the number of contending tasks.
 *
 * The header exports a ready-made global mutex, console_mutex, that
 * user programs can lock around UART output to prevent character-level
 * races on the shared console.
 */

#ifndef OS_MUTEX_H
#define OS_MUTEX_H

#include <stdint.h>
#include "../kernel/scheduler.h"

#define MUTEX_MAX_WAITERS 8       /* same order as scheduler MAX_TASKS */
#define MUTEX_UNLOCKED    (-1)

typedef struct mutex {
    volatile int32_t owner_id;                    /* MUTEX_UNLOCKED or task id */
    int32_t          waiters[MUTEX_MAX_WAITERS];  /* FIFO ring of task ids     */
    int32_t          head, tail, count;
} mutex_t;

/* Initialise to the unlocked state.  Safe to call at any time; existing
 * waiters (if any) are silently forgotten — the caller is responsible
 * for making sure the mutex is idle. */
void mutex_init(mutex_t *m);

/* Try to lock without blocking. Returns 1 if the lock was taken, 0 if
 * it was already held by someone else. */
int  mutex_try_lock(mutex_t *m);

/* Block until the lock is acquired.  Must be called from a task
 * (Thread + PSP) — calling from an ISR would try to yield the CPU
 * from handler mode, which is undefined. */
void mutex_lock(mutex_t *m);

/* Release the lock.  If a waiter is queued, ownership is transferred
 * to it (its state moves back to READY).  Only the current owner may
 * unlock; calls from any other task are silently ignored. */
void mutex_unlock(mutex_t *m);

/* Global mutex protecting concurrent access to the shared UART. */
extern mutex_t console_mutex;

#endif /* OS_MUTEX_H */
