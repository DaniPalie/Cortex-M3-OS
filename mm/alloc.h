/*
 * alloc.h — Contiguous first-fit allocator for the OS-Group08 user arena.
 *
 * The arena is the single contiguous byte range [_user_arena_start,
 * _user_arena_end) carved out by linker.ld between .bss and the reserved
 * kernel MSP stack. Each running program owns one contiguous chunk of it
 * (its stack + private data), tracked by a region descriptor.
 *
 * Design notes (align with the theory of chapter 2 "Memory Management"):
 *
 *   - Policy: first-fit. On a request of N bytes we return the FIRST
 *     free hole that fits. Simple, fast, and adequate for the fixed
 *     program set of a batch OS.
 *   - Data structure: a compact array of allocated regions kept sorted
 *     by base address. Holes are implicit — they live between two
 *     adjacent regions (or between the arena boundary and the first /
 *     last region). This makes coalescing on free() a no-op: removing a
 *     region automatically merges the holes on either side.
 *   - Alignment: every base and size is rounded up to 8 bytes so the
 *     regions can hold Cortex-M ABI stacks and 64-bit values.
 *   - Bounds: at most MEM_MAX_REGIONS live regions at once (see below).
 */

#ifndef OS_ALLOC_H
#define OS_ALLOC_H

#include <stdint.h>

#define MEM_MAX_REGIONS 8       /* also the shell's MAX_TASKS budget */

/* Public view of one allocated region — used by the shell's `stat` command. */
typedef struct {
    uint32_t     base;
    uint32_t     size;
    const char  *owner;
} mem_region_view_t;

/* Reset the allocator (drops all regions). Called between shell batches. */
void  mem_init(void);

/* First-fit allocation.  Returns NULL if no hole is big enough or the
 * region table is full. `owner` is stored for debugging/`stat`; can be NULL. */
void *mem_alloc(uint32_t size, const char *owner);

/* Free a region previously returned by mem_alloc(). No-op on NULL. */
void  mem_free(void *ptr);

/* Introspection helpers ---------------------------------------------- */
uint32_t mem_arena_bytes(void);   /* total arena size                   */
uint32_t mem_used_bytes (void);   /* sum of allocated region sizes      */
uint32_t mem_free_bytes (void);   /* arena - used                       */
int      mem_region_count(void);  /* number of live regions             */

/* Copy at most `max` region descriptors to `out`, sorted by base.
 * Returns the number actually copied. */
int      mem_snapshot(mem_region_view_t *out, int max);

/* Human-readable dump on the UART: regions + holes with sizes. */
void     mem_dump(void);

#endif /* OS_ALLOC_H */
