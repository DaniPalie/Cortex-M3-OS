# Cortex-M3 Batch Operating System (bare metal)

Minimal batch operating system for an ARM Cortex-M3 emulated with
QEMU (Stellaris `lm3s6965evb` board). Written from bare metal:
own bootloader, own reset handler, own scheduler, own contiguous
allocator, own blocking mutex.

Course project for *Operating Systems* (M.Sc. Electronic Engineering,
Politecnico di Torino), developed in a team of two.

> **Scope of this repository.** This is the OS core I wrote
> (phases 1–7: boot and UART bring-up, two-stage boot with VTOR
> relocation, shell, round-robin scheduler with PendSV context switch,
> first-fit allocator, LDREX/STREX mutex). The initial project skeleton
> is by [Thomas Vitale](https://github.com/thomas-vitale), who later
> extended the team version with adaptive priority scheduling and
> priority inheritance — those extensions are not included here.

---

## Quick start

### 1. Install the toolchain (macOS example)

```
brew install --cask gcc-arm-embedded
brew install qemu
```

On Ubuntu / Debian:

```
sudo apt install gcc-arm-none-eabi qemu-system-arm
```

Verify:

```
arm-none-eabi-gcc --version
qemu-system-arm  --version
```

### 2. Build and run

```
cd cortex-m3-os
make            # produces build/os.elf and build/os.bin
make run        # boots the image inside QEMU on stdio
```

Exit QEMU with `Ctrl-A x`.

### 3. Debug (optional)

```
make debug      # starts QEMU frozen at reset, gdbserver on :1234
# ...in another terminal:
make gdb        # attaches arm-none-eabi-gdb to the running QEMU
```

---

## Shell cheat sheet

Once booted, you get an interactive prompt on the UART:

```
os> help
commands:
  help                          this message
  list                          list available programs
  stat                          print user-arena occupancy
  run <name> [<name> ...]       execute one or more programs in parallel
```

Interesting things to try:

| Command                                    | What you should see |
|--------------------------------------------|---------------------|
| `list`                                     | table of every program with its RAM budget |
| `stat`                                     | one hole covering the full ~60 KiB arena |
| `run p_hello`                              | single program, returns to shell |
| `run p_counter p_alphabet p_hello`         | round-robin interleaving between three tasks |
| `run p_race_A p_race_B`                    | UART race: characters visibly mixed |
| `run p_safe_A p_safe_B`                    | same programs with `console_mutex`: clean lines |
| `run p_bigmem p_bigmem`                    | edge case: second task_create fails, batch aborted |

---

## Repository layout

```
cortex-m3-os/
├── Makefile          build pipeline (compile / link / objcopy / QEMU / gdb)
├── linker.ld         memory map: BL_FLASH / OS_FLASH / SRAM / user arena
├── boot/
│   ├── bootloader.c  stage-1: banner + VTOR relocation + chain-load
│   ├── startup.c     stage-2: reset handler, .data/.bss init, full vec table
│   └── uart.c/.h     UART0 driver (Stellaris)
├── kernel/
│   ├── main.c              stage-2 entry, hands off to shell_run()
│   ├── scheduler.c/.h      TCB, round-robin, SysTick/PendSV setup, helpers
│   └── context_switch.S    PendSV_Handler in assembly
├── mm/
│   ├── alloc.c/.h    contiguous first-fit allocator (chapter 2)
│   └── mutex.c/.h    blocking mutex on LDREX/STREX (chapter 3)
├── shell/
│   ├── shell.c/.h    REPL, tokenise, dispatch help/list/stat/run
│   └── programs.c/.h program table (name, description, entry, ram_bytes)
├── programs/         one .c per test program
└── docs/
    └── report.md     technical report (build, boot, scheduler, mm, mutex, tests)
```

---

## Design at a glance

Full technical write-up in [docs/report.md](docs/report.md). Highlights:

- **Two-stage boot.** Stage-1 lives in the first 4 KiB of flash, prints
  a banner, moves VTOR to `0x00001000`, reloads MSP, and branches to
  the stage-2 OS reset handler. Stage-2 does `.data`/`.bss` init and
  calls `main()`.
- **Round-robin scheduler.** SysTick fires periodically and pends
  PendSV; PendSV (priority `0xFF`, always the lowest) saves the
  outgoing task's `r4-r11` on its PSP, calls the C picker, and
  exception-returns into the incoming task with `EXC_RETURN=0xFFFFFFFD`.
  Returning to the shell after the last task uses `EXC_RETURN=0xFFFFFFF9`
  to unwind the MSP frame stashed on the very first PendSV entry.
- **Contiguous allocator.** First-fit over
  `[_user_arena_start, _user_arena_end)`; every allocated region has a
  `{base, size, owner}` descriptor. Holes are implicit, so `mem_free`
  is O(1) descriptor removal and coalescing is automatic.
- **Blocking mutex.** Fast path is an `ldrex`/`strex` CAS on `owner_id`.
  Slow path enqueues the caller (FIFO), moves it to `TASK_BLOCKED`
  and yields; `mutex_unlock` does a direct handoff to the head of the
  queue, guaranteeing bounded waiting.
