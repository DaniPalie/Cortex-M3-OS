# OS-Group08 — Technical Report

**Course:** Sistemi Operativi — Ingegneria Elettronica
**Group:** 08
**Target:** ARM Cortex-M3 emulated with QEMU (`lm3s6965evb`)

---

## Contents

1. Introduction & scope
2. Platform configuration
3. Build system and toolchain
4. Boot process (two-stage)
5. Memory map
6. Scheduler and context switch
7. Contiguous memory allocator
8. Shell and program table
9. Test programs
10. Optional extension: blocking mutex
11. Testing and edge cases
12. Course-concepts mapping
13. Repository, deliverables, build size
14. Limitations and possible extensions

---

## 1. Introduction & scope

The assignment asks for a minimal batch operating system running on
an ARM Cortex-M3 core inside QEMU, with a fixed set of user programs
stored in NOR flash. All code executes in place (XIP); program data
lives in RAM, allocated in a contiguous fashion. The system provides:

- a bootloader that initialises minimal hardware and transfers control
  to the OS,
- a textual shell that lists programs and accepts a "run this batch"
  command,
- a round-robin scheduler driven by a hardware timer interrupt,
- contiguous RAM allocation with tracking of `base` and `size` per
  region,
- (optional extension) a blocking mutex with LDREX/STREX.

The whole system compiles into a single ELF image of roughly **6 KiB
of flash + 400 B of RAM state**, out of the 256 KiB / 64 KiB the
target offers.

---

## 2. Platform configuration

**Machine.** QEMU's `lm3s6965evb` model of the Stellaris LM3S6965
evaluation board. Chosen over `mps2-an385` because Stellaris has
richer documentation and tutorial coverage while still providing
everything we need: a Cortex-M3 core, NOR flash starting at
`0x00000000`, SRAM at `0x20000000`, and UART0 at `0x4000C000` wired
to QEMU stdio in `-nographic` mode.

**CPU model.** ARMv7-M, Thumb-2 only. 8-word hardware exception frame
(`r0`, `r1`, `r2`, `r3`, `r12`, `LR`, `PC`, `xPSR`), separate MSP/PSP
stacks, exclusive monitor for `LDREX`/`STREX`, VTOR for vector table
relocation. All of these are used explicitly by our OS.

**QEMU launch command.** From the `Makefile`:

```
qemu-system-arm -M lm3s6965evb -nographic -kernel build/os.elf
```

`-nographic` sends UART0 to the host terminal (stdio), which is the
console the shell reads and writes.

---

## 3. Build system and toolchain

**Toolchain.** GNU Arm Embedded (`arm-none-eabi-gcc` 15.3), installed
on macOS via the official Arm cask (or the `gcc-arm-none-eabi` package
on Debian/Ubuntu). Compile flags:

```
-mcpu=cortex-m3 -mthumb          # architecture
-ffreestanding -nostdlib         # no libc, no crt0
-fno-common                      # no tentative definitions in .bss
-fdata-sections -ffunction-sections
-Wall -Wextra -std=gnu11
-g3 -Og                          # small, debuggable
```

Link flags:

```
-T linker.ld -Wl,--gc-sections   # dead-section stripping
-Wl,-Map=build/os.map            # produce a linker map
```

`--gc-sections` combined with `-ffunction-sections` means an unused
function is stripped from the final image. This is why the bootloader
uses `__attribute__((used))` on every function that is only reachable
through the vector table (see §4).

**Auto-discovery.** The Makefile globs every `.c` and `.S` under
`boot/`, `kernel/`, `mm/`, `shell/`, `programs/`, so adding a new
test program is a matter of dropping a source file in `programs/`
and registering it in `shell/programs.c`.

---

## 4. Boot process (two-stage)

Cortex-M3 boot sequence at reset:

1. The core reads 32 bits from address `0x00000000` and loads them
   into MSP.
2. It reads 32 bits from `0x00000004`, treats them as an address, and
   jumps there in Thumb mode.
