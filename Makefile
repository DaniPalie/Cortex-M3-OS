# -----------------------------------------------------------------------------
# Makefile — OS-Group08 (Cortex-M3 batch OS on QEMU / lm3s6965evb)
#
# Common targets:
#   make            build build/os.elf and build/os.bin
#   make run        boot the image inside QEMU (stdio console, no window)
#   make debug      boot with QEMU frozen at reset, gdbserver on :1234
#   make gdb        attach arm-none-eabi-gdb to a running `make debug`
#   make clean      delete build/ and any stray *.elf / *.bin
# -----------------------------------------------------------------------------

# --- Toolchain --------------------------------------------------------------
CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
AS       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
SIZE     = $(CROSS)size
GDB      = $(CROSS)gdb

QEMU    ?= qemu-system-arm
MACHINE ?= lm3s6965evb

# --- Build layout ----------------------------------------------------------
BUILD    = build
TARGET   = $(BUILD)/os
LINKER   = linker.ld

# Every .c under boot/ kernel/ mm/ shell/ programs/ is picked up automatically,
# so adding a new file in later phases needs no Makefile edit.
SRC_DIRS = boot kernel mm shell programs
C_SRCS   = $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c))
S_SRCS   = $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.S))

OBJS     = $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS)) \
           $(patsubst %.S,$(BUILD)/%.o,$(S_SRCS))
DEPS     = $(OBJS:.o=.d)

# --- Flags ------------------------------------------------------------------
CPUFLAGS = -mcpu=cortex-m3 -mthumb
WARN     = -Wall -Wextra -Wno-unused-parameter
CFLAGS   = $(CPUFLAGS) $(WARN) -std=gnu11 -ffreestanding -nostdlib \
           -fno-common -fdata-sections -ffunction-sections \
           -g3 -Og -MMD -MP
ASFLAGS  = $(CPUFLAGS) -g3
LDFLAGS  = $(CPUFLAGS) -nostdlib -T $(LINKER) \
           -Wl,--gc-sections -Wl,-Map=$(TARGET).map

# --- Rules ------------------------------------------------------------------
.PHONY: all run debug gdb clean size

all: $(TARGET).elf $(TARGET).bin size

$(TARGET).elf: $(OBJS) $(LINKER)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

size: $(TARGET).elf
	@$(SIZE) $<

run: $(TARGET).elf
	$(QEMU) -M $(MACHINE) -nographic -kernel $<

# `-s` opens gdbserver on tcp::1234; `-S` freezes CPU at reset.
debug: $(TARGET).elf
	$(QEMU) -M $(MACHINE) -nographic -kernel $< -s -S

gdb:
	$(GDB) -q $(TARGET).elf -ex "target remote localhost:1234"

clean:
	rm -rf $(BUILD) *.elf *.bin

-include $(DEPS)
