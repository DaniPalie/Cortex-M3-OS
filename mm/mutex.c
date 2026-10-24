/*
 * mutex.c — Blocking mutex over the ARMv7-M exclusive monitor.
 *
 * Reading order:
 *   1) Inline wrappers over LDREX / STREX / CLREX / DMB / IRQ masking.
 *      These are the atomic RMW primitives from chapter 3 mapped to
 *      Cortex-M instructions.
 *   2) Wait-queue helpers (FIFO ring buffer).
 *   3) The three public operations: mutex_init / try_lock / lock /
 *      unlock.  IRQ masking is only used around wait-queue mutation;
 *      the ownership word itself is manipulated with LDREX/STREX.
 */

#include "mutex.h"
#include "../boot/uart.h"

/* --- 1. Low-level primitives ---------------------------------------- */

/* Load-exclusive: read a 32-bit word and tag the address in the
 * exclusive monitor. Any intervening store (or exception boundary)
 * clears the tag, causing the paired strex_word to fail. */
static inline uint32_t ldrex_word(volatile int32_t *addr)
{
    uint32_t v;
    __asm__ volatile ("ldrex %0, [%1]" : "=r"(v) : "r"(addr));
    return v;
}

/* Store-exclusive: write `value` to `addr` only if the tag is still
 * valid.  Returns 0 on success, 1 on failure (retry the LDREX). */
static inline int strex_word(uint32_t value, volatile int32_t *addr)
{
    int r;
    __asm__ volatile ("strex %0, %1, [%2]" : "=&r"(r) : "r"(value), "r"(addr) : "memory");
    return r;
}

static inline void clrex(void) { __asm__ volatile ("clrex" ::: "memory"); }
static inline void dmb  (void) { __asm__ volatile ("dmb"   ::: "memory"); }

/* Global interrupt masking around wait-queue mutation.  The queue is
 * touched from task context (mutex_lock) and, indirectly, from
 * mutex_unlock (which also runs in task context) — but a SysTick tick
 * between the two would leave the queue inconsistent, so we lock IRQs
 * for the tiny window that manipulates head/tail/count. */
static inline uint32_t irq_disable(void)
{
    uint32_t primask;
    __asm__ volatile ("mrs %0, primask\n\tcpsid i" : "=r"(primask));
    return primask;
}

static inline void irq_restore(uint32_t primask)
{
    __asm__ volatile ("msr primask, %0" : : "r"(primask) : "memory");
}

/* --- 2. Wait-queue helpers ------------------------------------------- */

/* All three helpers assume IRQs are already disabled by the caller. */

static void queue_push(mutex_t *m, int32_t id)
{
    if (m->count >= MUTEX_MAX_WAITERS) return;   /* silently drop (would deadlock) */
    m->waiters[m->tail] = id;
    m->tail = (m->tail + 1) % MUTEX_MAX_WAITERS;
    m->count++;
}

static int32_t queue_pop(mutex_t *m)
{
    if (m->count == 0) return -1;
    int32_t id = m->waiters[m->head];
    m->head = (m->head + 1) % MUTEX_MAX_WAITERS;
    m->count--;
    return id;
}

/* --- 3. Public API --------------------------------------------------- */

void mutex_init(mutex_t *m)
{
    if (!m) return;
    m->owner_id = MUTEX_UNLOCKED;
    m->head = m->tail = m->count = 0;
    for (int i = 0; i < MUTEX_MAX_WAITERS; ++i) m->waiters[i] = -1;
    dmb();
}

int mutex_try_lock(mutex_t *m)
{
    int me = current_task_id();
    if (me < 0) return 0;

    for (;;) {
        int32_t owner = (int32_t) ldrex_word(&m->owner_id);
        if (owner != MUTEX_UNLOCKED) {
            clrex();          /* somebody else holds it — do not retry */
            return 0;
        }
        if (strex_word((uint32_t)me, &m->owner_id) == 0) {
            dmb();            /* seen-before-unlock ordering for callers */
            return 1;
        }
        /* strex failed: exclusive tag was cleared (e.g. by an ISR).
         * Loop and re-attempt the LDREX/STREX pair. */
    }
}

void mutex_lock(mutex_t *m)
{
    int me = current_task_id();
    if (me < 0) return;

    /* Fast path: uncontended acquisition via LDREX/STREX. */
    if (mutex_try_lock(m)) return;

    /* Slow path.  IRQs are masked around enqueue+block so a SysTick
     * cannot preempt us between "I'm now BLOCKED" and "here I am in
     * the queue" — otherwise a concurrent unlock could pop nobody
     * and leave us stuck. */
    uint32_t primask = irq_disable();

    /* Double-check under IRQ mask: the owner may have released
     * between our failed try_lock and here. */
    if (mutex_try_lock(m)) {
        irq_restore(primask);
        return;
    }

    queue_push(m, me);
    task_block(me);
    irq_restore(primask);

    sched_yield();      /* PendSV picks somebody else; we resume here
                           after mutex_unlock() has done the direct
                           handoff (owner_id already set to `me`). */

    /* Post-condition: m->owner_id == me. Nothing to re-check. */
}

void mutex_unlock(mutex_t *m)
{
    int me = current_task_id();
    if (me < 0) return;
    if (m->owner_id != me) return;    /* not the owner — silently ignore */

    uint32_t primask = irq_disable();

    int32_t next = queue_pop(m);
    if (next >= 0) {
        /* Direct handoff: the awoken waiter is the new owner, so a
         * task racing on the fast path cannot steal the lock. */
        m->owner_id = next;
        task_wake(next);
    } else {
        m->owner_id = MUTEX_UNLOCKED;
    }
    dmb();

    irq_restore(primask);
}

/* --- Shared instance ------------------------------------------------- */

mutex_t console_mutex = {
    .owner_id = MUTEX_UNLOCKED,
    .head = 0, .tail = 0, .count = 0,
};