3. Whatever code lives at that address is the reset handler.

We split the flash between a small **stage-1 bootloader** and the
**stage-2 OS**:

```
FLASH region      Address range       Content
────────────────  ──────────────────  ────────────────────────────────
BL_FLASH  (4KB)   0x00000000..0x0FFF  bl_vector_table + bl_reset_handler
                                       + inline UART + chain-loader
OS_FLASH (252KB)  0x00001000..0x3FFFF isr_vector_table (16 sys exc.)
                                       + Reset_Handler + kernel/shell/mm/programs
```

### Stage-1 responsibilities

- Print a short banner over UART0, using inline register writes only
  (no calls into the OS-side UART driver, so stage-1 stays fully
  standalone).
- Relocate the vector table by writing the OS vector table address
  to VTOR (`0xE000ED08`).
- Reload MSP from the OS vector table's first word.
- Branch to the OS Reset_Handler (second word).

Extract from [boot/bootloader.c](../boot/bootloader.c):

```c
SCB_VTOR = os_vt_addr;
__asm__ volatile ("dsb\n\tisb" ::: "memory");
__asm__ volatile (
    "msr msp, %0     \n\t"
    "bx  %1          \n\t"
    :
    : "r"(os_vt[0]), "r"(os_vt[1])
    : "memory"
);
```

`dsb`+`isb` ensure the VTOR change is committed and prefetched before
the next exception is taken.

### Stage-2 responsibilities

Executed at `Reset_Handler` after the jump from stage-1
([boot/startup.c](../boot/startup.c)):

1. Copy `.data` from its LMA in flash to its VMA in SRAM
   (initialised globals).
2. Zero `.bss` (zero-initialised globals).
3. Call `main()`.

`main()` [kernel/main.c](../kernel/main.c) prints a banner, dumps
VTOR so the chain-load is visually verifiable, then calls
`shell_run()`, which never returns.

### Why two stages, not one function call?

A plain function call would have been simpler, but it would not
demonstrate the concept the assignment is testing: that a real
bootloader relocates the vector table and hands off through a **CPU
reset-like** sequence, so that after the branch the OS effectively
"owns" the interrupt vector and MSP. The extra visibility (banner,
VTOR printout, gdb-inspectable jump) also gives the group a clean
anchor for the oral exam.

---

## 5. Memory map

Defined once in [linker.ld](../linker.ld) and exported to C via
linker-generated symbols.

```
FLASH (256 KiB)
  0x00000000  ┌─ .bl_vector      (16 B)   ── initial MSP + BL Reset
  0x00000010  │  .bl_text         ~200 B  ── BL body (BL_FLASH ends at 0x1000)
  0x00001000  ├─ .isr_vector      (64 B)  ── OS vector table (VTOR target)
  0x00001040  │  .text            ~5 KiB  ── kernel + shell + mm + programs
              │  .rodata                   ── string literals, program table
              │  .data (LMA)              ── initialised globals, source copy
  0x00040000  └─────

SRAM  (64 KiB)
  0x20000000  ┌─ .data (VMA)              ── copied here by Reset_Handler
              │  .bss                     ── zeroed here by Reset_Handler
  0x200001a8  ├─ _user_arena_start
              │  ...user arena (~60 KiB, first-fit-managed)...
  0x2000f000  ├─ _user_arena_end / kernel_stack_bottom
              │  MSP stack   (4 KiB)      ── kernel + all ISRs
  0x20010000  └─ _estack
```

Symbols exported to C: `_sidata`, `_sdata`, `_edata` (for the copy
loop), `_sbss`, `_ebss` (for the zeroing loop), `_user_arena_start`,
`_user_arena_end` (bounds of the arena consumed by `mm/alloc.c`),
`_estack` (used as `bl_vector_table[0]` and `isr_vector_table[0]`),
`_os_flash_base` (used by stage-1 to compute the chain-load target).

