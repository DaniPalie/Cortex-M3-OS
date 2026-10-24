/*
 * alloc.c — First-fit contiguous allocator over the linker-defined user
 * arena.  See alloc.h for the model.  Reading order:
 *
 *   1) The regions[] table (sorted by base, packed left).
 *   2) Small helpers (arena bounds, 8-byte round-up, hole computations).
 *   3) mem_alloc — walks holes left-to-right, returns the first fit.
 *   4) mem_free  — locates the region by base and removes it (shift).
 *   5) Introspection: mem_used/free/count/snapshot/dump.
 *
 * The allocator does NOT split regions or store metadata inside the arena
 * itself: everything is kept in the fixed-size regions[] table in .bss.
 * That means the table size is the upper bound on live tasks and no
 * embedded header is written past the end of a user allocation, which
 * matters for the requirement "the kernel must keep base and size of
 * each region" (chapter 2 of the study guide).
 */

#include "alloc.h"
#include "../boot/uart.h"

#define ALIGN_BYTES 8u

/* --- 1. Region table ------------------------------------------------- */
typedef struct {
    uint32_t     base;
    uint32_t     size;
    const char  *owner;
} region_t;

static region_t regions[MEM_MAX_REGIONS];
static int      region_count = 0;

/* --- 2. Helpers ------------------------------------------------------ */
extern uint32_t _user_arena_start;
extern uint32_t _user_arena_end;

static uint32_t arena_start(void) { return (uint32_t)(uintptr_t)&_user_arena_start; }
static uint32_t arena_end  (void) { return (uint32_t)(uintptr_t)&_user_arena_end;   }

static uint32_t align_up(uint32_t v, uint32_t a)
{
    return (v + (a - 1)) & ~(a - 1);
}

/* Base address of the hole that PRECEDES regions[i], and its size.
 * For i == region_count, this returns the tail hole (from the last
 * region up to the arena end). */
static uint32_t hole_base(int i)
{
    if (i == 0) return arena_start();
    return regions[i - 1].base + regions[i - 1].size;
}

static uint32_t hole_size(int i)
{
    uint32_t hi = (i < region_count) ? regions[i].base : arena_end();
    return hi - hole_base(i);
}

/* --- 3. mem_init / mem_alloc ---------------------------------------- */

void mem_init(void)
{
    region_count = 0;
}

void *mem_alloc(uint32_t size, const char *owner)
{
    if (size == 0)                     return 0;
    if (region_count >= MEM_MAX_REGIONS) return 0;

    size = align_up(size, ALIGN_BYTES);

    /* First-fit scan: try every hole in address order (including the
     * head hole before regions[0] and the tail hole after the last one). */
    for (int i = 0; i <= region_count; ++i) {
        if (hole_size(i) < size) continue;

        uint32_t base = hole_base(i);

        /* Insert a new region at index i by shifting the tail. */
        for (int j = region_count; j > i; --j) regions[j] = regions[j - 1];
        regions[i].base  = base;
        regions[i].size  = size;
        regions[i].owner = owner;
        region_count++;
        return (void *)(uintptr_t)base;
    }

    return 0;   /* no hole was big enough */
}

/* --- 4. mem_free ---------------------------------------------------- */

void mem_free(void *ptr)
{
    if (!ptr) return;
    uint32_t base = (uint32_t)(uintptr_t)ptr;

    for (int i = 0; i < region_count; ++i) {
        if (regions[i].base != base) continue;
        for (int j = i; j < region_count - 1; ++j) regions[j] = regions[j + 1];
        region_count--;
        /* Adjacent holes merge implicitly: nothing else to do. */
        return;
    }
    /* Silently ignore stale pointers — this is a batch OS, not malloc. */
}

/* --- 5. Introspection ------------------------------------------------ */

uint32_t mem_arena_bytes(void)
{
    return arena_end() - arena_start();
}

uint32_t mem_used_bytes(void)
{
    uint32_t sum = 0;
    for (int i = 0; i < region_count; ++i) sum += regions[i].size;
    return sum;
}

uint32_t mem_free_bytes(void)
{
    return mem_arena_bytes() - mem_used_bytes();
}

int mem_region_count(void)
{
    return region_count;
}

int mem_snapshot(mem_region_view_t *out, int max)
{
    int n = region_count < max ? region_count : max;
    for (int i = 0; i < n; ++i) {
        out[i].base  = regions[i].base;
        out[i].size  = regions[i].size;
        out[i].owner = regions[i].owner;
    }
    return n;
}

void mem_dump(void)
{
    uart_puts("[mm] arena=[");
    uart_put_hex(arena_start());
    uart_puts(", ");
    uart_put_hex(arena_end());
    uart_puts(")  total=");
    uart_put_dec(mem_arena_bytes());
    uart_puts(" used=");
    uart_put_dec(mem_used_bytes());
    uart_puts(" free=");
    uart_put_dec(mem_free_bytes());
    uart_puts(" regions=");
    uart_put_dec((uint32_t)region_count);
    uart_puts("\r\n");

    for (int i = 0; i <= region_count; ++i) {
        uint32_t hs = hole_size(i);
        if (hs > 0) {
            uart_puts("   [hole ");
            uart_put_hex(hole_base(i));
            uart_puts(" +");
            uart_put_dec(hs);
            uart_puts(" B]\r\n");
        }
        if (i < region_count) {
            uart_puts("   [reg  ");
            uart_put_hex(regions[i].base);
            uart_puts(" +");
            uart_put_dec(regions[i].size);
            uart_puts(" B owner=");
            uart_puts(regions[i].owner ? regions[i].owner : "?");
            uart_puts("]\r\n");
        }
    }
}