**Link-time safeguards.** Two `ASSERT`s at the end of the linker
script catch mistakes at build time rather than at boot:

```
ASSERT(_user_arena_start < _user_arena_end,
       "SRAM exhausted: no room for user arena");
ASSERT(SIZEOF(.bl_vector) + SIZEOF(.bl_text) <= LENGTH(BL_FLASH),
       "Bootloader too big for BL_FLASH");
```

---

## 6. Scheduler and context switch

### Design

Preemptive round-robin, exactly as described in chapter 1 of the
course study guide ("Round Robin = FCFS + preemption from clock").
The tasks are represented by a table of Task Control Blocks:

```c
typedef struct {
    uint32_t     *sp;            /* saved PSP                */
    task_state_t  state;         /* READY/RUNNING/BLOCKED/DONE */
    const char   *name;
    void        (*entry)(void);
    uint32_t     *stack_base;    /* allocated region base    */
    uint32_t      stack_size;
} tcb_t;
```

State machine of a task:

```
             task_create()
                  │
                  ▼
                READY ──picked──▶ RUNNING
                  ▲                  │
     mutex_unlock │                  │  SysTick preempts
     wakes waiter │                  ▼
                  └── BLOCKED ◀── (mutex_lock slow path)
                                   RUNNING
                                     │
                                     │  entry() returns → task_exit()
                                     ▼
                                    DONE
                                     │
                                     ▼
                            (stack freed by scheduler_pick_next,
                             running on MSP)
```

### Timer and priorities

- **SysTick** is programmed with a reload of 50 000 processor cycles
  and priority `0xE0` (relatively high). Its only job is to write
  `PENDSVSET` in `SCB_ICSR`.
- **PendSV** is at priority `0xFF`, the lowest. It only fires once no
  higher-priority handler is pending, so any real work runs first.

This SysTick → PendSV split is the standard "scheduler vs dispatcher"
separation from the course:

- SysTick is the **scheduler** (chooses to switch),
- PendSV is the **dispatcher** (does the switch).

### Context switch (`PendSV_Handler`)

Written in assembly in [kernel/context_switch.S](../kernel/context_switch.S):

```asm
mrs     r0, psp                 /* r0 = outgoing PSP           */
stmdb   r0!, {r4-r11}           /* save software-only regs      */
push    {lr}
bl      scheduler_pick_next     /* r0 in → r0 out (new PSP)     */
pop     {lr}
cmp     r0, #0
beq     .Lreturn_to_shell
ldmia   r0!, {r4-r11}           /* restore incoming             */
msr     psp, r0
ldr     lr, =0xFFFFFFFD         /* Thread + PSP return           */
bx      lr
.Lreturn_to_shell:
ldr     lr, =0xFFFFFFF9         /* Thread + MSP return           */
bx      lr
```

The two `EXC_RETURN` values are the mechanism the ARMv7-M ABI exposes:

- `0xFFFFFFFD` — return to Thread mode with PSP; the CPU pops
  `r0..xPSR` from PSP and continues at the popped `PC`. This starts
  or resumes a task.
- `0xFFFFFFF9` — return to Thread mode with MSP; used only once, when
  the last task has DONE. The hardware pops the exception frame that
  was pushed on MSP the very **first** time PendSV was ever entered
  (from `scheduler_start`), which puts the CPU right back at the
  instruction after `SCB_ICSR = PENDSVSET`.

### Bootstrapping

Cortex-M3 requires the first exception entry to have a valid PSP,
because `stmdb r0!, {r4-r11}` in `PendSV_Handler` will write there.
`scheduler_start` sets PSP to a small scratch buffer
(`initial_psp_scratch[16]`), pends PendSV, and enters `wfi`. On the
first PendSV entry the 32-byte scratch is written harmlessly; the C
picker returns the pre-initialised PSP of task 0; the assembly
restores `r4-r11` (all zero) and exception-returns with
`EXC_RETURN=0xFFFFFFFD`, and task 0 starts running.

### Initial stack frame

`task_create` prepares each task stack so an exception return can
step directly into the entry:

```
Stack top:
  +64  xPSR   = 0x01000000     (Thumb bit — mandatory)
  +60  PC     = entry
  +56  LR     = task_exit
  +52  r12, r3, r2, r1, r0     = 0                          ── hw frame
  +32  r11, r10, r9, r8,
        r7, r6, r5, r4         = 0                          ── sw frame
  +0   ← saved PSP in TCB
```

`LR` is set to `task_exit`, so when the entry function `return`s,
the ARM ABI takes it to `task_exit()`; that function flags the
task DONE and pends PendSV. On the next PendSV, `scheduler_pick_next`
sees the DONE state, frees the stack region, and picks the next
READY task (or signals "no more tasks" if there is none).

### Round-robin picker

```c
int start = (current_task + 1) % num_tasks;
for (int i = 0; i < num_tasks; ++i) {
    int idx = (start + i) % num_tasks;
    if (tasks[idx].state == TASK_READY) {
        current_task = idx;
        tasks[idx].state = TASK_RUNNING;
        return tasks[idx].sp;
    }
}
```

Because the scan restarts from `current_task + 1` on every switch and
the queue is a fixed ring, no task can wait more than `(n-1) * q`
(where `q` is the quantum and `n` is the number of contending READY
tasks) between two runs — this is the "no starvation" property from
chapter 1.

BLOCKED tasks are skipped (a `mutex_lock` slow-path waiter never
consumes CPU). If no READY task exists but some tasks are BLOCKED,
we detect the deadlock, print `[sched] DEADLOCK: task blocked with
nobody to wake it`, and unwind to the shell.

---

## 7. Contiguous memory allocator

Implemented in [mm/alloc.c](../mm/alloc.c). The arena is the byte
range `[_user_arena_start, _user_arena_end)` defined by the linker.
The allocator supports the requirement "each program gets a
contiguous RAM region, tracked by base and size".

### Data structure

A fixed-size array of allocated-region descriptors, kept **sorted by
base address** and packed:

```c
typedef struct {
    uint32_t     base;
    uint32_t     size;
    const char  *owner;
} region_t;
```

**Holes are implicit** — a hole exists between the end of one region
and the base of the next (or between the arena start and the first
region, and between the last region and the arena end). This design
choice trades a slightly more complex `mem_alloc` for a **very simple
`mem_free`**: removing a descriptor from the array automatically
merges the surrounding holes, so **coalescing is free**.

### Policy

**First-fit**, as recommended by the study guide for cases with a
small fixed workload:

- Simplest of the three placement policies (first-fit / best-fit /
  worst-fit).
- Fast in the common case.
- Fragmentation is not a real concern for a batch OS: at the end of
  each `run`, all regions are freed and the arena returns to a single
  hole.

### Introspection

The shell's `stat` command and the arena dump printed by `run`
between allocation and scheduler start both call `mem_dump()`, which
prints every region and every hole with base and size. Sample output
of `run p_hello p_counter p_alphabet`:

```
[mm] arena=[0x200001a8, 0x2000f000)  total=61016 used=1152 free=59864 regions=3
   [reg  0x200001a8 +256 B owner=p_hello]
   [reg  0x200002a8 +384 B owner=p_counter]
   [reg  0x20000428 +512 B owner=p_alphabet]
   [hole 0x20000628 +59864 B]
```

The three regions are strictly contiguous (each base equals the
previous base plus size), which is exactly the "contiguous
allocation" model of the course.

### External fragmentation — a known limit

As chapter 2 of the study guide notes, contiguous allocation is
vulnerable to external fragmentation (the "50% rule"). Because our
programs are launched together and freed together (batch), we cannot
observe the pathological case in normal operation. It would appear
if programs terminated at different times leaving isolated holes.
This is the trade-off contiguous allocation makes against paging —
which the course covers but the assignment explicitly excludes.

---

## 8. Shell and program table

The shell ([shell/shell.c](../shell/shell.c)) is a simple REPL
around `uart_gets()`. Grammar:

```
<line>    ::= <command> \n
<command> ::= "help"
            | "list"
            | "stat"
            | "run" <name> (" " <name>)*
            | (empty)
```

Tokenisation is done in-place: spaces in the input line are replaced
by `\0` and each token becomes an `argv[i]`, no allocation involved.

The **program table** lives in [shell/programs.c](../shell/programs.c)
as a static array of:

```c
typedef struct {
    const char       *name;
    const char       *description;
    program_entry_t   entry;      /* fn pointer in flash          */
    uint32_t          ram_bytes;  /* stack budget for task_create */
} program_t;
```

The `ram_bytes` field is the "resources needed for its execution"
requested by the assignment: it is passed as the stack size when the
shell asks the scheduler to launch the program.

`cmd_run` performs two passes:

1. First pass validates every argument (unknown name → whole batch
   is refused, nothing is started).
2. Second pass calls `task_create` for each program. If any call
   fails (arena full, task slot table full), the shell calls
   `scheduler_reset()` to drop the partial batch and returns a
   diagnostic including the current free-byte count.

Once every task is registered, the shell dumps the arena layout
(so the user actually gets to see the allocation before the batch
frees everything) and calls `scheduler_start()`. When that returns —
i.e. every task is DONE — the shell prints
`[shell] all tasks finished, arena drained` and returns to the
prompt.

---

## 9. Test programs

Every program lives under `programs/` as a single `.c` file
containing one or more entry functions with signature `void (void)`.

| Name          | RAM budget | Purpose |
|---------------|-----------:|---------|
| `p_hello`     | 256 B      | prints one greeting, returns — smoke test |
| `p_counter`   | 384 B      | counts 1..10 with a busy wait — visible interleaving |
| `p_alphabet`  | 512 B      | prints the alphabet — another distinct pattern |
| `p_race_A`    | 512 B      | writes `[A: AAAA...]` char-by-char WITHOUT mutex |
| `p_race_B`    | 512 B      | writes `[B: BBBB...]` char-by-char WITHOUT mutex |
| `p_safe_A`    | 512 B      | same as `p_race_A` but under `console_mutex` |
| `p_safe_B`    | 512 B      | same as `p_race_B` but under `console_mutex` |
| `p_bigmem`    | 40 KiB     | one instance fits, two do not — arena-full test |

The `busy_wait(volatile unsigned n)` used by several of them uses an
`__asm__ volatile ("nop")` inside the loop so the compiler cannot
optimise it away.

---

## 10. Optional extension: blocking mutex

Implemented in [mm/mutex.c](../mm/mutex.c) and covered by chapter 3
of the study guide.

### Data structure

```c
typedef struct mutex {
    volatile int32_t owner_id;                    /* -1 = unlocked */
    int32_t          waiters[MUTEX_MAX_WAITERS];  /* FIFO ring     */
    int32_t          head, tail, count;
} mutex_t;
```

`owner_id` uses negative-one as the "unlocked" sentinel so `0` (a
valid task id) is a legal owner value.

### Fast path — LDREX/STREX

```c
int mutex_try_lock(mutex_t *m) {
    int me = current_task_id();
    for (;;) {
        int32_t owner = (int32_t) ldrex_word(&m->owner_id);
        if (owner != MUTEX_UNLOCKED) { clrex(); return 0; }
        if (strex_word((uint32_t)me, &m->owner_id) == 0) {
            dmb(); return 1;
        }
    }
}
```

`LDREX`/`STREX` are the ARMv7-M realisation of the abstract
"atomic test-and-set" primitive described in chapter 3. The
exclusive monitor is invalidated by any intervening store to the
tagged address (or by exception entry/exit on the same processor),
so the retry loop guarantees the CAS is atomic even in the presence
of preemption. `DMB` after a successful `STREX` provides the
happens-before edge that any observer inside the critical section
needs.

### Slow path — enqueue and yield

```c
uint32_t primask = irq_disable();
if (mutex_try_lock(m)) { irq_restore(primask); return; }
queue_push(m, me);
task_block(me);
irq_restore(primask);
sched_yield();
```

IRQs are masked around the three lines that manipulate the wait
queue plus the state flip, so a SysTick between "I am now BLOCKED"
and "here I am in the queue" cannot cause a concurrent `mutex_unlock`
to pop nobody. Once IRQs are re-enabled, `sched_yield` pends PendSV;
the picker sees `TASK_BLOCKED` and skips us.

### Unlock — direct handoff

```c
int32_t next = queue_pop(m);
if (next >= 0) { m->owner_id = next; task_wake(next); }
else           { m->owner_id = MUTEX_UNLOCKED; }
```

Instead of "release the lock, wake the waiter, race everyone else",
we **transfer** ownership directly: `owner_id` is set to the waiter's
id **before** it is moved back to READY. A third task that happens
to hit the fast path in between will see `owner_id != UNLOCKED` and
fail its `LDREX/STREX`, so the awakened waiter cannot be starved by
a newcomer. This is the "bounded waiting" property from chapter 3.

### Correctness argument

- **Mutual exclusion** — `owner_id` is a single word touched
  atomically by `LDREX`/`STREX`; only the winning writer becomes the
  owner.
- **Progress** — if no task holds the mutex, the next caller wins on
  the fast path with no need for the scheduler to intervene.
- **Bounded waiting** — FIFO queue + direct handoff bound the wait
  time to `n-1` critical sections, where `n` is the number of tasks
  currently contending.

### Demonstration

Compare the two `run`s (real outputs, trimmed):

```
os> run p_race_A p_race_B
[A: AAAAAA[B: BBBBBBBBBBBBBBBBBBBB]
AAAAAAAAAAAAAA]
[A: AAAAAAAAAA[B: BBBBBBBBBBBBBBBBBBBAAAAAAAAAA]
B]
[B: BBBBBBBBBBBBBBBBBBBB]
[shell] all tasks finished, arena drained
```

```
os> run p_safe_A p_safe_B
[A: AAAAAAAAAAAAAAAAAAAA]
[A: AAAAAAAAAAAAAAAAAAAA]
[B: BBBBBBBBBBBBBBBBBBBB]
[A: AAAAAAAAAAAAAAAAAAAA]
[B: BBBBBBBBBBBBBBBBBBBB]
[B: BBBBBBBBBBBBBBBBBBBB]
[shell] all tasks finished, arena drained
```

The line **order** (`A`, `A`, `B`, `A`, `B`, `B`) still reflects
round-robin preemption, but the line **content** is now atomic —
`console_mutex` keeps each `[X: XXXX...X]` from being interleaved.

---

## 11. Testing and edge cases

Every command below has been executed live in QEMU on the current
main branch.

### 11.1 Nominal cases

| Command                                    | Expected                                |
|--------------------------------------------|-----------------------------------------|
| `list`                                     | 8 program rows with ram budgets         |
| `stat` (fresh boot)                        | one hole covering ~60 KiB               |
| `run p_hello`                              | single line, returns to shell           |
| `run p_hello p_counter p_alphabet`         | interleaved output, arena dump shows 3 regions |
| `run p_hello` (again)                      | arena is drained after each batch       |

### 11.2 Failure and edge cases

| Command                                    | Expected                                |
|--------------------------------------------|-----------------------------------------|
| `run`                                      | `usage: run <name> [<name> ...]`        |
| `run p_bogus`                              | `unknown program 'p_bogus' (nothing started)`, shell continues |
| `run p_hello p_bogus p_counter`            | validation-first pass refuses whole batch |
| `run p_bigmem p_bigmem`                    | second `task_create` returns rc=-2, message includes free-byte count, `scheduler_reset()` drains arena |
| Any subsequent `run` after a failure       | works normally — proof that reset is complete |
| `run p_alphabet` × 8 (fills task slots)    | 8th call would fail if attempted; the shell parser caps at 7 program args (`SHELL_MAX_TOKENS = 8`) |

### 11.3 Concurrency case

Run in this order live:

```
os> run p_race_A p_race_B          → visibly interleaved output
os> run p_safe_A p_safe_B          → clean, atomic lines
```

### 11.4 No-hang / no-leak

`stat` right after any `run` reports `used=0 free=61016 regions=0`
regardless of how the batch finished (success, unknown-program
error, arena-full error). Every stack region is either freed by the
scheduler (task DONE path) or drained by `scheduler_reset` on the
error path.

---

## 12. Course-concepts mapping

| Chapter / topic                     | Where in the code                     |
|-------------------------------------|---------------------------------------|
| CPU scheduling — round-robin        | `scheduler.c: scheduler_pick_next`    |
| CPU scheduling — quantum, no starvation | SysTick reload = 50000, N-1 bound argued in §6 |
| scheduler vs dispatcher             | `SysTick_Handler` (scheduler) vs `PendSV_Handler` (dispatcher) |
| Preemption vs blocking              | SysTick preempts; `mutex_lock` slow path blocks |
| Memory — partitions with OS "below" | linker.ld: `.data`/`.bss` at low SRAM, arena above, MSP stack on top |
| Memory — base + limit tracking      | `region_t { base, size, owner }` in `mm/alloc.c` |
| Memory — first-fit                  | `mem_alloc()` walk in `mm/alloc.c` |
| Memory — external fragmentation     | discussed in `mm/alloc.c` header and §7 above |
| Sync — critical section 3 properties| §10 correctness argument               |
| Sync — atomic RMW                   | `ldrex_word` / `strex_word` in `mm/mutex.c` |
| Sync — spinlock vs blocking         | slow path uses `task_block` + `sched_yield`, not busy-wait |
| Sync — mutex vs semaphore           | `owner_id` field: only the owner can `unlock` |

---

## 13. Repository, deliverables, build size

- Source: `boot/`, `kernel/`, `mm/`, `shell/`, `programs/`, `linker.ld`, `Makefile`
- Documentation: this file + `README.md`
- Build outputs: `build/os.elf`, `build/os.bin`, `build/os.map`

Sizes (`arm-none-eabi-size build/os.elf` output):

```
   text	   data	    bss	    dec	    hex	filename
   6120	     56	    368	   6544	   1990	build/os.elf
```

Meaning: **~6.0 KiB of code** in flash, **~56 B of initialised
globals**, **~370 B of zero-init globals**. The rest of the 256 KiB
flash and 60 KiB user arena is available for future programs.

---

## 14. Limitations and possible extensions

- **No MPU protection.** The Cortex-M3 has a Memory Protection Unit;
  we do not configure it. A rogue task can trample another's stack.
  Adding MPU regions per task around each `mem_alloc`'d chunk would
  turn the base+limit table we already keep into hardware-enforced
  isolation, without changing the allocator itself.
- **Simple deadlock detection.** We only detect "everyone blocked",
  not cycles. In the current one-mutex codebase this covers all
  cases; adding more mutexes would need a real waits-for graph.
- **No priority scheduling.** Round-robin is fair but not adaptive.
  A future extension could add a priority field to `tcb_t` and pick
  the highest-priority READY task on each context switch (multi-level
  round-robin).
- **Static program table.** Programs are baked in at build time.
  Loading arbitrary flash blobs at runtime would require a real
  ELF loader; that is out of scope for a batch OS.
- **Fixed number of task slots.** `MAX_TASKS = 8`; growing this is
  a one-line change but the current shell parser caps a `run` batch
  at seven program arguments (`SHELL_MAX_TOKENS = 8`).
